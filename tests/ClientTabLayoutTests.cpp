// Filesystem-only production-helper coverage. No app entry point, browser,
// displayed window, device input or clipboard access is used by this target.
#include "../ClientTabLayout.h"
#include "DirectoryJunctionFixture.h"
#include "RetainedFixture.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using client_tab_layout::Layout;
using client_tab_layout::Override;
static unsigned checks = 0, scenarios = 0;
static fs::path fixture, alpha, bravo;
static constexpr char inheritBytes[] =
    "ctSpaces-client-settings=1\r\ntab-layout=inherit\r\n";
static constexpr char horizontalBytes[] =
    "ctSpaces-client-settings=1\r\ntab-layout=horizontal\r\n";
static constexpr char verticalBytes[] =
    "ctSpaces-client-settings=1\r\ntab-layout=vertical\r\n";

static void Check(bool value, const char *description) {
  ++checks;
  if (!value) throw std::runtime_error(description);
}
static void Scenario(const char *description) {
  ++scenarios;
  std::cout << "Scenario " << scenarios << ": " << description << '\n';
}
static std::string Bytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Private fixture file could not be read");
  return {std::istreambuf_iterator<char>(input), {}};
}
static void Put(const fs::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) throw std::runtime_error("Private fixture file could not be written");
}
static fs::path NewClient(const std::wstring &name) {
  const auto root = fixture / name;
  Check(CreateDirectoryW(root.c_str(), nullptr) != FALSE,
        "A new private client directory was created without reuse");
  return root;
}
static fs::path Setting(const fs::path &root) {
  return root / client_tab_layout::kFileName;
}
static void OtherDataIntact() {
  Check(Bytes(alpha / L"notes.ctn") == std::string("ALPHA PRIVATE NOTE alpha\0tail", 29),
        "Alpha's independent note bytes remain exact");
  Check(Bytes(bravo / L"notes.ctn") == std::string("BRAVO PRIVATE NOTE beta\0tail", 28),
        "Bravo's independent note bytes remain exact");
  Check(Bytes(alpha / L"browser-session") == "ALPHA SESSION" &&
        Bytes(bravo / L"browser-session") == "BRAVO SESSION",
        "Tab-layout changes do not alter either private browser session");
}

struct NativeEvents {
  static inline unsigned visible = 0, foreground = 0, focus = 0;
  std::vector<HWINEVENTHOOK> hooks;
  static void CALLBACK Event(HWINEVENTHOOK, DWORD event, HWND window,
      LONG object, LONG, DWORD, DWORD) {
    DWORD pid = 0;
    if (!window || !GetWindowThreadProcessId(window, &pid) ||
        pid != GetCurrentProcessId()) return;
    if (event == EVENT_SYSTEM_FOREGROUND) ++foreground;
    else if (event == EVENT_OBJECT_FOCUS) ++focus;
    else if (event == EVENT_OBJECT_SHOW && object == OBJID_WINDOW &&
             IsWindowVisible(GetAncestor(window, GA_ROOT))) ++visible;
  }
  NativeEvents() {
    for (const DWORD event : {EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_SHOW,
                              EVENT_OBJECT_FOCUS}) {
      const auto hook = SetWinEventHook(event, event, nullptr, Event,
          GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
      if (!hook) throw std::runtime_error("Private native event observer unavailable");
      hooks.push_back(hook);
    }
  }
  ~NativeEvents() { for (const auto hook : hooks) UnhookWinEvent(hook); }
  static void Audit() {
    MSG message{};
    unsigned pumped = 0;
    while (pumped++ < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      Check(!(message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) &&
            !(message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST),
            "No keyboard or mouse messages enter this filesystem-only lane");
      if (message.message != WM_QUIT) DispatchMessageW(&message);
    }
    unsigned ownedVisible = 0;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
      DWORD pid = 0;
      GetWindowThreadProcessId(window, &pid);
      if (pid == GetCurrentProcessId() && IsWindowVisible(window))
        ++*reinterpret_cast<unsigned *>(data);
      return TRUE;
    }, reinterpret_cast<LPARAM>(&ownedVisible));
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    GUITHREADINFO thread{sizeof(thread)};
    const bool hasThreadInfo = GetGUIThreadInfo(GetCurrentThreadId(), &thread) != FALSE;
    const bool active = hasThreadInfo &&
        (thread.hwndActive || thread.hwndFocus || thread.hwndCapture);
    std::cout << "Client layout native guards: visible=" << ownedVisible
        << "; show/foreground/focus=" << visible << '/' << foreground << '/'
        << focus << "; active-focus-capture=" << active << '\n';
    Check(ownedVisible == 0 && visible == 0 && foreground == 0 && focus == 0 &&
          foregroundPid != GetCurrentProcessId() && !active,
          "Native observations confirm zero owned visibility, activation, focus or capture");
  }
};

