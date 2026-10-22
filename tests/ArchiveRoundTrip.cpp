#include "../BackupEngine.h"
#include "../ClientNotes.h"
#include "../InProc7z.h"
#include "../version.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <winioctl.h>
#include <cstring>

namespace fs = std::filesystem;

static std::vector<DWORD> forcedBackupGuids;
HRESULT CtBackupTestCreateGuid(GUID *identity) {
  const HRESULT hr = CoCreateGuid(identity);
  if (SUCCEEDED(hr) && !forcedBackupGuids.empty()) {
    identity->Data1 = forcedBackupGuids.front();
    forcedBackupGuids.erase(forcedBackupGuids.begin());
  }
  return hr;
}

struct ProgressState {
  unsigned highestPercent = 0;
};

struct DeleteDuringCompressionState {
  fs::path filePath;
  bool attempted = false;
  bool deleted = false;
};

struct ReplaceDuringExtractionState {
  fs::path archivePath;
  fs::path replacementPath;
  unsigned highestPercent = 0;
  bool attempted = false;
  bool replaced = false;
  DWORD error = ERROR_SUCCESS;
};

struct RestoreGuardState {
  bool called = false;
};

static bool RejectRestoreCommit(void *user, std::wstring &failureDetails) {
  auto *state = static_cast<RestoreGuardState *>(user);
  if (state)
    state->called = true;
  failureDetails = L"Focused restore guard rejection.";
  return false;
}

static void RecordProgress(void *user, _7zOp, unsigned percent,
                           const wchar_t *) {
  auto *state = static_cast<ProgressState *>(user);
  if (state && percent > state->highestPercent)
    state->highestPercent = percent;
}

static void DeleteDuringCompression(void *user, _7zOp operation, unsigned,
                                    const wchar_t *) {
  auto *state = static_cast<DeleteDuringCompressionState *>(user);
  if (!state || state->attempted || operation != _7zOp::Compress)
    return;
  state->attempted = true;
  std::error_code error;
  state->deleted=fs::remove(state->filePath, error);
}

static void AttemptArchiveReplacement(void *user, _7zOp operation,
                                      unsigned percent, const wchar_t *) {
  auto *state = static_cast<ReplaceDuringExtractionState *>(user);
  if (!state)
    return;
  if (percent > state->highestPercent)
    state->highestPercent = percent;
  if (state->attempted || operation != _7zOp::Extract)
    return;

  state->attempted = true;
  SetLastError(ERROR_SUCCESS);
  state->replaced =
      MoveFileExW(state->replacementPath.c_str(), state->archivePath.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  state->error = state->replaced ? ERROR_SUCCESS : GetLastError();
}

static bool WriteBytes(const fs::path &path, const std::vector<unsigned char> &data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
  return output.good();
}

static bool WriteBackupManifest(const fs::path &path,
                                unsigned long long profileCount) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output << "format=ctSpaces-backup\n"
         << "formatVersion=1\n"
         << "appVersion=" << CTSPACES_VERSION_TEXT << "\n"
         << "createdUtc=2026-08-30T00:00:00Z\n"
         << "profileCount=" << profileCount << "\n"
         << "payload=Sites\n";
  return output.good();
}

static bool TryCreateDirectorySymlink(const fs::path &linkPath,
                                      const fs::path &targetPath) {
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
  if (CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(), flags))
    return true;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  if (GetLastError() == ERROR_INVALID_PARAMETER) {
    return CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(),
                               SYMBOLIC_LINK_FLAG_DIRECTORY) != FALSE;
  }
#endif
  return false;
}

static std::vector<unsigned char> ReadBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return {};
  const std::streamsize size = input.tellg();
  if (size < 0)
    return {};
  input.seekg(0, std::ios::beg);
  std::vector<unsigned char> data(static_cast<size_t>(size));
  if (size > 0 && !input.read(reinterpret_cast<char *>(data.data()), size))
    return {};
  return data;
}

static bool CreateFixtureJunction(const fs::path &link, const fs::path &target,DWORD sharing=0) {
  fs::create_directory(link);
  const std::wstring print = fs::absolute(target).wstring();
  const std::wstring substitute = L"\\??\\" + print;
  struct MountPoint {
    DWORD tag; WORD length, reserved;
    WORD substituteOffset, substituteLength, printOffset, printLength;
    wchar_t names[1];
  };
  const size_t namesBytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
  std::vector<unsigned char> buffer(offsetof(MountPoint, names) + namesBytes, 0);
  auto *mount = reinterpret_cast<MountPoint *>(buffer.data());
  mount->tag = IO_REPARSE_TAG_MOUNT_POINT;
  mount->length = static_cast<WORD>(8 + namesBytes);
  mount->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
  mount->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
  mount->printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
  std::memcpy(mount->names, substitute.data(), mount->substituteLength);
  std::memcpy(reinterpret_cast<unsigned char *>(mount->names) + mount->printOffset,
              print.data(), mount->printLength);
  HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, sharing, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD returned = 0;
  const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
      static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != FALSE;
  CloseHandle(handle);
  return success;
}

