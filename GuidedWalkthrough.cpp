#include "GuidedWalkthrough.h"

#include <windows.h>

#include <array>
#include <cerrno>
#include <cwchar>
#include <limits>

namespace guided_walkthrough {
namespace {

// Command IDs intentionally mirror the stable Options-menu IDs without
// including the launcher's private implementation header.
constexpr unsigned kGuidedWalkthroughCommand = 41017;
constexpr unsigned kEditDefaultCommand = 41005;
constexpr unsigned kRenameClientCommand = 41012;
constexpr unsigned kCreateShortcutCommand = 41015;
constexpr unsigned kClientTitleFirstCommand = 41016;
constexpr unsigned kCleanupInactiveCommand = 41120;
constexpr unsigned kFirefoxCommand = 42004;

const std::vector<Topic> kTopics = {
    {L"welcome", 1, L"Your client spaces", L"Main window",
     L"ctSpaces gives each client a persistent browser profile: separate "
     L"sign-ins, cookies, browsing history, extensions, and session state. "
     L"Each client also has a separate slot for every supported browser.\r\n\r\n"
     L"These are normal browser profiles, not Incognito windows, encrypted "
     L"containers, or security boundaries. Anyone who can access your Windows "
     L"account and ctSpaces data may be able to access their contents.\r\n\r\n"
     L"You can Skip at any time. Reopen the full guide later from Options > "
     L"Guided walkthrough or by pressing F1.\r\n\r\n"
     L"This walkthrough is read-only. Moving through it never opens a browser, "
     L"changes a client, or runs the feature being described.",
     0, false},
    {L"create_open", 2, L"Create and open a client",
     L"New tab: CLIENT field, client icon, browser selector, and Create/Open",
     L"On the New tab, choose a browser, then choose an existing client or type "
     L"a new client name. Create makes that client's browser slot and opens it "
     L"immediately. For an existing client, the button says Open. Pressing Enter "
     L"in the client field performs the same primary action.\r\n\r\n"
     L"ctSpaces rejects reserved, invalid, empty, and overlong names. Watch the "
     L"text in the field: validation and sanitizing may change what you typed "
     L"before a client can be created. For an existing client, click the client "
     L"icon at the left of the CLIENT field. File Explorer opens the client "
     L"folder containing all of its browser slots. This is not the Temporary "
     L"button. Do not edit or delete profile files while a client browser is "
     L"open.",
     0, false},
    {L"browsers_restore", 2, L"Browsers and Restore tabs",
     L"Lower-left browser selector and Restore tabs switch",
     L"The browser selector chooses Edge, Chrome, Brave, or Firefox for the "
     L"current client. Browsers not found on this computer are disabled. A "
     L"client's browser slots are independent; ctSpaces does not merge or "
     L"convert their cookies, history, extensions, or sessions. The same client "
     L"can have Edge and Chrome or Firefox open at the same time.\r\n\r\n"
     L"For an existing slot, Restore tabs is a per-client, per-browser request "
     L"for the next launch. The browser still decides what it can restore. "
     L"Turning it off does not sign out or erase cookies. A newly created slot "
     L"always starts fresh.\r\n\r\n"
     L"When upgrading a client from 5.2, its existing Chromium data stays in "
     L"place and binds to the first Chromium browser used to open it; choose the "
     L"browser you previously used. Firefox selected first receives a clean separate slot "
     L"without binding or moving the existing Chromium data. Later browser "
     L"slots also start clean.",
     kFirefoxCommand, true},
    {L"sessions", 3, L"Open browser windows",
     L"Tabs across the top, Show, close x, and tab overflow",
     L"Open client windows appear as session tabs. The same client open in two "
     L"browsers can produce tabs with the same client name; ctSpaces tracks the "
     L"browser processes separately. Select a tab or use Show to bring its "
     L"window forward. Drag tabs to reorder them.\r\n\r\n"
     L"The x asks that browser session to close normally. Closing ctSpaces "
     L"while client browsers remain open requires confirmation so tracked "
     L"windows are not abandoned accidentally. Extra tabs move into the "
     L"overflow menu. Its first section lists client labels; choose one to show "
     L"that window. Below the divider, choose Close followed by a client label "
     L"to ask that session to close.",
     0, true},
    {L"pins_shortcuts", 3, L"Favorites, links, and shortcuts",
     L"Pushpin, pinned-client right-click menu, and Options",
     L"Pin an existing client for quick access; click the pushpin again to "
     L"unpin it. Unpinning removes only the favorite, not client data. You can "
     L"keep up to eight pinned clients; four are shown "
     L"directly and the rest are in overflow. Drag visible pins to reorder them, "
     L"or drag a pin onto the Windows Desktop to create that client's shortcut.\r\n\r\n"
     L"Click a pinned client to open it in the selected browser, or show its "
     L"existing window. Right-click a visible pin for distinct actions. Select client fills the "
     L"CLIENT field without launching. Open client opens the currently selected "
     L"browser, or shows that client/browser session if it is already open. "
     L"Restore tabs toggles the setting for that client and currently selected "
     L"browser without opening it. The menu can also open a copied http(s) URL; "
     L"other clipboard text is ignored. If that client's isolated Firefox "
     L"session is already open, Firefox cannot accept another command-line URL; "
     L"ctSpaces brings the window forward and does not open the copied link.\r\n\r\n"
     L"Create Desktop Shortcut records the browser selected at creation time. "
     L"Its visible name is exactly <client name>.lnk. Recreate it to change the browser; "
     L"ctSpaces updates only its verified managed shortcut. A shortcut request "
     L"hands off to an already-running ctSpaces instead of starting a second "
     L"launcher. Launcher and client/browser taskbar identities remain separate.",
     kCreateShortcutCommand, true},
    {L"temporary_default", 2, L"Temporary and starter profiles",
     L"Temporary button and Options > Edit Default profile",
     L"Temporary opens a disposable profile. Only one temporary profile exists "
     L"at a time. ctSpaces removes it after it closes when Windows releases its "
     L"files; locks can delay cleanup, and deletion is not a secure erase. Do "
     L"not do important work or store credentials there.\r\n\r\n"
     L"Default is a reusable Chromium starter profile for future client browser "
     L"slots. Editing it does not change existing clients, and Firefox does not "
     L"use it. After the Default browser closes, Save retains intended starter "
     L"bookmarks, bookmark favicons, and extension packages while excluding "
     L"private cookies, logins, history, and sessions. No/Discard leaves the "
     L"previous saved Default unchanged. Saved changes affect new profiles only. "
     L"Never sign a real client into Default.",
     kEditDefaultCommand, true},
    {L"identity", 3, L"Icons and window titles",
     L"Options > client icon and window-title commands",
     L"A custom client icon is shared by that client's browser slots and is "
     L"reflected in ctSpaces, supported live windows, and managed shortcuts. "
     L"Set Profile Icon uses a local image. Auto-fetch Icon asks for a website "
     L"domain and sends the domain you enter to Google's favicon service; use "
     L"Set Profile Icon if that network lookup is not appropriate. Remove "
     L"Custom Icon asks for confirmation, then returns the selected existing "
     L"client to the default icon and refreshes supported open windows without "
     L"removing browser data.\r\n\r\n"
     L"Client name first in window titles prefixes supported browser and "
     L"Alt+Tab titles, making many open client windows easier to distinguish.",
     kClientTitleFirstCommand, true},
    {L"organize", 2, L"Rename and archive clients", L"Options menu",
     L"Rename Client changes the client's name while preserving its browser "
     L"slots, ctSpaces preferences, icon, and verified managed shortcut. Every "
     L"slot for that client must be closed first.\r\n\r\n"
     L"Archive Client hides a closed client from the normal picker without "
     L"deleting its browser data. Use Options > Archived Clients to restore it "
     L"to the normal list. Archiving is organization, not backup or deletion.",
     kRenameClientCommand, true},
    {L"cleanup", 2, L"Cache and permanent deletion", L"Options menu",
     L"Vacuum clears supported browser caches while preserving the client's "
     L"stored profile, including logins and history. Clean Up Inactive Clients "
     L"previews clients not opened for three calendar months; an imported or "
     L"new client without reliable history begins a fresh tracking period. "
     L"Delete Multiple Clients has no inactivity waiting period.\r\n\r\n"
     L"Both bulk tools include archived clients, require explicit selection and "
     L"acknowledgement, recheck safety, and skip clients that are open or cannot "
     L"be verified. Deleting a client permanently removes all browser profiles "
     L"for it, including cookies, logins, bookmarks, history, extensions, and "
     L"sessions, plus managed metadata. Existing backup files and retained "
     L"Sites_PreRestore copies are separate and are not removed. Back up first. "
     L"Reset (Nuke) is intentionally unavailable. A failed or locked deletion "
     L"keeps the identifiers and activity date needed to retry. If the selection "
     L"window cannot open, nothing is deleted. If the results window fails, the "
     L"summary is shown through fallback messages.",
     kCleanupInactiveCommand, true},
    {L"backup_restore", 1, L"Back up and restore",
     L"Options > Back Up All Client Data or Restore Client Data",
     L"Close all client browsers before backup or restore, including matching "
     L"profiles started outside ctSpaces. A .7z backup contains sensitive "
     L"browser-profile data and is not encrypted by ctSpaces; protect the file "
     L"accordingly.\r\n\r\n"
     L"Restore validates current .7z backups and supported older .zip backups "
     L"before replacing the entire live client collection; restore is not a "
     L"merge. The prior collection is retained in a "
     L"Sites_PreRestore recovery folder when possible. Browser- or Windows-"
     L"protected sign-ins may not work after moving data to another computer.",
     0, false},
    {L"appearance", 2, L"Themes and keyboard",
     L"Options > Themes, and standard keyboard navigation",
     L"The Themes dialog previews a selection immediately. Apply saves it; "
     L"Cancel restores the theme that was active when the dialog opened.\r\n\r\n"
     L"Use Tab and Shift+Tab to move through controls, arrow keys in lists, and "
     L"Enter in the client field to launch. Press F1 from the main window to "
     L"open this walkthrough at any time. Right-click editable fields for Undo, "
     L"Cut, Copy, Paste, Delete, and Select All. Read-only result fields offer "
     L"Copy and Select All; normal shortcuts and international input still work.",
     0, false},
    {L"updates", 3, L"New since 5.2.0.14",
     L"Options > Quick tour, Guided walkthrough, or What's new",
     L"Guided walkthrough always opens the full topic list, so you can replay "
     L"the guide or jump directly to a subject. What's new shows only announced "
     L"topics whose current revision you have not read.\r\n\r\n"
     L"A dot and the text (New) identify unseen revised guidance. Next or Done "
     L"marks only the page currently shown as read. Skip, Close, Escape, and the "
     L"window X leave unseen topics unread. The guide never performs the client "
     L"or browser operations it describes.\r\n\r\n"
     L"New optional Quick tour: start it from Options or the full guide to "
     L"highlight real launcher controls with short Back, Next, Skip, and Done "
     L"callouts. It never clicks controls, changes selections or preferences, "
     L"creates clients, or launches browsers.\r\n\r\n"
     L"Expanded 20-step Quick tour: twelve illustration-only steps now cover "
     L"browser slots, the pinned right-click menu, Open copied link, Desktop "
     L"shortcuts by right-click or drag, pinned-client and session-tab "
     L"reordering, Default changes, rename, archive and restore, client-first "
     L"titles, inactive cleanup, and immediate multi-client deletion. These "
     L"steps explain existing workflows without performing their actions.\r\n\r\n"
     L"Updated guide: this revision documents existing folder, icon, pin, "
     L"shortcut, and session-menu actions more completely; reopen the full guide "
     L"from Options > Guided walkthrough or press F1 to review every topic. "
     L"The New dots identify workflows added since the colleague 5.2.0.14 "
     L"baseline; they do not mean every item was first implemented in 6.0.2.",
     kGuidedWalkthroughCommand, true},
};

bool IsMissingPathError(DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool KnownProfilePathIsAbsent(const std::filesystem::path &path) {
  SetLastError(ERROR_SUCCESS);
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES)
    return false;
  return IsMissingPathError(GetLastError());
}

unsigned ReadUnsigned(const std::filesystem::path &configPath,
                      const std::wstring &key) {
  wchar_t text[32]{};
  GetPrivateProfileStringW(L"guide", key.c_str(), L"", text,
                           static_cast<DWORD>(std::size(text)),
                           configPath.c_str());
  if (!text[0] || text[0] == L'-')
    return 0;
  wchar_t *end = nullptr;
  errno = 0;
  const unsigned long parsed = wcstoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != L'\0' ||
      parsed > (std::numeric_limits<unsigned>::max)()) {
    return 0;
  }
  return static_cast<unsigned>(parsed);
}

} // namespace