static void ResolveCases() {
  Scenario("global policy and client-wide override resolution");
  const Layout expected[3][3] = {
      {Layout::Browser, Layout::Horizontal, Layout::Vertical},
      {Layout::Horizontal, Layout::Horizontal, Layout::Vertical},
      {Layout::Vertical, Layout::Horizontal, Layout::Vertical}};
  for (int global = 0; global < 3; ++global)
    for (int client = 0; client < 3; ++client)
      Check(client_tab_layout::Resolve(static_cast<Layout>(global),
              static_cast<Override>(client)) == expected[global][client],
            "A client override wins and Follow global preserves each global choice");
  Check(client_tab_layout::Resolve(Layout::Vertical, Override::Horizontal) ==
            Layout::Horizontal,
        "Explicit horizontal disables an inherited vertical policy");
  Check(!client_tab_layout::IsValid(static_cast<Layout>(99)) &&
        !client_tab_layout::IsValid(static_cast<Override>(99)),
        "Invalid external enum values are distinguishable from default choices");
}
static void MissingCases() {
  Scenario("missing sidecar inherits without creating a file");
  Override choice = Override::Vertical;
  std::wstring error = L"old error";
  Check(client_tab_layout::Read(alpha, choice, &error) &&
            choice == Override::FollowGlobal && error.empty(),
        "Missing sidecar is a successful Follow global read");
  Check(!fs::exists(Setting(alpha)), "Reading missing settings is read-only");
  Check(!client_tab_layout::ValidateFile(Setting(alpha), &error) && !error.empty(),
        "Missing metadata is not existing deletion authority");
  const auto missing = fixture / L"not-an-existing-client";
  choice = Override::Horizontal;
  Check(!client_tab_layout::Read(missing, choice, &error) &&
            choice == Override::Horizontal && !error.empty(),
        "A missing client is an error and does not reset the caller's choice");
  Check(!client_tab_layout::Write(missing, Override::Vertical, &error) &&
            !fs::exists(missing),
        "Saving a preference never creates an unowned client folder");
  Check(!client_tab_layout::Write(alpha, static_cast<Override>(99), &error) &&
            !fs::exists(Setting(alpha)),
        "Invalid layout input cannot create a settings record");
  Check(!client_tab_layout::Read(L"relative-client", choice, &error) &&
        !client_tab_layout::Write(L"relative-client", Override::Vertical, &error),
        "Relative paths cannot redirect a later persistence operation");
  OtherDataIntact();
}
static void RoundTripCases() {
  Scenario("durable ON, OFF and Follow global remain client-owned");
  Override choice = Override::FollowGlobal;
  std::wstring error;
  Check(client_tab_layout::Write(alpha, Override::Vertical, &error),
        "Vertical preference atomically creates a bounded sidecar");
  Check(Bytes(Setting(alpha)) == verticalBytes &&
        client_tab_layout::Read(alpha, choice, &error) && choice == Override::Vertical,
        "The exact versioned vertical record reloads through production Read");
  Check(client_tab_layout::Write(bravo, Override::Horizontal, &error),
        "The second client independently opts out with horizontal tabs");
  const auto bravoBefore = Bytes(Setting(bravo));
  Check(bravoBefore == horizontalBytes, "Horizontal has its own explicit durable value");
  Check(client_tab_layout::Write(alpha, Override::Horizontal, &error) &&
        Bytes(Setting(alpha)) == horizontalBytes &&
        client_tab_layout::Read(alpha, choice, &error) && choice == Override::Horizontal,
        "Switching ON to OFF writes horizontal rather than merely dropping enforcement");
  Check(client_tab_layout::Write(alpha, Override::FollowGlobal, &error) &&
        fs::is_regular_file(Setting(alpha)) && Bytes(Setting(alpha)) == inheritBytes &&
        client_tab_layout::Read(alpha, choice, &error) && choice == Override::FollowGlobal,
        "Follow global persists inherit without deleting by pathname");
  Check(client_tab_layout::Write(alpha, Override::Vertical, &error) &&
        client_tab_layout::Write(alpha, Override::Vertical, &error) &&
        Bytes(Setting(alpha)) == verticalBytes,
        "Repeated explicit writes preserve one valid canonical setting");
  Check(client_tab_layout::ValidateFile(Setting(alpha), &error) &&
        Bytes(Setting(bravo)) == bravoBefore,
        "Safe sidecar validation and changes leave the other client's preference intact");
  OtherDataIntact();
}
static void RenameCase() {
  Scenario("quoted Unicode client rename carries the same setting and notes");
  const auto prior = alpha;
  const auto renamed = fixture / L"'M\u00fcnchen \u03b1\U0001f600 renamed'";
  Check(MoveFileW(prior.c_str(), renamed.c_str()) != FALSE,
        "The fixture performs a real whole-client directory rename");
  alpha = renamed;
  Override choice = Override::FollowGlobal;
  Check(!fs::exists(prior) && Bytes(Setting(alpha)) == verticalBytes &&
        client_tab_layout::Read(alpha, choice) && choice == Override::Vertical,
        "The exact quoted Unicode target inherits no other client's identity");
  Check(Bytes(Setting(bravo)) == horizontalBytes,
        "Renaming one client leaves the separate client's explicit setting unchanged");
  OtherDataIntact();
}
static void MalformedCases() {
  Scenario("malformed and future schemas are refused without rewriting");
  const std::vector<std::string> cases = {
      "", "ctSpaces-client-settings=2\r\ntab-layout=vertical\r\n",
      "ctSpaces-client-settings=1\ntab-layout=vertical\n",
      "ctSpaces-client-settings=1\r\ntab-layout=vertical",
      "ctSpaces-client-settings=1\r\ntab-layout=Vertical\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=0\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=off\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=horizontal \r\n",
      "ctSpaces-client-settings=1\r\n tab-layout=vertical\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout =vertical\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=vertical\r\ntab-layout=horizontal\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=inherit\r\nfuture=must-survive\r\n",
      "ctSpaces-client-settings=1\r\nfuture=must-survive\r\ntab-layout=vertical\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=vertical\r\n\r\n",
      std::string("\xef\xbb\xbf") + verticalBytes,
      std::string(verticalBytes) + '\0',
      "ctSpaces-client-settings=01\r\ntab-layout=vertical\r\n",
      "ctSpaces-client-settings=1\r\ntab-layout=\r\n"};
  unsigned index = 0;
  for (const auto &bytes : cases) {
    const auto root = NewClient(L"malformed-" + std::to_wstring(index++));
    Put(Setting(root), bytes);
    Override choice = Override::Horizontal;
    std::wstring error;
    Check(!client_tab_layout::Read(root, choice, &error) &&
              choice == Override::Horizontal && !error.empty(),
          "Malformed or future settings fail distinctly without changing the output");
    Check(!client_tab_layout::Write(root, Override::Vertical, &error) &&
              !error.empty() && Bytes(Setting(root)) == bytes,
          "An older writer preserves every unknown original byte");
    Check(client_tab_layout::ValidateFile(Setting(root), &error),
          "An existing bounded safe corrupt record remains explicit deletion retry authority");
  }
  OtherDataIntact();
  Check(Bytes(Setting(alpha)) == verticalBytes && Bytes(Setting(bravo)) == horizontalBytes,
        "The malformed corpus does not rewrite unrelated clients' choices");
  std::cout << "Malformed schema corpus: " << cases.size()
            << " exact preserved records\n";
}
static void SizeCases() {
  Scenario("size limit is enforced before interpreting or replacing records");
  for (const size_t length : {client_tab_layout::kMaximumBytes,
                              client_tab_layout::kMaximumBytes + 1}) {
    const auto root = NewClient(L"bounded-" + std::to_wstring(length));
    const std::string bytes(length, 'x');
    Put(Setting(root), bytes);
    Override choice = Override::Horizontal;
    Check(!client_tab_layout::Read(root, choice) && choice == Override::Horizontal,
          "Size-boundary invalid data is not interpreted as a layout choice");
    Check(!client_tab_layout::Write(root, Override::Vertical) &&
              Bytes(Setting(root)) == bytes,
          "Oversize and malformed boundary records retain exact original bytes");
    Check(client_tab_layout::ValidateFile(Setting(root)) ==
              (length == client_tab_layout::kMaximumBytes),
          "Deletion metadata validation has the independent exact byte bound");
  }
}
static void LockCases() {
  Scenario("unreadable and read-only files are distinct from inherited settings");
  const auto root = NewClient(L"unreadable");
  Check(client_tab_layout::Write(root, Override::Vertical), "Seed locked settings");
  const auto original = Bytes(Setting(root));
  HANDLE held = CreateFileW(Setting(root).c_str(), GENERIC_READ, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  Check(held != INVALID_HANDLE_VALUE, "An exclusive native fixture lease was acquired");
  Override choice = Override::Horizontal;
  std::wstring error;
  const bool read = client_tab_layout::Read(root, choice, &error);
  const bool saved = client_tab_layout::Write(root, Override::Horizontal, &error);
  const bool valid = client_tab_layout::ValidateFile(Setting(root), &error);
  CloseHandle(held);
  Check(!read && !saved && !valid && choice == Override::Horizontal && !error.empty() &&
            Bytes(Setting(root)) == original,
        "Unreadable existing settings neither inherit silently nor lose their original bytes");
  Check(SetFileAttributesW(Setting(root).c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
        "The private file is deliberately read-only");
  Check(client_tab_layout::Read(root, choice) && choice == Override::Vertical &&
        !client_tab_layout::Write(root, Override::Horizontal) &&
        Bytes(Setting(root)) == original,
        "Read-only preferences remain readable but are not overwritten");
  Check(SetFileAttributesW(Setting(root).c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE,
        "Restore only the test-created file's attributes");
}
static void UnsafeFileCases() {
  Scenario("directory and hardlink metadata cannot masquerade as an independent setting");
  const auto directory = NewClient(L"directory-record");
  Check(CreateDirectoryW(Setting(directory).c_str(), nullptr) != FALSE,
        "Create a private directory at the reserved settings filename");
  Put(Setting(directory) / L"private-sentinel", "DIRECTORY CONTENT MUST SURVIVE");
  Override choice = Override::Horizontal;
  Check(!client_tab_layout::Read(directory, choice) &&
        !client_tab_layout::Write(directory, Override::Vertical) &&
        !client_tab_layout::ValidateFile(Setting(directory)) &&
        Bytes(Setting(directory) / L"private-sentinel") == "DIRECTORY CONTENT MUST SURVIVE",
        "A settings directory and its contents are preserved and rejected");
  const auto linked = NewClient(L"hardlinked-record");
  const auto original = linked / L"other-private-file";
  Put(original, verticalBytes);
  Check(CreateHardLinkW(Setting(linked).c_str(), original.c_str(), nullptr) != FALSE,
        "The native fixture creates a real second hardlink");
  Check(!client_tab_layout::Read(linked, choice) &&
        !client_tab_layout::Write(linked, Override::Horizontal) &&
        !client_tab_layout::ValidateFile(Setting(linked)) &&
        Bytes(original) == verticalBytes && Bytes(Setting(linked)) == verticalBytes,
        "A multiply-linked record cannot mutate or authorize a shared private file");
  Check(!client_tab_layout::ValidateFile(original),
        "Only the reserved filename can be client settings deletion authority");
  OtherDataIntact();
}
static void ReparseCases() {
  Scenario("existing leaf, client and ancestor junctions cannot redirect reads or writes");
  const auto oldBravo = Bytes(Setting(bravo));
  const auto rootAlias = fixture / L"client-junction";
  Check(CtTest::CreateDirectoryJunction(rootAlias, bravo),
        "Create an actual NTFS junction at the candidate client root");
  Override choice = Override::Horizontal;
  Check(!client_tab_layout::Read(rootAlias, choice) &&
        !client_tab_layout::Write(rootAlias, Override::Vertical) &&
        !client_tab_layout::ValidateFile(Setting(rootAlias)),
        "A reparse client root is refused before resolving another client's metadata");
  const auto realParent = fixture / L"real-parent";
  Check(CreateDirectoryW(realParent.c_str(), nullptr) != FALSE,
        "Create the real private parent directory");
  const auto child = realParent / L"child";
  Check(CreateDirectoryW(child.c_str(), nullptr) != FALSE &&
        client_tab_layout::Write(child, Override::Horizontal),
        "Seed a private client below the real parent");
  const auto parentAlias = fixture / L"parent-junction";
  Check(CtTest::CreateDirectoryJunction(parentAlias, realParent),
        "Create an actual ancestor NTFS junction");
  Check(!client_tab_layout::Read(parentAlias / L"child", choice) &&
        !client_tab_layout::Write(parentAlias / L"child", Override::Vertical) &&
        Bytes(Setting(child)) == horizontalBytes,
        "An ancestor junction cannot redirect a preference operation");
  const auto leafRoot = NewClient(L"leaf-junction");
  Check(CtTest::CreateDirectoryJunction(Setting(leafRoot), bravo),
        "Create a reparse directory at the reserved metadata filename");
  Check(!client_tab_layout::Read(leafRoot, choice) &&
        !client_tab_layout::Write(leafRoot, Override::Vertical) &&
        !client_tab_layout::ValidateFile(Setting(leafRoot)) &&
        Bytes(Setting(bravo)) == oldBravo,
        "Reparse metadata does not touch the referenced client's settings");
  OtherDataIntact();
}

enum class EarlyFault { RootMove, ParentMove, Edit, ReplaceSameBytes, Appears };
struct EarlyContext {
  EarlyFault fault;
  fs::path root, displaced;
  bool reached = false, mutated = false;
  DWORD error = 0;
};
static void EarlyInjection(const fs::path &root, void *data) {
  auto &fault = *static_cast<EarlyContext *>(data);
  if (root != fault.root || fault.reached) return;
  fault.reached = true;
  if (fault.fault == EarlyFault::RootMove || fault.fault == EarlyFault::ParentMove) {
    const auto source = fault.fault == EarlyFault::RootMove ? root : root.parent_path();
    fault.mutated = MoveFileW(source.c_str(), fault.displaced.c_str()) != FALSE;
    fault.error = fault.mutated ? ERROR_SUCCESS : GetLastError();
  } else if (fault.fault == EarlyFault::ReplaceSameBytes) {
    const auto bytes = Bytes(Setting(root));
    fault.mutated = MoveFileW(Setting(root).c_str(), fault.displaced.c_str()) != FALSE;
    if (fault.mutated) Put(Setting(root), bytes);
  } else {
    Put(Setting(root), verticalBytes);
    fault.mutated = true;
  }
}
struct ResetHooks {
  ~ResetHooks() {
    client_tab_layout::SetBeforeMutationHookForTest(nullptr);
    config_persistence::SetIniPublicationHookForTest(nullptr, nullptr);
    config_persistence::FailIniPublicationForTest(config_persistence::IniPublicationFailure::None);
  }
};
static void EarlyBoundaryCases() {
  Scenario("original client and ancestor are pinned before the atomic publisher begins");
  for (const auto mode : {EarlyFault::RootMove, EarlyFault::ParentMove}) {
    const auto parent = NewClient(mode == EarlyFault::RootMove ? L"root-guard" : L"parent-guard");
    const auto root = parent / L"client";
    Check(CreateDirectoryW(root.c_str(), nullptr) != FALSE &&
          client_tab_layout::Write(root, Override::Horizontal),
          "Seed the original client for a pre-publisher namespace substitution");
    EarlyContext fault{mode, root, fixture /
        (mode == EarlyFault::RootMove ? L"root-displaced" : L"parent-displaced")};
    client_tab_layout::SetBeforeMutationHookForTest(EarlyInjection, &fault);
    const bool saved = client_tab_layout::Write(root, Override::Vertical);
    client_tab_layout::SetBeforeMutationHookForTest(nullptr);
    Check(fault.reached && !fault.mutated && fault.error != ERROR_SUCCESS,
          "The real root/ancestor rename attempt is blocked before shared publisher guards exist");
    Check(saved && Bytes(Setting(root)) == verticalBytes && !fs::exists(fault.displaced),
          "Successful preference publication stays attached to the original pinned client");
    std::cout << "Pre-publisher namespace: mode=" << static_cast<int>(mode)
        << "; attempted=" << fault.reached << "; retargeted=" << fault.mutated
        << "; WindowsError=" << fault.error << "; saved=" << saved << '\n';
  }
  Scenario("a newer file between pre-read and publisher snapshot is preserved");
  for (const auto mode : {EarlyFault::Edit, EarlyFault::ReplaceSameBytes, EarlyFault::Appears}) {
    const auto root = NewClient(L"pre-snapshot-" + std::to_wstring(static_cast<int>(mode)));
    if (mode != EarlyFault::Appears)
      Check(client_tab_layout::Write(root, Override::Horizontal), "Seed pre-snapshot settings");
    EarlyContext fault{mode, root, root / L"original-file-retained"};
    client_tab_layout::SetBeforeMutationHookForTest(EarlyInjection, &fault);
    std::wstring error;
    const bool saved = client_tab_layout::Write(root, Override::FollowGlobal, &error);
    client_tab_layout::SetBeforeMutationHookForTest(nullptr);
    Check(fault.reached && fault.mutated && !saved && !error.empty(),
          "An actual newer edit, replacement identity or newly appeared record defeats the stale write");
    const std::string expected = mode == EarlyFault::ReplaceSameBytes ? horizontalBytes : verticalBytes;
    Check(Bytes(Setting(root)) == expected,
          "The newer exact bytes survive even if an earlier file used identical text");
    if (mode == EarlyFault::ReplaceSameBytes)
      Check(Bytes(fault.displaced) == horizontalBytes,
            "Same-byte replacement preserves the displaced original evidence too");
  }
  OtherDataIntact();
}

struct LateContext {
  fs::path target;
  bool lock = false, reached = false;
  HANDLE held = INVALID_HANDLE_VALUE;
  ~LateContext() { if (held != INVALID_HANDLE_VALUE) CloseHandle(held); }
};
static void LateInjection(const fs::path &target, const fs::path &,
    config_persistence::IniPublicationBoundary stage, void *data) {
  auto &fault = *static_cast<LateContext *>(data);
  const auto wanted = fault.lock ? config_persistence::IniPublicationBoundary::BeforeNativePublication
                                : config_persistence::IniPublicationBoundary::AfterStageCopy;
  if (target != fault.target || stage != wanted || fault.reached) return;
  fault.reached = true;
  if (fault.lock) {
    fault.held = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Check(fault.held != INVALID_HANDLE_VALUE,
          "A native target lock is acquired at the exact publication boundary");
  } else Put(target, verticalBytes);
}
static void PublicationCases() {
  Scenario("native publication lock and concurrent edit preserve the authoritative file");
  for (const bool lock : {false, true}) {
    const auto root = NewClient(lock ? L"late-lock" : L"late-edit");
    Check(client_tab_layout::Write(root, Override::Horizontal), "Seed publication-boundary settings");
    LateContext fault{Setting(root), lock};
    config_persistence::SetIniPublicationHookForTest(LateInjection, &fault);
    std::wstring error;
    const bool saved = client_tab_layout::Write(root, Override::FollowGlobal, &error);
    config_persistence::SetIniPublicationHookForTest(nullptr, nullptr);
    if (fault.held != INVALID_HANDLE_VALUE) { CloseHandle(fault.held); fault.held = INVALID_HANDLE_VALUE; }
    Check(fault.reached && !saved && !error.empty(),
          "The actual publication lock or staged concurrent edit rejects the preference update");
    Check(Bytes(Setting(root)) == (lock ? horizontalBytes : verticalBytes),
          "A failed publication retains the original or newer writer's exact preference bytes");
  }
  Scenario("both native partial-publication stages restore the original preference");
  for (const auto mode : {config_persistence::IniPublicationFailure::OldDisplaced,
                          config_persistence::IniPublicationFailure::Installed}) {
    const auto root = NewClient(L"partial-publication-" + std::to_wstring(static_cast<int>(mode)));
    Check(client_tab_layout::Write(root, Override::Horizontal), "Seed partial-publication settings");
    config_persistence::FailIniPublicationForTest(mode);
    std::wstring error;
    const bool saved = client_tab_layout::Write(root, Override::Vertical, &error);
    config_persistence::FailIniPublicationForTest(config_persistence::IniPublicationFailure::None);
    Override choice = Override::FollowGlobal;
    Check(!saved && !error.empty() && Bytes(Setting(root)) == horizontalBytes &&
          client_tab_layout::Read(root, choice) && choice == Override::Horizontal,
          "A real displaced/installed native failure restores readable exact original settings");
    std::cout << "Partial publication: mode=" << static_cast<int>(mode)
        << "; saved=" << saved << "; original-restored=1\n";
  }
  OtherDataIntact();
  Check(Bytes(Setting(alpha)) == verticalBytes && Bytes(Setting(bravo)) == horizontalBytes,
        "Publication faults do not cross client settings boundaries");
}

static void DeletionAuthorityCases() {
  Scenario("metadata validation is compatible with the deleter's original DELETE lease");
  unsigned mode = 0;
  for (const auto &bytes : {std::string(verticalBytes),
          std::string("ctSpaces-client-settings=2\r\nfuture=retain-until-client-deletion\r\n"),
          std::string(client_tab_layout::kMaximumBytes + 1, 'x'),
          std::string(horizontalBytes)}) {
    const auto root = NewClient(L"delete-authority-" + std::to_wstring(mode));
    Put(Setting(root), bytes);
    if (mode == 3)
      Check(CreateHardLinkW((root / L"shared-alias").c_str(), Setting(root).c_str(),
                            nullptr) != FALSE,
            "Seed multiply-linked metadata before capturing the deletion tree");
    client_deletion_safety::BoundTree bound;
    std::wstring error;
    Check(bound.CaptureNamespace(root, error) && bound.CaptureContents(error),
          "The actual deletion flow upgrades its original root to DELETE access");
    HANDLE probe = CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD code = probe == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
    std::wstring independentError;
    const bool independent = client_tab_layout::ValidateFile(Setting(root), &independentError);
    const bool valid = client_tab_layout::ValidateFile(Setting(root), &error, &bound);
    std::wcout << L"Deletion lease diagnostic: mode=" << mode << L"; valid="
               << valid << L"; independent-validator=" << independent
               << L"; metadata-root-open-error=" << code
               << L"; details=" << error << L'\n';
    Check(valid == (mode < 2),
          "Deletion authority accepts bounded valid/corrupt sidecars under its held root lease and rejects unsafe ones");
    Check(!client_tab_layout::ValidateFile(Setting(bravo), &error, &bound) &&
              !error.empty() && Bytes(Setting(bravo)) == horizontalBytes,
          "A supplied original namespace never authorizes another client's metadata");
    Check(Bytes(Setting(root)) == bytes && bound.Verify(error),
          "Metadata validation preserves the exact original deletion tree and sidecar bytes");
    ++mode;
  }
  OtherDataIntact();
  std::cout << "Deletion authority lease: valid=1; corrupt-safe=1; oversized=0; hardlink=0; wrong-root=0\n";
}

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 2) throw std::runtime_error("Pass a private evidence directory below workspace build");
    const auto parent = fs::absolute(fs::path(argv[1])).lexically_normal();
    const auto build = (fs::current_path() / L"build").lexically_normal().wstring() + L"\\";
    const auto text = parent.wstring();
    Check(text.size() > build.size() &&
          CompareStringOrdinal(text.c_str(), static_cast<int>(build.size()),
              build.c_str(), static_cast<int>(build.size()), TRUE) == CSTR_EQUAL,
          "All retained fixture data is bounded inside workspace build");
    fixture = retained_fixture::CreateRoot(parent, L"client-tab-layout");
    std::wcout << L"Fixture retained: " << fixture << L'\n';
    alpha = NewClient(L"'M\u00fcnchen \u03b1\U0001f600 Client'");
    bravo = NewClient(L"M\u00fcnchen \u03b1\U0001f600 Client");
    Put(alpha / L"notes.ctn", std::string("ALPHA PRIVATE NOTE alpha\0tail", 29));
    Put(bravo / L"notes.ctn", std::string("BRAVO PRIVATE NOTE beta\0tail", 28));
    Put(alpha / L"browser-session", "ALPHA SESSION");
    Put(bravo / L"browser-session", "BRAVO SESSION");
    NativeEvents events;
    ResetHooks reset;
    ResolveCases();
    MissingCases();
    RoundTripCases();
    RenameCase();
    MalformedCases();
    SizeCases();
    LockCases();
    UnsafeFileCases();
    ReparseCases();
    EarlyBoundaryCases();
    PublicationCases();
    DeletionAuthorityCases();
    NativeEvents::Audit();
    std::cout << "Client tab layout: " << checks << " checks, " << scenarios
        << " private scenarios, 0 failures; filesystem-only, no browser/input/clipboard calls.\n";
    return 0;
  } catch (const std::exception &error) {
    client_tab_layout::SetBeforeMutationHookForTest(nullptr);
    config_persistence::SetIniPublicationHookForTest(nullptr, nullptr);
    config_persistence::FailIniPublicationForTest(config_persistence::IniPublicationFailure::None);
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    std::wcout << L"Fixture retained: " << fixture << L'\n';
    return 1;
  }
}