struct RestoreMutation {
  fs::path target, destination;
  unsigned mode = 0;
  bool called = false, attempted = false;
  DWORD error = ERROR_SUCCESS;
};
static bool MutateRestoreDuringGuard(void *user, std::wstring &) {
  auto &mutation = *static_cast<RestoreMutation *>(user);
  mutation.called = true;
  if (mutation.mode == 1) {
    HANDLE file = CreateFileW(mutation.target.c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    FILETIME creation{}, access{}, write{};
    GetFileTime(file, &creation, &access, &write);
    const char changed[] = "EVIL";
    DWORD written = 0;
    mutation.attempted = WriteFile(file, changed, 4, &written, nullptr) && written == 4 &&
        SetFileTime(file, &creation, &access, &write);
    CloseHandle(file);
  } else if (mutation.mode == 2) {
    mutation.attempted = WriteBytes(mutation.target, {'A', 'D', 'D'});
  } else if (mutation.mode == 3) {
    mutation.attempted = CreateDirectoryW(mutation.target.c_str(), nullptr) != FALSE;
  } else if (mutation.mode == 4) {
    mutation.attempted = MoveFileExW(mutation.target.c_str(), mutation.destination.c_str(), 0) != FALSE;
    mutation.error = mutation.attempted ? ERROR_SUCCESS : GetLastError();
    return false; // Test that the bound roots cannot be replaced while guarded.
  } else if (mutation.mode == 5) {
    fs::rename(mutation.target, mutation.destination);
    mutation.attempted = CreateFixtureJunction(mutation.target, mutation.destination);
  }
  return mutation.attempted;
}

static void MutateRestoreDuringRenameRetry(void *user) {
  auto &mutation = *static_cast<RestoreMutation *>(user);
  mutation.called = true;
  if (mutation.mode == 3) {
    mutation.attempted = CreateDirectoryW(mutation.target.c_str(), nullptr) != FALSE;
    if (mutation.attempted)
      mutation.attempted = WriteBytes(mutation.target / L"foreign.bin", {'K','E','E','P'});
  } else {
    mutation.attempted = MoveFileExW(mutation.target.c_str(), mutation.destination.c_str(), 0) != FALSE;
    mutation.error = mutation.attempted ? ERROR_SUCCESS : GetLastError();
  }
}

static void CheckRestoreIdentity(const fs::path &root) {
  unsigned checks = 0;
  const auto check = [&](bool passed, const char *description) {
    if (!passed) throw std::runtime_error(description);
    ++checks; std::cout << "PASS " << description << '\n';
  };
  const std::vector<unsigned char> stagedBytes{'S','A','F','E'}, liveBytes{'L','I','V','E'}, outsideBytes{'O','U','T','S'};
  unsigned fixtureNumber = 0;
  const auto run = [&](unsigned mode, const char *description) {
    const fs::path fixture = root / std::to_wstring(++fixtureNumber);
    CtBackup::StagedRestore staged{fixture / L"stage", fixture / L"stage" / L"Sites", 1};
    const fs::path live = fixture / L"live" / L"Sites", recovery = fixture / L"recovery";
    const fs::path outside = fixture / L"outside" / L"Sites";
    fs::create_directories(staged.payloadDir / L"Alpha" / L"Default");
    fs::create_directories(live / L"Keep");
    fs::create_directories(outside / L"Alpha" / L"Default");
    const fs::path stagedFile = staged.payloadDir / L"Alpha" / L"Default" / L"state.bin";
    check(WriteBytes(stagedFile, stagedBytes) && WriteBytes(live / L"Keep" / L"state.bin", liveBytes) &&
          WriteBytes(outside / L"Alpha" / L"Default" / L"state.bin", outsideBytes), "Identity fixture has three distinct sentinels");
    check(CtBackup::CaptureStagedRestoreIdentity(staged).ok() && staged.validation != nullptr,
          "Manual legacy-shaped stage seals its original real tree");
    RestoreMutation mutation;
    CtBackup::RestoreCommitGuard guard = nullptr;
    fs::path junction;
    if (mode == 0) staged.validation.reset();
    if (mode == 1 || mode == 11 || mode == 12) {
      const fs::path original = fixture / L"original";
      const fs::path replaced = mode == 11 ? staged.stagingRoot : mode == 12 ?
          staged.payloadDir / L"Alpha" / L"Default" : staged.payloadDir;
      fs::rename(replaced, original);
      fs::copy(original, replaced, fs::copy_options::recursive);
    } else if (mode == 2 || mode == 3 || mode == 4) {
      junction = mode == 2 ? staged.payloadDir : mode == 3 ? staged.stagingRoot : staged.payloadDir / L"Alpha" / L"Default";
      fs::rename(junction, fixture / L"original");
      const fs::path target = mode == 2 ? outside : mode == 3 ? fixture / L"outside" : outside / L"Alpha" / L"Default";
      check(CreateFixtureJunction(junction, target), "Sealed object is replaced by an NTFS junction to disposable outside data");
    } else if (mode >= 5 && mode <= 9) {
      guard = MutateRestoreDuringGuard;
      if (mode == 5) mutation = {stagedFile, {}, 1};
      if (mode == 6) mutation = {stagedFile.parent_path() / L"added.bin", {}, 2};
      if (mode == 7) mutation = {recovery, {}, 3};
      if (mode == 8) mutation = {staged.payloadDir, fixture / L"guard-original", 4};
      if (mode == 9) mutation = {stagedFile.parent_path(), fixture / L"guard-original", 5};
    } else if (mode == 10) CtBackup::FailRestoreRenameForTest(2);
    const CtBackup::Result result = CtBackup::CommitStagedRestore(staged, live, recovery, guard, &mutation);
    check(!result.ok(), description);
    check(ReadBytes(live / L"Keep" / L"state.bin") == liveBytes &&
          ReadBytes(outside / L"Alpha" / L"Default" / L"state.bin") == outsideBytes,
          "Rejected commit preserves original live and outside data");
    check(mode == 7 ? fs::is_directory(recovery) && fs::is_empty(recovery) : !fs::exists(recovery),
          "Rejected commit preserves the recovery namespace without overwriting another item");
    if (guard) {
      check(mutation.called, "Mutation happens inside the final precommit guard");
      check(mode == 8 ? !mutation.attempted && mutation.error == ERROR_SHARING_VIOLATION : mutation.attempted,
            "Guard mutation is observed or bound root replacement is prevented");
    }
    if (mode == 10) {
      check(ReadBytes(stagedFile) == stagedBytes && fs::exists(staged.stagingRoot),
            "Actual injected second move failure rolls back and keeps the sealed payload");
      const auto retry = CtBackup::CommitStagedRestore(staged, live, recovery);
      check(retry.ok() && ReadBytes(live / L"Alpha" / L"Default" / L"state.bin") == stagedBytes &&
            ReadBytes(recovery / L"Keep" / L"state.bin") == liveBytes && !fs::exists(staged.stagingRoot),
            "Rolled-back bound directory move can retry successfully with original recovery intact");
    }
    if (!junction.empty()) check(RemoveDirectoryW(junction.c_str()) != FALSE,
                                "Junction fixture cleanup removes only the link object");
    if (mode == 9) check(RemoveDirectoryW(stagedFile.parent_path().c_str()) != FALSE,
                         "Guard junction cleanup removes only its link");
  };
  run(0, "Reconstructed unsealed stage cannot bypass validation");
  run(1, "Identical contents in a replacement payload object are rejected");
  run(2, "Sealed payload root junction substitution is rejected");
  run(3, "Sealed staging root junction substitution is rejected");
  run(4, "Sealed child directory junction substitution is rejected");
  run(5, "Guard-time equal-length mutation with restored timestamps is rejected by SHA-256");
  run(6, "Guard-time newly added file is rejected");
  run(7, "Guard-time recovery destination creation is rejected without overwrite");
  run(8, "Bound payload root rejects guard-time rename and preserves the sealed object");
  run(9, "Guard-time child junction substitution is rejected");
  run(10, "Actual second bound directory move failure exercises recovery rollback");
  run(11, "Identical contents in a replacement staging root object are rejected");
  run(12, "Identical contents in a replacement child directory object are rejected");
  const fs::path collisionFixture = root / L"occupied-stage";
  const fs::path collisionSource = collisionFixture / L"source" / L"Sites";
  fs::create_directories(collisionSource / L"Alpha" / L"Default");
  check(WriteBytes(collisionSource / L"Alpha" / L"Default" / L"state.bin", stagedBytes),
        "Occupied-stage fixture source written");
  const fs::path archive = collisionFixture / L"backup.7z";
  check(CtBackup::CreateValidated7zBackup(collisionSource, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr).ok(),
        "Occupied-stage fixture creates a verified archive");
  const fs::path occupied = collisionFixture / L"stage";
  fs::create_directory(occupied);
  check(WriteBytes(occupied / L"unrelated.bin", outsideBytes), "Occupied stage has unrelated bytes before extraction");
  CtBackup::StagedRestore rejected;
  check(!CtBackup::StageValidated7zRestore(archive, occupied, nullptr, nullptr, rejected).ok() &&
        ReadBytes(occupied / L"unrelated.bin") == outsideBytes && !fs::exists(occupied / L"Sites") &&
        rejected.stagingRoot.empty(), "Native staging refuses a nonempty existing location without deleting its data");
  const fs::path linkedStage = collisionFixture / L"linked-stage";
  check(CreateFixtureJunction(linkedStage, occupied), "Occupied staging root junction fixture created");
  check(!CtBackup::StageValidated7zRestore(archive, linkedStage, nullptr, nullptr, rejected).ok() &&
        ReadBytes(occupied / L"unrelated.bin") == outsideBytes,
        "Native staging refuses a root junction without deleting its link or outside target");
  check(RemoveDirectoryW(linkedStage.c_str()) != FALSE, "Occupied stage junction cleanup removes only the link object");
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    // Stay inside the product's reserved new-client Win32 path budget even
    // when the host TEMP directory is longer than the project fixture root.
    const fs::path fixture = root / (L"t" + std::to_wstring(scenario));
    CtBackup::StagedRestore staged{fixture / L"stage", fixture / L"stage" / L"Sites", 1};
    const fs::path live = fixture / L"live" / L"Sites", recovery = fixture / L"recovery";
    const fs::path stagedFile = staged.payloadDir / L"Alpha" / L"Default" / L"state.bin";
    fs::create_directories(stagedFile.parent_path()); fs::create_directories(live / L"Keep");
    check(WriteBytes(stagedFile, stagedBytes) && WriteBytes(live / L"Keep" / L"state.bin", liveBytes),
          "Rename-retry fixture stores distinct staged and live bytes");
    check(CtBackup::CaptureStagedRestoreIdentity(staged).ok(), "Rename-retry fixture seals its exact real tree");
    RestoreMutation mutation;
    if (scenario == 0) CtBackup::DenyRestoreRenameAttemptsForTest(2, 3);
    if (scenario == 1) CtBackup::DenyRestoreRenameAttemptsForTest(2, 32);
    if (scenario == 2) {
      mutation = {recovery, {}, 3};
      CtBackup::DenyRestoreRenameAttemptsForTest(1, 1, MutateRestoreDuringRenameRetry, &mutation);
    }
    if (scenario == 3) {
      mutation = {staged.payloadDir, fixture / L"moved-payload", 4};
      CtBackup::DenyRestoreRenameAttemptsForTest(2, 1, MutateRestoreDuringRenameRetry, &mutation);
    }
    const auto result = CtBackup::CommitStagedRestore(staged, live, recovery);
    if (scenario == 0 || scenario == 3) {
      check(result.ok() && ReadBytes(live / L"Alpha" / L"Default" / L"state.bin") == stagedBytes &&
            ReadBytes(recovery / L"Keep" / L"state.bin") == liveBytes && !fs::exists(staged.stagingRoot),
            "Transient access denial retries promotion with exact installed and recovery bytes");
      check(CtBackup::RestoreRenameAttemptsForTest() >= (scenario == 0 ? 4u : 2u) &&
            CtBackup::RestoreRenameAttemptsForTest() <= 8,
            "Bound restore rename uses at most eight attempts");
      if (scenario == 3)
        check(mutation.called && !mutation.attempted && mutation.error == ERROR_SHARING_VIOLATION &&
              !fs::exists(mutation.destination), "Source root remains protected against namespace replacement during retry backoff");
    } else if (scenario == 1) {
      check(!result.ok() && result.code == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) &&
            CtBackup::RestoreRenameAttemptsForTest() == 8,
            "Persistent access denial exhausts the bounded attempts and preserves its exact error");
      check(ReadBytes(live / L"Keep" / L"state.bin") == liveBytes && ReadBytes(stagedFile) == stagedBytes &&
            !fs::exists(recovery), "Exhausted second promotion retries roll back exact old live and keep the sealed staged payload");
      CtBackup::DenyRestoreRenameAttemptsForTest(0, 0);
      check(CtBackup::CommitStagedRestore(staged, live, recovery).ok() &&
            ReadBytes(live / L"Alpha" / L"Default" / L"state.bin") == stagedBytes &&
            ReadBytes(recovery / L"Keep" / L"state.bin") == liveBytes,
            "An exhausted promotion can later retry the same valid sealed payload safely");
    } else {
      check(mutation.called && mutation.attempted && !result.ok() &&
            result.code == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && CtBackup::RestoreRenameAttemptsForTest() == 1,
            "A destination created during backoff rejects before a second native move attempt");
      check(ReadBytes(recovery / L"foreign.bin") == std::vector<unsigned char>{'K','E','E','P'} &&
            ReadBytes(live / L"Keep" / L"state.bin") == liveBytes && ReadBytes(stagedFile) == stagedBytes,
            "Retry never replaces newly occupied destination bytes or disturbs original live/staged data");
    }
  }
  CtBackup::DenyRestoreRenameAttemptsForTest(0, 0);
  std::cout << "Restore identity regressions: " << checks << " checks passed.\n";
}