const std::vector<Topic> &Catalog() { return kTopics; }

bool IsFreshDataFolder(const std::filesystem::path &dataDirectory) {
  if (dataDirectory.empty())
    return false;
  // An absent data folder is the clearest first-run signal.
  SetLastError(ERROR_SUCCESS);
  const DWORD dataAttributes = GetFileAttributesW(dataDirectory.c_str());
  if (dataAttributes == INVALID_FILE_ATTRIBUTES) {
    return IsMissingPathError(GetLastError());
  }
  if ((dataAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (dataAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return false;
  }

  return KnownProfilePathIsAbsent(dataDirectory / L"config.ini") &&
         KnownProfilePathIsAbsent(dataDirectory / L"Sites") &&
         KnownProfilePathIsAbsent(dataDirectory / L"Default") &&
         KnownProfilePathIsAbsent(dataDirectory / L"Temp");
}

State LoadState(const std::filesystem::path &configPath) {
  State state;
  state.readRevisions.assign(kTopics.size(), 0);
  if (configPath.empty() ||
      config_persistence::InspectConfigFile(configPath) !=
          config_persistence::ConfigFileState::Regular) {
    return state;
  }
  state.welcomeHandled =
      GetPrivateProfileIntW(L"guide", L"welcome_handled", 0,
                            configPath.c_str()) != 0;
  state.welcomePending =
      GetPrivateProfileIntW(L"guide", L"welcome_pending", 0,
                            configPath.c_str()) != 0;
  for (size_t index = 0; index < kTopics.size(); ++index) {
    state.readRevisions[index] =
        ReadUnsigned(configPath, L"read_" + std::wstring(kTopics[index].id));
  }
  return state;
}

bool IsUnread(const State &state, size_t topicIndex) {
  return topicIndex < kTopics.size() &&
         (topicIndex >= state.readRevisions.size() ||
          state.readRevisions[topicIndex] < kTopics[topicIndex].revision);
}

bool HasUnreadAnnouncement(const State &state) {
  for (size_t index = 0; index < kTopics.size(); ++index) {
    if (kTopics[index].announce && IsUnread(state, index))
      return true;
  }
  return false;
}

std::vector<size_t> AllTopicIndices() {
  std::vector<size_t> indices;
  indices.reserve(kTopics.size());
  for (size_t index = 0; index < kTopics.size(); ++index)
    indices.push_back(index);
  return indices;
}

std::vector<size_t> UnreadAnnouncementIndices(const State &state) {
  std::vector<size_t> indices;
  for (size_t index = 0; index < kTopics.size(); ++index) {
    if (kTopics[index].announce && IsUnread(state, index))
      indices.push_back(index);
  }
  return indices;
}

std::vector<config_persistence::IniMutation> WelcomePendingMutations() {
  return {{L"guide", std::wstring(L"welcome_pending"), std::wstring(L"1")}};
}

std::vector<config_persistence::IniMutation> WelcomeHandledMutations() {
  return {{L"guide", std::wstring(L"welcome_handled"), std::wstring(L"1")},
          {L"guide", std::wstring(L"welcome_pending"), std::wstring(L"0")}};
}

std::vector<config_persistence::IniMutation>
ReadTopicMutations(const State &state, size_t topicIndex,
                   bool alsoHandleWelcome) {
  std::vector<config_persistence::IniMutation> mutations;
  if (alsoHandleWelcome) {
    mutations = WelcomeHandledMutations();
  }
  if (topicIndex < kTopics.size() && IsUnread(state, topicIndex)) {
    mutations.push_back(
        {L"guide", L"read_" + std::wstring(kTopics[topicIndex].id),
         std::to_wstring(kTopics[topicIndex].revision)});
  }
  return mutations;
}

void ApplyWelcomePending(State &state) { state.welcomePending = true; }

void ApplyWelcomeHandled(State &state) {
  state.welcomeHandled = true;
  state.welcomePending = false;
}

void ApplyTopicRead(State &state, size_t topicIndex) {
  if (topicIndex >= kTopics.size())
    return;
  if (state.readRevisions.size() < kTopics.size())
    state.readRevisions.resize(kTopics.size(), 0);
  // Never downgrade a future revision written by a newer build.
  if (state.readRevisions[topicIndex] < kTopics[topicIndex].revision)
    state.readRevisions[topicIndex] = kTopics[topicIndex].revision;
}

} // namespace guided_walkthrough
