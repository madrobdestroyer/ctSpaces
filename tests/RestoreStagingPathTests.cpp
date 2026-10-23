#include "../BackupEngine.h"
#include "../RestoreIoPath.h"
#include "../version.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
#ifdef CTSPACES_BACKUP_TEST_HOOKS
HRESULT CtBackupTestCreateGuid(GUID* identity) { return CoCreateGuid(identity); }
#endif
static unsigned checks = 0, failures = 0;
static void Check(bool passed, const char* label) {
  ++checks;
  if (!passed) ++failures;
  std::cout << (passed ? "PASS " : "FAIL ") << label << std::endl;
}
static std::string Read(const fs::path& path) {
  std::ifstream input(CtRestoreIo::Path(path), std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static bool Write(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << bytes;
  output.flush();
  return static_cast<bool>(output);
}
static void Status(const wchar_t* key, const CtBackup::Result& result) {
  std::wcout << key << L"=0x" << std::hex << std::setw(8) << std::setfill(L'0')
             << static_cast<unsigned long>(result.code) << std::dec
             << L"\nDETAILS=" << result.details << L"\n";
}
static void Helpers() {
  const auto converted = [](const wchar_t* input) { return CtRestoreIo::Path(input).native(); };
  Check(converted(L"C:\\sample\\note") == L"\\\\?\\C:\\sample\\note", "Absolute drive path conversion preserves its ordinary spelling");
  Check(converted(L"C:/sample/note") == L"\\\\?\\C:\\sample\\note", "Drive separators are normalized");
  Check(converted(L"\\\\server\\share\\note") == L"\\\\?\\UNC\\server\\share\\note", "Absolute UNC path conversion is lexical only");
  Check(converted(L"\\\\?\\C:\\sample\\note") == L"\\\\?\\C:\\sample\\note", "Extended drive spelling is idempotent");
  Check(converted(L"\\\\?\\UNC\\server\\share\\note") == L"\\\\?\\UNC\\server\\share\\note", "Extended UNC spelling is idempotent");
  for (const auto* invalid : {L"", L"relative\\note", L"C:\\sample\\..\\note", L"\\\\server\\share\\folder\\..\\note", L"C:note", L"\\note", L"\\\\server", L"\\\\server\\", L"\\\\.\\C:\\note", L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\note"}) {
    bool rejected = false;
    try { (void)converted(invalid); } catch (const fs::filesystem_error&) { rejected = true; }
    Check(rejected, "Unsupported path conversion is rejected without I/O");
  }
  bool nulRejected = false;
  try { (void)CtRestoreIo::Path(fs::path(std::wstring(L"C:\\a\0b", 7))); } catch (const fs::filesystem_error&) { nulRejected = true; }
  Check(nulRejected, "Embedded null path conversion is rejected");
  bool lengthRejected = false;
  try { (void)CtRestoreIo::Path(fs::path(L"C:\\" + std::wstring(32767, L'p'))); } catch (const fs::filesystem_error&) { lengthRejected = true; }
  Check(lengthRejected, "Oversized extended path conversion is rejected");
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 3 || std::wstring(argv[1]) != L"--headless") {
    std::cerr << "Use --headless with a fresh private fixture directory.\n";
    return 2;
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) return 3;
  _7zSetHInstance(GetModuleHandleW(nullptr));
  try {
    Helpers();
    const fs::path fixture = fs::absolute(argv[2]);
    const fs::path originalSites = fixture / L"d" / L"Sites";
    const fs::path archive = fixture / L"backup.7z";
    if (fs::exists(fixture)) return 4;
    const fs::path sourceBase = originalSites / L"Alpha" / L"Default";
    if (sourceBase.native().size() + 1 >= 238) return 12;
    const fs::path sourceDirectory = sourceBase / std::wstring(238 - sourceBase.native().size() - 1, L'p');
    const fs::path sourceFile = sourceDirectory / L"state.bin";
    Check(Write(sourceFile, "ordinary ctSpaces staging regression\r\n"), "Ordinary private source is created");
    Check(sourceDirectory.native().size() == 238 && sourceFile.native().size() == 248, "Ordinary source has directory238/file248 lengths");
    Check(GetFileAttributesW(sourceDirectory.c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(sourceFile.c_str()) != INVALID_FILE_ATTRIBUTES, "Ordinary source paths are readable through legacy Win32 APIs");
    Check(CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(originalSites, originalSites), "Ordinary source maps within unchanged final Windows budgets");
    const auto backup = CtBackup::CreateValidated7zBackup(originalSites, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    Status(L"ORDINARY_BACKUP_HRESULT", backup);
    Check(backup.ok(), "Ordinary private backup is created through the production API");
    if (!backup.ok()) return 13;
    const fs::path originalDefault = originalSites / L"Alpha" / L"Default";
    fs::path relativeFile;
    for (const auto& entry : fs::recursive_directory_iterator(originalDefault)) {
      if (entry.is_regular_file()) relativeFile = entry.path().lexically_relative(originalSites);
    }
    const std::string original = Read(originalSites / relativeFile);
    Check(!relativeFile.empty() && !original.empty(), "Ordinary private source can be read");
    Check(_7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK, "Ordinary private archive passes a full CRC test");
    const fs::path data = fixture / L"d";
    fs::create_directories(data);
    const fs::path longStage = data / L"Restore_Temp_1234abcd";
    fs::create_directory(longStage);
    CtBackup::StagedRestore staged;
    const auto result = CtBackup::StageValidated7zRestore(archive, longStage, nullptr, nullptr, staged);
    Status(L"LONG_STAGE_HRESULT", result);
    Check(result.ok(), "Formerly failing GUI-shaped staging location succeeds");
    Check(result.ok() && Read(staged.payloadDir / relativeFile) == original, "Long stage restores the exact source bytes");
    if (!result.ok()) return 5;
    const fs::path live = fixture / L"l" / L"Sites";
    const fs::path recovery = fixture / L"l" / L"R";
    Check(Write(live / L"Previous" / L"old.bin", "old live sentinel"), "Preexisting live sentinel is created");
    const auto committed = CtBackup::CommitStagedRestore(staged, live, recovery);
    Status(L"COMMIT_HRESULT", committed);
    Check(committed.ok(), "Verified long stage commits to the valid final location");
    Check(committed.ok() && Read(live / relativeFile) == original, "Committed long-stage payload has exact original bytes");
    Check(Read(recovery / L"Previous" / L"old.bin") == "old live sentinel", "Preexisting live data is preserved in recovery");
    Check(!fs::exists(longStage), "Successful long-stage commit removes its empty temporary root");
    CtBackup::StagedRestore shortRestore;
    const auto shorter = CtBackup::StageValidated7zRestore(archive, data / L"r", nullptr, nullptr, shortRestore);
    Status(L"SHORT_STAGE_HRESULT", shorter);
    Check(shorter.ok() && Read(shortRestore.payloadDir / relativeFile) == original, "Ordinary shorter staging control remains exact");
    Check(Read(originalSites / relativeFile) == original, "Ordinary private source remains unchanged");
    Check(_7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK, "Ordinary private archive remains CRC-valid after staging and commit");
    const fs::path occupied = fixture / L"occupied";
    Check(Write(occupied / L"Sites", "ordinary occupied output file"), "Occupied extraction destination is a regular private file");
    _7zArchiveInfo occupiedInfo{};
    _7zRestoreSession* occupiedSession = nullptr;
    const HRESULT opened = _7zOpenRestoreSession(archive.c_str(), &occupiedInfo, &occupiedSession);
    Check(opened == S_OK && occupiedSession != nullptr, "CRC-valid archive opens for ordinary occupied-output diagnostic");
    std::wstring details;
    const HRESULT outputFailure = SUCCEEDED(opened) ? _7zExtractRestoreSession(occupiedSession, occupied.c_str(), nullptr, nullptr, false, INVALID_HANDLE_VALUE, &details) : opened;
    _7zCloseRestoreSession(occupiedSession);
    Status(L"OCCUPIED_OUTPUT_HRESULT", {outputFailure, details});
    Check(outputFailure == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), "Output-creation refusal preserves the actual Windows HRESULT");
    Check(details.find(L"staging directory") != std::wstring::npos && details.find(L"Archive item: Sites") != std::wstring::npos, "First output error identifies its operation and relative archive item");
    Check(details.find(L"integrity") == std::wstring::npos && details.find(L"CRC") == std::wstring::npos, "Ordinary output refusal does not accuse the archive checksum");
    Check(Read(occupied / L"Sites") == "ordinary occupied output file", "Occupied output file remains unchanged");
    Check(_7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK, "Occupied-output refusal leaves the archive CRC-valid");

    const fs::path boundary = fixture / L"b";
    const fs::path boundarySource = boundary / L"s" / L"Sites";
    const fs::path boundaryLive = boundary / L"l" / L"Sites";
    const fs::path boundaryBase = boundarySource / L"Alpha" / L"Default";
    const auto pad = 247 - boundaryBase.native().size() - 1;
    if (pad > 240) return 6;
    const fs::path boundaryDirectory = boundaryBase / std::wstring(pad, L'q');
    const fs::path boundaryFile = boundaryDirectory / L"stateAB.bin";
    const auto boundaryRelative = boundaryFile.lexically_relative(boundarySource);
    const std::string boundaryBytes = "exact directory247/file259 boundary";
    Check(Write(boundaryFile, boundaryBytes), "Ordinary source exists at the exact legacy path boundary");
    std::wcout << L"BOUNDARY_SOURCE_DIRECTORY_LENGTH=" << boundaryDirectory.native().size()
               << L"\nBOUNDARY_SOURCE_FILE_LENGTH=" << boundaryFile.native().size() << L"\n";
    Check(boundaryDirectory.native().size() == 247 && boundaryFile.native().size() == 259, "Boundary fixture has directory247/file259");
    const fs::path boundaryArchive = boundary / L"boundary.7z";
    const auto backed = CtBackup::CreateValidated7zBackup(boundarySource, boundaryArchive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    Status(L"BOUNDARY_BACKUP_HRESULT", backed);
    Check(backed.ok() && _7zTest7z(boundaryArchive.c_str(), nullptr, nullptr) == S_OK, "Exact-boundary ordinary backup is CRC-valid");
    if (!backed.ok()) return 7;
    Check(Write(boundaryLive / L"Previous" / L"old.bin", "boundary old live"), "Boundary live sentinel is created");
    CtBackup::StagedRestore boundaryStage;
    const auto extracted = CtBackup::StageValidated7zRestore(boundaryArchive, boundary / L"Restore_Temp_abcdef12", nullptr, nullptr, boundaryStage);
    Status(L"BOUNDARY_STAGE_HRESULT", extracted);
    Check(extracted.ok(), "Exact-final-boundary archive stages through longer private paths");
    if (!extracted.ok()) return 8;
    const auto promoted = CtBackup::CommitStagedRestore(boundaryStage, boundaryLive, boundary / L"l" / L"R");
    Status(L"BOUNDARY_COMMIT_HRESULT", promoted);
    Check(promoted.ok(), "Actual final directory247/file259 commit succeeds");
    Check(promoted.ok() && Read(boundaryLive / boundaryRelative) == boundaryBytes, "Boundary commit preserves exact bytes");
    Check(Read(boundary / L"l" / L"R" / L"Previous" / L"old.bin") == "boundary old live", "Boundary commit retains old live data in recovery");
    const fs::path tooLongLive = boundary / L"ll" / L"Sites";
    Check(Write(tooLongLive / L"Previous" / L"old.bin", "over budget old live"), "Over-budget configured location has an old live sentinel");
    CtBackup::StagedRestore tooLongStage;
    const auto stagedAgain = CtBackup::StageValidated7zRestore(boundaryArchive, boundary / L"Restore_Temp_abcdef34", nullptr, nullptr, tooLongStage);
    Check(stagedAgain.ok(), "Same valid boundary archive stages before final-budget refusal");
    if (!stagedAgain.ok()) return 9;
    const auto refused = CtBackup::CommitStagedRestore(tooLongStage, tooLongLive, boundary / L"ll" / L"R");
    Status(L"OVER_BUDGET_COMMIT_HRESULT", refused);
    std::wcout << L"OVER_BUDGET_FINAL_DIRECTORY_LENGTH=" << (tooLongLive / boundaryRelative.parent_path()).native().size()
               << L"\nOVER_BUDGET_FINAL_FILE_LENGTH=" << (tooLongLive / boundaryRelative).native().size() << L"\n";
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE), "Directory248/file260 configured final target is rejected");
    Check(Read(tooLongLive / L"Previous" / L"old.bin") == "over budget old live", "Final-budget refusal preserves old live bytes");
    Check(!fs::exists(boundary / L"ll" / L"R"), "Final-budget refusal does not move old live data");
    Check(Read(tooLongStage.payloadDir / boundaryRelative) == boundaryBytes, "Final-budget refusal preserves the verified staged payload");
    Check(Read(boundarySource / boundaryRelative) == boundaryBytes, "Boundary original source remains unchanged");
    Check(_7zTest7z(boundaryArchive.c_str(), nullptr, nullptr) == S_OK, "Boundary archive remains CRC-valid after accepted and refused commit");
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    const fs::path allocation = fixture / L"allocation";
    const fs::path allocationSource = allocation / L"s" / L"Sites";
    const fs::path allocationArchive = allocation / L"backup.7z";
    const fs::path allocationLive = allocation / L"l" / L"Sites";
    const std::string allocationBytes = "ordinary allocator failure and retry";
    Check(Write(allocationSource / L"Alpha" / L"empty.bin", ""), "Allocation fixture includes an ordinary empty file");
    Check(Write(allocationSource / L"Alpha" / L"state.bin", allocationBytes), "Allocation fixture includes exact ordinary payload bytes");
    Check(Write(allocationLive / L"Previous" / L"old.bin", "allocation old live"), "Allocation failure has an independent live sentinel");
    const auto allocationBackup = CtBackup::CreateValidated7zBackup(allocationSource, allocationArchive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    Check(allocationBackup.ok() && _7zTest7z(allocationArchive.c_str(), nullptr, nullptr) == S_OK, "Allocation fixture backup is CRC-valid before the fault");
    if (!allocationBackup.ok()) return 14;
    const auto archiveBefore = Read(allocationArchive);
    _7zArchiveInfo allocationInfo{};
    _7zRestoreSession* allocationSession = nullptr;
    const HRESULT allocationOpened = _7zOpenRestoreSession(allocationArchive.c_str(), &allocationInfo, &allocationSession);
    Check(allocationOpened == S_OK && allocationSession != nullptr, "Allocation fixture archive opens successfully");
    if (FAILED(allocationOpened)) return 15;
    _7zFailNextOutputOwnershipAllocationForTest();
    std::wstring allocationDetails;
    const fs::path failedStage = allocation / L"failed";
    std::cout << "OUTPUT_OWNERSHIP_ALLOCATION_CALL_BEGIN\n" << std::flush;
    const HRESULT allocationFailure = _7zExtractRestoreSession(allocationSession, failedStage.c_str(), nullptr, nullptr, true, INVALID_HANDLE_VALUE, &allocationDetails);
    Status(L"OUTPUT_OWNERSHIP_ALLOCATION_HRESULT", {allocationFailure, allocationDetails});
    Check(_7zOutputOwnershipAllocationFailureHitForTest(), "One-shot ownership allocation fault is reached after actual output creation");
    Check(allocationFailure == E_OUTOFMEMORY, "Output ownership allocation failure returns E_OUTOFMEMORY safely");
    Check(allocationDetails.find(L"memory") != std::wstring::npos && allocationDetails.find(L"CRC") == std::wstring::npos, "Allocation failure reports memory without a checksum accusation");
    const bool completeCleanup = _7zCleanupRestoreOutput(allocationSession);
    _7zCloseRestoreSession(allocationSession);
    std::wcout << L"ALLOCATION_FAILURE_FULL_CLEANUP=" << completeCleanup << L"\n";
    bool exclusive = true;
    unsigned retained = 0;
    if (fs::exists(failedStage)) {
      for (const auto& entry : fs::recursive_directory_iterator(failedStage)) {
        if (!entry.is_regular_file()) continue;
        ++retained;
        const HANDLE file = CreateFileW(entry.path().c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) exclusive = false;
        else CloseHandle(file);
      }
    }
    std::wcout << L"ALLOCATION_FAILURE_RETAINED_FILES=" << retained << L"\n";
    Check(exclusive, "Any safely retained partial output can be opened exclusively after close");
    Check(fs::is_regular_file(allocationSource / L"Alpha" / L"empty.bin") && fs::file_size(allocationSource / L"Alpha" / L"empty.bin") == 0, "Allocation failure preserves the original empty file");
    Check(Read(allocationSource / L"Alpha" / L"state.bin") == allocationBytes, "Allocation failure preserves original payload bytes");
    Check(Read(allocationArchive) == archiveBefore, "Allocation failure leaves archive bytes unchanged");
    Check(Read(allocationLive / L"Previous" / L"old.bin") == "allocation old live", "Allocation failure leaves live data unchanged");
    CtBackup::StagedRestore allocationRetry;
    const auto retried = CtBackup::StageValidated7zRestore(allocationArchive, allocation / L"retry", nullptr, nullptr, allocationRetry);
    Check(retried.ok(), "A fresh ordinary stage succeeds after the one-shot allocation fault");
    Check(retried.ok() && fs::is_regular_file(allocationRetry.payloadDir / L"Alpha" / L"empty.bin") && fs::file_size(allocationRetry.payloadDir / L"Alpha" / L"empty.bin") == 0, "Successful allocation retry restores the empty file and its zero size");
    Check(retried.ok() && Read(allocationRetry.payloadDir / L"Alpha" / L"state.bin") == allocationBytes, "Successful allocation retry restores exact payload bytes");
    Check(_7zTest7z(allocationArchive.c_str(), nullptr, nullptr) == S_OK, "Allocation failure and retry leave the archive CRC-valid");
#endif
    std::cout << "CHECKS=" << checks << "\nFAILURES=" << failures << "\n";
    CoUninitialize();
    if (!failures) std::cout << "Restore staging path tests passed: " << checks << " checks.\n";
    return failures ? 10 : 0;
  } catch (const std::exception& error) {
    std::cerr << "EXCEPTION=" << error.what() << "\nCHECKS=" << checks << "\nFAILURES=" << failures << "\n";
    CoUninitialize();
    return 11;
  }
}