static void CheckBackupStaging(const fs::path &root, const fs::path &sites) {
  unsigned checks = 0;
  const auto check = [&](bool passed, const char *description) {
    if (!passed)
      throw std::runtime_error(description);
    ++checks;
    std::cout << "PASS " << description << '\n';
  };
  const fs::path output = root / L"bounded-staging";
  fs::create_directories(output);
  const std::vector<unsigned char> sentinel{'O', 'L', 'D'};
  const fs::path finalNameCollision = output / L"deadbeef.7z";
  forcedBackupGuids = {0xDEADBEEF, 0x22222222, 0x33333333};
  auto forcedResult = CtBackup::CreateValidated7zBackup(
      sites, finalNameCollision, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  check(forcedResult.ok() && fs::is_regular_file(finalNameCollision),
        "Final-name GUID collision cannot let manifest cleanup delete the completed backup");
  _7zArchiveInfo collisionInfo{};
  check(SUCCEEDED(_7zInspect7z(finalNameCollision.c_str(), &collisionInfo)),
        "Backup with a deliberately colliding final filename remains a valid archive");
  fs::remove(finalNameCollision);
  const fs::path stagedCollision = output / L"11111111.7z";
  check(WriteBytes(stagedCollision, sentinel), "Deterministic staging collision sentinel created");
  forcedBackupGuids = {0x11111111, 0x22222222, 0x11111111, 0x33333333};
  forcedResult = CtBackup::CreateValidated7zBackup(
      sites, output / L"forced-collision.7z", CTSPACES_VERSION_TEXT, nullptr, nullptr);
  check(forcedResult.ok() && ReadBytes(stagedCollision) == sentinel &&
            !fs::exists(output / L"22222222.7z") && !fs::exists(output / L"33333333.7z"),
        "Manifest and archive GUID collisions retry without deleting unrelated files");
  forcedBackupGuids.clear();
  fs::remove(stagedCollision);
  fs::remove(output / L"forced-collision.7z");
  for (const size_t length : {size_t{249}, size_t{259}}) {
    const fs::path destination = output /
        (std::wstring(length - output.native().size() - 4, L'x') + L".7z");
    check(destination.native().size() == length &&
              WriteBytes(destination, sentinel),
          "Existing near-MAX_PATH backup target is valid");
    auto result = CtBackup::CreateValidated7zBackup(
        sites, destination, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    check(result.ok() && ReadBytes(destination) != sentinel,
          "Valid near-MAX_PATH backup succeeds and replaces sentinel");
    CtBackup::StagedRestore staged;
    const fs::path stage = root / L"long-target-stage";
    result = CtBackup::StageValidated7zRestore(destination, stage, nullptr,
                                            nullptr, staged);
    check(result.ok() && staged.profileCount == 2,
          "Near-MAX_PATH backup verifies and restores both clients");
    fs::remove_all(stage);
  }
  const fs::path deepBase = output / L"deep";
  const fs::path deep = deepBase /
      std::wstring(CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters -
                       deepBase.native().size() - 1, L'd');
  fs::create_directories(deep);
  const fs::path deepBackup = deep / L"x.7z";
  auto result = CtBackup::CreateValidated7zBackup(
      sites, deepBackup, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  check(result.ok() && fs::is_regular_file(deepBackup),
        "Backup stages safely beside a maximum-budget directory");
  check(std::distance(fs::directory_iterator(deep),
                      fs::directory_iterator{}) == 1,
        "Successful long-directory backup leaves no manifest or staging file");

  const fs::path tooLong = output /
      (std::wstring(260 - output.native().size() - 4, L'z') + L".7z");
  result = CtBackup::CreateValidated7zBackup(
      sites, tooLong, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  check(result.code == HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE) &&
            result.details.find(L"too long") != std::wstring::npos,
        "Over-budget output reports its actual path-length failure");

  const fs::path ordinary = output / L"ordinary.7z";
  result = CtBackup::CreateValidated7zBackup(
      sites, ordinary, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  check(result.ok(), "Backup failure-cleanup fixture created");
  const auto saved = ReadBytes(ordinary);
  const auto itemCount = std::distance(fs::directory_iterator(output),
                                       fs::directory_iterator{});
  HANDLE locked = CreateFileW(ordinary.c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  check(locked != INVALID_HANDLE_VALUE, "Previous backup locked against replacement");
  result = CtBackup::CreateValidated7zBackup(
      sites, ordinary, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  CloseHandle(locked);
  check(!result.ok() && ReadBytes(ordinary) == saved,
        "Failed replacement preserves previous backup bytes");
  check(std::distance(fs::directory_iterator(output),
                      fs::directory_iterator{}) == itemCount,
        "Failed replacement removes owned archive and manifest");

  const fs::path sourceFile = sites / L"Alpha" / L"Default" / L"state.bin";
  locked = CreateFileW(sourceFile.c_str(), GENERIC_READ, 0, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  check(locked != INVALID_HANDLE_VALUE, "Backup source file locked unreadable");
  result = CtBackup::CreateValidated7zBackup(
      sites, ordinary, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  CloseHandle(locked);
  check(!result.ok() && ReadBytes(ordinary) == saved,
        "Partial compression failure preserves previous backup");
  check(std::distance(fs::directory_iterator(output),
                      fs::directory_iterator{}) == itemCount,
        "Partial compression failure removes its owned staging file");

  const fs::path manifest = output / L"ownership.manifest";
  check(WriteBackupManifest(manifest, 2), "Collision ownership manifest created");
  const fs::path collision = output / L"collision.7z";
  check(WriteBytes(collision, sentinel), "Existing staging collision sentinel created");
  bool created = true;
  const HRESULT collisionResult = _7zCompress7zWithExtraFile(
      collision.c_str(), sites.c_str(), true, manifest.c_str(),
      L"ctSpacesBackup.manifest", nullptr, nullptr, &created);
  check(FAILED(collisionResult) && !created && ReadBytes(collision) == sentinel,
        "Atomic output collision reports no ownership and preserves bytes");
  const fs::path owned = output / L"owned.7z";
  const HRESULT ownedResult = _7zCompress7zWithExtraFile(
      owned.c_str(), sites.c_str(), true, manifest.c_str(),
      L"ctSpacesBackup.manifest", nullptr, nullptr, &created);
  check(SUCCEEDED(ownedResult) && created && fs::is_regular_file(owned),
        "Successful compression reports ownership of newly created output");
  fs::remove(manifest);
  fs::remove(collision);
  fs::remove(owned);
  const fs::path hardlink = output / L"hardlink.7z";
  const auto sourceBytes = ReadBytes(sourceFile);
  check(CreateHardLinkW(hardlink.c_str(), sourceFile.c_str(), nullptr) != FALSE,
        "Backup destination hardlink fixture created");
  result = CtBackup::CreateValidated7zBackup(
      sites, hardlink, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  check(result.ok() && ReadBytes(sourceFile) == sourceBytes &&
            ReadBytes(hardlink) != sourceBytes,
        "Replacing a destination hardlink leaves linked client source bytes intact");
  fs::remove(hardlink);
  const fs::path linkTarget = output / L"link-target.bin";
  const fs::path symbolic = output / L"symbolic.7z";
  check(WriteBytes(linkTarget, sentinel), "Backup destination symlink target fixture created");
  if (CreateSymbolicLinkW(symbolic.c_str(), linkTarget.c_str(),
                          SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    result = CtBackup::CreateValidated7zBackup(
        sites, symbolic, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    check(result.ok() && ReadBytes(linkTarget) == sentinel &&
              (GetFileAttributesW(symbolic.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
          "Backup replaces the destination symlink entry without overwriting its target");
    fs::remove(symbolic);
  } else {
    std::cout << "SKIP destination file symlink: Win32 " << GetLastError() << '\n';
  }
  fs::remove(linkTarget);
  for (unsigned iteration = 0; iteration < 20; ++iteration) {
    result = CtBackup::CreateValidated7zBackup(
        sites, ordinary, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    check(result.ok(), "Repeated bounded sibling backup remains valid");
  }
  check(std::distance(fs::directory_iterator(output),
                      fs::directory_iterator{}) == itemCount,
        "Repeated backups leave no staged archives or manifest files");
  std::cout << "Backup staging: " << checks << " regression checks passed.\n";
}

static void CheckBackupFailures(const fs::path &root, const fs::path &sites,
                                const fs::path &validArchive) {
  unsigned checks = 0;
  const auto check = [&](bool passed, const char *description) {
    if (!passed) throw std::runtime_error(description);
    ++checks; std::cout << "PASS " << description << '\n';
  };
  const fs::path fixture = root / L"failure-stress";
  fs::create_directory(fixture);
  const auto original = ReadBytes(validArchive);
  for (unsigned i = 0; i < 55; ++i) {
    const fs::path truncated = fixture / L"truncated.7z";
    check(WriteBytes(truncated, std::vector<unsigned char>(original.begin(),
              original.begin() + original.size() * i / 55)),
          "Truncated archive stress fixture written");
    CtBackup::StagedRestore staged;
    const fs::path stage = fixture / L"truncated-stage";
    const auto result = CtBackup::StageValidated7zRestore(
        truncated, stage, nullptr, nullptr, staged);
    check(!result.ok() && staged.stagingRoot.empty(),
          "Truncated archive rejects restore without a commit-ready result");
    check(!fs::exists(stage), "Rejected truncation removes extraction staging");
  }
  const fs::path manifest = fixture / L"manifest";
  const fs::path malformed = fixture / L"malformed.7z";
  for (const std::string &count : {std::string("0"),
          std::string("18446744073709551616"), std::string("-1"),
          std::string("3"), std::string("2\nprofileCount=2")}) {
    const std::string fields =
        "format=ctSpaces-backup\nformatVersion=1\nappVersion="
        CTSPACES_VERSION_TEXT "\ncreatedUtc=2026-10-14T00:00:00Z\n"
        "payload=Sites\nprofileCount=" + count + "\n";
    check(WriteBytes(manifest, std::vector<unsigned char>(fields.begin(), fields.end())),
          "Malformed manifest stress fixture written");
    fs::remove(malformed);
    check(SUCCEEDED(_7zCompress7zWithExtraFile(malformed.c_str(), sites.c_str(),
          true, manifest.c_str(), L"ctSpacesBackup.manifest", nullptr, nullptr)),
          "Malformed manifest compressed into otherwise valid archive");
    CtBackup::StagedRestore staged;
    const fs::path stage = fixture / L"manifest-stage";
    const auto result = CtBackup::StageValidated7zRestore(
        malformed, stage, nullptr, nullptr, staged);
    check(!result.ok() && !fs::exists(stage) && staged.stagingRoot.empty(),
          "Invalid or mismatched profile count rejects restore and cleans staging");
  }
  CtBackup::StagedRestore staged;
  auto result = CtBackup::StageValidated7zRestore(validArchive,
      fixture / L"rollback-stage", nullptr, nullptr, staged);
  check(result.ok(), "Restore swap-failure fixture stages a valid backup");
  const fs::path live = fixture / L"live" / L"Sites";
  const fs::path keep = live / L"Keep" / L"state.bin";
  fs::create_directories(keep.parent_path());
  const std::vector<unsigned char> sentinel{'L','I','V','E'};
  check(WriteBytes(keep, sentinel), "Restore rollback fixture preserves distinct live data");
  HANDLE lock = CreateFileW(staged.payloadDir.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  check(lock != INVALID_HANDLE_VALUE, "Staged payload locked against its second swap rename");
  const fs::path recovery = fixture / L"recovery";
  result = CtBackup::CommitStagedRestore(staged, live, recovery);
  CloseHandle(lock);
  check(!result.ok() && ReadBytes(keep) == sentinel && !fs::exists(recovery),
        "Locked payload is rejected before moving the original Sites");
  check(fs::is_regular_file(staged.payloadDir / L"Alpha" / L"Default" / L"state.bin"),
        "Failed directory swap keeps the complete verified restore payload");
  result = CtBackup::CommitStagedRestore(staged, live, recovery);
  if (!result.ok()) std::wcerr << L"Retry failure: " << std::hex << result.code << L" " << result.details << L"\n";
  check(result.ok() && ReadBytes(recovery / L"Keep" / L"state.bin") == sentinel,
        "Restore retries successfully after the lock is released and retains recovery");
  check(fs::is_regular_file(live / L"Alpha" / L"Default" / L"state.bin") &&
            !fs::exists(staged.stagingRoot),
        "Successful retry installs the backup and removes extraction staging");
  std::cout << "Backup failure stress: " << checks << " checks passed.\n";
}

#include "ArchiveSourceIsolationTests.inl"
#include "BackupPublicationTests.inl"
#include "BackupProfileCountTests.inl"
#include "ArchiveOutputOwnershipTests.inl"

int wmain(int argc, wchar_t **argv) {
  const fs::path root =
      fs::temp_directory_path() /
      (L"ctSpaces-archive-roundtrip-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  const fs::path source = root / L"source";
  const fs::path extracted = root / L"extracted";
  const fs::path archive = root / L"roundtrip.7z";

  struct Cleanup {
    fs::path path;
    bool passed = false;
    ~Cleanup() {
      if (!passed) {
        std::wcerr << L"Failed archive fixture retained: " << path << L'\n';
        return;
      }
      std::error_code error;
      fs::remove_all(path, error);
    }
  } cleanup{root};

  try {
    if(argc==2 && std::wstring(argv[1])==L"--output-inventory") {
      TestArchiveOutputOwnership(root/L"oi");cleanup.passed=true;return 0;
    }
    if(argc==2 && std::wstring(argv[1])==L"--profile-count") {
      TestBackupProfileCountReconciliation(root/L"pc");cleanup.passed=true;return 0;
    }
    fs::create_directories(source / L"nested" / L"empty");

    const std::vector<unsigned char> rootData = {
        'c', 't', 'S', 'p', 'a', 'c', 'e', 's', '\r', '\n'};
    std::vector<unsigned char> binaryData(256 * 1024);
    for (size_t i = 0; i < binaryData.size(); ++i)
      binaryData[i] = static_cast<unsigned char>((i * 37u) & 0xFFu);

    if (!WriteBytes(source / L"root.txt", rootData) ||
        !WriteBytes(source / L"nested" / L"data.bin", binaryData) ||
        !WriteBytes(source / L"nested" / L"unicode-\x00E9.txt", rootData)) {
      std::wcerr << L"Could not create the archive test fixture.\n";
      return 1;
    }

    _7zSetHInstance(GetModuleHandleW(nullptr));
    TestArchiveSourceIsolation(root/L"source-isolation");
    TestRestoreDestinationParentBindings(root);
    TestOwnedExtractionCleanupRetries(root);
    TestArchiveOutputOwnership(root/L"oi");
    TestBackupProfileCountReconciliation(root/L"pc");
    TestBackupPublicationOwnership(root/L"output-ownership");

    const std::vector<std::wstring> invalidClientNames = {
        L"",          L" Default", L"Client ", L"Client.",
        L"Default",   L"tEmP",     L"CON",     L"con.txt",
        L"PRN",       L"AUX.log",  L"NUL",     L"CLOCK$",
        L"CONIN$",    L"CONOUT$",  L"COM1",    L"com9.txt",
        L"LPT1",      L"lpt9.log", L"COM\u00B9",    L"LPT\u00B2.txt",
        L"Bad/Name",  std::wstring(241, L'A')};
    for (const std::wstring &name : invalidClientNames) {
      if (CtBackup::Detail::IsValidClientDirectoryName(name)) {
        std::wcerr << L"An invalid or reserved client name was accepted: "
                   << name << L"\n";
        return 22;
      }
    }
    const std::vector<std::wstring> validClientNames = {
        L"Legacy Client", L"Default Client", L"Tempest", L"Client.Name",
        L"unicode-\u00E9", std::wstring(240, L'A')};
    for (const std::wstring &name : validClientNames) {
      if (!CtBackup::Detail::IsValidClientDirectoryName(name)) {
        std::wcerr << L"A valid client name was rejected: " << name << L"\n";
        return 23;
      }
    }

    const fs::path budgetSites =
        root / L"path-budget-location" / L"Sites";
    const size_t budgetSitesPrefix =
        fs::absolute(budgetSites).native().size() + 1;
    if (budgetSitesPrefix +
                CtBackup::Detail::kNewClientManagedFileTailCharacters >=
            CtBackup::Detail::kLegacyMaximumFilePathCharacters ||
        budgetSitesPrefix +
                CtBackup::Detail::kNewClientManagedDirectoryTailCharacters >=
            CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters) {
      std::wcerr << L"The test root leaves no client path-budget fixture.\n";
      return 43;
    }
    const size_t maximumBudgetedClientName = (std::min)(
        CtBackup::Detail::kLegacyMaximumFilePathCharacters -
            CtBackup::Detail::kNewClientManagedFileTailCharacters -
            budgetSitesPrefix,
        CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters -
            CtBackup::Detail::kNewClientManagedDirectoryTailCharacters -
            budgetSitesPrefix);
    const std::wstring maximumBudgetedName(maximumBudgetedClientName, L'N');
    const std::wstring overBudgetName(maximumBudgetedClientName + 1, L'N');
    if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
            budgetSites, maximumBudgetedName) ||
        CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
            budgetSites, overBudgetName)) {
      std::wcerr << L"The new-client path budget boundary is incorrect.\n";
      return 44;
    }

    std::wstring treeBudgetError;
    if (!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
            source, root / L"tree-budget-target", &treeBudgetError) ||
        CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
            source, root / std::wstring(240, L'Z'), &treeBudgetError)) {
      std::wcerr << L"The mapped tree path budget boundary is incorrect.\n";
      return 45;
    }

    const fs::path budgetRestoreStage = root / L"budget-restore-stage";
    const fs::path budgetRestorePayload = budgetRestoreStage / L"Sites";
    fs::create_directories(budgetRestorePayload / L"Alpha");
    if (!WriteBytes(budgetRestorePayload / L"Alpha" / L"state.bin",
                    rootData)) {
      return 46;
    }
    const size_t allowedClientRootLength =
        CtBackup::Detail::kLegacyMaximumFilePathCharacters -
        CtBackup::Detail::kNewClientManagedFileTailCharacters;
    const fs::path minimumPaddedLiveRoot = root / L"P";
    const size_t minimumClientRootLength =
        fs::absolute(minimumPaddedLiveRoot / L"Sites" / L"Alpha")
            .native()
            .size();
    const size_t livePaddingLength =
        minimumClientRootLength > allowedClientRootLength
            ? 1
            : 1 + allowedClientRootLength - minimumClientRootLength + 1;
    const fs::path budgetLiveRoot =
        root / std::wstring(livePaddingLength, L'P');
    const fs::path budgetLiveSites = budgetLiveRoot / L"Sites";
    const fs::path budgetRecovery = budgetLiveRoot / L"Recovery";
    fs::create_directories(budgetLiveSites / L"OldClient");
    if (!WriteBytes(budgetLiveSites / L"OldClient" / L"old.bin", rootData)) {
      return 47;
    }
    CtBackup::StagedRestore overBudgetRestore{
        budgetRestoreStage, budgetRestorePayload, 1};
    if (!CtBackup::CaptureStagedRestoreIdentity(overBudgetRestore).ok()) return 48;
    const CtBackup::Result overBudgetCommit =
        CtBackup::CommitStagedRestore(overBudgetRestore, budgetLiveSites,
                                      budgetRecovery);
    if (overBudgetCommit.ok() ||
        !fs::is_regular_file(budgetLiveSites / L"OldClient" / L"old.bin") ||
        !fs::is_regular_file(budgetRestorePayload / L"Alpha" / L"state.bin") ||
        fs::exists(budgetRecovery)) {
      std::wcerr
          << L"An over-budget restore moved or replaced current Sites data.\n";
      return 48;
    }

    const fs::path guardStage = root / L"guard-stage";
    const fs::path guardPayload = guardStage / L"Sites";
    const fs::path guardLiveRoot = root / L"g";
    const fs::path guardLiveSites = guardLiveRoot / L"Sites";
    const fs::path guardRecovery = guardLiveRoot / L"Recovery";
    fs::create_directories(guardPayload / L"Alpha");
    fs::create_directories(guardLiveSites / L"OldClient");
    if (!WriteBytes(guardPayload / L"Alpha" / L"state.bin", rootData) ||
        !WriteBytes(guardLiveSites / L"OldClient" / L"old.bin", rootData)) {
      return 49;
    }
    RestoreGuardState guardState;
    CtBackup::StagedRestore guardedRestore{guardStage, guardPayload, 1};
    if (!CtBackup::CaptureStagedRestoreIdentity(guardedRestore).ok()) return 50;
    const CtBackup::Result guardResult = CtBackup::CommitStagedRestore(
        guardedRestore, guardLiveSites, guardRecovery, RejectRestoreCommit,
        &guardState);
    if (guardResult.ok() || !guardState.called ||
        !fs::is_regular_file(guardLiveSites / L"OldClient" / L"old.bin") ||
        !fs::is_regular_file(guardPayload / L"Alpha" / L"state.bin") ||
        fs::exists(guardRecovery)) {
      std::wcerr << L"A rejected final restore guard changed live or staged "
                    L"data.\n";
      return 50;
    }

    const fs::path recoveryStage = root / L"recovery-stage";
    const fs::path recoveryPayload = recoveryStage / L"Sites";
    const fs::path recoveryLiveRoot = root / L"r";
    const fs::path recoveryLiveSites = recoveryLiveRoot / L"Sites";
    const fs::path overBudgetRecovery =
        recoveryLiveRoot / L"Sites_PreRestore_recovery_path_budget";
    fs::create_directories(recoveryPayload / L"Alpha");
    fs::create_directories(recoveryLiveSites / L"OldClient");
    if (!WriteBytes(recoveryPayload / L"Alpha" / L"state.bin", rootData)) {
      return 51;
    }
    const fs::path recoveryClientRoot =
        recoveryLiveSites / L"OldClient";
    const size_t recoveryClientRootLength =
        fs::absolute(recoveryClientRoot).native().size();
    if (recoveryClientRootLength + 2 >=
        CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters) {
      return 52;
    }
    const size_t deepDirectoryNameLength =
        CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters -
        recoveryClientRootLength - 2;
    const fs::path deepCurrentDirectory =
        recoveryClientRoot /
        std::wstring(deepDirectoryNameLength, L'D');
    fs::create_directories(deepCurrentDirectory);
    if (!WriteBytes(deepCurrentDirectory / L"x.bin", rootData)) {
      return 53;
    }
    CtBackup::StagedRestore recoveryBudgetRestore{
        recoveryStage, recoveryPayload, 1};
    if (!CtBackup::CaptureStagedRestoreIdentity(recoveryBudgetRestore).ok()) return 54;
    RestoreGuardState recoveryGuardState;
    const CtBackup::Result recoveryBudgetResult =
        CtBackup::CommitStagedRestore(
            recoveryBudgetRestore, recoveryLiveSites, overBudgetRecovery,
            RejectRestoreCommit, &recoveryGuardState);
    if (recoveryBudgetResult.ok() || recoveryGuardState.called ||
        !fs::is_regular_file(deepCurrentDirectory / L"x.bin") ||
        !fs::is_regular_file(recoveryPayload / L"Alpha" / L"state.bin") ||
        fs::exists(overBudgetRecovery)) {
      std::wcerr << L"An over-budget recovery mapping changed live or staged "
                    L"data or reached the final guard.\n";
      return 54;
    }

    ProgressState compressProgress;
    const HRESULT compressResult =
        _7zCompress7z(archive.c_str(), source.c_str(), false, RecordProgress,
                      &compressProgress);
    if (FAILED(compressResult) || !fs::exists(archive) ||
        fs::file_size(archive) == 0 || compressProgress.highestPercent != 100) {
      std::wcerr << L"Compression failed: 0x" << std::hex
                 << static_cast<unsigned long>(compressResult) << L"\n";
      return 2;
    }

    ProgressState extractProgress;
    const HRESULT extractResult =
        _7zExtra_7z(archive.c_str(), extracted.c_str(), RecordProgress,
                    &extractProgress);
    if (FAILED(extractResult) || extractProgress.highestPercent != 100) {
      std::wcerr << L"Extraction failed: 0x" << std::hex
                 << static_cast<unsigned long>(extractResult) << L"\n";
      return 3;
    }

    if (ReadBytes(extracted / L"root.txt") != rootData ||
        ReadBytes(extracted / L"nested" / L"data.bin") != binaryData ||
        ReadBytes(extracted / L"nested" / L"unicode-\x00E9.txt") != rootData ||
        !fs::is_directory(extracted / L"nested" / L"empty")) {
      std::wcerr << L"Archive round-trip content did not match.\n";
      return 4;
    }

    if (argc >= 2) {
      const fs::path starterArchive = argv[1];
      const fs::path starterExtracted = root / L"starter";
      ProgressState starterProgress;
      const HRESULT starterResult =
          _7zExtra_7z(starterArchive.c_str(), starterExtracted.c_str(),
                      RecordProgress, &starterProgress);
      if (FAILED(starterResult) || starterProgress.highestPercent != 100 ||
          !fs::is_regular_file(starterExtracted / L"Default" / L"Bookmarks") ||
          !fs::is_regular_file(starterExtracted / L"Default" / L"Favicons")) {
        std::wcerr << L"Starter-profile extraction failed: 0x" << std::hex
                   << static_cast<unsigned long>(starterResult) << L"\n";
        return 5;
      }
    }

    const fs::path backupSites = root / L"backup-source" / L"Sites";
    const fs::path alphaProfile = backupSites / L"Alpha" / L"Default";
    const fs::path betaProfile = backupSites / L"Beta" / L"Default";
    fs::create_directories(alphaProfile);
    fs::create_directories(betaProfile / L"empty");
    const fs::path hiddenPath = alphaProfile / L"hidden-state.bin";
    const std::vector<unsigned char> notesData = {
        'C', 'a', 'f', 0xc3, 0xa9, '\r', '\n', 'R', 'e', 'm', 'i', 'n', 'd', 'e', 'r'};
    std::string notebook;
    if (!client_notes::SerializeNotebook(
            {{L"Notes", true, "{\\rtf1 Legacy reminder}", {}},
             {L"INC-123", true, "{\\rtf1\\b Formatted ticket\\b0}", {}}}, notebook))
      return 5;
    const std::vector<unsigned char> notebookData(notebook.begin(), notebook.end());
    if (!WriteBytes(alphaProfile / L"state.bin", binaryData) ||
        !WriteBytes(backupSites / L"Alpha" / client_notes::kFileName, notesData) ||
        !WriteBytes(backupSites / L"Alpha" / client_notes::kNotebookFileName, notebookData) ||
        !WriteBytes(alphaProfile / L"unicode-\x00E9.txt", rootData) ||
        !WriteBytes(hiddenPath, rootData) ||
        !WriteBytes(betaProfile / L"bookmarks.bin", rootData) ||
        !SetFileAttributesW(hiddenPath.c_str(),
                            GetFileAttributesW(hiddenPath.c_str()) |
                                FILE_ATTRIBUTE_HIDDEN)) {
      std::wcerr << L"Could not create the backup workflow fixture.\n";
      return 5;
    }

    const fs::path managedDataRoot = backupSites.parent_path();
    const fs::path internalStarterArchive = managedDataRoot / L"Default.7z";
    if (!WriteBytes(internalStarterArchive, rootData))
      return 24;
    CtBackup::Result internalStarterResult =
        CtBackup::CreateValidated7zBackup(
            backupSites, internalStarterArchive, CTSPACES_VERSION_TEXT,
            nullptr, nullptr);
    if (internalStarterResult.ok() ||
        ReadBytes(internalStarterArchive) != rootData) {
      std::wcerr
          << L"An application-managed Default.7z destination was accepted.\n";
      return 25;
    }

    const fs::path internalNestedDestination =
        managedDataRoot / L"_DefBak" / L"nested-backup.7z";
    CtBackup::Result internalNestedResult =
        CtBackup::CreateValidated7zBackup(
            backupSites, internalNestedDestination, CTSPACES_VERSION_TEXT,
            nullptr, nullptr);
    if (internalNestedResult.ok() || fs::exists(internalNestedDestination)) {
      std::wcerr << L"A nested application-managed destination was accepted.\n";
      return 26;
    }

    const fs::path caseVariantInternalDestination =
        root / L"BACKUP-SOURCE" / L"case-variant.7z";
    CtBackup::Result caseVariantInternalResult =
        CtBackup::CreateValidated7zBackup(
            backupSites, caseVariantInternalDestination,
            CTSPACES_VERSION_TEXT, nullptr, nullptr);
    if (caseVariantInternalResult.ok() ||
        fs::exists(caseVariantInternalDestination)) {
      std::wcerr
          << L"A case-variant application-managed destination was accepted.\n";
      return 27;
    }

    const fs::path managedAlias = root / L"managed-data-alias";
    if (TryCreateDirectorySymlink(managedAlias, managedDataRoot)) {
      const fs::path aliasDestination = managedAlias / L"alias-backup.7z";
      CtBackup::Result aliasDestinationResult =
          CtBackup::CreateValidated7zBackup(
              backupSites, aliasDestination, CTSPACES_VERSION_TEXT, nullptr,
              nullptr);
      if (aliasDestinationResult.ok() || fs::exists(aliasDestination)) {
        std::wcerr
            << L"A reparse alias into application-managed data was accepted.\n";
        return 28;
      }
    }

    const fs::path rootFileSites =
        root / L"root-file-source" / L"Sites";
    fs::create_directories(rootFileSites / L"Legacy" / L"Default");
    if (!WriteBytes(rootFileSites / L"unexpected.bin", rootData))
      return 29;
    const fs::path rootFileBackup = root / L"root-file-backup.7z";
    CtBackup::Result rootFileBackupResult =
        CtBackup::CreateValidated7zBackup(
            rootFileSites, rootFileBackup, CTSPACES_VERSION_TEXT, nullptr,
            nullptr);
    if (rootFileBackupResult.ok() || fs::exists(rootFileBackup)) {
      std::wcerr << L"A direct file in Sites was accepted for backup.\n";
      return 30;
    }

    for (const std::wstring &reservedName :
         {std::wstring(L"Default"), std::wstring(L"Temp")}) {
      const fs::path invalidNameSites =
          root / (L"invalid-name-source-" + reservedName) / L"Sites";
      fs::create_directories(invalidNameSites / reservedName / L"ProfileData");
      const fs::path invalidNameBackup =
          root / (L"invalid-name-" + reservedName + L".7z");
      CtBackup::Result invalidNameResult =
          CtBackup::CreateValidated7zBackup(
              invalidNameSites, invalidNameBackup, CTSPACES_VERSION_TEXT,
              nullptr, nullptr);
      if (invalidNameResult.ok() || fs::exists(invalidNameBackup)) {
        std::wcerr << L"A reserved client directory was accepted: "
                   << reservedName << L"\n";
        return 31;
      }
    }

    const fs::path reparseSites =
        root / L"reparse-source" / L"Sites";
    const fs::path reparseTarget = root / L"reparse-client-target";
    fs::create_directories(reparseSites);
    fs::create_directories(reparseTarget / L"Default");
    const fs::path reparseClient = reparseSites / L"LinkedClient";
    if (TryCreateDirectorySymlink(reparseClient, reparseTarget)) {
      const fs::path reparseBackup = root / L"reparse-source.7z";
      CtBackup::Result reparseResult =
          CtBackup::CreateValidated7zBackup(
              reparseSites, reparseBackup, CTSPACES_VERSION_TEXT, nullptr,
              nullptr);
      if (reparseResult.ok() || fs::exists(reparseBackup)) {
        std::wcerr << L"A reparse-point client directory was accepted.\n";
        return 32;
      }
    }

    const fs::path rootFileArchiveFixture =
        root / L"root-file-archive-fixture";
    fs::create_directories(rootFileArchiveFixture / L"Sites" / L"Legacy" /
                           L"Default");
    if (!WriteBackupManifest(
            rootFileArchiveFixture / L"ctSpacesBackup.manifest", 1) ||
        !WriteBytes(rootFileArchiveFixture / L"Sites" / L"unexpected.bin",
                    rootData)) {
      return 33;
    }
    const fs::path rootFileArchive = root / L"root-file-archive.7z";
    if (FAILED(_7zCompress7z(rootFileArchive.c_str(),
                            rootFileArchiveFixture.c_str(), false, nullptr,
                            nullptr))) {
      return 34;
    }
    CtBackup::StagedRestore rejectedRootFileRestore;
    const fs::path rejectedRootFileStage = root / L"root-file-stage";
    CtBackup::Result rejectedRootFileResult =
        CtBackup::StageValidated7zRestore(
            rootFileArchive, rejectedRootFileStage, nullptr, nullptr,
            rejectedRootFileRestore);
    if (rejectedRootFileResult.ok() || fs::exists(rejectedRootFileStage)) {
      std::wcerr
          << L"A restored backup with a direct Sites file was accepted.\n";
      return 35;
    }

    const fs::path reservedArchiveFixture =
        root / L"reserved-archive-fixture";
    fs::create_directories(reservedArchiveFixture / L"Sites" / L"Default" /
                           L"ProfileData");
    if (!WriteBackupManifest(
            reservedArchiveFixture / L"ctSpacesBackup.manifest", 1)) {
      return 36;
    }
    const fs::path reservedArchive = root / L"reserved-client.7z";
    if (FAILED(_7zCompress7z(reservedArchive.c_str(),
                            reservedArchiveFixture.c_str(), false, nullptr,
                            nullptr))) {
      return 37;
    }
    CtBackup::StagedRestore rejectedReservedRestore;
    const fs::path rejectedReservedStage = root / L"reserved-client-stage";
    CtBackup::Result rejectedReservedResult =
        CtBackup::StageValidated7zRestore(
            reservedArchive, rejectedReservedStage, nullptr, nullptr,
            rejectedReservedRestore);
    if (rejectedReservedResult.ok() || fs::exists(rejectedReservedStage)) {
      std::wcerr << L"A restored backup with a reserved client name was "
                    L"accepted.\n";
      return 38;
    }

    const fs::path legacySites = root / L"legacy-source" / L"Sites";
    const fs::path legacyProfile =
        legacySites / L"Legacy Customer" / L"Default";
    fs::create_directories(legacyProfile);
    if (!WriteBytes(legacyProfile / L"legacy-state.bin", rootData) ||
        fs::exists(legacySites / L"Legacy Customer" / L"ctSpaces")) {
      return 39;
    }
    const fs::path legacyBackup = root / L"legacy-profile.7z";
    CtBackup::Result legacyBackupResult =
        CtBackup::CreateValidated7zBackup(
            legacySites, legacyBackup, CTSPACES_VERSION_TEXT, nullptr,
            nullptr);
    if (!legacyBackupResult.ok()) {
      std::wcerr << L"A valid markerless legacy profile was rejected: "
                 << legacyBackupResult.details << L"\n";
      return 40;
    }
    CtBackup::StagedRestore legacyRestore;
    const fs::path legacyStage = root / L"legacy-stage";
    CtBackup::Result legacyStageResult =
        CtBackup::StageValidated7zRestore(
            legacyBackup, legacyStage, nullptr, nullptr, legacyRestore);
    if (!legacyStageResult.ok() || legacyRestore.profileCount != 1 ||
        ReadBytes(legacyRestore.payloadDir / L"Legacy Customer" / L"Default" /
                  L"legacy-state.bin") != rootData ||
        fs::exists(legacyRestore.payloadDir / L"Legacy Customer" /
                   L"ctSpaces")) {
      std::wcerr << L"A valid markerless legacy profile did not round-trip: "
                 << legacyStageResult.details << L"\n";
      return 41;
    }

    const fs::path validatedBackup = root / L"ctSpaces_Backup_test.7z";
    ProgressState backupProgress;
    CtBackup::Result backupResult = CtBackup::CreateValidated7zBackup(
        backupSites, validatedBackup, CTSPACES_VERSION_TEXT, RecordProgress,
        &backupProgress);
    if (!backupResult.ok() || !fs::is_regular_file(validatedBackup) ||
        backupProgress.highestPercent != 100) {
      std::wcerr << L"Validated backup creation failed: "
                 << backupResult.details << L"\n";
      return 6;
    }

    CtBackup::StagedRestore stagedRestore;
    CheckBackupStaging(root, backupSites);
    CheckRestoreIdentity(root / L"identity");
    CheckBackupFailures(root, backupSites, validatedBackup);
    const fs::path replacementBackup = root / L"replacement.7z";
    fs::copy_file(validatedBackup, replacementBackup);
    ReplaceDuringExtractionState replacementState{
        validatedBackup, replacementBackup};
    const fs::path restoreStage = root / L"restore-stage";
    CtBackup::Result stageResult = CtBackup::StageValidated7zRestore(
        validatedBackup, restoreStage, AttemptArchiveReplacement,
        &replacementState, stagedRestore);
    const fs::path stagedAlpha =
        stagedRestore.payloadDir / L"Alpha" / L"Default";
    if (!stageResult.ok() || stagedRestore.profileCount != 2 ||
        replacementState.highestPercent != 100 ||
        !replacementState.attempted || replacementState.replaced ||
        (replacementState.error != ERROR_SHARING_VIOLATION &&
         replacementState.error != ERROR_ACCESS_DENIED) ||
        ReadBytes(stagedAlpha / L"state.bin") != binaryData ||
        ReadBytes(stagedRestore.payloadDir / L"Alpha" /
                  client_notes::kFileName) != notesData ||
        ReadBytes(stagedRestore.payloadDir / L"Alpha" /
                  client_notes::kNotebookFileName) != notebookData ||
        ReadBytes(stagedAlpha / L"unicode-\x00E9.txt") != rootData ||
        (GetFileAttributesW((stagedAlpha / L"hidden-state.bin").c_str()) &
         FILE_ATTRIBUTE_HIDDEN) == 0 ||
        !fs::is_directory(stagedRestore.payloadDir / L"Beta" / L"Default" /
                          L"empty")) {
      std::wcerr << L"Validated backup staging failed: "
                 << stageResult.details << L"\n";
      return 7;
    }

    const fs::path liveRoot = root / L"live";
    const fs::path liveSites = liveRoot / L"Sites";
    const fs::path recovery = liveRoot / L"Sites_PreRestore_test";
    fs::create_directories(liveSites / L"OldClient");
    if (!WriteBytes(liveSites / L"OldClient" / L"old.bin", rootData))
      return 8;
    CtBackup::Result commitResult =
        CtBackup::CommitStagedRestore(stagedRestore, liveSites, recovery);
    if (!commitResult.ok() ||
        ReadBytes(liveSites / L"Alpha" / L"Default" / L"state.bin") !=
            binaryData ||
        ReadBytes(liveSites / L"Alpha" / client_notes::kFileName) != notesData ||
        ReadBytes(liveSites / L"Alpha" / client_notes::kNotebookFileName) != notebookData ||
        ReadBytes(recovery / L"OldClient" / L"old.bin") != rootData ||
        fs::exists(restoreStage)) {
      std::wcerr << L"Transactional restore commit failed: "
                 << commitResult.details << L"\n";
      return 9;
    }

    const fs::path corruptBackup = root / L"corrupt.7z";
    std::vector<unsigned char> corruptData = ReadBytes(validatedBackup);
    if (corruptData.size() < 32)
      return 10;
    corruptData[corruptData.size() / 2] ^= 0x5Au;
    if (!WriteBytes(corruptBackup, corruptData))
      return 11;
    CtBackup::StagedRestore corruptRestore;
    const fs::path corruptStage = root / L"corrupt-stage";
    CtBackup::Result corruptResult = CtBackup::StageValidated7zRestore(
        corruptBackup, corruptStage, nullptr, nullptr, corruptRestore);
    if (corruptResult.ok() || fs::exists(corruptStage)) {
      std::wcerr << L"A corrupted backup was incorrectly accepted.\n";
      return 12;
    }

    const fs::path invalidManifest = root / L"invalid.manifest";
    const std::vector<unsigned char> invalidManifestData = {
        'f', 'o', 'r', 'm', 'a', 't', '=', 'n', 'o', 't', '-', 'c', 't', '\n'};
    if (!WriteBytes(invalidManifest, invalidManifestData))
      return 13;
    const fs::path invalidBackup = root / L"invalid-manifest.7z";
    HRESULT invalidArchiveResult = _7zCompress7zWithExtraFile(
        invalidBackup.c_str(), backupSites.c_str(), true,
        invalidManifest.c_str(), L"ctSpacesBackup.manifest", nullptr,
        nullptr);
    CtBackup::StagedRestore invalidRestore;
    const fs::path invalidStage = root / L"invalid-stage";
    CtBackup::Result invalidResult = CtBackup::StageValidated7zRestore(
        invalidBackup, invalidStage, nullptr, nullptr, invalidRestore);
    if (FAILED(invalidArchiveResult) || invalidResult.ok() ||
        fs::exists(invalidStage)) {
      std::wcerr << L"An invalid backup manifest was incorrectly accepted.\n";
      return 14;
    }

    const fs::path unsafeArchive = root / L"unsafe.7z";
    HRESULT unsafeResult = _7zCompress7zWithExtraFile(
        unsafeArchive.c_str(), backupSites.c_str(), true,
        invalidManifest.c_str(), L"..\\escape.manifest", nullptr, nullptr);
    if (SUCCEEDED(unsafeResult) || fs::exists(unsafeArchive)) {
      std::wcerr << L"An unsafe archive path was incorrectly accepted.\n";
      return 15;
    }

    const fs::path superscriptDeviceArchive =
        root / L"superscript-device-path.7z";
    const HRESULT superscriptDeviceResult = _7zCompress7zWithExtraFile(
        superscriptDeviceArchive.c_str(), backupSites.c_str(), true,
        invalidManifest.c_str(), L"Sites\\Alpha\\COM\x00B9\\payload.bin",
        nullptr, nullptr);
    if (SUCCEEDED(superscriptDeviceResult) ||
        fs::exists(superscriptDeviceArchive)) {
      std::wcerr << L"A superscript Windows device path was incorrectly "
                    L"accepted.\n";
      return 42;
    }

    const fs::path duplicateSource = root / L"duplicate-source";
    const fs::path duplicateExtra = root / L"duplicate-extra.txt";
    fs::create_directories(duplicateSource);
    if (!WriteBytes(duplicateSource / L"same.txt", rootData) ||
        !WriteBytes(duplicateExtra, binaryData)) {
      return 19;
    }
    const fs::path duplicateArchive = root / L"duplicate-paths.7z";
    const HRESULT duplicateCreateResult = _7zCompress7zWithExtraFile(
        duplicateArchive.c_str(), duplicateSource.c_str(), false,
        duplicateExtra.c_str(), L"./SAME.TXT", nullptr, nullptr);
    if (FAILED(duplicateCreateResult) ||
        !fs::is_regular_file(duplicateArchive)) {
      std::wcerr << L"Could not create the duplicate-path archive fixture.\n";
      return 20;
    }

    _7zArchiveInfo duplicateInfo{};
    const HRESULT duplicateInspectResult =
        _7zInspect7z(duplicateArchive.c_str(), &duplicateInfo);
    const fs::path duplicateExtracted = root / L"duplicate-extracted";
    const HRESULT duplicateExtractResult =
        _7zExtra_7z(duplicateArchive.c_str(), duplicateExtracted.c_str(),
                    nullptr, nullptr);
    const HRESULT duplicateExpected =
        HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (duplicateInspectResult != duplicateExpected ||
        duplicateExtractResult != duplicateExpected ||
        fs::exists(duplicateExtracted)) {
      std::wcerr << L"A case-insensitive normalized duplicate archive path "
                    L"was incorrectly accepted.\n";
      return 21;
    }

    const fs::path disappearingSource = root / L"disappearing-source";
    fs::create_directories(disappearingSource);
    const fs::path disappearingFile = disappearingSource / L"large.bin";
    if (!WriteBytes(disappearingFile, binaryData))
      return 16;
    DeleteDuringCompressionState deleteState{disappearingFile};
    const fs::path incompleteArchive = root / L"incomplete.7z";
    HRESULT incompleteResult = _7zCompress7z(
        incompleteArchive.c_str(), disappearingSource.c_str(), false,
        DeleteDuringCompression, &deleteState);
    if (!deleteState.attempted || deleteState.deleted || FAILED(incompleteResult) ||
        ReadBytes(disappearingFile)!=binaryData) {
      std::wcerr << L"The bound backup source did not prevent a concurrent deletion.\n";
      return 17;
    }

    std::wcout
        << L"In-process archive, validated backup, corruption, and restore "
           L"tests passed.\n";
    cleanup.passed = true;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Archive round-trip threw: " << error.what() << '\n';
    return 18;
  }
}
