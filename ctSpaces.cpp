// ctSpaces.cpp

// Because I didn't have the paitence to port this from the ground up, I used
// Gemini 2.5 Pro for heavylifting and filled in the gaps.

#pragma comment(                                                               \
    linker,                                                                    \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "Version.lib")

#include "BackupEngine.h"
#include "BrowserTitle.h"
#include "BrowserPreferencesJson.h"
#include "ClientShortcutName.h"
#include "ClientActivity.h"
#include "CleanupResultPages.h"
#include "ConfigPersistence.h"
#include "GuidedWalkthrough.h"
#include "InProc7z.h"
#include "OwnerDrawUi.h"
#include "ProgressUI.h"
#include "SiblingStageName.h"
#include "theme.h"
#include "version.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip> // FIX: Added for std::put_time
#include <iostream>
#include <limits> // for std::numeric_limits
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "resource.h" // For bundled resource IDs.

static int ShowThemedMessageBox(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags);
// Keep Windows' message-box layout, return values and keyboard safety, while
// routing every app-owned message through the same palette treatment.
#undef MessageBox
#define MessageBox(owner, text, title, flags) ShowThemedMessageBox(owner, text, title, flags)
#define MessageBoxW(owner, text, title, flags) ShowThemedMessageBox(owner, text, title, flags)

namespace fs = std::filesystem;

#ifdef CTSPACES_INSTALLER_TEST_HOOKS
extern int CtSpacesInstallerTestMessageBox(HWND owner, LPCWSTR text,
                                           LPCWSTR title, UINT flags);
extern bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &filePath,
                                                   std::wstring &version);
extern void CtSpacesInstallerTestAfterClientDeletePreflight(
    const fs::path &deleteRoot);
#endif

constexpr unsigned long long kMaxImportedImageFileBytes =
    16ull * 1024ull * 1024ull;
constexpr UINT kMaxImportedImageDimension = 8192;
constexpr unsigned long long kMaxImportedImagePixels = 32ull * 1024ull * 1024ull;
constexpr unsigned kMaxIcoEntries = 256;
constexpr UINT kMaxIcoPayloadDimension = 1024;
constexpr unsigned long long kMaxIcoPayloadPixels = 1024ull * 1024ull;
constexpr size_t kMaxClientNameLength = 240;

#pragma pack(push, 2)
struct GRPICONDIRENTRY {
  BYTE bWidth;
  BYTE bHeight;
  BYTE bColorCount;
  BYTE bReserved;
  WORD wPlanes;
  WORD wBitCount;
  DWORD dwBytesInRes;
  WORD nID; // RT_ICON id
};
struct GRPICONDIR {
  WORD idReserved;
  WORD idType;
  WORD idCount;
  GRPICONDIRENTRY idEntries[1];
};
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  BYTE bWidth;
  BYTE bHeight;
  BYTE bColorCount;
  BYTE bReserved;
  WORD wPlanes;
  WORD wBitCount;
  DWORD dwBytesInRes;
  DWORD dwImageOffset;
} ICONDIRENTRY;

typedef struct {
  WORD idReserved;
  WORD idType;
  WORD idCount;
  ICONDIRENTRY idEntries[1];
} ICONDIR;
#pragma pack(pop)

enum class ProfileType { Standard, Default, Temporary };
enum class BrowserKind : int { Edge = 0, Chrome = 1, Brave = 2, Firefox = 3 };
enum class ProfileUseState { NotInUse, InUse, Indeterminate };

struct CaseInsensitiveLess {
  bool operator()(const std::wstring &left,
                  const std::wstring &right) const noexcept {
    return _wcsicmp(left.c_str(), right.c_str()) < 0;
  }
};

static const wchar_t *GetBrowserId(BrowserKind browser) {
  switch (browser) {
  case BrowserKind::Edge:
    return L"edge";
  case BrowserKind::Chrome:
    return L"chrome";
  case BrowserKind::Brave:
    return L"brave";
  case BrowserKind::Firefox:
    return L"firefox";
  }
  return L"invalid";
}

static const char *GetBrowserIdAscii(BrowserKind browser) {
  switch (browser) {
  case BrowserKind::Edge:
    return "edge";
  case BrowserKind::Chrome:
    return "chrome";
  case BrowserKind::Brave:
    return "brave";
  case BrowserKind::Firefox:
    return "firefox";
  }
  return "invalid";
}

static const wchar_t *GetBrowserDisplayName(BrowserKind browser) {
  switch (browser) {
  case BrowserKind::Edge:
    return L"Microsoft Edge";
  case BrowserKind::Chrome:
    return L"Google Chrome";
  case BrowserKind::Brave:
    return L"Brave Browser";
  case BrowserKind::Firefox:
    return L"Mozilla Firefox";
  }
  return L"Unknown Browser";
}

static std::wstring GetClientAppUserModelId(const std::wstring &clientName,
                                            BrowserKind browser) {
  constexpr size_t kMaximumSlugLength = 48;
  constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
  constexpr uint64_t kFnvPrime = 1099511628211ULL;
  uint64_t hash = kFnvOffset;
  std::wstring slug;
  slug.reserve((std::min)(clientName.size(), kMaximumSlugLength));
  bool previousWasSeparator = false;
  for (wchar_t character : clientName) {
    const wchar_t lower = static_cast<wchar_t>(towlower(character));
    hash ^= static_cast<uint64_t>(static_cast<uint16_t>(lower));
    hash *= kFnvPrime;

    wchar_t safeCharacter = 0;
    if ((lower >= L'a' && lower <= L'z') ||
        (lower >= L'0' && lower <= L'9')) {
      safeCharacter = lower;
    } else if (!previousWasSeparator) {
      safeCharacter = L'-';
    }
    if (safeCharacter && slug.size() < kMaximumSlugLength) {
      slug.push_back(safeCharacter);
      previousWasSeparator = safeCharacter == L'-';
    }
  }
  while (!slug.empty() && slug.back() == L'-')
    slug.pop_back();
  if (slug.empty())
    slug = L"client";

  // AppUserModelIDs are limited to 128 characters and should not contain raw
  // spaces or punctuation. The hash keeps truncated/case-insensitive names
  // distinct while the slug remains useful in Explorer diagnostics.
  return std::format(L"ctSpaces.client.{}.{:016X}.{}", slug, hash,
                     GetBrowserId(browser));
}

static bool IsChromiumBrowser(BrowserKind browser) {
  return browser == BrowserKind::Edge || browser == BrowserKind::Chrome ||
         browser == BrowserKind::Brave;
}

static std::optional<BrowserKind> ParseBrowserKind(const std::wstring &value) {
  for (BrowserKind browser : {BrowserKind::Edge, BrowserKind::Chrome,
                              BrowserKind::Brave, BrowserKind::Firefox}) {
    if (_wcsicmp(value.c_str(), GetBrowserId(browser)) == 0)
      return browser;
  }
  if (value.size() == 1 && value[0] >= L'0' && value[0] <= L'3')
    return static_cast<BrowserKind>(value[0] - L'0');
  return std::nullopt;
}

struct ClientBrowserKey {
  std::wstring clientName;
  BrowserKind browser = BrowserKind::Edge;
};

struct ClientBrowserKeyLess {
  bool operator()(const ClientBrowserKey &left,
                  const ClientBrowserKey &right) const noexcept {
    const int nameOrder =
        _wcsicmp(left.clientName.c_str(), right.clientName.c_str());
    if (nameOrder != 0)
      return nameOrder < 0;
    return static_cast<int>(left.browser) < static_cast<int>(right.browser);
  }
};

struct ActiveProfileInfo {
  DWORD pid = 0;
  fs::path executablePath;
  fs::path profilePath;
  ProfileType type = ProfileType::Standard;
};

struct ProfileExitPayload {
  DWORD pid = 0;
  std::wstring clientName;
  BrowserKind browser = BrowserKind::Edge;
  ProfileType type = ProfileType::Standard;
  bool launchRollback = false;
  bool sessionAnnounced = true;
};

using ActiveProfileMap =
    std::map<ClientBrowserKey, ActiveProfileInfo, ClientBrowserKeyLess>;

const std::wstring APP_ALIAS = L"ctSpaces";
constexpr wchar_t LAUNCHER_APP_USER_MODEL_ID[] = L"ctSpaces.launcher";
const std::wstring APP_VERSION = CTSPACES_DISPLAY_VERSION_WTEXT;
const std::wstring APP_VARIANT = L"";
const std::wstring APP_TITLE =
    std::format(L"{} v{}{}", APP_ALIAS, APP_VERSION, APP_VARIANT);
const std::wstring GUI_CLASS_NAME = L"ctSpacesLauncherClass";
constexpr int DEFAULT_TEMPLATE_REVISION = 4;
constexpr int MAIN_GUI_WIDTH_DIP = 500;
constexpr int MAIN_GUI_HEIGHT_DIP = 190;
constexpr int MAIN_GUI_EMPTY_PIN_HEIGHT_DIP = 148;
constexpr int CLIENT_SELECTOR_ICON_LANE_DIP = 34;
constexpr int CLIENT_SELECTOR_DROP_LANE_DIP = 28;
constexpr size_t MAX_PINNED_CLIENTS = 8;
constexpr int MAX_VISIBLE_PINNED_CLIENTS = 4;
constexpr UINT MAX_RESTORE_TAB_PREFERENCES = 2048;
constexpr UINT MAX_ARCHIVED_CLIENTS = 2048;
HICON g_hIconBtnTemp = nullptr;
HICON g_hIconBtnConfig = nullptr;
ULONG_PTR g_gdiplusToken;
HINSTANCE g_hInst;
HWND g_hGui = NULL;
HWND g_hComboClient = NULL;
HWND g_hClientEdit = NULL;
HWND g_hClientEditSurface = NULL;
HWND g_hValidationTooltip = NULL;
HWND g_hBtnTmpProfTip = NULL;
HWND g_hBtnConfigTip = NULL;
static std::wstring g_sConfigTooltip = L"Options / Profile actions";
HWND g_hIconPreviewTip = NULL;
HWND g_hBtnGo = NULL;
HWND g_hBtnClientIcon = NULL;
HWND g_hBtnClientDrop = NULL;
HWND g_hBtnPin = NULL;
HWND g_hBtnRestoreTabs = NULL;
HWND g_hBtnTmpProf = NULL;
HWND g_hBtnConfig = NULL;
HWND g_hBtnPinTip = NULL;
HWND g_hBtnRestoreTabsTip = NULL;
static RECT g_rcSessionTabs{};
static RECT g_rcUtilityBar{};
static RECT g_rcPinnedArea{};
static RECT g_rcPinnedOverflow{};
static RECT g_rcBrowserSelector{};
static std::vector<RECT> g_sessionTabRects;
static std::vector<RECT> g_sessionTabCloseRects;
static std::vector<RECT> g_pinnedClientRects;
static RECT g_rcSessionOverflow{};
static std::vector<int> g_overflowSessionTabs;
static std::vector<std::wstring> g_pinnedClients;
static std::vector<std::wstring> g_clientsWithoutTabRestore;
static std::vector<std::wstring> g_archivedClients;
static std::mutex g_restoreTabsPreferenceMutex;
static int g_iHotPinnedClient = -1;
static bool g_bHotPinnedOverflow = false;
static bool g_bHotBrowserSelector = false;
static int g_iSelectedSessionTab = 0; // 0 = New, 1..n = open client
static bool g_bUiEnabled = true;
static bool g_bDefaultProfileUiBusy = false;
static bool g_bSyncingClientInput = false;
static bool g_bExitWhenProfilesClose = false;
static bool g_bClosePendingDuringLaunch = false;
static bool g_bArchiveTaskInProgress = false;
static bool g_bClientSelectorRestackPending = false;
static bool g_bRestoreTabsToggleAvailable = false;
static bool g_bRestoreTabsForSelection = false;
static std::atomic_bool g_bClientTitleFirst = false;
static bool g_bQaInstance = false;
static guided_walkthrough::State g_guideState;
static HWND g_hGuideDialog = nullptr;
static HWND g_hQuickTourDialog = nullptr;
static HWND g_hQuickTourFrame = nullptr;
enum class MainDragKind { None, PinnedClient, SessionTab };
static MainDragKind g_mainDragKind = MainDragKind::None;
static int g_iMainDragIndex = -1;
static POINT g_ptMainDragStart{};
static bool g_bMainDragActive = false;
static bool g_bPinnedOrderChanged = false;
static bool g_bPinnedDragSnapshotValid = false;
static std::vector<std::wstring> g_pinnedClientsBeforeDrag;
#define WM_APP_SESSION_STARTED (WM_APP + 5)
#define WM_APP_PROFILE_EXITED (WM_APP + 6)
#define WM_APP_FINALIZE_LAYOUT (WM_APP + 8)
#define WM_APP_RESTACK_CLIENT_SELECTOR (WM_APP + 9)
#define WM_APP_QA_REORDER_PINNED (WM_APP + 10)
#define WM_APP_QA_RESTORE_ARCHIVED (WM_APP + 11)
#define WM_APP_QA_CREATE_SHORTCUT (WM_APP + 12)
#define WM_APP_QA_RENAME_CLIENT (WM_APP + 13)
#define WM_APP_QA_ARCHIVE_CLIENT (WM_APP + 14)
#define WM_APP_PROFILE_SHUTDOWN_FAILED (WM_APP + 15)
#define WM_APP_LAUNCH_FAILED (WM_APP + 16)
#define WM_APP_PROFILE_MONITOR_FAILED (WM_APP + 17)
#define WM_APP_QA_CAN_EXPORT (WM_APP + 18)
#define WM_APP_SHOW_GUIDE (WM_APP + 19)
#define WM_APP_DISMISS_GUIDE (WM_APP + 20)
#define WM_APP_REFRESH_QUICK_TOUR (WM_APP + 21)

static int GetMainGuiHeightDip() {
  return g_pinnedClients.empty() ? MAIN_GUI_EMPTY_PIN_HEIGHT_DIP
                                 : MAIN_GUI_HEIGHT_DIP;
}

struct Session {
  std::wstring clientName;
  BrowserKind browser = BrowserKind::Edge;
  DWORD pid;
};
std::vector<Session> g_sessions;

HMENU g_hConfigMenu = nullptr;

HMENU g_hBrowserSubMenu = nullptr;
std::vector<HBITMAP> g_menuBitmaps; // owned; freed on WM_DESTROY
HWND g_hThemeCombo = nullptr;
const int g_iThemeSeparatorIndex = 3;
static bool g_bThemeApplyInProgress = false;
static bool g_bThemeMenuRefreshPending = false;
HFONT g_hFont = NULL;
static HFONT g_hFontLabel = NULL;
static HFONT g_hFontStrong = NULL;
static HFONT g_hFontClient = NULL;
static UINT g_uiDpi = USER_DEFAULT_SCREEN_DPI;
fs::path g_sDataDir;
fs::path g_sEdgePath;
fs::path g_sChromePath;
fs::path g_sBravePath;
fs::path g_sFirefoxPath;
fs::path g_sExeDir;
fs::path g_sTaskbarQaLogPath;
fs::path g_sShortcutDesktopOverride;
fs::path g_sConfigPath;
std::wstring g_sStartupInitFailure;
std::wstring g_sLastValidComboText = L"";
std::wstring g_sClientSel = L"";
static std::wstring g_sPinTooltip = L"Add to pinned clients";
std::mutex g_watcherLifecycleMutex;
std::jthread g_watcherThread;
std::jthread g_launchThread;
std::jthread g_shutdownThread;
struct ManagedReaperWorker {
  std::shared_ptr<std::atomic_bool> completed;
  std::jthread thread;
};
std::mutex g_reaperThreadsMutex;
std::vector<ManagedReaperWorker> g_reaperThreads;
std::mutex g_activeProfilesMutex;
std::atomic<bool> g_isWatcherRunning = false;
std::atomic<bool> g_isShuttingDown = false;
std::atomic<bool> g_isLaunchInFlight = false;
struct IconCacheSet {
  std::map<int, HICON> byPx; // requestedPx -> icon handle
  bool sourceInitialized = false;
  bool sourceExists = false;
  uintmax_t sourceSize = 0;
  fs::file_time_type::duration::rep sourceWriteStamp = 0;
};
std::map<std::wstring, IconCacheSet> g_iconCache;
std::map<std::wstring, std::vector<HICON>> g_retiredIconHandles;

std::mutex g_iconCacheMutex;
IShellLink *shellLink = NULL;
#define WM_APP_TASK_COMPLETE (WM_APP + 1)
#define IDC_BTN_TEMP 200
#define IDC_BTN_CONFIG 201
#define IDC_BTN_PIN 202
#define IDC_BTN_CLIENT_DROP 203
#define IDC_BTN_CLIENT_ICON 204
#define IDC_CLIENT_EDIT_SURFACE 205
#define IDC_CLIENT_EDIT 206
#define IDC_BTN_RESTORE_TABS 207

// Config menu command IDs
#define IDM_CTX_SET_PROFILE_ICON 41001
#define IDM_CTX_RESET_PROFILE 41003
#define IDM_CTX_DELETE_PROFILE 41004
#define IDM_CTX_CLEANUP_INACTIVE 41120
#define IDM_CTX_DELETE_MULTIPLE 41121
#define IDM_CTX_EDIT_DEFAULT_PROFILE 41005
#define IDM_CTX_THEME_COLOR 41006
#define IDM_CTX_VACUUM_PROFILE 41007
#define IDM_CTX_FETCH_ICON 41008
#define IDM_CTX_EXPORT_ALL 41009
#define IDM_CTX_REMOVE_ICON 41010
#define IDM_CTX_RESTORE_ALL 41011
#define IDM_CTX_RENAME_PROFILE 41012
#define IDM_CTX_ARCHIVE_PROFILE 41013
#define IDM_CTX_ARCHIVED_CLIENTS 41014
#define IDM_CTX_CREATE_SHORTCUT 41015
#define IDM_CTX_CLIENT_TITLE_FIRST 41016
#define IDM_GUIDED_WALKTHROUGH 41017
#define IDM_WHATS_NEW 41018
#define IDM_BROWSER_EDGE 42001
#define IDM_BROWSER_CHROME 42002
#define IDM_BROWSER_BRAVE 42003
#define IDM_BROWSER_FIREFOX 42004
#define IDC_ABOUT_ICON 5101
#define IDC_ABOUT_TITLE 5102
#define IDC_ABOUT_BY 5103
#define IDC_ABOUT_GAP 5104
#define IDC_ABOUT_HOME 5105
#define IDC_STATIC_PROMPT 101
#define IDC_GRP_ICON 301
#define IDC_ICON_PREVIEW 302
#define IDC_LBL_ICON 303
#define IDC_THEME_COMBO 5201
#define IDC_THEME_APPLY 5202
#define IDC_THEME_CANCEL 5203

static HWND g_hLblIcon = nullptr;

// Icon preview controls/state
static HWND g_hGrpIcon = nullptr;
static HWND g_hIconPreview = nullptr;
static HICON g_hIconPreviewHandle = nullptr;
static HWND g_hHotButton = nullptr;

// Menu tooltip state
static HWND g_hMenuTip = nullptr;
static TOOLINFOW g_menuTi{};
static std::map<UINT, std::wstring> g_menuTipText; // menu id -> tooltip text

static HFONT g_hFontAboutSmall = nullptr;
static HFONT g_hAboutFont = nullptr;
static HICON g_hAboutIcon64 = nullptr;
static HICON g_hAboutDlgSmall = nullptr;
static HICON g_hAboutDlgBig = nullptr;
static int g_iComboItemHeight = 0;
static constexpr wchar_t DEFAULT_THEME_NAME[] = L"Dark - Gothic";
static int g_iThemeMode =
    1; // 0=System Auto, 1=System Light, 2=System Dark, >=3 custom themes
static BrowserKind g_selectedBrowser = BrowserKind::Edge;
static bool g_bThemeIsDark = false;
static HBRUSH g_hbrThemeWindow = nullptr;
static HBRUSH g_hbrThemeControl = nullptr;
static HBRUSH g_hbrThemeButton = nullptr;
static HBRUSH g_hbrThemeControlHot = nullptr;
static HBRUSH g_hbrThemeMenu = nullptr;
static HBRUSH g_hbrThemeMenuSel = nullptr;
static HBRUSH g_hbrThemeBorder = nullptr;
static HBRUSH g_hbrThemeIconPreview = nullptr;
static HICON g_hIconColorLight = nullptr;
static HICON g_hIconColorDark = nullptr;
static HBITMAP g_hBmpColorLight = nullptr;
static HBITMAP g_hBmpColorDark = nullptr;

void GuiProfVacuum();
void GuiProfReset();
void GuiOpenDef();
void GuiOpenTmp();
void GuiAutoIcon();
void GuiProfExportAll();
void GuiProfRestoreAll();
void GuiRemoveIcon();
static std::wstring GetSelectedClientNameSanitized(bool preferListSelection);
static void LoadPinnedClients();
static bool SavePinnedClients();
static void PrunePinnedClients();
static bool IsClientPinned(const std::wstring &clientName);
static void LoadArchivedClients();
static bool SaveArchivedClients();
static void PruneArchivedClients();
static bool IsClientArchived(const std::wstring &clientName);
static void LoadRestoreTabsPreferences();
static bool SaveRestoreTabsPreferences();
static void PruneRestoreTabsPreferences();
static bool ShouldRestoreTabsForClient(const std::wstring &clientName,
                                       BrowserKind browser);
static BrowserKind GetBrowserForCurrentSelection();
static void UpdateRestoreTabsToggleState();
static void ToggleRestoreTabsForSelectedClient();
static void UpdatePinButtonState();
static void ToggleSelectedClientPin();
static bool SelectPinnedClient(const std::wstring &clientName);
static void OpenPinnedClient(const std::wstring &clientName,
                             BrowserKind browser = g_selectedBrowser);
static bool OpenWebUrlForClient(const std::wstring &clientName,
                                const std::wstring &url,
                                BrowserKind browser = g_selectedBrowser);
static std::optional<std::wstring> GetClipboardWebUrl();
static void ShowPinnedClientOptionsMenu(HWND hWnd,
                                        const std::wstring &clientName,
                                        POINT screenPoint);
static bool CreateClientDesktopShortcut(const std::wstring &clientName,
                                         std::optional<BrowserKind> browser,
                                         bool showConfirmation = true,
                                         std::wstring *diagnostics = nullptr);
static bool ProcessClientLaunchRequest(const std::wstring &arguments,
                                       bool showErrors = true);
static bool HasClientLaunchRequest(const std::wstring &arguments);
static void ShowGuidedWalkthrough(bool whatsNewOnly,
                                  bool initialWelcome = false);
static void ShowQuickTour();
static void DismissQuickTour(bool restoreFocus);
static void DismissQuickTourForOwnerClosing();
static void RefreshQuickTourPlacement();
static void UpdateGuideIndicators();
static bool RenameClientProfile(const std::wstring &oldName,
                                const std::wstring &newName,
                                std::wstring &errorMessage);
static bool ArchiveClientProfile(const std::wstring &clientName,
                                 std::wstring &errorMessage);
static void GuiRenameClient();
static void GuiArchiveClient();
static void ShowArchivedClientsMenu(HWND hWnd);
static void OpenSelectedClientFolder();

struct ThemeColors {
  COLORREF crWindow;
  COLORREF crWindowText;
  COLORREF crCaption;
  COLORREF crCaptionText;
  COLORREF crControl;
  COLORREF crControlText;
  COLORREF crButtonFace;
  COLORREF crButtonText;
  COLORREF crControlHot;
  COLORREF crControlBorder;
  COLORREF crAccent;
  COLORREF crAccentText;
  COLORREF crMenu;
  COLORREF crMenuText;
  COLORREF crMenuSel;
  COLORREF crMenuSelText;
  COLORREF crTip;
  COLORREF crTipText;
};

static ThemeColors g_themeColors{};
struct ThemeEntry {
  std::wstring name;
  ThemeColors colors{};
  bool dark = false;
};
static std::vector<ThemeEntry> g_aCustomThemes;

static int NormalizeThemeMode(int iMode) {
  if (iMode < 0)
    return 1;
  if (iMode <= 2)
    return iMode;
  if (g_aCustomThemes.empty())
    return 1;
  const int iMax = 3 + (int)g_aCustomThemes.size() - 1;
  if (iMode > iMax)
    return 1;
  return iMode;
}

// Minimal InputBox using in-memory dialog template
// Robust InputBox using standard CreateWindow (no dialog templates)
struct MenuItemData {
  std::wstring text;
  HICON hIcon = nullptr; // weak ref
  bool separator = false;
  bool isSubmenu = false;
  bool broomGlyph = false;
  bool newDot = false;
};

static std::vector<std::unique_ptr<MenuItemData>> g_aMenuItemData;
static int g_iMenuIconPx = 0;
static int g_iMenuMinWidth = 0;

static void ApplyThemeToWindow(HWND hWnd);
static HBRUSH HandleThemeCtlColor(UINT msg, HDC hdc, HWND hCtl);
static void DrawOwnerDrawItem(const DRAWITEMSTRUCT &ds);
static void ApplyEditContextMenuTheme(HWND edit);

static void ClearMenuItemData() {
  for (auto &vItem : g_aMenuItemData) {
    if (vItem && vItem->hIcon) {
      DestroyIcon(vItem->hIcon);
      vItem->hIcon = nullptr;
    }
  }
  g_aMenuItemData.clear();
  g_iMenuIconPx = 0;
  g_iMenuMinWidth = 0;
}

static MenuItemData *AddMenuItemData(const wchar_t *text, HICON hIcon,
                                     bool separator, bool isSubmenu = false,
                                     bool newDot = false) {
  auto vItem = std::make_unique<MenuItemData>();
  if (text)
    vItem->text = text;
  vItem->hIcon = hIcon;
  vItem->separator = separator;
  vItem->isSubmenu = isSubmenu;
  vItem->newDot = newDot;
  MenuItemData *ptr = vItem.get();
  g_aMenuItemData.push_back(std::move(vItem));
  return ptr;
}

class InputBoxWindow {
public:
  static std::optional<std::wstring>
  Show(HWND owner, const std::wstring &title, const std::wstring &prompt,
       const std::wstring &defaultValue = L"") {
    InputParams params{title, prompt, defaultValue, L"", false, false,
                       GetCurrentThreadId()};

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"InputBoxWndClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    const ATOM inputClass = RegisterClassExW(&wc);
    if (!inputClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return std::nullopt;

    const DWORD style = WS_VISIBLE | WS_POPUP | WS_CAPTION | WS_SYSMENU |
                        WS_CLIPCHILDREN;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    RECT rcWindow{0, 0, Scale(392, dpi), Scale(138, dpi)};
    auto adjustForDpi =
        reinterpret_cast<BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT)>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"),
                           "AdjustWindowRectExForDpi"));
    if (adjustForDpi) {
      adjustForDpi(&rcWindow, style, FALSE, exStyle, dpi);
    } else {
      AdjustWindowRectEx(&rcWindow, style, FALSE, exStyle);
    }

    RECT rcOwner{};
    if (owner)
      GetWindowRect(owner, &rcOwner);
    else
      GetWindowRect(GetDesktopWindow(), &rcOwner);
    const int w = rcWindow.right - rcWindow.left;
    const int h = rcWindow.bottom - rcWindow.top;
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;

    HMONITOR monitor = MonitorFromWindow(
        owner ? owner : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
      x = max(monitorInfo.rcWork.left,
              min(x, monitorInfo.rcWork.right - w));
      y = max(monitorInfo.rcWork.top,
              min(y, monitorInfo.rcWork.bottom - h));
    }

    HWND hWnd = CreateWindowExW(
        exStyle, L"InputBoxWndClass", title.c_str(), style, x, y, w, h, owner,
        NULL, GetModuleHandle(NULL), &params);

    if (!hWnd)
      return std::nullopt;

    if (owner)
      EnableWindow(owner, FALSE); // Simulate modal

    MSG msg{};
    while (!params.finished) {
      const BOOL messageResult = GetMessageW(&msg, NULL, 0, 0);
      if (messageResult == 0) {
        PostQuitMessage(static_cast<int>(msg.wParam));
        break;
      }
      if (messageResult == -1)
        break;
      if (params.finished)
        break;
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
        DestroyWindow(hWnd);
        break;
      }
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
        HWND hFocus = GetFocus();
        const int command =
            hFocus == GetDlgItem(hWnd, IDCANCEL) ? IDCANCEL : IDOK;
        SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED),
                     (LPARAM)GetDlgItem(hWnd, command));
        if (!IsWindow(hWnd))
          break;
        continue;
      }
      if (!IsDialogMessage(hWnd, &msg)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      if (!IsWindow(hWnd))
        break; // Window closed
    }

    if (owner) {
      EnableWindow(owner, TRUE);
      SetForegroundWindow(owner);
    }

    if (!params.confirmed)
      return std::nullopt;
    return params.result;
  }

private:
  static constexpr int kPromptId = 101;
  static constexpr int kEditId = 102;

  static int Scale(int dip, UINT dpi) {
    return MulDiv(dip, (int)(dpi ? dpi : USER_DEFAULT_SCREEN_DPI),
                  USER_DEFAULT_SCREEN_DPI);
  }

  static std::wstring ReadEditText(HWND hWnd) {
    const HWND edit = GetDlgItem(hWnd, kEditId);
    if (!edit)
      return L"";

    const LRESULT length = SendMessageW(edit, WM_GETTEXTLENGTH, 0, 0);
    if (length <= 0)
      return L"";

    std::vector<wchar_t> text((size_t)length + 1, L'\0');
    SendMessageW(edit, WM_GETTEXT, (WPARAM)text.size(),
                 (LPARAM)text.data());
    return std::wstring(text.data());
  }

  static void LayoutControls(HWND hWnd, UINT dpi) {
    RECT rc{};
    GetClientRect(hWnd, &rc);

    const int margin = Scale(14, dpi);
    const int gap = Scale(6, dpi);
    const int promptH = Scale(20, dpi);
    const int editH = Scale(30, dpi);
    const int buttonW = Scale(78, dpi);
    const int buttonH = Scale(28, dpi);
    const int contentW = max(Scale(120, dpi), rc.right - margin * 2);
    const int editY = margin + promptH + gap;
    const int buttonY = rc.bottom - margin - buttonH;
    const int border = max(1, Scale(1, dpi));

    MoveWindow(GetDlgItem(hWnd, kPromptId), margin, margin, contentW, promptH,
               TRUE);
    MoveWindow(GetDlgItem(hWnd, kEditId), margin + border, editY + border,
               contentW - border * 2, editH - border * 2, TRUE);
    MoveWindow(GetDlgItem(hWnd, IDOK),
               rc.right - margin - buttonW * 2 - gap, buttonY, buttonW,
               buttonH, TRUE);
    MoveWindow(GetDlgItem(hWnd, IDCANCEL), rc.right - margin - buttonW,
               buttonY, buttonW, buttonH, TRUE);
    InvalidateRect(hWnd, nullptr, TRUE);
  }

  static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR subclassId,
                                           DWORD_PTR) {
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE) {
      InvalidateRect(GetParent(hWnd), nullptr, FALSE);
    } else if (msg == WM_NCDESTROY) {
      RemoveWindowSubclass(hWnd, EditSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
  }

  static void PaintBackground(HWND hWnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc,
             g_hbrThemeWindow ? g_hbrThemeWindow
                              : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE));

    HWND hEdit = GetDlgItem(hWnd, kEditId);
    if (!hEdit)
      return;

    RECT rcEdit{};
    GetWindowRect(hEdit, &rcEdit);
    MapWindowPoints(nullptr, hWnd, reinterpret_cast<POINT *>(&rcEdit), 2);
    const UINT dpi = GetDpiForWindow(hWnd);
    InflateRect(&rcEdit, max(1, Scale(1, dpi)), max(1, Scale(1, dpi)));
    const bool focused = GetFocus() == hEdit;
    const COLORREF borderColor =
        focused ? g_themeColors.crAccent : g_themeColors.crControlBorder;
    const int penWidth = max(1, Scale(focused ? 2 : 1, dpi));
    HPEN hPen = CreatePen(PS_SOLID, penWidth, borderColor);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rcEdit.left, rcEdit.top, rcEdit.right, rcEdit.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);
  }

  struct InputParams {
    std::wstring title;
    std::wstring prompt;
    std::wstring def;
    std::wstring result;
    bool confirmed;
    bool finished;
    DWORD ownerThreadId;
  };

  static void Finish(HWND hWnd, InputParams *params, bool confirmed) {
    if (params) {
      params->confirmed = confirmed;
      params->finished = true;
    }
    DestroyWindow(hWnd);
    if (params && params->ownerThreadId)
      PostThreadMessageW(params->ownerThreadId, WM_NULL, 0, 0);
  }

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam) {
    if (msg == WM_CREATE) {
      LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
      InputParams *p = (InputParams *)pcs->lpCreateParams;
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)p);

      HFONT hFont = g_hFont ? g_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);

      HWND hSt = CreateWindowW(L"STATIC", p->prompt.c_str(),
                               WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                               0, 0, 10, 10, hWnd,
                               (HMENU)(INT_PTR)kPromptId,
                               GetModuleHandle(NULL), NULL);
      SendMessage(hSt, WM_SETFONT,
                  (WPARAM)(g_hFontLabel ? g_hFontLabel : hFont), TRUE);

      HWND hEd = CreateWindowExW(
          0, L"EDIT", p->def.c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
          hWnd, (HMENU)(INT_PTR)kEditId, GetModuleHandle(NULL), NULL);
      SendMessage(hEd, WM_SETFONT, (WPARAM)hFont, TRUE);
      const int editPad = Scale(7, GetDpiForWindow(hWnd));
      SendMessageW(hEd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                   MAKELPARAM(editPad, editPad));
      if (p->def.empty())
        SendMessageW(hEd, EM_SETCUEBANNER, TRUE, (LPARAM)L"example.com");
      SendMessage(hEd, EM_SETSEL, 0, -1);
      SetWindowSubclass(hEd, EditSubclassProc, 1, 0);
      ApplyEditContextMenuTheme(hEd);
      SetFocus(hEd);

      HWND hBtnOK = CreateWindowW(
          L"BUTTON", L"OK",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_OWNERDRAW, 0, 0,
          10, 10,
          hWnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
      SendMessage(hBtnOK, WM_SETFONT, (WPARAM)hFont, TRUE);

      HWND hBtnCancel = CreateWindowW(
          L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
          hWnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
      SendMessage(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

      LayoutControls(hWnd, GetDpiForWindow(hWnd));
      ApplyThemeToWindow(hWnd);

      return 0;
    }

    if (msg == WM_SIZE) {
      LayoutControls(hWnd, GetDpiForWindow(hWnd));
      return 0;
    }

    if (msg == WM_DPICHANGED) {
      const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
      if (suggested) {
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      LayoutControls(hWnd, HIWORD(wParam));
      ApplyThemeToWindow(hWnd);
      return 0;
    }

    if (msg == WM_DRAWITEM) {
      const DRAWITEMSTRUCT *draw =
          reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
      if (draw)
        DrawOwnerDrawItem(*draw);
      return TRUE;
    }

    if (msg == WM_CTLCOLORBTN || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORSTATIC) {
      return (LRESULT)HandleThemeCtlColor(msg, (HDC)wParam, (HWND)lParam);
    }

    if (msg == WM_ERASEBKGND) {
      PaintBackground(hWnd, (HDC)wParam);
      return TRUE;
    }

    if (msg == WM_PAINT) {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hWnd, &ps);
      PaintBackground(hWnd, hdc);
      EndPaint(hWnd, &ps);
      return 0;
    }

    if (msg == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id == kEditId && HIWORD(wParam) == EN_CHANGE) {
        InputParams *p = (InputParams *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (p)
          p->result = ReadEditText(hWnd);
        return 0;
      }
      if (id == IDOK) {
        InputParams *p = (InputParams *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (p)
          p->result = ReadEditText(hWnd);
        Finish(hWnd, p, true);
        return 0;
      }
      if (id == IDCANCEL) {
        InputParams *p = (InputParams *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        Finish(hWnd, p, false);
        return 0;
      }
    }

    if (msg == WM_CLOSE) {
      InputParams *p = (InputParams *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
      Finish(hWnd, p, false);
      return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
  }
};

static int FindThemeModeByName(const std::wstring &name) {
  if (_wcsicmp(name.c_str(), L"System (Auto)") == 0)
    return 0;
  if (_wcsicmp(name.c_str(), L"System (Light)") == 0)
    return 1;
  if (_wcsicmp(name.c_str(), L"System (Dark)") == 0)
    return 2;
  for (int i = 0; i < (int)g_aCustomThemes.size(); ++i) {
    if (_wcsicmp(g_aCustomThemes[i].name.c_str(), name.c_str()) == 0)
      return 3 + i;
  }
  return -1;
}

static std::wstring GetThemePreferenceName(int mode) {
  if (mode == 0)
    return L"System (Auto)";
  if (mode == 1)
    return L"System (Light)";
  if (mode == 2)
    return L"System (Dark)";
  const int customIndex = mode - 3;
  if (customIndex >= 0 && customIndex < (int)g_aCustomThemes.size())
    return g_aCustomThemes[customIndex].name;
  return L"System (Light)";
}

static bool HasReadableConfigFile() {
  if (g_sConfigPath.empty())
    return false;

  unsigned long windowsError = ERROR_SUCCESS;
  const auto state = config_persistence::InspectConfigFile(
      g_sConfigPath, &windowsError);
  if (state == config_persistence::ConfigFileState::Missing)
    return false;
  if (state == config_persistence::ConfigFileState::Unavailable) {
    throw std::runtime_error(std::format(
        "config.ini is unavailable, unreadable, or not a safe regular file "
        "(Windows error {}).",
        windowsError));
  }
  return true;
}

static bool SaveConfigMutations(
    const wchar_t *description,
    const std::vector<config_persistence::IniMutation> &mutations,
    bool reportFailure = true) {
  if (g_sConfigPath.empty())
    return false;

  std::wstring details;
  if (config_persistence::ApplyIniMutationsAtomically(
          g_sConfigPath, mutations, &details)) {
    return true;
  }

  std::wstring message =
      std::format(L"The {} could not be saved safely. Existing saved "
                  L"settings were left unchanged, and ctSpaces will keep "
                  L"running without overwriting them.",
                  description);
  if (!details.empty())
    message += L"\n\n" + details;
  if (reportFailure) {
    MessageBoxW(g_hGui, message.c_str(), L"Configuration Was Not Saved",
                MB_OK | MB_ICONWARNING);
  }
  return false;
}

static bool SaveThemePreference() {
  if (g_sConfigPath.empty())
    return false;
  const std::wstring v = std::to_wstring(g_iThemeMode);
  const std::wstring themeName = GetThemePreferenceName(g_iThemeMode);
  return SaveConfigMutations(
      L"theme preference",
      {{L"user", std::wstring(L"theme"), v},
       {L"user", std::wstring(L"theme_name"), themeName}});
}

static bool SaveBrowserPreference() {
  if (g_sConfigPath.empty())
    return false;
  const std::wstring v = GetBrowserId(g_selectedBrowser);
  return SaveConfigMutations(
      L"browser preference",
      {{L"user", std::wstring(L"browser"), v}});
}

static void LoadThemePreference() {
  if (g_sConfigPath.empty())
    return;
  if (!HasReadableConfigFile()) {
    const int defaultMode = FindThemeModeByName(DEFAULT_THEME_NAME);
    g_iThemeMode = defaultMode >= 0 ? defaultMode : 2;
    return;
  }

  wchar_t nameBuffer[128]{};
  GetPrivateProfileStringW(L"user", L"theme_name", L"", nameBuffer,
                           (DWORD)std::size(nameBuffer),
                           g_sConfigPath.c_str());
  if (nameBuffer[0]) {
    const int namedMode = FindThemeModeByName(nameBuffer);
    if (namedMode >= 0) {
      g_iThemeMode = namedMode;
      return;
    }
  }

  wchar_t buf[32]{};
  GetPrivateProfileStringW(L"user", L"theme", L"", buf,
                           (DWORD)(sizeof(buf) / sizeof(buf[0])),
                           g_sConfigPath.c_str());
  if (buf[0] != L'\0') {
    g_iThemeMode = _wtoi(buf);
    return;
  }

  const int defaultMode = FindThemeModeByName(DEFAULT_THEME_NAME);
  g_iThemeMode = defaultMode >= 0 ? defaultMode : 2;
}

static bool SaveClientTitlePreference() {
  if (g_sConfigPath.empty())
    return false;
  return SaveConfigMutations(
      L"client-title preference",
      {{L"user", std::wstring(L"client_title_first"),
        std::wstring(g_bClientTitleFirst.load() ? L"1" : L"0")}});
}

static void LoadClientTitlePreference() {
  if (g_sConfigPath.empty())
    return;
  if (!HasReadableConfigFile())
    return;
  g_bClientTitleFirst =
      GetPrivateProfileIntW(L"user", L"client_title_first", 0,
                            g_sConfigPath.c_str()) != 0;
}

static void LoadBrowserPreference() {
  if (g_sConfigPath.empty())
    return;
  if (!HasReadableConfigFile())
    return;
  wchar_t buf[32]{};
  GetPrivateProfileStringW(L"user", L"browser", L"", buf,
                           (DWORD)(sizeof(buf) / sizeof(buf[0])),
                           g_sConfigPath.c_str());
  if (buf[0] != L'\0') {
    const auto storedBrowser = ParseBrowserKind(buf);
    if (storedBrowser)
      g_selectedBrowser = *storedBrowser;
  }
}

static HFONT GetAboutSmallFont() {
  if (g_hFontAboutSmall)
    return g_hFontAboutSmall;
  LOGFONTW lf{};
  HFONT base = g_hAboutFont ? g_hAboutFont : g_hFont;
  if (base && GetObjectW(base, sizeof(lf), &lf) == sizeof(lf)) {
    lf.lfHeight = (lf.lfHeight * 85) / 100; // smaller
    g_hFontAboutSmall = CreateFontIndirectW(&lf);
  }
  return g_hFontAboutSmall;
}

static void EnableDpiAwareness() {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32)
    return;

  auto setCtx = reinterpret_cast<BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT)>(
      GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
  if (setCtx) {
    if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
      return;
    setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    return;
  }

  auto setAware = reinterpret_cast<BOOL(WINAPI *)(void)>(
      GetProcAddress(user32, "SetProcessDPIAware"));
  if (setAware)
    setAware();
}

static int ScaleByDpi(int value, UINT dpi) {
  return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
}

static HFONT CreateUiFont(UINT dpi) {
  const int pointSize = 10;
  return CreateFontW(-MulDiv(pointSize, (int)dpi, 72), 0, 0, 0, FW_NORMAL,
                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Text");
}

static HFONT CreateUiLabelFont(UINT dpi) {
  const int pointSize = 8;
  return CreateFontW(-MulDiv(pointSize, (int)dpi, 72), 0, 0, 0, FW_SEMIBOLD,
                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Text");
}

static HFONT CreateUiStrongFont(UINT dpi) {
  const int pointSize = 10;
  return CreateFontW(-MulDiv(pointSize, (int)dpi, 72), 0, 0, 0, FW_SEMIBOLD,
                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Text");
}

static HFONT CreateClientFont(UINT dpi) {
  const int pointSize = 11;
  return CreateFontW(-MulDiv(pointSize, (int)dpi, 72), 0, 0, 0, FW_NORMAL,
                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Text");
}

static void UpdateComboBoxMetrics(UINT dpi) {
  if (!g_hComboClient || !g_hFont)
    return;

  HDC hdc = GetDC(g_hComboClient);
  if (!hdc)
    return;

  HFONT old = (HFONT)SelectObject(hdc, g_hFont);
  TEXTMETRICW tm{};
  if (GetTextMetricsW(hdc, &tm)) {
    const int listItemH = max(
        tm.tmHeight + tm.tmExternalLeading + ScaleByDpi(6, dpi),
        ScaleByDpi(24, dpi));
    const int selectionH = ScaleByDpi(32, dpi);
    g_iComboItemHeight = listItemH;
    SendMessageW(g_hComboClient, CB_SETITEMHEIGHT, (WPARAM)-1, selectionH);
    SendMessageW(g_hComboClient, CB_SETITEMHEIGHT, 0, listItemH);
  }
  SelectObject(hdc, old);
  ReleaseDC(g_hComboClient, hdc);
}

static void UpdateUiFont(HWND hWnd, UINT dpi) {
  HFONT hNew = CreateUiFont(dpi);
  HFONT hNewLabel = CreateUiLabelFont(dpi);
  HFONT hNewStrong = CreateUiStrongFont(dpi);
  HFONT hNewClient = CreateClientFont(dpi);
  if (!hNew || !hNewLabel || !hNewStrong || !hNewClient) {
    if (hNew)
      DeleteObject(hNew);
    if (hNewLabel)
      DeleteObject(hNewLabel);
    if (hNewStrong)
      DeleteObject(hNewStrong);
    if (hNewClient)
      DeleteObject(hNewClient);
    return;
  }

  if (g_hFont)
    DeleteObject(g_hFont);
  if (g_hFontLabel)
    DeleteObject(g_hFontLabel);
  if (g_hFontStrong)
    DeleteObject(g_hFontStrong);
  if (g_hFontClient)
    DeleteObject(g_hFontClient);
  g_hFont = hNew;
  g_hFontLabel = hNewLabel;
  g_hFontStrong = hNewStrong;
  g_hFontClient = hNewClient;

  if (hWnd)
    SendMessageW(hWnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (hWnd) {
    EnumChildWindows(
        hWnd,
        [](HWND hwnd, LPARAM lParam) -> BOOL {
          SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
          return TRUE;
        },
        (LPARAM)g_hFont);
  }

  HWND hPrompt = hWnd ? GetDlgItem(hWnd, IDC_STATIC_PROMPT) : nullptr;
  if (hPrompt)
    SendMessageW(hPrompt, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
  if (g_hBtnGo)
    SendMessageW(g_hBtnGo, WM_SETFONT, (WPARAM)g_hFontStrong, TRUE);
  if (g_hComboClient)
    SendMessageW(g_hComboClient, WM_SETFONT, (WPARAM)g_hFontClient, TRUE);
  if (g_hClientEdit)
    SendMessageW(g_hClientEdit, WM_SETFONT, (WPARAM)g_hFontClient, TRUE);

  if (g_hValidationTooltip)
    SendMessageW(g_hValidationTooltip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (g_hMenuTip)
    SendMessageW(g_hMenuTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (g_hBtnTmpProfTip)
    SendMessageW(g_hBtnTmpProfTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (g_hBtnConfigTip)
    SendMessageW(g_hBtnConfigTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (g_hBtnPinTip)
    SendMessageW(g_hBtnPinTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (g_hBtnRestoreTabsTip)
    SendMessageW(g_hBtnRestoreTabsTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  if (g_hIconPreviewTip)
    SendMessageW(g_hIconPreviewTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);

  UpdateComboBoxMetrics(dpi);
}

static void LayoutMainGui(HWND hWnd);
static void DrawMainSurface(HDC hdc);
static void UpdateIconPreviewForSelection(bool preferListSelection = false);
static void BuildSessionTabRects(HWND hWnd, HDC hdc);
static void DrawSessionTabs(HWND hWnd, HDC hdc);
static void DrawPinnedClients(HDC hdc);
static void BuildPinnedClientRects();
static int HitTestPinnedClient(POINT point);
static bool HitTestPinnedOverflow(POINT point);
static void ShowPinnedOverflowMenu(HWND hWnd, bool selectOnly = false);
static bool RestoreArchivedClient(const std::wstring &clientName,
                                  std::wstring *statusMessage = nullptr);
static int HitTestSessionTab(HWND hWnd, POINT pt);
static int HitTestSessionClose(HWND hWnd, POINT pt);
static bool HitTestSessionOverflow(HWND hWnd, POINT pt);
static void StartMainDragCandidate(HWND hWnd, MainDragKind kind, int index,
                                   POINT point);
static void MovePinnedClientForDrag(int fromIndex, int toIndex);
static void BuildPinnedClientRects();
static void FinishPinnedDragPersistence(bool savePinnedOrder);
static bool UpdateMainDrag(HWND hWnd, POINT point, WPARAM keyState);
static bool FinishMainDrag(HWND hWnd, POINT point);
static void SelectSessionTab(int iTab, bool bBringToFront);
static void CloseSessionTab(int iTab);
static void ShowSessionOverflowMenu(HWND hWnd);
static bool RequestSessionClose(DWORD pid);
static bool RequestSessionShutdown(DWORD pid);
static void SwitchToLaunchModeForInput();
static void InvalidateSessionTabs(HWND hWnd);
static void EnsureConfigMenu(HWND hWnd);
static void RebuildConfigMenuForTheme(HWND hWnd);
static void SetButtonIcon(HWND hBtn, int iconResId, HICON &hStore);
static LRESULT CALLBACK ButtonHotSubclassProc(HWND hWnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam,
                                              UINT_PTR uIdSubclass,
                                              DWORD_PTR /*dwRefData*/);
static void PreloadThemeIcons(UINT dpi);
static HICON LoadIconResBestDownscale(HINSTANCE hInst, int groupIconResId,
                                      int cxDesired, int cyDesired);
static HBITMAP IconToMenuBitmap(HICON hIcon, int cx, int cy);
static void ClearMenuItemData();
static bool GetIconSizePx(HICON hIcon, int &w, int &h);

static void ApplyDpiScaling(HWND hWnd, UINT dpi, const RECT *suggestedRect) {
  if (dpi == 0)
    dpi = USER_DEFAULT_SCREEN_DPI;
  g_uiDpi = dpi;

  UpdateUiFont(hWnd, dpi);

  if (g_hValidationTooltip) {
    SendMessageW(g_hValidationTooltip, TTM_SETMAXTIPWIDTH, 0,
                 ScaleByDpi(400, dpi));
  }
  if (g_hMenuTip) {
    SendMessageW(g_hMenuTip, TTM_SETMAXTIPWIDTH, 0, ScaleByDpi(450, dpi));
  }

  if (suggestedRect) {
    SetWindowPos(hWnd, nullptr, suggestedRect->left, suggestedRect->top,
                 suggestedRect->right - suggestedRect->left,
                 suggestedRect->bottom - suggestedRect->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  SetButtonIcon(g_hBtnTmpProf, g_bThemeIsDark ? IDI_TEMPW : IDI_TEMPB,
                g_hIconBtnTemp);
  SetButtonIcon(g_hBtnConfig, g_bThemeIsDark ? IDI_CFGW : IDI_CFGB,
                g_hIconBtnConfig);

  PreloadThemeIcons(dpi);
  if (g_hConfigMenu) {
    RebuildConfigMenuForTheme(hWnd);
  }

  LayoutMainGui(hWnd);
  UpdateIconPreviewForSelection(true);
}

static void ResizeMainWindowForDpi(HWND hWnd, UINT dpi) {
  if (!hWnd)
    return;
  if (dpi == 0)
    dpi = USER_DEFAULT_SCREEN_DPI;

  const DWORD style = (DWORD)GetWindowLongPtrW(hWnd, GWL_STYLE);
  const DWORD exStyle = (DWORD)GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
  RECT wr{0, 0, ScaleByDpi(MAIN_GUI_WIDTH_DIP, dpi),
          ScaleByDpi(GetMainGuiHeightDip(), dpi)};
  const HMODULE user32 = GetModuleHandleW(L"user32.dll");
  auto adjustForDpi = user32
                          ? reinterpret_cast<BOOL(WINAPI *)(
                                LPRECT, DWORD, BOOL, DWORD, UINT)>(
                                GetProcAddress(user32,
                                               "AdjustWindowRectExForDpi"))
                          : nullptr;
  if (adjustForDpi) {
    adjustForDpi(&wr, style, FALSE, exStyle, dpi);
  } else {
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
  }

  SetWindowPos(hWnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  ApplyDpiScaling(hWnd, dpi, nullptr);
}

static void ResetAboutFonts(UINT dpi) {
  if (g_hAboutFont) {
    DeleteObject(g_hAboutFont);
    g_hAboutFont = nullptr;
  }
  if (g_hFontAboutSmall) {
    DeleteObject(g_hFontAboutSmall);
    g_hFontAboutSmall = nullptr;
  }

  g_hAboutFont = CreateUiFont(dpi);
  GetAboutSmallFont();
}

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

static bool IsSystemDarkMode() {
  DWORD iValue = 1;
  DWORD iSize = sizeof(iValue);
  if (SHGetValueW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"AppsUseLightTheme", nullptr, &iValue, &iSize) == ERROR_SUCCESS) {
    return iValue == 0;
  }
  return false;
}

static COLORREF BlendColor(COLORREF crA, COLORREF crB, int iAlpha);
static COLORREF TintColor(COLORREF cr, int iPercent);

static bool IsColorDark(COLORREF cr) {
  const int iR = GetRValue(cr);
  const int iG = GetGValue(cr);
  const int iB = GetBValue(cr);
  const int iLuma = (iR * 299 + iG * 587 + iB * 114) / 1000;
  return iLuma < 128;
}

static void BuildThemeColors(bool bDark, ThemeColors &aColors) {
  if (!bDark) {
    aColors.crWindow = GetSysColor(COLOR_BTNFACE);
    aColors.crWindowText = GetSysColor(COLOR_BTNTEXT);
    aColors.crCaption = aColors.crWindow;
    aColors.crCaptionText = aColors.crWindowText;
    // Light theme: window bg light gray, edit bg white, button face white
    // (system colors)
    aColors.crControl = GetSysColor(COLOR_WINDOW);
    aColors.crControlText = GetSysColor(COLOR_WINDOWTEXT);
    aColors.crButtonFace = GetSysColor(COLOR_WINDOW);
    aColors.crButtonText = GetSysColor(COLOR_BTNTEXT);
    aColors.crControlHot = GetSysColor(COLOR_3DLIGHT);
    aColors.crControlBorder = GetSysColor(COLOR_3DSHADOW);
    aColors.crAccent = GetSysColor(COLOR_HIGHLIGHT);
    aColors.crAccentText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    aColors.crMenu = GetSysColor(COLOR_MENU);
    aColors.crMenuText = GetSysColor(COLOR_MENUTEXT);
    aColors.crMenuSel = GetSysColor(COLOR_HIGHLIGHT);
    aColors.crMenuSelText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    aColors.crTip = GetSysColor(COLOR_INFOBK);
    aColors.crTipText = GetSysColor(COLOR_INFOTEXT);
    return;
  }

  aColors.crWindow = RGB(32, 32, 32);
  aColors.crWindowText = RGB(240, 240, 240);
  aColors.crCaption = aColors.crWindow;
  aColors.crCaptionText = aColors.crWindowText;
  aColors.crControl =
      aColors.crWindow; // keep UI/control backgrounds consistent
  aColors.crControlText = RGB(240, 240, 240);
  aColors.crButtonFace = aColors.crControl;
  aColors.crButtonText = aColors.crControlText;
  aColors.crControlHot = RGB(64, 64, 64);
  aColors.crControlBorder = RGB(90, 90, 90);
  aColors.crAccent = RGB(0, 120, 215);
  aColors.crAccentText = RGB(255, 255, 255);
  aColors.crMenu = RGB(40, 40, 40);
  aColors.crMenuText = RGB(240, 240, 240);
  aColors.crMenuSel = RGB(0, 120, 215);
  aColors.crMenuSelText = RGB(255, 255, 255);
  aColors.crTip = RGB(45, 45, 45);
  aColors.crTipText = RGB(240, 240, 240);
}

static ThemeColors BuildThemeColorsFromPalette(const CtThemePalette &aPalette) {
  ThemeColors aOut{};
  aOut.crWindow = aPalette.appBg;
  aOut.crWindowText = aPalette.buttonText;
  aOut.crCaption = aPalette.borderActive;
  aOut.crCaptionText = aPalette.appText;
  aOut.crControl = aPalette.windowBg;
  aOut.crControlText = aPalette.windowText;
  aOut.crButtonFace = aPalette.buttonBg;
  aOut.crButtonText = aPalette.buttonText;
  aOut.crControlHot = TintColor(aOut.crButtonFace, aPalette.dark ? 10 : -10);
  aOut.crControlBorder = aPalette.borderInactive;
  if (aOut.crControlBorder == aOut.crWindow) {
    aOut.crControlBorder = BlendColor(
        aOut.crWindow, aPalette.dark ? RGB(255, 255, 255) : RGB(0, 0, 0), 35);
  }
  aOut.crAccent = aPalette.selBg;
  aOut.crAccentText = aPalette.selText;
  aOut.crMenu = aPalette.menuBg;
  aOut.crMenuText = aPalette.menuText;
  aOut.crMenuSel = aPalette.selBg;
  aOut.crMenuSelText = aPalette.selText;
  aOut.crTip = aPalette.tipBg;
  aOut.crTipText = aPalette.tipText;
  return aOut;
}

static bool HasCustomThemeName(const std::wstring &aName) {
  for (const auto &vTheme : g_aCustomThemes) {
    if (_wcsicmp(vTheme.name.c_str(), aName.c_str()) == 0) {
      return true;
    }
  }
  return false;
}

static void LoadPresetThemesFromHeader() {
  for (int i = 0; i < g_ctThemeCatalogCount; i++) {
    const CtThemeDef &def = g_ctThemeCatalog[i];
    if (!def.name || !def.name[0])
      continue;
    const std::wstring vName = def.name;
    if (vName.rfind(L"Default", 0) == 0)
      continue;
    if (HasCustomThemeName(vName))
      continue;

    ThemeEntry entry{};
    entry.name = vName;
    entry.colors = BuildThemeColorsFromPalette(def.p);
    entry.dark = def.p.dark ? true : IsColorDark(entry.colors.crWindow);
    g_aCustomThemes.push_back(std::move(entry));
  }
}

static void LoadBakedThemesFromHeader() {
  g_aCustomThemes.clear();
  LoadPresetThemesFromHeader();
}

static void ClearThemeBrushes() {
  if (g_hbrThemeWindow) {
    DeleteObject(g_hbrThemeWindow);
    g_hbrThemeWindow = nullptr;
  }
  if (g_hbrThemeControl) {
    DeleteObject(g_hbrThemeControl);
    g_hbrThemeControl = nullptr;
  }
  if (g_hbrThemeButton) {
    DeleteObject(g_hbrThemeButton);
    g_hbrThemeButton = nullptr;
  }
  if (g_hbrThemeControlHot) {
    DeleteObject(g_hbrThemeControlHot);
    g_hbrThemeControlHot = nullptr;
  }
  if (g_hbrThemeMenu) {
    DeleteObject(g_hbrThemeMenu);
    g_hbrThemeMenu = nullptr;
  }
  if (g_hbrThemeMenuSel) {
    DeleteObject(g_hbrThemeMenuSel);
    g_hbrThemeMenuSel = nullptr;
  }
  if (g_hbrThemeBorder) {
    DeleteObject(g_hbrThemeBorder);
    g_hbrThemeBorder = nullptr;
  }
  if (g_hbrThemeIconPreview) {
    DeleteObject(g_hbrThemeIconPreview);
    g_hbrThemeIconPreview = nullptr;
  }
}

static void UpdateThemeBrushes() {
  ClearThemeBrushes();
  g_hbrThemeWindow = CreateSolidBrush(g_themeColors.crWindow);
  g_hbrThemeControl = CreateSolidBrush(g_themeColors.crControl);
  g_hbrThemeButton = CreateSolidBrush(g_themeColors.crButtonFace);
  g_hbrThemeControlHot = CreateSolidBrush(g_themeColors.crControlHot);
  g_hbrThemeMenu = CreateSolidBrush(g_themeColors.crMenu);
  g_hbrThemeMenuSel = CreateSolidBrush(g_themeColors.crMenuSel);
  g_hbrThemeBorder = CreateSolidBrush(g_themeColors.crControlBorder);
  const COLORREF crIconBg =
      TintColor(g_themeColors.crWindow, g_bThemeIsDark ? 12 : -8);
  g_hbrThemeIconPreview = CreateSolidBrush(crIconBg);
}

static COLORREF BlendColor(COLORREF crA, COLORREF crB, int iAlpha) {
  const int iInv = 100 - iAlpha;
  const int iR = (GetRValue(crA) * iInv + GetRValue(crB) * iAlpha) / 100;
  const int iG = (GetGValue(crA) * iInv + GetGValue(crB) * iAlpha) / 100;
  const int iB = (GetBValue(crA) * iInv + GetBValue(crB) * iAlpha) / 100;
  return RGB(iR, iG, iB);
}

static COLORREF TintColor(COLORREF cr, int iPercent) {
  if (iPercent == 0)
    return cr;
  const COLORREF crTarget = (iPercent > 0) ? RGB(255, 255, 255) : RGB(0, 0, 0);
  const int iAlpha = (iPercent < 0) ? -iPercent : iPercent;
  return BlendColor(cr, crTarget, iAlpha);
}

static COLORREF ScaleColor(COLORREF cr, int iNum, int iDen) {
  if (iDen <= 0)
    return cr;
  const int iR = GetRValue(cr) * iNum / iDen;
  const int iG = GetGValue(cr) * iNum / iDen;
  const int iB = GetBValue(cr) * iNum / iDen;
  return RGB(iR, iG, iB);
}

static COLORREF AdjustButtonFaceForContrast(COLORREF crFace, COLORREF crBg) {
  if (crFace == crBg) {
    return TintColor(crFace, 25); // lighten for contrast
  }
  return crFace;
}

static void AddRoundRectPath(Gdiplus::GraphicsPath &path,
                             const Gdiplus::RectF &rc, float radius) {
  const float fDiameter = radius * 2.0f;
  if (fDiameter <= 0.0f) {
    path.AddRectangle(rc);
    return;
  }
  Gdiplus::RectF rcArc(rc.X, rc.Y, fDiameter, fDiameter);
  path.AddArc(rcArc, 180.0f, 90.0f);
  rcArc.X = rc.X + rc.Width - fDiameter;
  path.AddArc(rcArc, 270.0f, 90.0f);
  rcArc.Y = rc.Y + rc.Height - fDiameter;
  path.AddArc(rcArc, 0.0f, 90.0f);
  rcArc.X = rc.X;
  path.AddArc(rcArc, 90.0f, 90.0f);
  path.CloseFigure();
}

static void DrawRoundedRect(HDC hdc, const RECT &rc, COLORREF crFill,
                            COLORREF crBorder, int iRadius) {
  if (!hdc)
    return;
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  const Gdiplus::RectF rf((Gdiplus::REAL)rc.left + 0.5f,
                          (Gdiplus::REAL)rc.top + 0.5f,
                          (Gdiplus::REAL)(rc.right - rc.left - 1),
                          (Gdiplus::REAL)(rc.bottom - rc.top - 1));
  Gdiplus::GraphicsPath path;
  AddRoundRectPath(path, rf, (Gdiplus::REAL)iRadius);
  Gdiplus::SolidBrush brush(Gdiplus::Color(
      255, GetRValue(crFill), GetGValue(crFill), GetBValue(crFill)));
  Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(crBorder), GetGValue(crBorder),
                                  GetBValue(crBorder)),
                   1.0f);
  g.FillPath(&brush, &path);
  g.DrawPath(&pen, &path);
}

static const wchar_t *GetSelectedBrowserDisplayName() {
  return GetBrowserDisplayName(g_selectedBrowser);
}

static void DrawBrowserGlyph(HDC hdc, const RECT &rc, COLORREF color) {
  const int stroke = max(1, ScaleByDpi(1, g_uiDpi));
  HPEN pen = CreatePen(PS_SOLID, stroke, color);
  HPEN oldPen = (HPEN)SelectObject(hdc, pen);
  HBRUSH oldBrush =
      (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
  const int bodyBottom = rc.bottom - ScaleByDpi(5, g_uiDpi);
  RoundRect(hdc, rc.left, rc.top, rc.right, bodyBottom,
            ScaleByDpi(2, g_uiDpi), ScaleByDpi(2, g_uiDpi));
  const int cx = (rc.left + rc.right) / 2;
  MoveToEx(hdc, cx, bodyBottom, nullptr);
  LineTo(hdc, cx, rc.bottom - ScaleByDpi(2, g_uiDpi));
  MoveToEx(hdc, cx - ScaleByDpi(4, g_uiDpi),
           rc.bottom - ScaleByDpi(2, g_uiDpi), nullptr);
  LineTo(hdc, cx + ScaleByDpi(5, g_uiDpi),
         rc.bottom - ScaleByDpi(2, g_uiDpi));
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
}

static void DrawMainSurface(HDC hdc) {
  if (!hdc || g_rcUtilityBar.right <= g_rcUtilityBar.left ||
      g_rcUtilityBar.bottom <= g_rcUtilityBar.top) {
    return;
  }

  const UINT dpi = g_uiDpi ? g_uiDpi : USER_DEFAULT_SCREEN_DPI;
  const COLORREF crBand = BlendColor(
      g_themeColors.crWindow, g_themeColors.crControl,
      g_bThemeIsDark ? 16 : 34);
  HBRUSH hBand = CreateSolidBrush(crBand);
  FillRect(hdc, &g_rcUtilityBar, hBand);
  DeleteObject(hBand);

  const COLORREF crLine =
      BlendColor(g_themeColors.crControlBorder, g_themeColors.crWindow, 46);
  HPEN hPen = CreatePen(PS_SOLID, 1, crLine);
  HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
  MoveToEx(hdc, g_rcUtilityBar.left, g_rcUtilityBar.top, nullptr);
  LineTo(hdc, g_rcUtilityBar.right, g_rcUtilityBar.top);
  SelectObject(hdc, hOldPen);
  DeleteObject(hPen);

  if (g_rcBrowserSelector.right <= g_rcBrowserSelector.left)
    return;

  const COLORREF selectorFill = BlendColor(
      crBand, g_themeColors.crControl,
      g_bHotBrowserSelector && g_bUiEnabled ? 46 : 24);
  const COLORREF selectorBorder =
      g_bHotBrowserSelector && g_bUiEnabled
          ? g_themeColors.crAccent
          : BlendColor(g_themeColors.crControlBorder, crBand, 34);
  DrawRoundedRect(hdc, g_rcBrowserSelector, selectorFill, selectorBorder,
                  ScaleByDpi(4, dpi));

  SetBkMode(hdc, TRANSPARENT);
  const int selectorCy =
      (g_rcBrowserSelector.top + g_rcBrowserSelector.bottom) / 2;
  RECT iconRect{g_rcBrowserSelector.left + ScaleByDpi(10, dpi),
                selectorCy - ScaleByDpi(9, dpi),
                g_rcBrowserSelector.left + ScaleByDpi(28, dpi),
                selectorCy + ScaleByDpi(9, dpi)};
  DrawBrowserGlyph(hdc, iconRect, g_themeColors.crWindowText);

  HFONT oldFont = (HFONT)SelectObject(
      hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
  RECT textRect{iconRect.right + ScaleByDpi(9, dpi),
                g_rcBrowserSelector.top,
                g_rcBrowserSelector.right - ScaleByDpi(26, dpi),
                g_rcBrowserSelector.bottom};
  SetTextColor(hdc, g_themeColors.crWindowText);
  DrawTextW(hdc, GetSelectedBrowserDisplayName(), -1, &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                DT_NOPREFIX);

  const int chevronX =
      g_rcBrowserSelector.right - ScaleByDpi(14, dpi);
  const int chevronY = selectorCy - ScaleByDpi(2, dpi);
  HPEN chevronPen = CreatePen(PS_SOLID, max(1, ScaleByDpi(1, dpi)),
                              g_themeColors.crWindowText);
  HPEN oldChevronPen = (HPEN)SelectObject(hdc, chevronPen);
  MoveToEx(hdc, chevronX - ScaleByDpi(4, dpi), chevronY, nullptr);
  LineTo(hdc, chevronX, chevronY + ScaleByDpi(4, dpi));
  LineTo(hdc, chevronX + ScaleByDpi(4, dpi), chevronY);
  SelectObject(hdc, oldChevronPen);
  DeleteObject(chevronPen);
  SelectObject(hdc, oldFont);
}

static void ApplyThemeToWindow(HWND hWnd) {
  if (!hWnd || !IsWindow(hWnd))
    return;

  BOOL bDark = g_bThemeIsDark ? TRUE : FALSE;
  DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark,
                        sizeof(bDark));

  const COLORREF crCaption = g_themeColors.crCaption;
  const COLORREF crText = g_themeColors.crCaptionText;
  const COLORREF crBorder = g_themeColors.crControlBorder;
  DwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &crCaption,
                        sizeof(crCaption));
  DwmSetWindowAttribute(hWnd, DWMWA_TEXT_COLOR, &crText, sizeof(crText));
  DwmSetWindowAttribute(hWnd, DWMWA_BORDER_COLOR, &crBorder, sizeof(crBorder));
}

static void PreloadThemeIcons(UINT dpi) {
  const int iPx = MulDiv(28, dpi, 96);

  if (g_hIconColorLight) {
    DestroyIcon(g_hIconColorLight);
    g_hIconColorLight = nullptr;
  }
  if (g_hIconColorDark) {
    DestroyIcon(g_hIconColorDark);
    g_hIconColorDark = nullptr;
  }
  if (g_hBmpColorLight) {
    DeleteObject(g_hBmpColorLight);
    g_hBmpColorLight = nullptr;
  }
  if (g_hBmpColorDark) {
    DeleteObject(g_hBmpColorDark);
    g_hBmpColorDark = nullptr;
  }

  g_hIconColorLight = LoadIconResBestDownscale(g_hInst, IDI_COLRB, iPx, iPx);
  g_hIconColorDark = LoadIconResBestDownscale(g_hInst, IDI_COLRW, iPx, iPx);

  if (g_hIconColorLight)
    g_hBmpColorLight = IconToMenuBitmap(g_hIconColorLight, iPx, iPx);
  if (g_hIconColorDark)
    g_hBmpColorDark = IconToMenuBitmap(g_hIconColorDark, iPx, iPx);
}

static HBITMAP GetThemeColorMenuBitmap() {
  return g_bThemeIsDark ? g_hBmpColorDark : g_hBmpColorLight;
}

static void UpdateTooltipColors() {
  COLORREF crTipBk = g_themeColors.crTip;
  COLORREF crTipText = g_themeColors.crTipText;
  const DWORD iCurrentPid = GetCurrentProcessId();
  auto vApplyColors = [&](HWND hTip) {
    if (!hTip || !IsWindow(hTip))
      return;
    DWORD iOwnerPid = 0;
    GetWindowThreadProcessId(hTip, &iOwnerPid);
    if (iOwnerPid != iCurrentPid)
      return;
    // Native visual styles otherwise override explicit tooltip palette colors.
    SetWindowTheme(hTip, L"", L"");
    SendMessageW(hTip, TTM_SETTIPBKCOLOR, crTipBk, 0);
    SendMessageW(hTip, TTM_SETTIPTEXTCOLOR, crTipText, 0);
  };
  vApplyColors(g_hValidationTooltip);
  vApplyColors(g_hMenuTip);
  vApplyColors(g_hBtnTmpProfTip);
  vApplyColors(g_hBtnConfigTip);
  vApplyColors(g_hBtnPinTip);
  vApplyColors(g_hIconPreviewTip);
}

static std::mutex g_themedPopupsMutex;
static std::vector<HWND> g_themedPopups;

static void RefreshThemeWindow(HWND hWnd, bool bUpdateWindowChrome) {
  if (!hWnd || !IsWindow(hWnd))
    return;
  // The walkthrough remains visible while the Themes dialog previews a
  // palette. Keep its non-client caption in sync with its already-live client
  // colors; other windows retain the established apply/cancel chrome behavior.
  if (bUpdateWindowChrome || hWnd == g_hGuideDialog)
    ApplyThemeToWindow(hWnd);
  if (hWnd == g_hGuideDialog) {
    for (const int id : {IDC_GUIDE_TOPICS, IDC_GUIDE_BODY}) {
      HWND control = GetDlgItem(hWnd, id);
      if (control) {
        SetWindowTheme(control,
                       g_bThemeIsDark ? L"DarkMode_Explorer" : L"Explorer",
                       nullptr);
      }
    }
  }
  RedrawWindow(hWnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void InvalidateThemeWindows(bool bUpdateWindowChrome) {
  RefreshThemeWindow(g_hGui, bUpdateWindowChrome);

  HWND hThemeDialog =
      g_hThemeCombo ? GetAncestor(g_hThemeCombo, GA_ROOT) : nullptr;
  if (hThemeDialog && hThemeDialog != g_hGui)
    RefreshThemeWindow(hThemeDialog, bUpdateWindowChrome);
  std::vector<HWND> popups;
  {
    std::lock_guard<std::mutex> lock(g_themedPopupsMutex);
    popups = g_themedPopups;
  }
  for (HWND window : popups)
    RefreshThemeWindow(window, bUpdateWindowChrome);
}

static void RebuildConfigMenuForTheme(HWND hWnd) {
  if (!g_hConfigMenu || !hWnd || !IsWindow(hWnd))
    return;

  HMENU hOldMenu = g_hConfigMenu;
  g_hConfigMenu = nullptr;
  g_hBrowserSubMenu = nullptr;
  DestroyMenu(hOldMenu);

  for (auto hBitmap : g_menuBitmaps) {
    if (hBitmap)
      DeleteObject(hBitmap);
  }
  g_menuBitmaps.clear();
  ClearMenuItemData();
  EnsureConfigMenu(hWnd);
}

static void ApplyTheme(int iMode, bool bPreview) {
  if (g_bThemeApplyInProgress)
    return;
  g_bThemeApplyInProgress = true;
  struct ThemeApplyReset {
    bool &flag;
    ~ThemeApplyReset() { flag = false; }
  } vReset{g_bThemeApplyInProgress};

  const int iNormalized = NormalizeThemeMode(iMode);

  if (!bPreview) {
    g_iThemeMode = iNormalized;
    SaveThemePreference();
  }

  iMode = iNormalized;
  bool bDark = false;
  if (iMode >= 3) {
    const int iCustom = iMode - 3;
    if (iCustom >= 0 && iCustom < (int)g_aCustomThemes.size()) {
      g_themeColors = g_aCustomThemes[iCustom].colors;
      bDark = g_aCustomThemes[iCustom].dark;
    } else {
      bDark = (iMode == 2) || (iMode == 0 && IsSystemDarkMode());
      BuildThemeColors(bDark, g_themeColors);
    }
  } else {
    bDark = (iMode == 2) || (iMode == 0 && IsSystemDarkMode());
    BuildThemeColors(bDark, g_themeColors);
  }
  g_bThemeIsDark = bDark;
  UpdateThemeBrushes();

  if (!bPreview) {
    UpdateTooltipColors();
    ProgressUI_SetTheme(g_themeColors.crWindow, g_themeColors.crWindowText,
                        g_themeColors.crAccent, g_themeColors.crControl, bDark);
    SetButtonIcon(g_hBtnTmpProf, bDark ? IDI_TEMPW : IDI_TEMPB,
                  g_hIconBtnTemp);
    SetButtonIcon(g_hBtnConfig, bDark ? IDI_CFGW : IDI_CFGB,
                  g_hIconBtnConfig);
  }

  InvalidateThemeWindows(!bPreview);

  if (!bPreview) {
    if (g_hThemeCombo && IsWindow(g_hThemeCombo)) {
      g_bThemeMenuRefreshPending = true;
    } else {
      RebuildConfigMenuForTheme(g_hGui);
    }
    UpdateIconPreviewForSelection(true);
  }
}

static HBRUSH HandleThemeCtlColor(UINT msg, HDC hdc, HWND hCtl) {
  if (!hdc)
    return (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);

  switch (msg) {
  case WM_CTLCOLORDLG:
    SetTextColor(hdc, g_themeColors.crWindowText);
    SetBkColor(hdc, g_themeColors.crWindow);
    return g_hbrThemeWindow ? g_hbrThemeWindow
                            : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
  case WM_CTLCOLORSTATIC:
    if (hCtl == g_hIconPreview) {
      const COLORREF crIconBg =
          TintColor(g_themeColors.crWindow, g_bThemeIsDark ? 12 : -8);
      SetTextColor(hdc, g_themeColors.crWindowText);
      SetBkColor(hdc, crIconBg);
      SetBkMode(hdc, OPAQUE);
      return g_hbrThemeIconPreview ? g_hbrThemeIconPreview
                                   : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
    }
    SetTextColor(
        hdc,
        hCtl && GetDlgCtrlID(hCtl) == IDC_STATIC_PROMPT
            ? BlendColor(g_themeColors.crWindowText, g_themeColors.crWindow,
                         g_bThemeIsDark ? 30 : 42)
            : g_themeColors.crWindowText);
    SetBkColor(hdc, g_themeColors.crWindow);
    SetBkMode(hdc, TRANSPARENT);
    return g_hbrThemeWindow ? g_hbrThemeWindow
                            : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
    SetTextColor(hdc, g_themeColors.crControlText);
    SetBkColor(hdc, g_themeColors.crControl);
    return g_hbrThemeControl ? g_hbrThemeControl
                             : (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
  case WM_CTLCOLORBTN:
    SetTextColor(hdc, g_themeColors.crButtonText);
    SetBkColor(hdc, g_themeColors.crButtonFace);
    return g_hbrThemeButton ? g_hbrThemeButton
                            : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
  default:
    break;
  }
  return g_hbrThemeWindow ? g_hbrThemeWindow
                          : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
}

static RECT GetComboLocalRect(HWND hCombo) {
  RECT rect{};
  if (!hCombo || !GetWindowRect(hCombo, &rect))
    return rect;
  OffsetRect(&rect, -rect.left, -rect.top);
  return rect;
}

static RECT GetClientComboIconRect(HWND hCombo) {
  RECT rect = GetComboLocalRect(hCombo);
  if (!hCombo || rect.right <= rect.left || rect.bottom <= rect.top)
    return RECT{};

  const UINT dpi = GetDpiForWindow(hCombo);
  const int inset = ScaleByDpi(4, dpi);
  const int tile = max(1, min(ScaleByDpi(26, dpi),
                             (rect.bottom - rect.top) - inset * 2));
  const int top = rect.top + max(0, (rect.bottom - rect.top - tile) / 2);
  return RECT{rect.left + inset, top, rect.left + inset + tile, top + tile};
}

static RECT GetComboDropZoneRect(HWND hCombo) {
  RECT rect = GetComboLocalRect(hCombo);
  if (!hCombo || rect.right <= rect.left || rect.bottom <= rect.top)
    return RECT{};

  const UINT dpi = GetDpiForWindow(hCombo);
  int width = ScaleByDpi(28, dpi);
  if (hCombo == g_hComboClient) {
    width = ScaleByDpi(CLIENT_SELECTOR_DROP_LANE_DIP, dpi);
  } else {
    COMBOBOXINFO comboInfo{sizeof(COMBOBOXINFO)};
    if (GetComboBoxInfo(hCombo, &comboInfo))
      width = max(width, comboInfo.rcButton.right - comboInfo.rcButton.left);
  }
  width = min(width, max(1, rect.right - rect.left));
  return RECT{rect.right - width, rect.top, rect.right, rect.bottom};
}

static void LayoutClientComboChildren(HWND hCombo) {
  if (!hCombo || hCombo != g_hComboClient || !g_hGui || !g_hClientEdit ||
      !g_hClientEditSurface)
    return;

  HFONT clientFont = g_hFontClient ? g_hFontClient : g_hFont;
  if (clientFont &&
      (HFONT)SendMessageW(g_hClientEdit, WM_GETFONT, 0, 0) != clientFont) {
    SendMessageW(g_hClientEdit, WM_SETFONT, (WPARAM)clientFont, TRUE);
  }

  RECT comboRect{};
  if (!GetWindowRect(hCombo, &comboRect))
    return;
  MapWindowPoints(nullptr, g_hGui, reinterpret_cast<POINT *>(&comboRect), 2);

  const UINT dpi = GetDpiForWindow(hCombo);
  const int iconLaneWidth =
      ScaleByDpi(CLIENT_SELECTOR_ICON_LANE_DIP, dpi);
  const int dropLaneWidth =
      ScaleByDpi(CLIENT_SELECTOR_DROP_LANE_DIP, dpi);
  RECT surfaceRect = comboRect;
  if (surfaceRect.right <= surfaceRect.left ||
      surfaceRect.bottom <= surfaceRect.top)
    return;

  int textHeight = ScaleByDpi(20, dpi);
  HDC editDc = GetDC(g_hClientEdit);
  if (editDc) {
    HFONT oldFont = (HFONT)SelectObject(
        editDc, clientFont ? clientFont : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW metrics{};
    if (GetTextMetricsW(editDc, &metrics)) {
      textHeight = max(textHeight, metrics.tmHeight + metrics.tmExternalLeading +
                                       ScaleByDpi(1, dpi));
    }
    SelectObject(editDc, oldFont);
    ReleaseDC(g_hClientEdit, editDc);
  }
  textHeight = min(textHeight, surfaceRect.bottom - surfaceRect.top);

  const int textPadLeft = ScaleByDpi(5, dpi);
  const int textPadRight = ScaleByDpi(4, dpi);
  RECT editRect{surfaceRect.left + iconLaneWidth + textPadLeft,
                 comboRect.top + (comboRect.bottom - comboRect.top - textHeight) /
                                     2,
                 surfaceRect.right - dropLaneWidth - textPadRight, 0};
  editRect.bottom = editRect.top + textHeight;

  auto moveIfNeeded = [&](HWND hWnd, const RECT &target) {
    RECT current{};
    if (!GetWindowRect(hWnd, &current))
      return;
    MapWindowPoints(nullptr, g_hGui, reinterpret_cast<POINT *>(&current), 2);
    if (current.left != target.left || current.top != target.top ||
        current.right != target.right || current.bottom != target.bottom) {
      MoveWindow(hWnd, target.left, target.top, target.right - target.left,
                 target.bottom - target.top, TRUE);
    }
  };
  moveIfNeeded(g_hClientEditSurface, surfaceRect);
  moveIfNeeded(g_hClientEdit, editRect);

  SendMessageW(g_hClientEdit, EM_SETMARGINS,
               EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
}

static void HideClientComboChrome(HWND hCombo) {
  if (!hCombo || hCombo != g_hComboClient)
    return;

  // The combo remains alive as the native popup-list host, but its collapsed
  // face must never paint over the custom selector surface.
  HRGN hiddenRegion = CreateRectRgn(0, 0, 0, 0);
  if (!hiddenRegion)
    return;
  if (!SetWindowRgn(hCombo, hiddenRegion, FALSE))
    DeleteObject(hiddenRegion);
}

static void StackClientSelectorWindows() {
  if (g_hComboClient) {
    SetWindowPos(g_hComboClient, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  if (g_hClientEditSurface) {
    SetWindowPos(g_hClientEditSurface, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  if (g_hClientEdit) {
    SetWindowPos(g_hClientEdit, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

static void QueueClientSelectorRestack() {
  if (!g_hGui || g_bClientSelectorRestackPending)
    return;

  g_bClientSelectorRestackPending = true;
  if (!PostMessageW(g_hGui, WM_APP_RESTACK_CLIENT_SELECTOR, 0, 0))
    g_bClientSelectorRestackPending = false;
}

static void DrawClientIconTile(HWND hOwner, HDC hdc) {
  if (!hOwner || !hdc)
    return;

  const UINT dpi = GetDpiForWindow(hOwner);
  const RECT tileRect = GetClientComboIconRect(hOwner);
  if (tileRect.right <= tileRect.left || tileRect.bottom <= tileRect.top)
    return;

  const COLORREF tileFill = BlendColor(
      g_themeColors.crControl, g_themeColors.crAccent,
      g_bThemeIsDark ? 13 : 8);
  const COLORREF tileBorder = BlendColor(
      g_themeColors.crControlBorder, g_themeColors.crControl, 35);
  DrawRoundedRect(hdc, tileRect, tileFill, tileBorder, ScaleByDpi(4, dpi));

  if (!g_hIconPreviewHandle)
    return;

  const int tileW = tileRect.right - tileRect.left;
  const int tileH = tileRect.bottom - tileRect.top;
  const int iconPx = max(8, min(ScaleByDpi(20, dpi),
                                min(tileW, tileH) - ScaleByDpi(4, dpi)));
  const int iconX = tileRect.left + (tileW - iconPx) / 2;
  const int iconY = tileRect.top + (tileH - iconPx) / 2;
  DrawIconEx(hdc, iconX, iconY, g_hIconPreviewHandle, iconPx, iconPx, 0,
             nullptr, DI_NORMAL);
}

static void DrawModernComboChevron(HDC hdc, const RECT &rect,
                                   COLORREF color, UINT dpi) {
  if (!hdc || rect.right <= rect.left || rect.bottom <= rect.top)
    return;

  const Gdiplus::REAL scale = (Gdiplus::REAL)dpi / 96.0f;
  const Gdiplus::REAL centerX = (rect.left + rect.right) / 2.0f;
  const Gdiplus::REAL centerY = (rect.top + rect.bottom) / 2.0f;
  const Gdiplus::REAL halfWidth = 4.75f * scale;
  const Gdiplus::REAL rise = 2.75f * scale;
  const Gdiplus::PointF points[] = {
      {centerX - halfWidth, centerY - rise},
      {centerX, centerY + rise},
      {centerX + halfWidth, centerY - rise}};

  Gdiplus::Graphics graphics(hdc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                                  GetBValue(color)),
                   max(1.6f, 1.6f * scale));
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  graphics.DrawLines(&pen, points, 3);
}

static bool IsClientSelectorFocused() {
  const HWND focus = GetFocus();
  return focus &&
         (focus == g_hClientEdit || focus == g_hComboClient ||
          (g_hComboClient && IsChild(g_hComboClient, focus)));
}

static void DrawThemedComboFrame(HWND hCombo) {
  if (!hCombo)
    return;
  HDC hdc = GetWindowDC(hCombo);
  if (!hdc)
    return;

  const RECT rcWnd = GetComboLocalRect(hCombo);

  const bool bDisabled = !IsWindowEnabled(hCombo);
  const bool bFocused = hCombo == g_hComboClient
                            ? IsClientSelectorFocused()
                            : ((GetFocus() == hCombo) ||
                               IsChild(hCombo, GetFocus()));
  const COLORREF crBorder =
      bDisabled
          ? BlendColor(g_themeColors.crControlBorder, g_themeColors.crWindow,
                       50)
          : (bFocused ? g_themeColors.crAccent : g_themeColors.crControlBorder);

  const bool bDropped = SendMessageW(hCombo, CB_GETDROPPEDSTATE, 0, 0) != 0;
  const COLORREF crDropFill =
      bDropped ? g_themeColors.crControlHot : g_themeColors.crControl;
  const COLORREF crChevron =
      bDisabled ? BlendColor(g_themeColors.crControlText,
                             g_themeColors.crWindowText, 50)
                : g_themeColors.crControlText;
  RECT dropRect = GetComboDropZoneRect(hCombo);
  InflateRect(&dropRect, -1, -1);
  HBRUSH dropBrush = CreateSolidBrush(crDropFill);
  FillRect(hdc, &dropRect, dropBrush);
  DeleteObject(dropBrush);

  DrawModernComboChevron(hdc, dropRect, crChevron, GetDpiForWindow(hCombo));

  const int cornerMask = max(2, ScaleByDpi(2, GetDpiForWindow(hCombo)));
  HBRUSH windowBrush = CreateSolidBrush(g_themeColors.crWindow);
  RECT corners[] = {{rcWnd.left, rcWnd.top, rcWnd.left + cornerMask,
                     rcWnd.top + cornerMask},
                    {rcWnd.right - cornerMask, rcWnd.top, rcWnd.right,
                     rcWnd.top + cornerMask},
                    {rcWnd.left, rcWnd.bottom - cornerMask,
                     rcWnd.left + cornerMask, rcWnd.bottom},
                    {rcWnd.right - cornerMask, rcWnd.bottom - cornerMask,
                     rcWnd.right, rcWnd.bottom}};
  for (RECT &corner : corners)
    FillRect(hdc, &corner, windowBrush);
  DeleteObject(windowBrush);

  Gdiplus::Graphics graphics(hdc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  const Gdiplus::RectF frameRect(
      (Gdiplus::REAL)rcWnd.left + 0.6f, (Gdiplus::REAL)rcWnd.top + 0.6f,
      (Gdiplus::REAL)(rcWnd.right - rcWnd.left) - 1.2f,
      (Gdiplus::REAL)(rcWnd.bottom - rcWnd.top) - 1.2f);
  Gdiplus::GraphicsPath framePath;
  AddRoundRectPath(framePath, frameRect,
                   (Gdiplus::REAL)ScaleByDpi(4, GetDpiForWindow(hCombo)));
  Gdiplus::Pen framePen(
      Gdiplus::Color(255, GetRValue(crBorder), GetGValue(crBorder),
                     GetBValue(crBorder)),
      bFocused ? 1.7f : 1.0f);
  graphics.DrawPath(&framePen, &framePath);

  ReleaseDC(hCombo, hdc);
}

static LRESULT CALLBACK ClientEditSubclassProc(HWND hWnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam,
                                                UINT_PTR uIdSubclass,
                                                DWORD_PTR /*dwRefData*/) {
  switch (msg) {
  case WM_NCPAINT:
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (hWnd == g_hClientEdit && g_hComboClient) {
      const bool dropped =
          SendMessageW(g_hComboClient, CB_GETDROPPEDSTATE, 0, 0) != 0;
      if (wParam == VK_F4 ||
          (wParam == VK_DOWN && (GetKeyState(VK_MENU) & 0x8000))) {
        SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, !dropped, 0);
        return 0;
      }
      if (wParam == VK_DOWN || wParam == VK_UP) {
        if (!dropped)
          SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, TRUE, 0);
        SendMessageW(g_hComboClient, WM_KEYDOWN, wParam, lParam);
        return 0;
      }
      if (wParam == VK_RETURN) {
        if (dropped) {
          SendMessageW(g_hComboClient, WM_KEYDOWN, wParam, lParam);
        } else if (g_hGui) {
          PostMessageW(g_hGui, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                       (LPARAM)g_hBtnGo);
        }
        return 0;
      }
      if (wParam == VK_ESCAPE && dropped) {
        SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, FALSE, 0);
        return 0;
      }
    }
    break;
  case WM_NCDESTROY:
    if (g_hClientEdit == hWnd)
      g_hClientEdit = nullptr;
    RemoveWindowSubclass(hWnd, ClientEditSubclassProc, uIdSubclass);
    break;
  case WM_SETFOCUS:
  case WM_KILLFOCUS: {
    const LRESULT result = DefSubclassProc(hWnd, msg, wParam, lParam);
    if (g_hComboClient)
      RedrawWindow(g_hComboClient, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_FRAME);
    if (g_hBtnClientIcon)
      InvalidateRect(g_hBtnClientIcon, nullptr, TRUE);
    if (g_hBtnClientDrop)
      InvalidateRect(g_hBtnClientDrop, nullptr, TRUE);
    if (g_hClientEditSurface)
      InvalidateRect(g_hClientEditSurface, nullptr, TRUE);
    return result;
  }
  default:
    break;
  }
  return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ComboThemeSubclassProc(HWND hWnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam,
                                               UINT_PTR uIdSubclass,
                                               DWORD_PTR /*dwRefData*/) {
  switch (msg) {
  case WM_NCDESTROY:
    RemoveWindowSubclass(hWnd, ComboThemeSubclassProc, uIdSubclass);
    break;
  case WM_PAINT:
  case WM_NCPAINT: {
    LRESULT lRes = DefSubclassProc(hWnd, msg, wParam, lParam);
    LayoutClientComboChildren(hWnd);
    if (hWnd != g_hComboClient)
      DrawThemedComboFrame(hWnd);
    return lRes;
  }
  case WM_SIZE:
  case WM_WINDOWPOSCHANGED:
  case WM_DPICHANGED_AFTERPARENT: {
    LRESULT lRes = DefSubclassProc(hWnd, msg, wParam, lParam);
    LayoutClientComboChildren(hWnd);
    InvalidateRect(hWnd, nullptr, TRUE);
    return lRes;
  }
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
  case WM_ENABLE: {
    LRESULT lRes = DefSubclassProc(hWnd, msg, wParam, lParam);
    InvalidateRect(hWnd, nullptr, TRUE);
    if (hWnd == g_hComboClient && g_hBtnClientDrop)
      InvalidateRect(g_hBtnClientDrop, nullptr, TRUE);
    if (hWnd == g_hComboClient && g_hBtnClientIcon)
      InvalidateRect(g_hBtnClientIcon, nullptr, TRUE);
    if (hWnd == g_hComboClient && g_hClientEditSurface)
      InvalidateRect(g_hClientEditSurface, nullptr, TRUE);
    return lRes;
  }
  case CB_SHOWDROPDOWN: {
    LRESULT lRes = DefSubclassProc(hWnd, msg, wParam, lParam);
    if (hWnd == g_hComboClient) {
      HideClientComboChrome(hWnd);
      StackClientSelectorWindows();
      QueueClientSelectorRestack();
      if (g_hClientEditSurface) {
        RedrawWindow(g_hClientEditSurface, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW);
      }
    }
    return lRes;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, g_themeColors.crControlText);
    SetBkColor(hdc, g_themeColors.crControl);
    return (LRESULT)(g_hbrThemeControl
                         ? g_hbrThemeControl
                         : (HBRUSH)GetSysColorBrush(COLOR_WINDOW));
  }
  default:
    break;
  }
  return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void ApplyComboTheme(HWND hCombo) {
  if (!hCombo)
    return;
  SetWindowSubclass(hCombo, ComboThemeSubclassProc, 1, 0);
  if (hCombo == g_hComboClient) {
    SendMessageW(hCombo, WM_SETFONT,
                 (WPARAM)(g_hFontClient ? g_hFontClient : g_hFont), TRUE);
    LayoutClientComboChildren(hCombo);
  }
  InvalidateRect(hCombo, nullptr, TRUE);
}

static LRESULT CALLBACK ButtonHotSubclassProc(HWND hWnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam,
                                              UINT_PTR uIdSubclass,
                                              DWORD_PTR /*dwRefData*/) {
  switch (msg) {
  case WM_NCDESTROY:
    if (g_hHotButton == hWnd)
      g_hHotButton = nullptr;
    RemoveWindowSubclass(hWnd, ButtonHotSubclassProc, uIdSubclass);
    break;
  case WM_MOUSEMOVE: {
    if (g_hHotButton != hWnd) {
      HWND hPrev = g_hHotButton;
      g_hHotButton = hWnd;
      if (hPrev)
        InvalidateRect(hPrev, nullptr, TRUE);
      InvalidateRect(hWnd, nullptr, TRUE);
    }
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hWnd, 0};
    TrackMouseEvent(&tme);
    break;
  }
  case WM_MOUSELEAVE:
    if (g_hHotButton == hWnd) {
      g_hHotButton = nullptr;
      InvalidateRect(hWnd, nullptr, TRUE);
    }
    break;
  case WM_SETCURSOR:
    if (hWnd == g_hBtnClientIcon) {
      SetCursor(LoadCursorW(nullptr, IDC_HAND));
      return TRUE;
    }
    break;
  default:
    break;
  }
  return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static UINT GetOwnerDrawDpi(HWND sourceWindow) {
  return owner_draw_ui::GetDpi(sourceWindow, g_hGui);
}

static HFONT GetOwnerDrawFont(HWND sourceWindow,
                              bool preserveLauncherStrong = false) {
  return owner_draw_ui::GetFont(sourceWindow, g_hGui,
                                preserveLauncherStrong, g_hFontStrong,
                                g_hFont);
}

static void DrawThemedMenuItem(const DRAWITEMSTRUCT &ds,
                               HWND fontAndDpiSource = nullptr) {
  auto *vData = reinterpret_cast<MenuItemData *>(ds.itemData);
  if (!vData)
    return;

  const bool bSelected = (ds.itemState & ODS_SELECTED) != 0;
  const bool bDisabled = (ds.itemState & ODS_DISABLED) != 0;
  const bool bChecked = (ds.itemState & ODS_CHECKED) != 0;

  if (vData->separator) {
    HBRUSH hbrBg =
        g_hbrThemeMenu ? g_hbrThemeMenu : (HBRUSH)GetSysColorBrush(COLOR_MENU);
    FillRect(ds.hDC, &ds.rcItem, hbrBg);
    const int iCy = (ds.rcItem.top + ds.rcItem.bottom) / 2;
    HPEN hPen = CreatePen(PS_SOLID, 1, g_themeColors.crControlBorder);
    HGDIOBJ hOldPen = SelectObject(ds.hDC, hPen);
    MoveToEx(ds.hDC, ds.rcItem.left + 4, iCy, nullptr);
    LineTo(ds.hDC, ds.rcItem.right - 4, iCy);
    SelectObject(ds.hDC, hOldPen);
    DeleteObject(hPen);
    return;
  }

  HBRUSH hbrBg = bSelected ? g_hbrThemeMenuSel : g_hbrThemeMenu;
  FillRect(ds.hDC, &ds.rcItem,
           hbrBg ? hbrBg : (HBRUSH)GetSysColorBrush(COLOR_MENU));

  const UINT dpi = GetOwnerDrawDpi(fontAndDpiSource);
  const int iPad = MulDiv(6, dpi, 96);
  const int iIconPad = MulDiv(6, dpi, 96);
  const int iIconPx = fontAndDpiSource
                          ? MulDiv(20, dpi, 96)
                          : ((g_iMenuIconPx > 0) ? g_iMenuIconPx
                                                : MulDiv(20, dpi, 96));

  RECT rcIcon = ds.rcItem;
  rcIcon.left += iPad;
  rcIcon.right = rcIcon.left + iIconPx;
  rcIcon.top += ((ds.rcItem.bottom - ds.rcItem.top) - iIconPx) / 2;
  rcIcon.bottom = rcIcon.top + iIconPx;

  if (vData->broomGlyph) {
    // A vector broom uses the current menu text color, not the dark raster
    // shared by the old light/dark resource IDs. Also adapts on selection.
    const COLORREF color = bSelected ? g_themeColors.crMenuSelText
                                     : g_themeColors.crMenuText;
    Gdiplus::Graphics graphics(ds.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float scale = iIconPx / 20.0f;
    graphics.TranslateTransform(static_cast<float>(rcIcon.left),
                                static_cast<float>(rcIcon.top));
    graphics.ScaleTransform(scale, scale);
    Gdiplus::Pen pen(Gdiplus::Color(bDisabled ? 130 : 255,
        GetRValue(color), GetGValue(color), GetBValue(color)), 1.8f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&pen, 17.0f, 2.0f, 10.0f, 10.0f);
    const Gdiplus::PointF head[] = {{8, 9}, {13, 13}, {9, 18}, {2, 13}, {8, 9}};
    graphics.DrawLines(&pen, head, 5);
    graphics.DrawLine(&pen, 5.0f, 15.0f, 9.0f, 11.0f);
    graphics.DrawLine(&pen, 7.0f, 17.0f, 11.0f, 13.0f);
  } else if (vData->hIcon) {
    if (bDisabled) {
      DrawStateW(ds.hDC, nullptr, nullptr, (LPARAM)vData->hIcon, 0, rcIcon.left,
                 rcIcon.top, iIconPx, iIconPx, DST_ICON | DSS_DISABLED);
    } else {
      DrawIconEx(ds.hDC, rcIcon.left, rcIcon.top, vData->hIcon, iIconPx,
                 iIconPx, 0, nullptr, DI_NORMAL);
    }
  } else if (bChecked) {
    // Draw Custom Checkmark
    bool bDark = g_bThemeIsDark;
    COLORREF crCheck = bDisabled ? g_themeColors.crControlBorder
                                 : (bSelected ? g_themeColors.crMenuSelText
                                              : g_themeColors.crMenuText);

    // Simple checkmark drawing manually or use DrawFrameControl
    // DrawFrameControl uses system colors which might fight our theme.
    // Let's draw a Polyline checkmark.
    // Coordinates relative to rcIcon
    POINT pts[3];
    int cx = rcIcon.left + iIconPx / 2;
    int cy = rcIcon.top + iIconPx / 2;
    int sz = iIconPx / 3;
    pts[0] = {cx - sz, cy};
    pts[1] = {cx - sz / 3, cy + sz};
    pts[2] = {cx + sz, cy - sz};

    HPEN hPen = CreatePen(PS_SOLID, 2, crCheck);
    HGDIOBJ hOld = SelectObject(ds.hDC, hPen);
    Polyline(ds.hDC, pts, 3);
    SelectObject(ds.hDC, hOld);
    DeleteObject(hPen);
  }

  RECT rcText = ds.rcItem;
  rcText.left = rcIcon.right + iIconPad;
  rcText.right -= iIconPx; // Reserve space for chevron/right align

  if (vData->newDot) {
    const int dotSize = max(MulDiv(7, dpi, 96), 5);
    const int dotX = ds.rcItem.right - iPad - dotSize;
    const int dotY = ds.rcItem.top +
                     (ds.rcItem.bottom - ds.rcItem.top - dotSize) / 2;
    const COLORREF dotColor =
        bSelected ? g_themeColors.crMenuSelText : g_themeColors.crAccent;
    Gdiplus::Graphics graphics(ds.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        bDisabled ? 120 : 255, GetRValue(dotColor), GetGValue(dotColor),
        GetBValue(dotColor)));
    graphics.FillEllipse(&brush, dotX, dotY, dotSize, dotSize);
    rcText.right = dotX - iPad;
  }

  const COLORREF crText =
      bDisabled ? BlendColor(g_themeColors.crMenuText, g_themeColors.crMenu, 60)
                : (bSelected ? g_themeColors.crMenuSelText
                             : g_themeColors.crMenuText);
  HFONT hOldFont =
      (HFONT)SelectObject(ds.hDC, GetOwnerDrawFont(fontAndDpiSource));
  SetBkMode(ds.hDC, TRANSPARENT);
  SetTextColor(ds.hDC, crText);
  DrawTextW(ds.hDC, vData->text.c_str(), -1, &rcText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(ds.hDC, hOldFont);
}

static void MeasureThemedMenuItem(MEASUREITEMSTRUCT &ms,
                                  HWND fontAndDpiSource = nullptr) {
  auto *vData = reinterpret_cast<MenuItemData *>(ms.itemData);
  if (!vData)
    return;

  const UINT dpi = GetOwnerDrawDpi(fontAndDpiSource);
  const int iIconPx = fontAndDpiSource
                          ? MulDiv(16, dpi, 96)
                          : ((g_iMenuIconPx > 0) ? g_iMenuIconPx
                                                : MulDiv(16, dpi, 96));

  if (vData->separator) {
    ms.itemHeight = MulDiv(6, dpi, 96); // Thinner separator
    ms.itemWidth = MulDiv(20, dpi, 96);
    return;
  }

  const auto metrics = owner_draw_ui::MeasureMenuItem(
      vData->text, GetOwnerDrawFont(fontAndDpiSource), dpi, iIconPx,
      g_iMenuMinWidth);
  ms.itemHeight = metrics.height;
  ms.itemWidth = metrics.width;
}

static LRESULT CALLBACK EditMenuOwnerSubclass(HWND owner, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR editHandle);

static void ShowEditContextMenu(HWND edit, LPARAM location) {
  if (!IsWindowEnabled(edit))
    return;
  const LONG_PTR style = GetWindowLongPtrW(edit, GWL_STYLE);
  const bool readOnly = (style & ES_READONLY) != 0;
  const bool password = (style & ES_PASSWORD) != 0;
  DWORD start = 0, end = 0;
  SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
               reinterpret_cast<LPARAM>(&end));
  const bool selected = start != end;
  const bool hasText = GetWindowTextLengthW(edit) > 0;
  constexpr UINT undo = 1, cut = 2, copy = 3, paste = 4, clear = 5, all = 6;
  std::vector<MenuItemData> items;
  items.reserve(8); // Owner-draw pointers must remain stable during tracking.
  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;
  MENUINFO info{sizeof(info)};
  info.fMask = MIM_BACKGROUND | MIM_STYLE;
  info.hbrBack = g_hbrThemeMenu;
  info.dwStyle = MNS_NOCHECK;
  SetMenuInfo(menu, &info);
  const auto append = [&](UINT command, const wchar_t *text, bool enabled,
                          bool separator = false) {
    items.push_back({text, nullptr, separator});
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_DATA | MIIM_STRING;
    item.wID = command;
    item.fType = MFT_OWNERDRAW | (separator ? MFT_SEPARATOR : 0);
    item.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(&items.back());
    item.dwTypeData = const_cast<wchar_t *>(text);
    InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &item);
  };
  if (!readOnly) {
    append(undo, L"Undo", SendMessageW(edit, EM_CANUNDO, 0, 0) != 0);
    append(0, L"", true, true);
    append(cut, L"Cut", selected && !password);
  }
  append(copy, L"Copy", selected && !password);
  if (!readOnly) {
    append(paste, L"Paste", IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                           IsClipboardFormatAvailable(CF_TEXT));
    append(clear, L"Delete", selected);
  }
  append(0, L"", true, true);
  append(all, L"Select All", hasText);

  POINT point{static_cast<short>(LOWORD(location)),
              static_cast<short>(HIWORD(location))};
  if (location == static_cast<LPARAM>(-1)) {
    RECT rect{};
    GetClientRect(edit, &rect);
    if (GetFocus() != edit || !GetCaretPos(&point) || !PtInRect(&rect, point))
      point = {rect.left + ScaleByDpi(8, GetDpiForWindow(edit)), rect.top + rect.bottom / 2};
    ClientToScreen(edit, &point);
  }
  SetFocus(edit);
  HWND owner = GetAncestor(edit, GA_ROOT);
  constexpr UINT_PTR menuSubclassId = 0xED17;
  if (!SetWindowSubclass(owner, EditMenuOwnerSubclass, menuSubclassId,
                         reinterpret_cast<DWORD_PTR>(edit))) {
    DestroyMenu(menu);
    return;
  }
  SetForegroundWindow(owner);
  const UINT command = TrackPopupMenuEx(menu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
      point.x, point.y, owner, nullptr);
  RemoveWindowSubclass(owner, EditMenuOwnerSubclass, menuSubclassId);
  PostMessageW(owner, WM_NULL, 0, 0);
  const UINT state = command ? GetMenuState(menu, command, MF_BYCOMMAND) : static_cast<UINT>(-1);
  DestroyMenu(menu);
  if (!IsWindow(edit) || state == static_cast<UINT>(-1) ||
      (state & (MF_DISABLED | MF_GRAYED)))
    return;
  // Native edit messages preserve clipboard behavior, undo and EN_CHANGE.
  switch (command) {
  case undo: SendMessageW(edit, WM_UNDO, 0, 0); break;
  case cut: SendMessageW(edit, WM_CUT, 0, 0); break;
  case copy: SendMessageW(edit, WM_COPY, 0, 0); break;
  case paste: SendMessageW(edit, WM_PASTE, 0, 0); break;
  case clear: SendMessageW(edit, WM_CLEAR, 0, 0); break;
  case all: SendMessageW(edit, EM_SETSEL, 0, -1); break;
  }
}

static LRESULT CALLBACK EditMenuOwnerSubclass(HWND owner, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR editHandle) {
  if (message == WM_MEASUREITEM) {
    auto *measure = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
    if (measure && measure->CtlType == ODT_MENU) {
      HWND edit = reinterpret_cast<HWND>(editHandle);
      MeasureThemedMenuItem(*measure, edit);
      measure->itemWidth = ScaleByDpi(180, GetDpiForWindow(reinterpret_cast<HWND>(editHandle)));
      return TRUE;
    }
  }
  if (message == WM_DRAWITEM) {
    const auto *draw = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
    if (draw && draw->CtlType == ODT_MENU) {
      DrawThemedMenuItem(*draw, reinterpret_cast<HWND>(editHandle));
      return TRUE;
    }
  }
  if (message == WM_NCDESTROY)
    RemoveWindowSubclass(owner, EditMenuOwnerSubclass, id);
  return DefSubclassProc(owner, message, wParam, lParam);
}

static LRESULT CALLBACK EditContextMenuSubclass(HWND edit, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
  if (message == WM_CONTEXTMENU) {
    ShowEditContextMenu(edit, lParam);
    return 0;
  }
  if (message == WM_NCDESTROY)
    RemoveWindowSubclass(edit, EditContextMenuSubclass, id);
  return DefSubclassProc(edit, message, wParam, lParam);
}

static void ApplyEditContextMenuTheme(HWND edit) {
  if (edit)
    SetWindowSubclass(edit, EditContextMenuSubclass, 1, 0);
}

static void DrawPushpinGlyph(HDC hdc, const RECT &rc, COLORREF color,
                             bool filled) {
  if (!hdc)
    return;

  const float cx = (rc.left + rc.right) / 2.0f;
  const float cy = (rc.top + rc.bottom) / 2.0f;
  const float scale = max(0.8f, (float)g_uiDpi / USER_DEFAULT_SCREEN_DPI);
  const Gdiplus::PointF points[] = {
      {-5.0f * scale, -8.0f * scale},
      {5.0f * scale, -8.0f * scale},
      {5.0f * scale, -4.0f * scale},
      {3.0f * scale, -3.0f * scale},
      {3.0f * scale, 2.0f * scale},
      {6.0f * scale, 4.0f * scale},
      {6.0f * scale, 6.0f * scale},
      {-6.0f * scale, 6.0f * scale},
      {-6.0f * scale, 4.0f * scale},
      {-3.0f * scale, 2.0f * scale},
      {-3.0f * scale, -3.0f * scale},
      {-5.0f * scale, -4.0f * scale},
  };

  const Gdiplus::Color pinColor(255, GetRValue(color), GetGValue(color),
                                GetBValue(color));
  Gdiplus::Graphics graphics(hdc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::Matrix transform;
  transform.Translate(cx, cy - 1.0f * scale);
  transform.Rotate(38.0f);
  graphics.SetTransform(&transform);
  if (filled) {
    Gdiplus::SolidBrush brush(pinColor);
    graphics.FillPolygon(&brush, points, (INT)std::size(points));
  }
  Gdiplus::Pen pen(pinColor, (filled ? 1.2f : 1.7f) * scale);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  graphics.DrawPolygon(&pen, points, (INT)std::size(points));
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  graphics.DrawLine(&pen, 0.0f, 6.0f * scale, 0.0f, 12.0f * scale);
}

static void DrawClientDropButton(HDC hdc, const RECT &rect, UINT itemState,
                                 HWND hButton) {
  if (!hdc || !hButton)
    return;

  const UINT dpi = GetDpiForWindow(hButton);
  const bool disabled = (itemState & ODS_DISABLED) != 0;
  const bool pressed = (itemState & ODS_SELECTED) != 0;
  const bool hot = (itemState & ODS_HOTLIGHT) != 0 || hButton == g_hHotButton;
  const bool dropped = g_hComboClient &&
                       SendMessageW(g_hComboClient, CB_GETDROPPEDSTATE, 0, 0);
  const bool focused = IsClientSelectorFocused();

  COLORREF fill = (hot || pressed || dropped) ? g_themeColors.crControlHot
                                               : g_themeColors.crControl;
  COLORREF border = focused ? g_themeColors.crAccent
                            : g_themeColors.crControlBorder;
  COLORREF chevron = g_themeColors.crControlText;
  if (disabled) {
    fill = BlendColor(fill, g_themeColors.crWindow, 45);
    border = BlendColor(border, g_themeColors.crWindow, 50);
    chevron = BlendColor(chevron, g_themeColors.crWindowText, 55);
  }

  HBRUSH fillBrush = CreateSolidBrush(fill);
  FillRect(hdc, &rect, fillBrush);
  DeleteObject(fillBrush);

  const int radius = ScaleByDpi(4, dpi);
  const int mask = max(2, radius);
  HBRUSH windowBrush = CreateSolidBrush(g_themeColors.crWindow);
  RECT topRight{max(rect.left, rect.right - mask), rect.top, rect.right,
                min(rect.bottom, rect.top + mask)};
  RECT bottomRight{max(rect.left, rect.right - mask),
                   max(rect.top, rect.bottom - mask), rect.right, rect.bottom};
  FillRect(hdc, &topRight, windowBrush);
  FillRect(hdc, &bottomRight, windowBrush);
  DeleteObject(windowBrush);

  {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL left = (Gdiplus::REAL)rect.left;
    const Gdiplus::REAL top = (Gdiplus::REAL)rect.top + 0.6f;
    const Gdiplus::REAL right = (Gdiplus::REAL)rect.right - 0.6f;
    const Gdiplus::REAL bottom = (Gdiplus::REAL)rect.bottom - 0.6f;
    const Gdiplus::REAL arc = (Gdiplus::REAL)radius * 2.0f;
    Gdiplus::GraphicsPath path;
    path.StartFigure();
    path.AddLine(left, top, right - radius, top);
    path.AddArc(right - arc, top, arc, arc, 270.0f, 90.0f);
    path.AddLine(right, top + radius, right, bottom - radius);
    path.AddArc(right - arc, bottom - arc, arc, arc, 0.0f, 90.0f);
    path.AddLine(right - radius, bottom, left, bottom);
    Gdiplus::Pen borderPen(
        Gdiplus::Color(255, GetRValue(border), GetGValue(border),
                       GetBValue(border)),
        focused ? 1.7f : 1.0f);
    graphics.DrawPath(&borderPen, &path);
  }

  DrawModernComboChevron(hdc, rect, chevron, dpi);
}

static void DrawClientIconButton(HDC hdc, const RECT &rect, UINT itemState,
                                 HWND hButton) {
  if (!hdc || !hButton)
    return;

  const UINT dpi = GetDpiForWindow(hButton);
  const bool disabled = (itemState & ODS_DISABLED) != 0;
  const bool pressed = (itemState & ODS_SELECTED) != 0;
  const bool hot = (itemState & ODS_HOTLIGHT) != 0 || hButton == g_hHotButton;
  const bool focused = IsClientSelectorFocused();

  COLORREF fill = (hot || pressed) ? g_themeColors.crControlHot
                                   : g_themeColors.crControl;
  COLORREF border = focused ? g_themeColors.crAccent
                            : g_themeColors.crControlBorder;
  if (disabled) {
    fill = BlendColor(fill, g_themeColors.crWindow, 45);
    border = BlendColor(border, g_themeColors.crWindow, 50);
  }

  HBRUSH fillBrush = CreateSolidBrush(fill);
  FillRect(hdc, &rect, fillBrush);
  DeleteObject(fillBrush);

  const int radius = ScaleByDpi(4, dpi);
  const int mask = max(2, radius);
  HBRUSH windowBrush = CreateSolidBrush(g_themeColors.crWindow);
  RECT topLeft{rect.left, rect.top, min(rect.right, rect.left + mask),
               min(rect.bottom, rect.top + mask)};
  RECT bottomLeft{rect.left, max(rect.top, rect.bottom - mask),
                  min(rect.right, rect.left + mask), rect.bottom};
  FillRect(hdc, &topLeft, windowBrush);
  FillRect(hdc, &bottomLeft, windowBrush);
  DeleteObject(windowBrush);

  {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL left = (Gdiplus::REAL)rect.left + 0.6f;
    const Gdiplus::REAL top = (Gdiplus::REAL)rect.top + 0.6f;
    const Gdiplus::REAL right = (Gdiplus::REAL)rect.right;
    const Gdiplus::REAL bottom = (Gdiplus::REAL)rect.bottom - 0.6f;
    const Gdiplus::REAL arc = (Gdiplus::REAL)radius * 2.0f;
    Gdiplus::GraphicsPath path;
    path.StartFigure();
    path.AddLine(right, top, left + radius, top);
    path.AddArc(left, top, arc, arc, 270.0f, -90.0f);
    path.AddLine(left, top + radius, left, bottom - radius);
    path.AddArc(left, bottom - arc, arc, arc, 180.0f, -90.0f);
    path.AddLine(left + radius, bottom, right, bottom);
    Gdiplus::Pen borderPen(
        Gdiplus::Color(255, GetRValue(border), GetGValue(border),
                       GetBValue(border)),
        focused ? 1.7f : 1.0f);
    graphics.DrawPath(&borderPen, &path);
  }

  DrawClientIconTile(hButton, hdc);
}

static void DrawThemedButtonCore(HDC hdc, const RECT &rc, int iId,
                                 UINT itemState, HWND hWndItem) {
  const bool bDisabled = (itemState & ODS_DISABLED) != 0;
  const bool bPressed = (itemState & ODS_SELECTED) != 0;
  const bool bHot =
      (itemState & ODS_HOTLIGHT) != 0 || (hWndItem && hWndItem == g_hHotButton);
  const bool bFocus = (itemState & ODS_FOCUS) != 0;
  const LRESULT defaultId = hWndItem ? SendMessageW(GetParent(hWndItem), DM_GETDEFID, 0, 0) : 0;
  const bool bDefault = (itemState & ODS_DEFAULT) != 0 ||
      (HIWORD(defaultId) == DC_HASDEFID && LOWORD(defaultId) == iId);
  const bool bPrimary = bDefault || iId == IDOK;
  const bool bUtility = iId == IDC_BTN_TEMP || iId == IDC_BTN_CONFIG ||
                        iId == IDC_BTN_PIN;

  COLORREF crButtonFace = AdjustButtonFaceForContrast(
      g_themeColors.crButtonFace, g_themeColors.crWindow);
  crButtonFace = TintColor(crButtonFace, g_bThemeIsDark ? 10 : -10);
  COLORREF crFill = bPrimary ? g_themeColors.crAccent : crButtonFace;
  COLORREF crText =
      bPrimary ? g_themeColors.crAccentText : g_themeColors.crButtonText;
  COLORREF crBorder =
      bPrimary ? g_themeColors.crAccent : g_themeColors.crControlBorder;

  if (bUtility) {
    crFill = BlendColor(g_themeColors.crWindow, g_themeColors.crControl,
                        g_bThemeIsDark ? 18 : 30);
    crBorder = BlendColor(g_themeColors.crControlBorder,
                          g_themeColors.crWindow, 42);
  }

  if (bHot) {
    crFill = bPrimary ? TintColor(crFill, g_bThemeIsDark ? 10 : -8)
                      : g_themeColors.crControlHot;
    if (bUtility)
      crBorder = g_themeColors.crAccent;
  }
  if (bPressed) {
    crFill = TintColor(crFill, g_bThemeIsDark ? -12 : -12);
  }
  if (bDisabled) {
    crFill = BlendColor(crFill, g_themeColors.crWindow, 50);
    crText = BlendColor(crText, g_themeColors.crWindowText, 60);
    crBorder = BlendColor(crBorder, g_themeColors.crWindow, 50);
  }

  // Fill background to avoid dark corners when using rounded rects on a memory
  // DC
  {
    HBRUSH hbrBg = CreateSolidBrush(g_themeColors.crWindow);
    FillRect(hdc, &rc, hbrBg);
    DeleteObject(hbrBg);
  }

  if (!hWndItem || !IsWindow(hWndItem))
    return;
  const UINT dpi = GetDpiForWindow(hWndItem);
  const int iRadius = min(MulDiv(7, dpi, 96), max(2, (rc.right - rc.left) / 6));
  DrawRoundedRect(hdc, rc, crFill, crBorder, iRadius);

  HICON hIcon = nullptr;
  if (iId == IDC_BTN_TEMP)
    hIcon = g_hIconBtnTemp;
  if (iId == IDC_BTN_CONFIG)
    hIcon = g_hIconBtnConfig;

  // Secondary dialogs own their DPI-scaled fonts. Use the actual control font
  // there, while preserving the launcher's semibold primary action.
  HFONT hButtonFont = GetOwnerDrawFont(hWndItem, bPrimary);
  HFONT hOldFont = (HFONT)SelectObject(
      hdc, hButtonFont);
  if (iId == IDC_BTN_PIN) {
    const std::wstring clientName = GetSelectedClientNameSanitized(false);
    const bool isPinned = IsClientPinned(clientName);
    DrawPushpinGlyph(hdc, rc,
                     bDisabled ? crText : g_themeColors.crAccent, isPinned);
  } else if (hIcon) {
    int iW = 0, iH = 0;
    if (GetIconSizePx(hIcon, iW, iH)) {
      const int iX = rc.left + (rc.right - rc.left - iW) / 2;
      const int iY = rc.top + (rc.bottom - rc.top - iH) / 2;
      DrawIconEx(hdc, iX, iY, hIcon, iW, iH, 0, nullptr, DI_NORMAL);
    }
  } else {
    wchar_t wszText[128]{};
    GetWindowTextW(hWndItem, wszText, (int)(sizeof(wszText) / sizeof(wchar_t)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, crText);
    DrawTextW(hdc, wszText, -1, const_cast<RECT *>(&rc),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SelectObject(hdc, hOldFont);

  if (iId == IDC_BTN_CONFIG &&
      guided_walkthrough::HasUnreadAnnouncement(g_guideState)) {
    const int dotSize = max(ScaleByDpi(8, dpi), 6);
    const int inset = ScaleByDpi(3, dpi);
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const COLORREF dotColor = g_themeColors.crAccent;
    Gdiplus::SolidBrush dotBrush(Gdiplus::Color(
        bDisabled ? 120 : 255, GetRValue(dotColor), GetGValue(dotColor),
        GetBValue(dotColor)));
    graphics.FillEllipse(&dotBrush, rc.right - inset - dotSize,
                         rc.top + inset, dotSize, dotSize);
  }

  if (bFocus) {
    RECT rcFocus = rc;
    InflateRect(&rcFocus, -3, -3);
    DrawFocusRect(hdc, &rcFocus);
  }
}

static void DrawRestoreTabsToggle(HDC hdc, const RECT &rc, UINT itemState,
                                  HWND hWndItem) {
  const UINT dpi = hWndItem ? GetDpiForWindow(hWndItem) : g_uiDpi;
  const bool disabled = (itemState & ODS_DISABLED) != 0;
  const bool pressed = (itemState & ODS_SELECTED) != 0;
  const bool hot = !disabled &&
                   ((itemState & ODS_HOTLIGHT) != 0 ||
                    (hWndItem && hWndItem == g_hHotButton));
  const bool focused = (itemState & ODS_FOCUS) != 0;
  const COLORREF band = BlendColor(
      g_themeColors.crWindow, g_themeColors.crControl,
      g_bThemeIsDark ? 16 : 34);

  HBRUSH background = CreateSolidBrush(band);
  FillRect(hdc, &rc, background);
  DeleteObject(background);

  RECT content = rc;
  if (pressed)
    OffsetRect(&content, 0, ScaleByDpi(1, dpi));

  const int trackW = ScaleByDpi(36, dpi);
  const int trackH = ScaleByDpi(18, dpi);
  const int trackY = content.top + (content.bottom - content.top - trackH) / 2;
  RECT track{content.right - trackW, trackY, content.right,
             trackY + trackH};
  RECT label{content.left, content.top,
             track.left - ScaleByDpi(8, dpi), content.bottom};

  COLORREF text = g_themeColors.crWindowText;
  if (disabled) {
    text = BlendColor(text, band, g_bThemeIsDark ? 58 : 48);
  } else if (hot) {
    text = g_themeColors.crAccent;
  }

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, text);
  HFONT oldFont = (HFONT)SelectObject(
      hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
  DrawTextW(hdc, L"Restore tabs", -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                DT_NOPREFIX);
  SelectObject(hdc, oldFont);

  COLORREF trackFill = g_bRestoreTabsForSelection
                           ? g_themeColors.crAccent
                           : BlendColor(g_themeColors.crControl,
                                        g_themeColors.crControlBorder, 48);
  COLORREF trackBorder = g_bRestoreTabsForSelection
                             ? g_themeColors.crAccent
                             : g_themeColors.crControlBorder;
  if (hot) {
    trackFill = BlendColor(trackFill, RGB(255, 255, 255),
                           g_bThemeIsDark ? 14 : 8);
  }
  if (disabled) {
    trackFill = BlendColor(trackFill, band, 62);
    trackBorder = BlendColor(trackBorder, band, 58);
  }
  DrawRoundedRect(hdc, track, trackFill, trackBorder, trackH / 2);

  const int knobPad = ScaleByDpi(2, dpi);
  const int knobSize = trackH - knobPad * 2;
  const int knobX = g_bRestoreTabsForSelection
                        ? track.right - knobPad - knobSize
                        : track.left + knobPad;
  const int knobY = track.top + knobPad;
  const COLORREF knobColor = disabled
                                 ? BlendColor(g_themeColors.crWindowText, band,
                                              g_bThemeIsDark ? 55 : 48)
                                 : (g_bRestoreTabsForSelection
                                        ? g_themeColors.crAccentText
                                        : g_themeColors.crWindowText);
  Gdiplus::Graphics graphics(hdc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::SolidBrush knobBrush(Gdiplus::Color(
      255, GetRValue(knobColor), GetGValue(knobColor), GetBValue(knobColor)));
  graphics.FillEllipse(&knobBrush, knobX, knobY, knobSize, knobSize);

  if (focused) {
    RECT focus = track;
    InflateRect(&focus, ScaleByDpi(2, dpi), ScaleByDpi(2, dpi));
    HPEN focusPen =
        CreatePen(PS_SOLID, max(1, ScaleByDpi(1, dpi)), g_themeColors.crAccent);
    HGDIOBJ oldPen = SelectObject(hdc, focusPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(hdc, focus.left, focus.top, focus.right, focus.bottom,
              trackH + ScaleByDpi(4, dpi), trackH + ScaleByDpi(4, dpi));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(focusPen);
  }
}

static void DrawOwnerDrawItem(const DRAWITEMSTRUCT &ds) {
  if (ds.CtlType == ODT_STATIC && ds.CtlID == IDC_CLIENT_EDIT_SURFACE) {
    const bool focused = IsClientSelectorFocused();
    const bool disabled = !g_hClientEdit || !IsWindowEnabled(g_hClientEdit);
    COLORREF fill = g_themeColors.crControl;
    COLORREF border = focused ? g_themeColors.crAccent
                              : g_themeColors.crControlBorder;
    COLORREF text = g_themeColors.crControlText;
    if (disabled) {
      fill = BlendColor(fill, g_themeColors.crWindow, 45);
      border = BlendColor(border, g_themeColors.crWindow, 50);
      text = BlendColor(text, g_themeColors.crWindowText, 55);
    }

    const UINT dpi = GetDpiForWindow(ds.hwndItem);
    HBRUSH windowBrush = CreateSolidBrush(g_themeColors.crWindow);
    FillRect(ds.hDC, &ds.rcItem, windowBrush);
    DeleteObject(windowBrush);

    DrawRoundedRect(ds.hDC, ds.rcItem, fill, border, ScaleByDpi(4, dpi));

    const int laneWidth = min(ScaleByDpi(CLIENT_SELECTOR_DROP_LANE_DIP, dpi),
                              ds.rcItem.right - ds.rcItem.left);
    RECT dropZone{ds.rcItem.right - laneWidth, ds.rcItem.top,
                  ds.rcItem.right, ds.rcItem.bottom};
    DrawClientIconTile(ds.hwndItem, ds.hDC);
    DrawModernComboChevron(ds.hDC, dropZone, text, dpi);
    return;
  }

  if (ds.CtlType == ODT_BUTTON) {
    const int iId = static_cast<int>(ds.CtlID);
    const bool bIsGroup = (iId == IDC_GRP_ICON);

    if (iId == IDC_BTN_CLIENT_ICON) {
      DrawClientIconButton(ds.hDC, ds.rcItem, ds.itemState, ds.hwndItem);
      return;
    }

    if (iId == IDC_BTN_CLIENT_DROP) {
      DrawClientDropButton(ds.hDC, ds.rcItem, ds.itemState, ds.hwndItem);
      return;
    }

    if (iId == IDC_BTN_RESTORE_TABS) {
      DrawRestoreTabsToggle(ds.hDC, ds.rcItem, ds.itemState, ds.hwndItem);
      return;
    }

    if (bIsGroup) {
      const UINT dpi = GetDpiForWindow(ds.hwndItem);
      RECT rc = ds.rcItem;
      InflateRect(&rc, -1, -1);
      const int iRadius =
          min(MulDiv(6, dpi, 96), max(2, (rc.right - rc.left) / 6));
      HPEN hPen = CreatePen(PS_SOLID, 1, g_themeColors.crControlBorder);
      HGDIOBJ hOldPen = SelectObject(ds.hDC, hPen);
      HGDIOBJ hOldBrush = SelectObject(ds.hDC, GetStockObject(HOLLOW_BRUSH));
      RoundRect(ds.hDC, rc.left, rc.top, rc.right, rc.bottom, iRadius * 2,
                iRadius * 2);
      SelectObject(ds.hDC, hOldBrush);
      SelectObject(ds.hDC, hOldPen);
      DeleteObject(hPen);
      return;
    }

    const int w = ds.rcItem.right - ds.rcItem.left;
    const int h = ds.rcItem.bottom - ds.rcItem.top;
    if (w <= 0 || h <= 0)
      return;

    HDC memDC = CreateCompatibleDC(ds.hDC);
    HBITMAP memBmp = CreateCompatibleBitmap(ds.hDC, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    RECT rcLocal{0, 0, w, h};
    DrawThemedButtonCore(memDC, rcLocal, iId, ds.itemState, ds.hwndItem);
    BitBlt(ds.hDC, ds.rcItem.left, ds.rcItem.top, w, h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    return;
  }

  if (ds.CtlType == ODT_STATIC) {
    if (ds.CtlID != IDC_ICON_PREVIEW)
      return;

    const int w = ds.rcItem.right - ds.rcItem.left;
    const int h = ds.rcItem.bottom - ds.rcItem.top;
    if (w <= 0 || h <= 0)
      return;

    HDC memDC = CreateCompatibleDC(ds.hDC);
    HBITMAP memBmp = CreateCompatibleBitmap(ds.hDC, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    RECT rcLocal{0, 0, w, h};
    HBRUSH hbrBg = g_hbrThemeIconPreview
                       ? g_hbrThemeIconPreview
                       : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
    FillRect(memDC, &rcLocal, hbrBg);

    if (g_hIconPreviewHandle) {
      int iW = 0, iH = 0;
      if (GetIconSizePx(g_hIconPreviewHandle, iW, iH)) {
        const int iX = (w - iW) / 2;
        const int iY = (h - iH) / 2;
        DrawIconEx(memDC, iX, iY, g_hIconPreviewHandle, iW, iH, 0, nullptr,
                   DI_NORMAL);
      }
    }

    BitBlt(ds.hDC, ds.rcItem.left, ds.rcItem.top, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    return;
  }

  if (ds.CtlType == ODT_MENU) {
    DrawThemedMenuItem(ds);
    return;
  }

  if (ds.CtlType == ODT_COMBOBOX) {
    if (ds.itemID == (UINT)-1)
      return;

    if (g_hThemeCombo && ds.hwndItem == g_hThemeCombo &&
        ds.itemID == (UINT)g_iThemeSeparatorIndex) {
      FillRect(ds.hDC, &ds.rcItem,
               g_hbrThemeWindow ? g_hbrThemeWindow
                                : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE));
      const int iCy = (ds.rcItem.top + ds.rcItem.bottom) / 2;
      HPEN hPen = CreatePen(PS_SOLID, 1, g_themeColors.crControlBorder);
      HGDIOBJ hOld = SelectObject(ds.hDC, hPen);
      MoveToEx(ds.hDC, ds.rcItem.left + 4, iCy, nullptr);
      LineTo(ds.hDC, ds.rcItem.right - 4, iCy);
      SelectObject(ds.hDC, hOld);
      DeleteObject(hPen);
      return;
    }

    const bool bSelected = (ds.itemState & ODS_SELECTED) != 0;
    HBRUSH hbrFill = bSelected ? g_hbrThemeMenuSel : g_hbrThemeControl;
    FillRect(ds.hDC, &ds.rcItem,
             hbrFill ? hbrFill : (HBRUSH)GetSysColorBrush(COLOR_WINDOW));

    wchar_t wszText[256]{};
    SendMessageW(ds.hwndItem, CB_GETLBTEXT, ds.itemID, (LPARAM)wszText);
    SetBkMode(ds.hDC, TRANSPARENT);
    SetTextColor(ds.hDC, bSelected ? g_themeColors.crMenuSelText
                                   : g_themeColors.crControlText);

    RECT rcText = ds.rcItem;
    rcText.left += ScaleByDpi(6, g_uiDpi);
    DrawTextW(ds.hDC, wszText, -1, &rcText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (ds.itemState & ODS_FOCUS) {
      RECT rcFocus = ds.rcItem;
      InflateRect(&rcFocus, -2, -2);
      DrawFocusRect(ds.hDC, &rcFocus);
    }
    return;
  }
}

struct _7zUiCtx {
  HWND mainWnd = nullptr;
  std::wstring status;
  int lastPercent = -999;
  ULONGLONG lastTick = 0;
  bool sentMarquee = false;
};

struct AboutDlgState {
  HICON hIco64 = nullptr;
  HFONT hSmall = nullptr;
};

static void __stdcall _7zProgress(void *user, _7zOp op, unsigned int percent,
                                  const wchar_t * /*currentItem*/) {
  auto *ctx = reinterpret_cast<_7zUiCtx *>(user);
  if (!ctx || !ctx->mainWnd || !IsWindow(ctx->mainWnd))
    return;

  // Unknown progress -> marquee once.
  if (percent < 0) {
    if (!ctx->sentMarquee) {
      ctx->sentMarquee = true;
      ctx->lastPercent = -1;
      ctx->lastTick = GetTickCount64();
      ProgressUI_PostUpdate(ctx->mainWnd, ctx->status, -1);
    }
    return;
  }

  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;

  ULONGLONG now = GetTickCount64();

  // Throttle: don't spam the GUI thread.
  if (percent == ctx->lastPercent && (now - ctx->lastTick) < 150)
    return;
  if ((now - ctx->lastTick) < 33 && percent < 100)
    return; // ~30fps max

  ctx->lastTick = now;
  ctx->lastPercent = percent;
  ProgressUI_PostUpdate(ctx->mainWnd, ctx->status, percent);
}

ActiveProfileMap g_activeProfiles;

void GuiProfOpen();
void GuiSetIcon();
void GuiProfReset();
void GuiOpenDef();
void GuiOpenTmp();
void GuiProfExportAll();
void GuiProfRestoreAll();
bool IsValidFilenameChar(wchar_t c);
std::wstring SanitizeName(const std::wstring &name);
std::wstring ResolveExistingClientName(const std::wstring &name);
void UpdateClientsComboBox();
void SetUiState(bool enabled);

bool ExtractResourceToFile(UINT resourceID, const fs::path &destPath,
                           bool overwrite = false);
bool EnsureBundledDefaultTemplate();
bool extDef(const fs::path &profileDataPath, const wchar_t *statusText);
bool SetBrowserStartupPreference(const fs::path &profilePath,
                                 bool restoreTabs);
bool ClearBrowserStartupState(const fs::path &profilePath);
bool PrepareFreshProfileState(const fs::path &profilePath);
bool SetWindowAppId(HWND hWnd, const std::wstring &appId,
                    const fs::path &iconPath = fs::path(),
                    const fs::path &clientRoot = fs::path(),
                    bool recreateTaskbar = true);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
std::wstring AnsiToWide(const std::string &str);
std::vector<std::wstring> GetSupportedImageTypes();
bool ConvertImageToIcon(const fs::path &sourceImagePath,
                        const fs::path &destIconPath);
bool SaveIconsToFile(const fs::path &filePath, std::vector<HICON> &icons,
                     bool compressLargeImages = true, size_t startIndex = 0);
std::vector<BYTE> CompressBitmapToPng(HBITMAP hBitmap);
CLSID GetEncoderClsid(const WCHAR *format);
HICON Create32BitHICON(HICON hIcon);
bool IsAlphaBitmap(HBITMAP hBitmap);
void EnsureWatcherIsRunning();
void WatcherThread(std::stop_token stopToken);
static void StopWatcher();
static void StopLaunchWorker();
static void StopShutdownWorker();
static void StopReaperThreads();
static void ReaperThread(DWORD pid, std::wstring clientName,
                         BrowserKind browser, ProfileType type,
                         HANDLE processHandle);
static void HandleClosedDefaultProfile();
static bool HandleProfileExitOnUiThread(const ProfileExitPayload &payload);
static bool PostProfileExitMessage(DWORD pid,
                                   const std::wstring &clientName,
                                   BrowserKind browser, ProfileType type,
                                   bool launchRollback = false,
                                   bool sessionAnnounced = true);
DWORD LaunchProfile(const std::wstring &clientName, bool isTemp,
                    bool isDefault, BrowserKind &browser,
                    const std::wstring &startupUrl = L"",
                    HANDLE *processHandle = nullptr,
                    fs::path *launchedProfilePath = nullptr,
                    fs::path *launchedExecutablePath = nullptr,
                    std::wstring *launchError = nullptr,
                    bool *freshTransientProfile = nullptr);
bool FindBrowsers();
void RequestCloseAllProfiles();
std::wstring GetExeVersion(const fs::path &filePath);
bool chkUpdate();
bool doInstall();
HWND CreateToolTip(HWND toolHWND, HWND hDlg, PTSTR pszText);
static bool PostTaskComplete(const std::wstring &name);
static bool PostOwnedStringMessage(UINT message, WPARAM wParam,
                                   const std::wstring &value,
                                   bool retryWhileWindowExists = false);
static bool IsClientActiveAnyBrowser(const std::wstring &clientName);
static ProfileUseState ProbeClientProfilesInUse(
    const std::wstring &clientName, std::wstring &errorDetails);
static ProfileUseState ProbeExactBrowserProfileInUse(
    const fs::path &profilePath, std::wstring &errorDetails);
static ProfileUseState ProbeAnySitesProfileInUse(std::wstring &errorDetails);
static bool TryMakeUniqueSiblingStagePath(const fs::path &destination,
                                          const wchar_t *suffix,
                                          fs::path &stagingPath,
                                          std::wstring &errorDetails);
static void RemoveSafeStagingFile(const fs::path &stagingPath);
static void LaunchProfileAsync(const std::wstring &name, bool isTemp,
                               bool isDefault, BrowserKind browser,
                               const std::wstring &startupUrl = L"");
void GuiProfDel();
static void GuiCleanupInactiveClients(bool manualSelection = false);
inline void EnsureMouseVisible();
inline void FocusClientEdit();
static void ShowAboutDialog();
static void ShowThemeDialog();
static INT_PTR CALLBACK ThemeDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam);
static HBRUSH HandleThemeCtlColor(UINT msg, HDC hdc, HWND hCtl);
static void DrawOwnerDrawItem(const DRAWITEMSTRUCT &ds);
static UINT ShowConfigMenuFromButton(HWND hWnd);
static UINT ShowBrowserMenuFromSelector(HWND hWnd);
static int IcoDim(BYTE b) { return (b == 0) ? 256 : (int)b; }
static HICON LoadIconResBestDownscale(HINSTANCE hInst, int groupIconResId,
                                      int cxDesired, int cyDesired);
static HICON LoadIconFromIcoBestDownscale(const fs::path &icoPath,
                                          int pxDesired);
static HFONT CreateSmallerFontFrom(HFONT baseFont, int pxHeight, UINT dpi);
static HICON ScaleIconDown_HQ(HICON hSrc, int dstCx, int dstCy);
static bool GetIconSizePx(HICON hIcon, int &w, int &h);
static void EnsureMenuTooltips(HWND hWnd);
static void HideMenuTooltip();
static void HandleMenuSelect(HWND hWnd, WPARAM wParam, LPARAM lParam);
static void UpdateConfigMenuEnabledState();
static std::wstring
GetSelectedClientNameSanitized(bool preferListSelection = false);
static fs::path GetProfileDirFromName(const std::wstring &name);
static void BringSessionToFront(DWORD pid);
static void OnSessionStarted(DWORD pid, const std::wstring &name);
static void OnSessionEnded(DWORD pid);
static void ClearClientIconCache(const std::wstring &clientName);
static void RefreshOpenClientWindowIcon(const std::wstring &clientName,
                                        bool forceNotify = false);
static bool ApplyWindowIcons(HWND hWnd, HICON hSmall, HICON hBig,
                             bool forceNotify = false);

static std::wstring GetClientInputText() {
  if (!g_hClientEdit)
    return L"";
  const int length = GetWindowTextLengthW(g_hClientEdit);
  if (length <= 0)
    return L"";
  std::wstring text((size_t)length + 1, L'\0');
  GetWindowTextW(g_hClientEdit, text.data(), length + 1);
  text.resize((size_t)length);
  return text;
}

static void SetClientInputSelection(int start, int end) {
  if (g_hClientEdit)
    SendMessageW(g_hClientEdit, EM_SETSEL, (WPARAM)start, (LPARAM)end);
}

static void SetClientInputText(const std::wstring &text) {
  const HWND clientEdit = g_hClientEdit;
  if (!clientEdit)
    return;
  if (GetClientInputText() == text)
    return;
  const bool wasSyncing = g_bSyncingClientInput;
  g_bSyncingClientInput = true;
  SetWindowTextW(clientEdit, text.c_str());
  g_bSyncingClientInput = wasSyncing;
}

static bool SyncClientInputFromComboSelection(bool selectAll) {
  if (!g_hComboClient)
    return false;
  const LRESULT selection =
      SendMessageW(g_hComboClient, CB_GETCURSEL, 0, 0);
  if (selection == CB_ERR)
    return false;

  wchar_t text[512]{};
  SendMessageW(g_hComboClient, CB_GETLBTEXT, selection, (LPARAM)text);
  SetClientInputText(text);
  const int length = (int)wcslen(text);
  SetClientInputSelection(selectAll ? 0 : length, selectAll ? -1 : length);
  return true;
}

static void HandleClientInputChanged() {
  if (g_bSyncingClientInput || !g_hComboClient)
    return;

  SwitchToLaunchModeForInput();
  const std::wstring currentText = GetClientInputText();
  bool hasInvalidChar = false;
  for (wchar_t c : currentText) {
    if (!IsValidFilenameChar(c)) {
      hasInvalidChar = true;
      break;
    }
  }

  if (GetAsyncKeyState(VK_BACK) & 0x8000 ||
      GetAsyncKeyState(VK_DELETE) & 0x8000) {
    if (!hasInvalidChar) {
      g_sLastValidComboText = currentText;
      TOOLINFOW tip{sizeof(TOOLINFOW)};
      tip.hwnd = g_hGui;
      tip.uId = (UINT_PTR)g_hComboClient;
      SendMessageW(g_hValidationTooltip, TTM_TRACKACTIVATE, FALSE,
                   (LPARAM)&tip);
    }
    UpdateIconPreviewForSelection(false);
    return;
  }

  if (hasInvalidChar) {
    SetClientInputText(g_sLastValidComboText);
    const int end = (int)g_sLastValidComboText.length();
    SetClientInputSelection(end, end);

    TOOLINFOW tip{sizeof(TOOLINFOW)};
    tip.hwnd = g_hGui;
    tip.uFlags = TTF_ABSOLUTE;
    tip.uId = (UINT_PTR)g_hComboClient;
    tip.lpszText =
        (LPWSTR)L"A client name can't contain any of the following "
                 L"characters:\n    \\ / : * ? \" < > |";
    SendMessageW(g_hValidationTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&tip);
    RECT rect{};
    GetWindowRect(g_hComboClient, &rect);
    const int tipPad = ScaleByDpi(4, g_uiDpi);
    SendMessageW(g_hValidationTooltip, TTM_TRACKPOSITION, 0,
                 MAKELPARAM(rect.left + tipPad, rect.bottom - tipPad));
    SendMessageW(g_hValidationTooltip, TTM_TRACKACTIVATE, TRUE,
                 (LPARAM)&tip);
    SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, FALSE, 0);
    UpdateIconPreviewForSelection(false);
    return;
  }

  g_sLastValidComboText = currentText;
  TOOLINFOW tip{sizeof(TOOLINFOW)};
  tip.hwnd = g_hGui;
  tip.uId = (UINT_PTR)g_hComboClient;
  SendMessageW(g_hValidationTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&tip);

  if (currentText.empty()) {
    SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, FALSE, 0);
    UpdateIconPreviewForSelection(false);
    return;
  }

  int caseInsensitiveMatch = -1;
  int caseSensitiveMatch = -1;
  wchar_t listText[512]{};
  const int count = (int)SendMessageW(g_hComboClient, CB_GETCOUNT, 0, 0);
  for (int i = 0; i < count; ++i) {
    SendMessageW(g_hComboClient, CB_GETLBTEXT, i, (LPARAM)listText);
    const std::wstring item = listText;
    if (caseSensitiveMatch == -1 && item.size() >= currentText.size() &&
        item.compare(0, currentText.size(), currentText) == 0) {
      caseSensitiveMatch = i;
      break;
    }
    if (caseInsensitiveMatch == -1 && item.size() >= currentText.size() &&
        _wcsnicmp(item.c_str(), currentText.c_str(), currentText.size()) == 0) {
      caseInsensitiveMatch = i;
    }
  }

  if (caseSensitiveMatch != -1) {
    SendMessageW(g_hComboClient, CB_GETLBTEXT, caseSensitiveMatch,
                 (LPARAM)listText);
    SendMessageW(g_hComboClient, CB_SETCURSEL, caseSensitiveMatch, 0);
    SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, TRUE, 0);
    EnsureMouseVisible();
    SetClientInputText(listText);
  } else if (caseInsensitiveMatch != -1) {
    SendMessageW(g_hComboClient, CB_SETCURSEL, caseInsensitiveMatch, 0);
    SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, TRUE, 0);
    EnsureMouseVisible();
  } else {
    SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, FALSE, 0);
  }

  SetClientInputSelection((int)currentText.length(), -1);
  UpdateIconPreviewForSelection(false);
}

static bool SameExecutablePath(const fs::path &left, const fs::path &right) {
  try {
    return fs::equivalent(left, right);
  } catch (...) {
    return _wcsicmp(left.wstring().c_str(), right.wstring().c_str()) == 0;
  }
}

static bool GetProcessImagePath(DWORD processId, fs::path &imagePath) {
  HANDLE hProcess =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!hProcess)
    return false;

  bool resolved = false;
  std::vector<wchar_t> imagePathText(512, L'\0');
  while (imagePathText.size() <= 32768) {
    DWORD pathSize = static_cast<DWORD>(imagePathText.size());
    if (QueryFullProcessImageNameW(hProcess, 0, imagePathText.data(),
                                   &pathSize) &&
        pathSize > 0 && pathSize < imagePathText.size()) {
      imagePath = fs::path(std::wstring(imagePathText.data(), pathSize));
      resolved = true;
      break;
    }
    const DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER || imagePathText.size() == 32768)
      break;
    imagePathText.resize(
        (std::min)(imagePathText.size() * 2, size_t{32768}), L'\0');
  }
  CloseHandle(hProcess);
  return resolved;
}

static std::optional<int>
GetExistingInstanceExitCode(const fs::path &currentExePath,
                            bool mutexAlreadyExists,
                            const std::wstring &arguments) {
  HWND hExistingWnd = FindWindowW(GUI_CLASS_NAME.c_str(), NULL);
  if (hExistingWnd) {
    DWORD existingProcId = 0;
    GetWindowThreadProcessId(hExistingWnd, &existingProcId);

    fs::path existingExePath;
    const bool hasExistingPath =
        GetProcessImagePath(existingProcId, existingExePath);
    if (!hasExistingPath) {
      MessageBoxW(NULL,
                  L"Windows found a ctSpaces window, but its executable "
                  L"identity could not be verified. No launch request was "
                  L"sent. Close that window and try again.",
                  L"ctSpaces Is Open", MB_OK | MB_ICONERROR);
      return 1;
    }
    if (!SameExecutablePath(currentExePath, existingExePath)) {
      std::wstring message =
          L"ctSpaces is already open from another location:\n\n" +
          existingExePath.wstring() +
          L"\n\nClose that ctSpaces window, then run this installer again.";
      MessageBoxW(NULL, message.c_str(), L"ctSpaces Is Open",
                  MB_OK | MB_ICONWARNING);
      return 1;
    }

    DWORD_PTR deliveryResult = 0;
    bool delivered = false;
    if (!arguments.empty()) {
      constexpr ULONG_PTR kLaunchRequestTag = 0x43545350;
      if (arguments.size() + 1 > MAXDWORD / sizeof(wchar_t))
        return 1;
      COPYDATASTRUCT request{};
      request.dwData = kLaunchRequestTag;
      request.cbData =
          (DWORD)((arguments.size() + 1) * sizeof(wchar_t));
      request.lpData = (void *)arguments.c_str();
      delivered = SendMessageTimeoutW(
                      hExistingWnd, WM_COPYDATA, 0, (LPARAM)&request,
                      SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000,
                      &deliveryResult) != 0 &&
                  deliveryResult != FALSE;
    } else {
      delivered = SendMessageTimeoutW(
                      hExistingWnd, WM_NULL, 0, 0,
                      SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000,
                      &deliveryResult) != 0;
    }
    if (!delivered) {
      MessageBoxW(NULL,
                  L"The existing ctSpaces window was verified, but it did "
                  L"not confirm the launch request. Nothing was handed off.",
                  L"ctSpaces Is Open", MB_OK | MB_ICONERROR);
      return 1;
    }
    ShowWindow(hExistingWnd, SW_RESTORE);
    SetForegroundWindow(hExistingWnd);
    return 0;
  }

  if (mutexAlreadyExists) {
    MessageBoxW(NULL,
                L"ctSpaces appears to still be starting or shutting down.\n\n"
                L"Wait a few seconds and try again. If it keeps happening, "
                 L"check Task Manager for ctSpaces.exe.",
                 L"ctSpaces Is Open", MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  return std::nullopt;
}

static std::vector<DWORD> FindProcessesUsingImage(const fs::path &imagePath) {
  std::vector<DWORD> processIds;
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE)
    return processIds;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(hSnapshot, &entry)) {
    do {
      fs::path processPath;
      if (GetProcessImagePath(entry.th32ProcessID, processPath) &&
          SameExecutablePath(processPath, imagePath)) {
        processIds.push_back(entry.th32ProcessID);
      }
    } while (Process32NextW(hSnapshot, &entry));
  }

  CloseHandle(hSnapshot);
  return processIds;
}

static std::wstring FormatProcessList(const std::vector<DWORD> &processIds) {
  std::wostringstream ss;
  for (size_t i = 0; i < processIds.size(); ++i) {
    if (i > 0)
      ss << L", ";
    ss << processIds[i];
  }
  return ss.str();
}

static fs::path NormalizePathForScope(const fs::path &path) {
  try {
    if (fs::exists(path))
      return fs::weakly_canonical(path);
    const fs::path parent = path.has_parent_path() ? path.parent_path()
                                                   : fs::current_path();
    return (fs::weakly_canonical(parent) / path.filename()).lexically_normal();
  } catch (...) {
    return fs::absolute(path).lexically_normal();
  }
}

static bool IsStrictChildPath(const fs::path &candidate,
                              const fs::path &root) {
  const fs::path normalizedCandidate = NormalizePathForScope(candidate);
  const fs::path normalizedRoot = NormalizePathForScope(root);
  auto candidatePart = normalizedCandidate.begin();
  for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end();
       ++rootPart, ++candidatePart) {
    if (candidatePart == normalizedCandidate.end() ||
        _wcsicmp(candidatePart->c_str(), rootPart->c_str()) != 0) {
      return false;
    }
  }
  return candidatePart != normalizedCandidate.end();
}

static bool IsDirectChildPath(const fs::path &candidate,
                              const fs::path &root) {
  const fs::path normalizedCandidate = NormalizePathForScope(candidate);
  const fs::path normalizedRoot = NormalizePathForScope(root);
  auto candidatePart = normalizedCandidate.begin();
  for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end();
       ++rootPart, ++candidatePart) {
    if (candidatePart == normalizedCandidate.end() ||
        _wcsicmp(candidatePart->c_str(), rootPart->c_str()) != 0) {
      return false;
    }
  }
  if (candidatePart == normalizedCandidate.end())
    return false;
  ++candidatePart;
  return candidatePart == normalizedCandidate.end();
}

static constexpr wchar_t kClientSchemaMarkerName[] = L"ctSpaces-client-v2";
static constexpr wchar_t kBrowserSchemaMarkerName[] = L"ctSpaces-browser-v2";
static constexpr wchar_t kLegacyBrowserBindingMarkerName[] =
    L"ctSpaces-legacy-browser-v2";
static constexpr char kClientSchemaMarkerText[] =
    "ctSpaces-client-schema=2\r\n";

static bool IsSafeExistingDirectory(const fs::path &path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

static bool IsSafeExistingRegularFile(const fs::path &path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                        FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

static bool ValidateSitesRoot(bool createIfMissing) {
  const fs::path sitesRoot = g_sDataDir / L"Sites";
  DWORD attributes = GetFileAttributesW(sitesRoot.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
      return false;
    if (!createIfMissing)
      return true;
    if (!CreateDirectoryW(sitesRoot.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      return false;
    }
    attributes = GetFileAttributesW(sitesRoot.c_str());
  }
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

static bool WriteDurableMarker(const fs::path &markerPath,
                               const std::string &contents) {
  HANDLE marker = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
  if (marker == INVALID_HANDLE_VALUE)
    return false;

  DWORD written = 0;
  const bool writeSucceeded =
      contents.size() <= MAXDWORD &&
      WriteFile(marker, contents.data(), static_cast<DWORD>(contents.size()),
                &written, nullptr) != FALSE &&
      written == contents.size() && FlushFileBuffers(marker) != FALSE;
  const bool closeSucceeded = CloseHandle(marker) != FALSE;
  if (!writeSucceeded || !closeSucceeded)
    return false;

  const DWORD attributes = GetFileAttributesW(markerPath.c_str());
  std::error_code sizeError;
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                        FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
         fs::file_size(markerPath, sizeError) == contents.size() && !sizeError;
}

static bool MarkerHasExactContents(const fs::path &markerPath,
                                   const std::string &expected) {
  const DWORD attributes = GetFileAttributesW(markerPath.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                     FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    return false;
  }
  std::ifstream marker(markerPath, std::ios::binary);
  if (!marker)
    return false;
  std::string contents((std::istreambuf_iterator<char>(marker)),
                       std::istreambuf_iterator<char>());
  return marker.good() || marker.eof() ? contents == expected : false;
}

static bool WriteDurableMarkerAtomically(const fs::path &markerPath,
                                         const std::string &contents) {
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const fs::path stagedPath =
        markerPath.parent_path() /
        std::format(L".{}-{}-{}-{}", markerPath.filename().wstring(),
                    GetCurrentProcessId(), GetTickCount64(), attempt);
    if (GetFileAttributesW(stagedPath.c_str()) != INVALID_FILE_ATTRIBUTES)
      continue;
    if (!WriteDurableMarker(stagedPath, contents)) {
      std::error_code cleanupError;
      fs::remove(stagedPath, cleanupError);
      return false;
    }
    if (!MoveFileExW(stagedPath.c_str(), markerPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      std::error_code cleanupError;
      fs::remove(stagedPath, cleanupError);
      return false;
    }
    return MarkerHasExactContents(markerPath, contents);
  }
  return false;
}

static std::string GetBrowserMarkerText(BrowserKind browser) {
  return std::string("ctSpaces-browser-schema=2\r\nbrowser=") +
         GetBrowserIdAscii(browser) + "\r\n";
}

static std::string GetLegacyBrowserBindingText(BrowserKind browser) {
  return std::string("ctSpaces-legacy-browser-schema=2\r\nbrowser=") +
         GetBrowserIdAscii(browser) + "\r\n";
}

static bool IsV2ClientContainer(const fs::path &clientRoot) {
  return IsSafeExistingDirectory(clientRoot) &&
         MarkerHasExactContents(clientRoot / kClientSchemaMarkerName,
                                kClientSchemaMarkerText);
}

static bool TryGetSafeClientProfilePath(const std::wstring &clientName,
                                        fs::path &profilePath) {
  if (clientName.empty() || SanitizeName(clientName) != clientName)
    return false;

  const fs::path sitesRoot = g_sDataDir / L"Sites";
  if (!ValidateSitesRoot(false))
    return false;
  const fs::path candidate = sitesRoot / clientName;
  if (!IsDirectChildPath(candidate, sitesRoot))
    return false;

  const DWORD attributes = GetFileAttributesW(candidate.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
      return false;
  } else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
             (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return false;
  }

  profilePath = candidate;
  return true;
}

static bool RevalidateSafeClientContainerPath(const std::wstring &clientName,
                                              const fs::path &clientRoot) {
  fs::path currentPath;
  return TryGetSafeClientProfilePath(clientName, currentPath) &&
         _wcsicmp(currentPath.c_str(), clientRoot.c_str()) == 0 &&
         IsSafeExistingDirectory(clientRoot);
}

static bool GetNestedBrowserSlotPaths(const fs::path &clientRoot,
                                      BrowserKind browser, fs::path &slotRoot,
                                      fs::path &profileRoot) {
  if (!IsSafeExistingDirectory(clientRoot))
    return false;
  const fs::path browsersRoot = clientRoot / L"Browsers";
  slotRoot = browsersRoot / GetBrowserId(browser);
  profileRoot = slotRoot / L"Profile";
  if (!IsStrictChildPath(slotRoot, clientRoot) ||
      !IsStrictChildPath(profileRoot, slotRoot)) {
    return false;
  }
  if (fs::exists(browsersRoot) && !IsSafeExistingDirectory(browsersRoot))
    return false;
  if (fs::exists(slotRoot) && !IsSafeExistingDirectory(slotRoot))
    return false;
  if (fs::exists(profileRoot) && !IsSafeExistingDirectory(profileRoot))
    return false;
  return true;
}

static bool GetBrowserSlotPaths(const fs::path &clientRoot,
                                BrowserKind browser, fs::path &slotRoot,
                                fs::path &profileRoot) {
  return IsV2ClientContainer(clientRoot) &&
         GetNestedBrowserSlotPaths(clientRoot, browser, slotRoot, profileRoot);
}

static bool IsValidBrowserSlot(const fs::path &slotRoot,
                               const fs::path &profileRoot,
                               BrowserKind browser) {
  return IsSafeExistingDirectory(slotRoot) &&
         IsSafeExistingDirectory(profileRoot) &&
         MarkerHasExactContents(slotRoot / kBrowserSchemaMarkerName,
                                GetBrowserMarkerText(browser)) &&
         (!IsChromiumBrowser(browser) ||
          MarkerHasExactContents(profileRoot / L"ctSpaces",
                                 "ctSpaces-profile=2\r\n"));
}

static bool TryGetLegacyBrowserBinding(const fs::path &clientRoot,
                                       std::optional<BrowserKind> &binding) {
  binding.reset();
  const fs::path markerPath = clientRoot / kLegacyBrowserBindingMarkerName;
  const DWORD attributes = GetFileAttributesW(markerPath.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  }
  if ((attributes & (FILE_ATTRIBUTE_DIRECTORY |
                     FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    return false;
  }
  for (BrowserKind browser : {BrowserKind::Edge, BrowserKind::Chrome,
                              BrowserKind::Brave}) {
    if (MarkerHasExactContents(markerPath,
                               GetLegacyBrowserBindingText(browser))) {
      binding = browser;
      return true;
    }
  }
  return false;
}

static bool ValidateExistingBrowserSlotsRoot(const fs::path &clientRoot) {
  const fs::path browsersRoot = clientRoot / L"Browsers";
  const DWORD attributes = GetFileAttributesW(browsersRoot.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  }
  if (!IsSafeExistingDirectory(browsersRoot))
    return false;
  try {
    for (const auto &entry : fs::directory_iterator(browsersRoot)) {
      const std::wstring id = entry.path().filename().wstring();
      const auto browser = ParseBrowserKind(id);
      fs::path slotRoot;
      fs::path profileRoot;
      if (!browser || !entry.is_directory() ||
          !GetNestedBrowserSlotPaths(clientRoot, *browser, slotRoot,
                                     profileRoot) ||
          _wcsicmp(slotRoot.c_str(), entry.path().c_str()) != 0 ||
          !IsValidBrowserSlot(slotRoot, profileRoot, *browser)) {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

static bool IsSafeHybridClientRoot(
    const fs::path &clientRoot,
    std::optional<BrowserKind> *legacyBinding = nullptr) {
  if (!IsSafeExistingDirectory(clientRoot) ||
      IsV2ClientContainer(clientRoot) ||
      !IsSafeExistingDirectory(clientRoot / L"Default") ||
      !ValidateExistingBrowserSlotsRoot(clientRoot)) {
    return false;
  }
  std::optional<BrowserKind> binding;
  if (!TryGetLegacyBrowserBinding(clientRoot, binding))
    return false;
  if (legacyBinding)
    *legacyBinding = binding;
  return true;
}

static bool ValidateNoReparsePointsInTree(const fs::path &root,
                                          std::wstring &errorDetails) {
  const auto fail = [&errorDetails](const std::wstring &message) {
    errorDetails = message;
    return false;
  };

  const DWORD rootAttributes = GetFileAttributesW(root.c_str());
  if (rootAttributes == INVALID_FILE_ATTRIBUTES ||
      (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return fail(L"The client folder is no longer an accessible directory.");
  }
  if ((rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return fail(L"The client folder is a reparse point.");
  }

  std::error_code iteratorError;
  fs::recursive_directory_iterator entry(
      root, fs::directory_options::none, iteratorError);
  const fs::recursive_directory_iterator end;
  if (iteratorError) {
    return fail(L"The client folder could not be inspected completely.");
  }

  while (entry != end) {
    const fs::path currentPath = entry->path();
    const DWORD attributes = GetFileAttributesW(currentPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      return fail(L"A client item could not be inspected safely: " +
                  currentPath.filename().wstring());
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      return fail(L"The client contains a reparse point and was not deleted: " +
                  currentPath.filename().wstring());
    }

    entry.increment(iteratorError);
    if (iteratorError) {
      return fail(L"The client folder changed or became inaccessible while it "
                  L"was being inspected.");
    }
  }
  return true;
}

static bool ValidateWholeClientDeleteTarget(const std::wstring &clientName,
                                            const fs::path &expectedRoot,
                                            fs::path &validatedRoot,
                                            std::wstring &errorDetails) {
  const auto fail = [&errorDetails](const std::wstring &message) {
    errorDetails = message;
    return false;
  };

  fs::path currentRoot;
  if (!TryGetSafeClientProfilePath(clientName, currentRoot) ||
      _wcsicmp(currentRoot.c_str(), expectedRoot.c_str()) != 0 ||
      !RevalidateSafeClientContainerPath(clientName, currentRoot)) {
    return fail(L"The selected client path changed or is no longer safe.");
  }

  const fs::path sitesRoot = g_sDataDir / L"Sites";
  if (!IsSafeExistingDirectory(sitesRoot) ||
      !IsDirectChildPath(currentRoot, sitesRoot) ||
      SameExecutablePath(currentRoot, g_sDataDir / L"Default") ||
      SameExecutablePath(currentRoot, g_sDataDir / L"Temp")) {
    return fail(L"The selected folder is not the exact safe client child of "
                L"the Sites folder.");
  }

  const fs::path clientMarker = currentRoot / kClientSchemaMarkerName;
  const fs::path legacyMarker =
      currentRoot / kLegacyBrowserBindingMarkerName;
  const fs::path legacyDefault = currentRoot / L"Default";
  const DWORD clientMarkerAttributes = GetFileAttributesW(clientMarker.c_str());
  const DWORD legacyMarkerAttributes = GetFileAttributesW(legacyMarker.c_str());
  const DWORD legacyDefaultAttributes =
      GetFileAttributesW(legacyDefault.c_str());
  const bool clientMarkerExists =
      clientMarkerAttributes != INVALID_FILE_ATTRIBUTES;
  const bool legacyMarkerExists =
      legacyMarkerAttributes != INVALID_FILE_ATTRIBUTES;
  const bool legacyDefaultExists =
      legacyDefaultAttributes != INVALID_FILE_ATTRIBUTES;

  const bool isV2 = IsV2ClientContainer(currentRoot);
  if (isV2) {
    if (legacyMarkerExists || legacyDefaultExists ||
        !ValidateExistingBrowserSlotsRoot(currentRoot)) {
      return fail(L"The client has conflicting or invalid v2 and legacy "
                  L"profile data.");
    }
  } else {
    if (clientMarkerExists || !IsSafeHybridClientRoot(currentRoot)) {
      return fail(L"The client does not have a safe recognized profile "
                  L"layout.");
    }
  }

  if (!ValidateNoReparsePointsInTree(currentRoot, errorDetails))
    return false;

  validatedRoot = currentRoot;
  return true;
}

struct ClientBrowserProfileLocation {
  BrowserKind browser = BrowserKind::Edge;
  fs::path profileRoot;
};

static bool CollectClientBrowserProfileLocations(
    const std::wstring &clientName, const fs::path &clientRoot,
    std::vector<ClientBrowserProfileLocation> &locations) {
  locations.clear();
  if (!RevalidateSafeClientContainerPath(clientName, clientRoot) ||
      !ValidateExistingBrowserSlotsRoot(clientRoot)) {
    return false;
  }

  if (IsV2ClientContainer(clientRoot)) {
    // Pure v2 clients have only nested browser profiles.
  } else {
    std::optional<BrowserKind> legacyBinding;
    if (!IsSafeHybridClientRoot(clientRoot, &legacyBinding))
      return false;
    locations.push_back(
        {legacyBinding.value_or(BrowserKind::Edge), clientRoot});
  }

  const fs::path browsersRoot = clientRoot / L"Browsers";
  std::error_code browsersError;
  const bool browsersExist = fs::exists(browsersRoot, browsersError);
  if (browsersError)
    return false;
  if (!browsersExist)
    return !locations.empty() || IsV2ClientContainer(clientRoot);
  try {
    for (const auto &entry : fs::directory_iterator(browsersRoot)) {
      const auto browser =
          ParseBrowserKind(entry.path().filename().wstring());
      fs::path slotRoot;
      fs::path profileRoot;
      if (!browser ||
          !GetNestedBrowserSlotPaths(clientRoot, *browser, slotRoot,
                                     profileRoot) ||
          !IsValidBrowserSlot(slotRoot, profileRoot, *browser)) {
        return false;
      }
      locations.push_back({*browser, profileRoot});
    }
  } catch (...) {
    return false;
  }
  return !locations.empty();
}

static bool RemoveNestedCacheDirectory(const fs::path &profileRoot,
                                       const fs::path &relativePath,
                                       bool &removed) {
  removed = false;
  if (!IsSafeExistingDirectory(profileRoot) || relativePath.empty() ||
      relativePath.is_absolute()) {
    return false;
  }
  const fs::path target = profileRoot / relativePath;
  if (!IsStrictChildPath(target, profileRoot))
    return false;

  fs::path current = profileRoot;
  for (const auto &part : relativePath) {
    current /= part;
    const DWORD attributes = GetFileAttributesW(current.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      return false;
    if (current != target &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      return false;
    }
  }

  std::error_code removeError;
  fs::remove_all(target, removeError);
  if (removeError)
    return false;
  std::error_code existsError;
  removed = !fs::exists(target, existsError) && !existsError;
  return removed;
}

static fs::path GetCurrentExecutablePath() {
  std::vector<wchar_t> buffer(512, L'\0');
  while (buffer.size() <= 32768) {
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return {};
    if (length < buffer.size() - 1)
      return fs::path(std::wstring(buffer.data(), length));
    if (buffer.size() == 32768)
      break;
    buffer.resize((std::min)(buffer.size() * 2, size_t{32768}), L'\0');
  }
  return {};
}

static void WaitForPreviousLauncher(const wchar_t *commandLine) {
  if (!commandLine || !*commandLine)
    return;

  std::wsmatch match;
  const std::wstring arguments(commandLine);
  const std::wregex waitPattern(LR"((?:^|\s)--wait-for-pid=(\d+)(?:\s|$))");
  if (!std::regex_search(arguments, match, waitPattern) || match.size() < 2)
    return;

  try {
    const DWORD processId = (DWORD)std::stoul(match[1].str());
    if (processId == 0 || processId == GetCurrentProcessId())
      return;

    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (hProcess) {
      WaitForSingleObject(hProcess, 15000);
      CloseHandle(hProcess);
    }
  } catch (...) {
  }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE,
                      _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
  g_hInst = hInstance;
  const HRESULT launcherAppIdResult =
      SetCurrentProcessExplicitAppUserModelID(LAUNCHER_APP_USER_MODEL_ID);
  if (FAILED(launcherAppIdResult)) {
    const std::wstring message = std::format(
        L"ctSpaces could not establish its separate launcher identity "
        L"(Windows error 0x{:08X}).",
        static_cast<unsigned long>(launcherAppIdResult));
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  const HRESULT comResult =
      CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(comResult)) {
    const std::wstring message = std::format(
        L"Windows COM initialization failed (0x{:08X}).",
        static_cast<unsigned long>(comResult));
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  struct ComApartmentCleanup {
    ~ComApartmentCleanup() { CoUninitialize(); }
  } comCleanup;

  WaitForPreviousLauncher(lpCmdLine);
  EnableDpiAwareness();
  const fs::path currentExePath = GetCurrentExecutablePath();
  if (currentExePath.empty()) {
    MessageBoxW(nullptr, L"The ctSpaces executable path could not be resolved.",
                L"Startup Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  g_sExeDir = currentExePath.parent_path();

  std::wsmatch qaInstanceMatch;
  const std::wstring commandLine(lpCmdLine ? lpCmdLine : L"");
  const std::wregex qaInstancePattern(
      LR"((?:^|\s)--qa-instance=([A-Za-z0-9_-]{1,40})(?:\s|$))");
  const bool isQaInstance =
      std::regex_search(commandLine, qaInstanceMatch, qaInstancePattern) &&
      qaInstanceMatch.size() > 1;
  g_bQaInstance = isQaInstance;

  std::wstring mutexName =
      L"Local\\{E19C159D-62C3-4412-A0A3-1A55A67C8C56}";
  if (isQaInstance)
    mutexName += L"-QA-" + qaInstanceMatch[1].str();
  SetLastError(ERROR_SUCCESS);
  HANDLE hMutex = CreateMutexW(NULL, TRUE, mutexName.c_str());
  if (!hMutex) {
    const std::wstring message =
        std::format(L"ctSpaces could not create its single-instance guard "
                    L"(Windows error {}). No profile data was opened.",
                    GetLastError());
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  const bool mutexAlreadyExists = GetLastError() == ERROR_ALREADY_EXISTS;
  struct MutexCleanup {
    HANDLE handle = nullptr;
    bool owns = false;
    ~MutexCleanup() {
      if (owns)
        ReleaseMutex(handle);
      if (handle)
        CloseHandle(handle);
    }
  } mutexCleanup{hMutex, !mutexAlreadyExists};

  if (isQaInstance && mutexAlreadyExists) {
    MessageBoxW(nullptr,
                L"A QA instance with this identifier is already running. "
                L"Choose a unique --qa-instance value.",
                L"QA Instance Is Open", MB_OK | MB_ICONERROR);
    return 1;
  }
  if (!isQaInstance) {
    const auto existingInstanceExitCode = GetExistingInstanceExitCode(
        currentExePath, mutexAlreadyExists, commandLine);
    if (existingInstanceExitCode)
      return *existingInstanceExitCode;
  }

  PWSTR path = NULL;
  const HRESULT dataPathResult =
      SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path);
  if (SUCCEEDED(dataPathResult) && path) {
    g_sDataDir = fs::path(path) / "InfinitySys" / "ctSpaces";
  }
  if (path)
    CoTaskMemFree(path);

  if (isQaInstance) {
    std::wsmatch qaDataMatch;
    const std::wregex qaDataPattern(
        LR"QA((?:^|\s)--qa-data-dir=(?:"([^"]+)"|([^\s]+))(?:\s|$))QA");
    if (!std::regex_search(commandLine, qaDataMatch, qaDataPattern)) {
      MessageBoxW(nullptr,
                  L"A QA instance requires an explicit isolated "
                   L"--qa-data-dir folder.",
                   L"Missing QA Data Folder", MB_OK | MB_ICONERROR);
      return 1;
    }
    const std::wstring qaDataValue = qaDataMatch[1].matched
                                         ? qaDataMatch[1].str()
                                         : qaDataMatch[2].str();
    const fs::path qaDataPath(qaDataValue);
    if (!qaDataPath.is_absolute() ||
        !IsStrictChildPath(qaDataPath, g_sExeDir)) {
      MessageBoxW(nullptr,
                  L"The QA data folder must be an absolute child of the "
                   L"portable QA folder.",
                   L"Invalid QA Data Folder", MB_OK | MB_ICONERROR);
      return 1;
    }
    g_sDataDir = NormalizePathForScope(qaDataPath);

    std::wsmatch qaShortcutMatch;
    const std::wregex qaShortcutPattern(
        LR"QA((?:^|\s)--qa-shortcut-dir=(?:"([^"]+)"|([^\s]+))(?:\s|$))QA");
    if (std::regex_search(commandLine, qaShortcutMatch,
                          qaShortcutPattern)) {
      const std::wstring qaShortcutValue = qaShortcutMatch[1].matched
                                               ? qaShortcutMatch[1].str()
                                               : qaShortcutMatch[2].str();
      const fs::path qaShortcutPath(qaShortcutValue);
      if (!qaShortcutPath.is_absolute() ||
          !IsStrictChildPath(qaShortcutPath, g_sExeDir)) {
        MessageBoxW(nullptr,
                    L"The QA shortcut folder must be an absolute child of "
                     L"the portable QA folder.",
                     L"Invalid QA Shortcut Folder", MB_OK | MB_ICONERROR);
        return 1;
      }
      g_sShortcutDesktopOverride =
          NormalizePathForScope(qaShortcutPath);
    }
    g_sTaskbarQaLogPath = g_sExeDir / L"taskbar-identity.log";
    std::error_code staleLogError;
    fs::remove(g_sTaskbarQaLogPath, staleLogError);
  }

  const bool qaGuideStartup =
      std::regex_search(commandLine,
                        std::wregex(LR"((?:^|\s)--qa-guide-startup(?:\s|$))"));
  const bool freshGuideCandidate =
      guided_walkthrough::IsFreshDataFolder(g_sDataDir);

  if (g_sDataDir.empty()) {
    MessageBoxW(nullptr, L"The ctSpaces data folder could not be resolved.",
                L"Startup Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  try {
    std::filesystem::create_directories(g_sDataDir);
  } catch (const std::exception &error) {
    const std::wstring details = AnsiToWide(error.what());
    MessageBoxW(nullptr,
                (L"The ctSpaces data folder could not be created.\n\n" +
                 details)
                     .c_str(),
                 L"Startup Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  g_sConfigPath = g_sDataDir / L"config.ini";
  g_guideState = guided_walkthrough::LoadState(g_sConfigPath);
  if (freshGuideCandidate && (!isQaInstance || qaGuideStartup) &&
      !g_guideState.welcomeHandled) {
    guided_walkthrough::ApplyWelcomePending(g_guideState);
    // A background/client launch must never be delayed by a persistence error.
    // Keep the offer pending in memory for this run and write it silently.
    SaveConfigMutations(L"guided-walkthrough welcome state",
                        guided_walkthrough::WelcomePendingMutations(), false);
  }
  try {
    LoadBakedThemesFromHeader();
    LoadThemePreference();
    LoadClientTitlePreference();
    LoadBrowserPreference();
    LoadArchivedClients();
    LoadPinnedClients();
    LoadRestoreTabsPreferences();
  } catch (const std::exception &error) {
    const std::wstring details = AnsiToWide(error.what());
    MessageBoxW(nullptr,
                (L"ctSpaces could not read its configuration safely.\n\n" +
                 details)
                    .c_str(),
                L"Startup Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  bool shouldRun = false;
  const fs::path installedExePath = g_sDataDir / L"ctSpaces.exe";
  std::error_code startupPathError;
  const bool runPortable =
      fs::exists(g_sExeDir / L"ctSpaces.portable", startupPathError);
  if (startupPathError) {
    MessageBoxW(nullptr,
                L"ctSpaces could not inspect its portable-mode marker. No "
                L"profile data was opened.",
                L"Startup Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  startupPathError.clear();
  const bool installedCopyExists =
      fs::exists(installedExePath, startupPathError);
  if (startupPathError) {
    MessageBoxW(nullptr,
                L"ctSpaces could not inspect the installed executable. No "
                L"update or setup action was attempted.",
                L"Startup Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  if (runPortable) {
    shouldRun = true;
  } else if (installedCopyExists) {
    shouldRun = chkUpdate();
  } else {
    shouldRun = doInstall();
  }
  if (!shouldRun)
    return 0;

  bool browsersFound = false;
  try {
    browsersFound = FindBrowsers();
  } catch (...) {
    browsersFound = false;
  }
  if (!browsersFound) {
    MessageBox(NULL,
               L"No supported browser (Edge, Chrome, Brave, or Firefox) "
               L"could be found in a standard installation location.",
               L"Application Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  Gdiplus::GdiplusStartupInput gdiplusStartupInput;
  const Gdiplus::Status gdiplusResult =
      Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
  if (gdiplusResult != Gdiplus::Ok) {
    const std::wstring message =
        std::format(L"Windows graphics initialization failed (status {}).",
                    static_cast<int>(gdiplusResult));
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  struct GdiplusCleanup {
    ~GdiplusCleanup() {
      if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
      }
    }
  } gdiplusCleanup;

  if (!EnsureBundledDefaultTemplate()) {
    MessageBoxW(NULL,
                L"ctSpaces could not safely update its new-client template.\n\n"
                L"No client profiles were changed. Close any security scan "
                 L"using the ctSpaces data folder, then try again.",
                 L"Template Update Failed", MB_OK | MB_ICONERROR);
    return 1;
  }
  INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX),
                                ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS};
  if (!InitCommonControlsEx(&icex)) {
    const std::wstring message =
        std::format(L"Windows common controls initialization failed "
                    L"(error {}).",
                    GetLastError());
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  ProgressUI_Init(hInstance);
  _7zSetHInstance(hInstance);

  const ATOM windowClass = MyRegisterClass(hInstance);
  if (!windowClass) {
    const std::wstring message =
        std::format(L"The ctSpaces window class could not be registered "
                    L"(Windows error {}).",
                    GetLastError());
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  struct WindowClassCleanup {
    HINSTANCE instance = nullptr;
    ~WindowClassCleanup() {
      if (instance)
        UnregisterClassW(GUI_CLASS_NAME.c_str(), instance);
    }
  } windowClassCleanup{hInstance};

  if (!InitInstance(hInstance, nCmdShow)) {
    std::wstring message =
        L"The ctSpaces main window or a required control could not be created.";
    if (!g_sStartupInitFailure.empty())
      message += L"\n\nDetails: " + g_sStartupInitFailure;
    MessageBoxW(nullptr, message.c_str(), L"Startup Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  if (!commandLine.empty())
    ProcessClientLaunchRequest(commandLine, true);

  const bool hiddenOrMinimized =
      nCmdShow == SW_HIDE || nCmdShow == SW_MINIMIZE ||
      nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_SHOWMINNOACTIVE ||
      nCmdShow == SW_SHOWNOACTIVATE || nCmdShow == SW_SHOWNA;
  const bool mayAutoShowGuide =
      g_guideState.welcomePending && !g_guideState.welcomeHandled &&
      (!isQaInstance || qaGuideStartup) && !hiddenOrMinimized &&
      !HasClientLaunchRequest(commandLine) && IsWindowVisible(g_hGui) &&
      !IsIconic(g_hGui);
  if (mayAutoShowGuide)
    PostMessageW(g_hGui, WM_APP_SHOW_GUIDE, 1, 0);

  MSG msg{};
  int exitCode = 0;
  bool messageLoopFailed = false;
  for (;;) {
    const BOOL messageResult = GetMessageW(&msg, nullptr, 0, 0);
    if (messageResult == 0) {
      exitCode = static_cast<int>(msg.wParam);
      break;
    }
    if (messageResult == -1) {
      exitCode = 1;
      messageLoopFailed = true;
      break;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1 &&
        !g_hGuideDialog && !g_hQuickTourDialog) {
      SendMessageW(g_hGui, WM_COMMAND, IDM_GUIDED_WALKTHROUGH, 0);
      continue;
    }
    if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog) &&
        IsDialogMessageW(g_hQuickTourDialog, &msg)) {
      continue;
    }
    // intercept Enter when focus is in the combo or its edit
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
      HWND hFocus = GetFocus();
      if (hFocus == g_hComboClient ||
          (hFocus && GetParent(hFocus) == g_hComboClient)) {
        // Activate the primary Open button.
        SendMessage(g_hGui, WM_COMMAND, MAKELONG(IDOK, BN_CLICKED),
                    (LPARAM)g_hBtnGo);
        // don't let the dialog logic eat this
        continue;
      }
    }

    if (!IsDialogMessage(g_hGui, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  if (messageLoopFailed) {
    MessageBoxW(g_hGui, L"The Windows message loop failed unexpectedly.",
                L"Application Error", MB_OK | MB_ICONERROR);
  }
  StopLaunchWorker();
  StopShutdownWorker();
  StopReaperThreads();
  StopWatcher();
  if (g_hGui && IsWindow(g_hGui))
    DestroyWindow(g_hGui);
  return exitCode;
}

ATOM MyRegisterClass(HINSTANCE hInstance) {
  WNDCLASSEXW wcex = {};
  wcex.cbSize = sizeof(WNDCLASSEXW);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.hInstance = hInstance;
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wcex.lpszClassName = GUI_CLASS_NAME.c_str();
  wcex.lpszMenuName = NULL;
  return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
  g_hInst = hInstance;
  const int baseGuiW = MAIN_GUI_WIDTH_DIP;
  const int baseGuiH = GetMainGuiHeightDip();
  const int baseGuiM = 14;
  const int baseCtrlW = (baseGuiW - baseGuiM * 2);

  UINT dpi = GetDpiForSystem();
  g_uiDpi = dpi;

  const int guiClientW = ScaleByDpi(baseGuiW, dpi);
  const int guiClientH = ScaleByDpi(baseGuiH, dpi);
  RECT wr{0, 0, guiClientW, guiClientH};

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                      WS_CLIPCHILDREN;
  const DWORD exStyle = 0;
  auto adjustForDpi =
      reinterpret_cast<BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT)>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"),
                         "AdjustWindowRectExForDpi"));
  if (adjustForDpi) {
    adjustForDpi(&wr, style, FALSE, exStyle, dpi);
  } else {
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
  }

  g_hGui =
      CreateWindowW(GUI_CLASS_NAME.c_str(), APP_TITLE.c_str(), style,
                    CW_USEDEFAULT, 0, wr.right - wr.left, wr.bottom - wr.top,
                    nullptr, nullptr, hInstance, nullptr);
  if (!g_hGui)
    return FALSE;
  if (!SetWindowAppId(g_hGui, LAUNCHER_APP_USER_MODEL_ID, fs::path(),
                      fs::path(), false)) {
    g_sStartupInitFailure =
        L"The launcher window identity could not be established or verified.";
    DestroyWindow(g_hGui);
    return FALSE;
  }

  dpi = GetDpiForWindow(g_hGui);
  g_uiDpi = dpi;

  RECT dpiWindowRect{0, 0, ScaleByDpi(baseGuiW, dpi),
                     ScaleByDpi(baseGuiH, dpi)};
  if (adjustForDpi) {
    adjustForDpi(&dpiWindowRect, style, FALSE, exStyle, dpi);
  } else {
    AdjustWindowRectEx(&dpiWindowRect, style, FALSE, exStyle);
  }
  SetWindowPos(g_hGui, nullptr, 0, 0,
               dpiWindowRect.right - dpiWindowRect.left,
               dpiWindowRect.bottom - dpiWindowRect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  g_hFont = CreateUiFont(dpi);
  g_hFontLabel = CreateUiLabelFont(dpi);
  g_hFontStrong = CreateUiStrongFont(dpi);
  g_hFontClient = CreateClientFont(dpi);

  const int iGuiM = ScaleByDpi(baseGuiM, dpi);
  const int iGuiCtrlW = ScaleByDpi(baseCtrlW, dpi);
  const int labelH = ScaleByDpi(17, dpi);
  HICON hAppIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CTSPACES));
  if (hAppIcon) {
    SendMessage(g_hGui, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
    SendMessage(g_hGui, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
  }

  HWND hPrompt = CreateWindowW(L"STATIC", L"CLIENT",
                WS_CHILD | WS_VISIBLE, iGuiM, iGuiM, iGuiCtrlW, labelH, g_hGui,
                (HMENU)IDC_STATIC_PROMPT, hInstance, nullptr);
  if (hPrompt && g_hFontLabel)
    SendMessageW(hPrompt, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
  g_hValidationTooltip = CreateWindowEx(
      WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
      TTS_BALLOON | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, g_hGui, NULL, g_hInst, NULL);
  SendMessageW(g_hGui, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessageW(g_hValidationTooltip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(g_hValidationTooltip, TTM_SETMAXTIPWIDTH, 0,
              ScaleByDpi(400, dpi));
  g_hComboClient = CreateWindowW(
      L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | CBS_DROPDOWN |
          CBS_AUTOHSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
      iGuiM, labelH + iGuiM * 2, iGuiCtrlW - iGuiM * 4, ScaleByDpi(150, dpi),
      g_hGui, (HMENU)102, hInstance, nullptr);
  SendMessageW(g_hComboClient, CB_SETCUEBANNER, 0,
               (LPARAM)L"Choose or type a client name");
  UpdateComboBoxMetrics(dpi);
  HideClientComboChrome(g_hComboClient);
  COMBOBOXINFO clientCombo{sizeof(clientCombo)};
  if (GetComboBoxInfo(g_hComboClient, &clientCombo))
    ApplyEditContextMenuTheme(clientCombo.hwndItem);
  g_hClientEditSurface = CreateWindowW(
      L"STATIC", L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY,
      ScaleByDpi(1, dpi), ScaleByDpi(1, dpi), ScaleByDpi(100, dpi),
      ScaleByDpi(34, dpi), g_hGui,
      (HMENU)(INT_PTR)IDC_CLIENT_EDIT_SURFACE, hInstance, nullptr);
  g_hClientEdit = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL,
      ScaleByDpi(1, dpi), ScaleByDpi(1, dpi), ScaleByDpi(100, dpi),
      ScaleByDpi(22, dpi), g_hGui, (HMENU)(INT_PTR)IDC_CLIENT_EDIT, hInstance,
      nullptr);
  if (g_hClientEdit) {
    SendMessageW(g_hClientEdit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"Choose or type a client name");
    SendMessageW(g_hClientEdit, EM_SETLIMITTEXT,
                 static_cast<WPARAM>(kMaxClientNameLength), 0);
    SendMessageW(g_hClientEdit, WM_SETFONT,
                 (WPARAM)(g_hFontClient ? g_hFontClient : g_hFont), TRUE);
    SetWindowSubclass(g_hClientEdit, ClientEditSubclassProc, 1, 0);
    ApplyEditContextMenuTheme(g_hClientEdit);
  }
  g_hBtnClientIcon = CreateWindowW(
      L"BUTTON", L"Open profile folder",
      WS_CHILD | BS_OWNERDRAW, ScaleByDpi(1, dpi),
      ScaleByDpi(1, dpi), ScaleByDpi(34, dpi), ScaleByDpi(36, dpi), g_hGui,
      (HMENU)(INT_PTR)IDC_BTN_CLIENT_ICON, hInstance, nullptr);
  g_hBtnClientDrop = CreateWindowW(
      L"BUTTON", L"Show client list", WS_CHILD | BS_OWNERDRAW,
      ScaleByDpi(1, dpi), ScaleByDpi(1, dpi), ScaleByDpi(34, dpi),
      ScaleByDpi(36, dpi), g_hGui,
      (HMENU)(INT_PTR)IDC_BTN_CLIENT_DROP, hInstance, nullptr);

  TOOLINFOW tic = {sizeof(TOOLINFOW)};
  tic.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT | TTF_TRACK;
  tic.hwnd = g_hGui;
  tic.hinst = g_hInst;
  tic.uId = (UINT_PTR)g_hComboClient;
  tic.lpszText = LPSTR_TEXTCALLBACK;
  SendMessage(g_hValidationTooltip, TTM_ADDTOOL, 0, (LPARAM)&tic);
  const int goW = ScaleByDpi(64, dpi);
  const int goH = ScaleByDpi(33, dpi);
  g_hBtnGo =
      CreateWindowW(L"BUTTON", L"Open",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    (iGuiCtrlW / 2) - ScaleByDpi(32, dpi) - ScaleByDpi(4, dpi),
                    ScaleByDpi(50, dpi) + iGuiM, goW, goH, g_hGui, (HMENU)IDOK,
                    hInstance, nullptr);
  g_hBtnPin = CreateWindowW(
      L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
      ScaleByDpi(1, dpi), ScaleByDpi(1, dpi), ScaleByDpi(34, dpi),
      ScaleByDpi(34, dpi), g_hGui, (HMENU)(INT_PTR)IDC_BTN_PIN, hInstance,
      nullptr);
  g_hBtnPinTip =
      CreateToolTip(g_hBtnPin, g_hGui, g_sPinTooltip.data());
  g_hBtnRestoreTabs = CreateWindowW(
      L"BUTTON", L"Restore tabs",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      ScaleByDpi(1, dpi), ScaleByDpi(1, dpi), ScaleByDpi(132, dpi),
      ScaleByDpi(30, dpi), g_hGui,
      (HMENU)(INT_PTR)IDC_BTN_RESTORE_TABS, hInstance, nullptr);
  if (g_hBtnRestoreTabs) {
    SendMessageW(g_hBtnRestoreTabs, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    g_hBtnRestoreTabsTip = CreateToolTip(
        g_hBtnRestoreTabs, g_hGui,
        (LPWSTR)L"Reopen this client's tabs next time. Sign-ins and profile "
                 L"data stay saved when this is off.");
  }
  // HWND
  // hToolTip=CreateWindowEx(0,TOOLTIPS_CLASS,NULL,TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,g_hGui,NULL,g_hInst,NULL);
  RECT rc{};
  GetClientRect(g_hGui, &rc);
  const int iBtnS = ScaleByDpi(24, dpi);
  const int iBtnT = rc.bottom - iGuiM - iBtnS;
  int iBtnL = rc.right - iGuiM - iBtnS;
  g_hBtnConfig = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_ICON,
                               iBtnL, iBtnT, iBtnS, iBtnS, g_hGui,
                               (HMENU)(INT_PTR)201, g_hInst, nullptr);
  g_hBtnTmpProf = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_ICON,
                                 iBtnL - iBtnS - iGuiM, iBtnT, iBtnS, iBtnS,
                                 g_hGui, (HMENU)(INT_PTR)200, g_hInst, nullptr);
  if (!hPrompt || !g_hValidationTooltip || !g_hComboClient ||
      !g_hClientEditSurface || !g_hClientEdit || !g_hBtnClientIcon ||
      !g_hBtnClientDrop || !g_hBtnGo || !g_hBtnPin ||
      !g_hBtnRestoreTabs || !g_hBtnConfig || !g_hBtnTmpProf) {
    std::vector<std::wstring> missingControls;
    const auto recordMissing = [&missingControls](HWND control,
                                                   const wchar_t *name) {
      if (!control)
        missingControls.emplace_back(name);
    };
    recordMissing(hPrompt, L"client label");
    recordMissing(g_hValidationTooltip, L"validation tooltip");
    recordMissing(g_hComboClient, L"client list");
    recordMissing(g_hClientEditSurface, L"client editor surface");
    recordMissing(g_hClientEdit, L"client editor");
    recordMissing(g_hBtnClientIcon, L"client icon button");
    recordMissing(g_hBtnClientDrop, L"client list button");
    recordMissing(g_hBtnGo, L"Open button");
    recordMissing(g_hBtnPin, L"pin button");
    recordMissing(g_hBtnRestoreTabs, L"restore-tabs button");
    recordMissing(g_hBtnConfig, L"options button");
    recordMissing(g_hBtnTmpProf, L"temporary-profile button");
    g_sStartupInitFailure = L"Missing required control(s): ";
    for (size_t index = 0; index < missingControls.size(); ++index) {
      if (index)
        g_sStartupInitFailure += L", ";
      g_sStartupInitFailure += missingControls[index];
    }
    if (g_bQaInstance) {
      std::wofstream qaError(g_sDataDir / L"qa-startup-error.txt",
                             std::ios::trunc);
      if (qaError)
        qaError << g_sStartupInitFailure << L'\n';
    }
    DestroyWindow(g_hGui);
    return FALSE;
  }
  SetButtonIcon(g_hBtnTmpProf, IDI_TEMPB, g_hIconBtnTemp);
  SetButtonIcon(g_hBtnConfig, IDI_CFGB, g_hIconBtnConfig);
  g_hBtnTmpProfTip =
      CreateToolTip(g_hBtnTmpProf, g_hGui, (LPWSTR)L"Launch temporary profile");
  g_hBtnConfigTip =
      CreateToolTip(g_hBtnConfig, g_hGui, g_sConfigTooltip.data());

  EnsureConfigMenu(g_hGui);

  EnsureMenuTooltips(g_hGui);

  if (g_hBtnGo) {
    LONG_PTR iStyle = GetWindowLongPtrW(g_hBtnGo, GWL_STYLE);
    SetWindowLongPtrW(g_hBtnGo, GWL_STYLE, iStyle | BS_OWNERDRAW);
    SetWindowSubclass(g_hBtnGo, ButtonHotSubclassProc, 1, 0);
  }
  if (g_hBtnTmpProf) {
    LONG_PTR iStyle = GetWindowLongPtrW(g_hBtnTmpProf, GWL_STYLE);
    SetWindowLongPtrW(g_hBtnTmpProf, GWL_STYLE, iStyle | BS_OWNERDRAW);
    SetWindowSubclass(g_hBtnTmpProf, ButtonHotSubclassProc, 1, 0);
  }
  if (g_hBtnConfig) {
    LONG_PTR iStyle = GetWindowLongPtrW(g_hBtnConfig, GWL_STYLE);
    SetWindowLongPtrW(g_hBtnConfig, GWL_STYLE, iStyle | BS_OWNERDRAW);
    SetWindowSubclass(g_hBtnConfig, ButtonHotSubclassProc, 1, 0);
  }
  if (g_hBtnClientIcon)
    SetWindowSubclass(g_hBtnClientIcon, ButtonHotSubclassProc, 1, 0);
  if (g_hBtnClientDrop)
    SetWindowSubclass(g_hBtnClientDrop, ButtonHotSubclassProc, 1, 0);
  if (g_hBtnRestoreTabs)
    SetWindowSubclass(g_hBtnRestoreTabs, ButtonHotSubclassProc, 1, 0);
  if (g_hComboClient) {
    ApplyComboTheme(g_hComboClient);
    UpdateComboBoxMetrics(dpi);
  }
  if (g_hBtnClientIcon)
    g_hIconPreviewTip = CreateToolTip(
        g_hBtnClientIcon, g_hGui, (LPWSTR)L"Open profile folder");

  // Apply the DPI-aware main layout.
  LayoutMainGui(g_hGui);

  // Initial preview
  UpdateIconPreviewForSelection();
  // for(size_t ix=0; ix<g_iconButtons.size(); ++ix){
  //     int
  //     iBtnT=iGuiH-((iBtnS+iBtnM)*(2-((ix/4)%((g_iconButtons.size()/4)*4))))-32-6;
  //     int xPos=iBtnL+((iBtnS+iBtnM+10)*(static_cast<int>(ix%4)+1));
  //     g_iconButtons[ix].hWnd=CreateWindowW(L"BUTTON",g_iconButtons[ix].symbol.c_str(),WS_CHILD|WS_VISIBLE,xPos,iBtnT,iBtnS+10,iBtnS,g_hGui,(HMENU)(INT_PTR)g_iconButtons[ix].id,g_hInst,nullptr);
  //     CreateToolTip(g_iconButtons[ix].hWnd,g_hGui,(LPWSTR)g_iconButtons[ix].tooltip.c_str());
  // }
  EnumChildWindows(
      g_hGui,
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
      },
      (LPARAM)g_hFont);
  if (hPrompt && g_hFontLabel)
    SendMessageW(hPrompt, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
  if (g_hBtnGo && g_hFontStrong)
    SendMessageW(g_hBtnGo, WM_SETFONT, (WPARAM)g_hFontStrong, TRUE);
  if (g_hComboClient && g_hFontClient)
    SendMessageW(g_hComboClient, WM_SETFONT, (WPARAM)g_hFontClient, TRUE);
  if (g_hClientEdit && g_hFontClient)
    SendMessageW(g_hClientEdit, WM_SETFONT, (WPARAM)g_hFontClient, TRUE);
  LayoutClientComboChildren(g_hComboClient);
  UpdateClientsComboBox();
  ApplyTheme(g_iThemeMode, false);
  FocusClientEdit();
  ShowWindow(g_hGui, nCmdShow);
  PostMessageW(g_hGui, WM_APP_FINALIZE_LAYOUT, 0, 0);
  UpdateWindow(g_hGui);
  return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  switch (message) {
  case WM_COPYDATA: {
    constexpr ULONG_PTR kLaunchRequestTag = 0x43545350;
    const auto *request = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
    if (!request || request->dwData != kLaunchRequestTag || !request->lpData ||
        request->cbData < sizeof(wchar_t) ||
        request->cbData > 32768 * sizeof(wchar_t) ||
        request->cbData % sizeof(wchar_t) != 0) {
      return FALSE;
    }
    const size_t capacity = request->cbData / sizeof(wchar_t);
    const auto *text = static_cast<const wchar_t *>(request->lpData);
    size_t length = 0;
    while (length < capacity && text[length] != L'\0')
      ++length;
    if (length == capacity)
      return FALSE;
    if (g_hGuideDialog && IsWindow(g_hGuideDialog)) {
      // Do not let a read-only modal guide delay a client shortcut. This is
      // not a user Skip/Close action, so it must preserve welcome and read
      // state before the handoff proceeds.
      SendMessageW(g_hGuideDialog, WM_APP_DISMISS_GUIDE, 0, 0);
    }
    if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog)) {
      // A client shortcut must not wait behind the read-only tour. Dismiss it
      // synchronously before preserving the request's selected browser intent.
      DismissQuickTour(false);
    }
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
    return ProcessClientLaunchRequest(std::wstring(text, length), true)
               ? TRUE
               : FALSE;
  }
  case WM_COMMAND: {
    int wmId = LOWORD(wParam);
    int wmEvent = HIWORD(wParam);
    if (wmId == IDC_CLIENT_EDIT) {
      if (wmEvent == EN_CHANGE) {
        HandleClientInputChanged();
        return 0;
      }
      if (wmEvent == EN_KILLFOCUS) {
        UpdateIconPreviewForSelection(false);
        return 0;
      }
    } else if (wmId == IDC_CLIENT_EDIT_SURFACE &&
               wmEvent == STN_CLICKED) {
      POINT point{};
      const DWORD messagePos = GetMessagePos();
      point.x = static_cast<short>(LOWORD(messagePos));
      point.y = static_cast<short>(HIWORD(messagePos));
      ScreenToClient(g_hClientEditSurface, &point);

      RECT selectorRect{};
      GetClientRect(g_hClientEditSurface, &selectorRect);
      const UINT selectorDpi = GetDpiForWindow(g_hClientEditSurface);
      const int iconLaneWidth =
          ScaleByDpi(CLIENT_SELECTOR_ICON_LANE_DIP, selectorDpi);
      const int dropLaneWidth =
          ScaleByDpi(CLIENT_SELECTOR_DROP_LANE_DIP, selectorDpi);
      if (point.x < selectorRect.left + iconLaneWidth) {
        OpenSelectedClientFolder();
        FocusClientEdit();
      } else if (point.x >= selectorRect.right - dropLaneWidth) {
        const bool show =
            SendMessageW(g_hComboClient, CB_GETDROPPEDSTATE, 0, 0) == 0;
        FocusClientEdit();
        SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, show, 0);
        InvalidateRect(g_hClientEditSurface, nullptr, TRUE);
      } else {
        FocusClientEdit();
      }
      return 0;
    } else if (wmId == 102) {
      if (wmEvent == CBN_EDITCHANGE)
        return 0;
      if (wmEvent == CBN_DROPDOWN) {
        QueueClientSelectorRestack();
        return 0;
      }
      if (wmEvent == CBN_SELCHANGE) {
        SwitchToLaunchModeForInput();
        SyncClientInputFromComboSelection(false);
        UpdateIconPreviewForSelection(true);
        return 0;
      }
      if (wmEvent == CBN_SELENDOK) {
        SwitchToLaunchModeForInput();
        SyncClientInputFromComboSelection(true);
        FocusClientEdit();
        UpdateIconPreviewForSelection(true);
        return 0;
      }
      if (wmEvent == CBN_CLOSEUP) {
        SyncClientInputFromComboSelection(true);
        UpdateIconPreviewForSelection(true);
        QueueClientSelectorRestack();
        return 0;
      }
      if (wmEvent == CBN_KILLFOCUS) {
        UpdateIconPreviewForSelection(false);
        return 0;
      }
    } else if (wmId == IDOK) {
      if (g_iSelectedSessionTab > 0 &&
          g_iSelectedSessionTab <= (int)g_sessions.size()) {
        BringSessionToFront(g_sessions[g_iSelectedSessionTab - 1].pid);
        return 0;
      }
      GuiProfOpen();
    } else if (wmId == IDC_BTN_CLIENT_ICON && wmEvent == BN_CLICKED) {
      OpenSelectedClientFolder();
      FocusClientEdit();
    } else if (wmId == IDC_BTN_CLIENT_DROP && wmEvent == BN_CLICKED) {
      const bool show =
          SendMessageW(g_hComboClient, CB_GETDROPPEDSTATE, 0, 0) == 0;
      FocusClientEdit();
      SendMessageW(g_hComboClient, CB_SHOWDROPDOWN, show, 0);
      InvalidateRect(g_hBtnClientDrop, nullptr, TRUE);
    } else if (wmId == IDC_BTN_TEMP && wmEvent == BN_CLICKED) {
      GuiOpenTmp();
    } else if (wmId == IDC_BTN_PIN && wmEvent == BN_CLICKED) {
      ToggleSelectedClientPin();
    } else if (wmId == IDC_BTN_RESTORE_TABS && wmEvent == BN_CLICKED) {
      ToggleRestoreTabsForSelectedClient();
    } else if (wmId == IDC_BTN_CONFIG && wmEvent == BN_CLICKED) {
      UINT cmd = ShowConfigMenuFromButton(hWnd);
      if (cmd)
        SendMessageW(hWnd, WM_COMMAND, cmd, 0);
    } else if (wmId == IDM_CTX_SET_PROFILE_ICON) {
      GuiSetIcon();
    } else if (wmId == IDM_CTX_REMOVE_ICON) {
      GuiRemoveIcon();
    } else if (wmId == IDM_CTX_RESET_PROFILE) {
      GuiProfReset();
    } else if (wmId == IDM_CTX_DELETE_PROFILE) {
      GuiProfDel();
    } else if (wmId == IDM_CTX_CLEANUP_INACTIVE) {
      GuiCleanupInactiveClients();
    } else if (wmId == IDM_CTX_DELETE_MULTIPLE) {
      GuiCleanupInactiveClients(true);
    } else if (wmId == IDM_CTX_RENAME_PROFILE) {
      GuiRenameClient();
    } else if (wmId == IDM_CTX_ARCHIVE_PROFILE) {
      GuiArchiveClient();
    } else if (wmId == IDM_CTX_ARCHIVED_CLIENTS) {
      ShowArchivedClientsMenu(hWnd);
    } else if (wmId == IDM_CTX_CREATE_SHORTCUT) {
      const std::wstring clientName =
          GetSelectedClientNameSanitized(false);
      if (!clientName.empty())
        CreateClientDesktopShortcut(clientName, g_selectedBrowser);
    } else if (wmId == IDM_CTX_EDIT_DEFAULT_PROFILE) {
      GuiOpenDef();
    } else if (wmId == IDM_CTX_VACUUM_PROFILE) {
      GuiProfVacuum();
    } else if (wmId == IDM_CTX_FETCH_ICON) {
      GuiAutoIcon();
    } else if (wmId == IDM_CTX_THEME_COLOR) {
      ShowThemeDialog();
    } else if (wmId == IDM_CTX_CLIENT_TITLE_FIRST) {
      const bool previousValue = g_bClientTitleFirst.load();
      g_bClientTitleFirst = !previousValue;
      if (!SaveClientTitlePreference()) {
        g_bClientTitleFirst = previousValue;
      } else {
        EnsureWatcherIsRunning();
      }
      UpdateConfigMenuEnabledState();
    } else if (wmId == IDM_CTX_EXPORT_ALL) {
      GuiProfExportAll();
    } else if (wmId == IDM_CTX_RESTORE_ALL) {
      GuiProfRestoreAll();
    } else if (wmId == IDM_GUIDED_WALKTHROUGH) {
      ShowGuidedWalkthrough(false);
    } else if (wmId == IDM_WHATS_NEW) {
      ShowGuidedWalkthrough(true);
    } else if (wmId == IDM_QUICK_TOUR) {
      ShowQuickTour();
    } else if (wmId == IDM_ABOUT) {
      ShowAboutDialog();
    } else if (wmId == IDM_BROWSER_EDGE) {
      g_selectedBrowser = BrowserKind::Edge;
      SaveBrowserPreference();
      UpdateRestoreTabsToggleState();
      UpdateConfigMenuEnabledState();
      InvalidateRect(g_hGui, &g_rcUtilityBar, TRUE);
    } else if (wmId == IDM_BROWSER_CHROME) {
      g_selectedBrowser = BrowserKind::Chrome;
      SaveBrowserPreference();
      UpdateRestoreTabsToggleState();
      UpdateConfigMenuEnabledState();
      InvalidateRect(g_hGui, &g_rcUtilityBar, TRUE);
    } else if (wmId == IDM_BROWSER_BRAVE) {
      g_selectedBrowser = BrowserKind::Brave;
      SaveBrowserPreference();
      UpdateRestoreTabsToggleState();
      UpdateConfigMenuEnabledState();
      InvalidateRect(g_hGui, &g_rcUtilityBar, TRUE);
    } else if (wmId == IDM_BROWSER_FIREFOX) {
      g_selectedBrowser = BrowserKind::Firefox;
      SaveBrowserPreference();
      UpdateRestoreTabsToggleState();
      UpdateConfigMenuEnabledState();
      InvalidateRect(g_hGui, &g_rcUtilityBar, TRUE);
    } else {
    }

    return 0;
  }
  case WM_APP_SHOW_GUIDE:
    ShowGuidedWalkthrough(false, wParam != 0);
    return 0;
  case WM_APP_REFRESH_QUICK_TOUR:
    RefreshQuickTourPlacement();
    return 0;
  case WM_LBUTTONDOWN: {
    if (!g_bUiEnabled)
      return 0;
    POINT point{(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
    const int pinnedClient = HitTestPinnedClient(point);
    if (pinnedClient >= 0) {
      StartMainDragCandidate(hWnd, MainDragKind::PinnedClient, pinnedClient,
                             point);
      return 0;
    }
    if (HitTestSessionClose(hWnd, point) < 0) {
      const int sessionTab = HitTestSessionTab(hWnd, point);
      if (sessionTab > 0) {
        StartMainDragCandidate(hWnd, MainDragKind::SessionTab, sessionTab,
                               point);
        return 0;
      }
    }
    return 0;
  }
  case WM_MOUSEMOVE: {
    POINT point{(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
    UpdateMainDrag(hWnd, point, wParam);
    const int hotPinned =
        g_bUiEnabled ? HitTestPinnedClient(point) : -1;
    const bool hotOverflow =
        g_bUiEnabled && HitTestPinnedOverflow(point);
    const bool hotBrowser =
        g_bUiEnabled && PtInRect(&g_rcBrowserSelector, point);
    if (hotPinned != g_iHotPinnedClient ||
        hotOverflow != g_bHotPinnedOverflow ||
        hotBrowser != g_bHotBrowserSelector) {
      g_iHotPinnedClient = hotPinned;
      g_bHotPinnedOverflow = hotOverflow;
      g_bHotBrowserSelector = hotBrowser;
      InvalidateRect(hWnd, &g_rcPinnedArea, TRUE);
      InvalidateRect(hWnd, &g_rcUtilityBar, FALSE);
    }
    TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0};
    TrackMouseEvent(&tracking);
    return 0;
  }
  case WM_MOUSELEAVE:
    if (g_iHotPinnedClient >= 0 || g_bHotPinnedOverflow ||
        g_bHotBrowserSelector) {
      g_iHotPinnedClient = -1;
      g_bHotPinnedOverflow = false;
      g_bHotBrowserSelector = false;
      InvalidateRect(hWnd, &g_rcPinnedArea, TRUE);
      InvalidateRect(hWnd, &g_rcUtilityBar, FALSE);
    }
    return 0;
  case WM_LBUTTONUP: {
    int x = (int)(short)LOWORD(lParam);
    int y = (int)(short)HIWORD(lParam);
    POINT pt{x, y};
    if (FinishMainDrag(hWnd, pt))
      return 0;
    const int iCloseTab = HitTestSessionClose(hWnd, pt);
    if (iCloseTab > 0) {
      CloseSessionTab(iCloseTab);
      return 0;
    }
    if (HitTestSessionOverflow(hWnd, pt)) {
      ShowSessionOverflowMenu(hWnd);
      return 0;
    }
    const int iHitTab = HitTestSessionTab(hWnd, pt);
    if (iHitTab >= 0) {
      SelectSessionTab(iHitTab, true);
      return 0;
    }
    if (g_bUiEnabled && PtInRect(&g_rcBrowserSelector, pt)) {
      const UINT command = ShowBrowserMenuFromSelector(hWnd);
      if (command)
        SendMessageW(hWnd, WM_COMMAND, command, 0);
      return 0;
    }
    if (g_bUiEnabled && HitTestPinnedOverflow(pt)) {
      ShowPinnedOverflowMenu(hWnd);
      return 0;
    }
    const int pinnedClient = g_bUiEnabled ? HitTestPinnedClient(pt) : -1;
    if (pinnedClient >= 0 && pinnedClient < (int)g_pinnedClients.size()) {
      OpenPinnedClient(g_pinnedClients[pinnedClient]);
      return 0;
    }
    return 0;
  }
  case WM_CAPTURECHANGED:
    if (g_mainDragKind != MainDragKind::None) {
      const bool savePinnedOrder = g_bPinnedOrderChanged;
      g_mainDragKind = MainDragKind::None;
      g_iMainDragIndex = -1;
      g_bMainDragActive = false;
      g_bPinnedOrderChanged = false;
      FinishPinnedDragPersistence(savePinnedOrder);
    }
    return 0;
  case WM_RBUTTONUP: {
    POINT point{(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
    if (!g_bUiEnabled)
      return 0;
    if (HitTestPinnedOverflow(point)) {
      ShowPinnedOverflowMenu(hWnd, true);
      return 0;
    }
    const int pinnedClient = HitTestPinnedClient(point);
    if (pinnedClient >= 0 && pinnedClient < (int)g_pinnedClients.size()) {
      POINT screenPoint = point;
      ClientToScreen(hWnd, &screenPoint);
      ShowPinnedClientOptionsMenu(hWnd, g_pinnedClients[pinnedClient],
                                  screenPoint);
      return 0;
    }
    return 0;
  }
  case WM_SETCURSOR: {
    if (LOWORD(lParam) == HTCLIENT) {
      POINT pt{};
      GetCursorPos(&pt);
      ScreenToClient(hWnd, &pt);
      if ((g_bUiEnabled && PtInRect(&g_rcBrowserSelector, pt)) ||
          (g_bUiEnabled &&
           (HitTestPinnedClient(pt) >= 0 || HitTestPinnedOverflow(pt)))) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
  }
  case WM_PAINT: {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hWnd, &ps);
    DrawMainSurface(hdc);
    DrawSessionTabs(hWnd, hdc);
    DrawPinnedClients(hdc);
    EndPaint(hWnd, &ps);
    return 0;
  }
  case WM_DRAWITEM: {
    const DRAWITEMSTRUCT *vDraw = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
    if (vDraw) {
      DrawOwnerDrawItem(*vDraw);
    }
    return TRUE;
  }
  case WM_MEASUREITEM: {
    MEASUREITEMSTRUCT *vMeasure = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
    if (vMeasure) {
      if (vMeasure->CtlType == ODT_COMBOBOX) {
        vMeasure->itemHeight =
            (UINT)(g_iComboItemHeight > 0 ? g_iComboItemHeight
                                          : ScaleByDpi(20, g_uiDpi));
        return TRUE;
      }
      if (vMeasure->CtlType == ODT_MENU) {
        MeasureThemedMenuItem(*vMeasure);
        return TRUE;
      }
    }
    break;
  }
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLORSTATIC: {
    return (INT_PTR)HandleThemeCtlColor(message, (HDC)wParam, (HWND)lParam);
  }
  case WM_ERASEBKGND: {
    HDC hdc = (HDC)wParam;
    RECT rc{};
    GetClientRect(hWnd, &rc);
    const int iSaved = SaveDC(hdc);
    if (g_hIconPreview) {
      RECT rcIcon{};
      GetWindowRect(g_hIconPreview, &rcIcon);
      MapWindowPoints(nullptr, hWnd, (LPPOINT)&rcIcon, 2);
      ExcludeClipRect(hdc, rcIcon.left, rcIcon.top, rcIcon.right,
                      rcIcon.bottom);
    }
    if (g_hLblIcon) {
      RECT rcLbl{};
      GetWindowRect(g_hLblIcon, &rcLbl);
      MapWindowPoints(nullptr, hWnd, (LPPOINT)&rcLbl, 2);
      ExcludeClipRect(hdc, rcLbl.left, rcLbl.top, rcLbl.right, rcLbl.bottom);
    }
    FillRect(hdc, &rc,
             g_hbrThemeWindow ? g_hbrThemeWindow
                              : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE));
    RestoreDC(hdc, iSaved);
    return 1;
  }
  case WM_APP_PROGRESS_SHOW: {
    auto *p = reinterpret_cast<CtProgressPayload *>(lParam);
    if (p) {
      ProgressUI_Show(g_hGui ? g_hGui : hWnd, p->text, p->percent);
      delete p;
    }
    return 0;
  }
  case WM_APP_PROGRESS_UPDATE: {
    auto *p = reinterpret_cast<CtProgressPayload *>(lParam);
    if (p) {
      if (p->text.empty())
        ProgressUI_Update(p->percent);
      else
        ProgressUI_Update(p->text, p->percent);
      delete p;
    }
    return 0;
  }
  case WM_APP_PROGRESS_HIDE: {
    ProgressUI_Hide();
    return 0;
  }
  case WM_APP_SESSION_STARTED: {
    DWORD pid = (DWORD)wParam;
    std::wstring *pName = reinterpret_cast<std::wstring *>(lParam);
    if (pName) {
      OnSessionStarted(pid, *pName);
      delete pName;
    }
    return 0;
  }
  case WM_APP_PROFILE_EXITED: {
    std::unique_ptr<ProfileExitPayload> payload(
        reinterpret_cast<ProfileExitPayload *>(lParam));
    if (payload)
      HandleProfileExitOnUiThread(*payload);
    return 0;
  }
  case WM_APP_LAUNCH_FAILED: {
    std::unique_ptr<std::wstring> message(
        reinterpret_cast<std::wstring *>(lParam));
    if (message && !message->empty()) {
      MessageBoxW(hWnd, message->c_str(), L"Cannot Open Profile",
                  MB_OK | MB_ICONERROR);
    }
    return 0;
  }
  case WM_APP_PROFILE_MONITOR_FAILED: {
    std::unique_ptr<std::wstring> message(
        reinterpret_cast<std::wstring *>(lParam));
    if (g_bExitWhenProfilesClose) {
      g_bExitWhenProfilesClose = false;
      SetUiState(true);
    }
    if (message && !message->empty()) {
      MessageBoxW(hWnd, message->c_str(), L"Browser Monitoring Error",
                  MB_OK | MB_ICONERROR);
    }
    return 0;
  }
  case WM_APP_FINALIZE_LAYOUT: {
    ResizeMainWindowForDpi(hWnd, GetDpiForWindow(hWnd));
    RefreshQuickTourPlacement();
    return 0;
  }
  case WM_APP_RESTACK_CLIENT_SELECTOR: {
    g_bClientSelectorRestackPending = false;
    if (g_hComboClient) {
      HideClientComboChrome(g_hComboClient);
      LayoutClientComboChildren(g_hComboClient);
      StackClientSelectorWindows();
    }
    if (g_hClientEditSurface) {
      RedrawWindow(g_hClientEditSurface, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_FRAME);
    }
    if (g_hClientEdit) {
      RedrawWindow(g_hClientEdit, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_FRAME);
    }
    return 0;
  }
  case WM_APP_QA_REORDER_PINNED: {
    if (!g_bQaInstance)
      return FALSE;
    const int fromIndex = static_cast<int>(wParam);
    const int toIndex = static_cast<int>(lParam);
    if (fromIndex < 0 || toIndex < 0 ||
        fromIndex >= static_cast<int>(g_pinnedClients.size()) ||
        toIndex >= static_cast<int>(g_pinnedClients.size()) ||
        fromIndex == toIndex) {
      return FALSE;
    }
    const std::vector<std::wstring> originalPinnedClients = g_pinnedClients;
    MovePinnedClientForDrag(fromIndex, toIndex);
    if (!SavePinnedClients()) {
      g_pinnedClients = originalPinnedClients;
      BuildPinnedClientRects();
      InvalidateRect(g_hGui, &g_rcPinnedArea, TRUE);
      g_iMainDragIndex = -1;
      g_bPinnedOrderChanged = false;
      return FALSE;
    }
    g_iMainDragIndex = -1;
    g_bPinnedOrderChanged = false;
    return TRUE;
  }
  case WM_APP_QA_RESTORE_ARCHIVED: {
    if (!g_bQaInstance || wParam >= g_archivedClients.size())
      return FALSE;
    return RestoreArchivedClient(g_archivedClients[(size_t)wParam]) ? TRUE
                                                                    : FALSE;
  }
  case WM_APP_QA_CREATE_SHORTCUT: {
    if (!g_bQaInstance)
      return FALSE;
    const std::wstring clientName = GetSelectedClientNameSanitized(false);
    return CreateClientDesktopShortcut(clientName, g_selectedBrowser, false)
               ? TRUE
               : FALSE;
  }
  case WM_APP_QA_RENAME_CLIENT: {
    if (!g_bQaInstance)
      return FALSE;
    wchar_t newName[256]{};
    GetPrivateProfileStringW(L"qa", L"rename_to", L"", newName,
                             (DWORD)std::size(newName),
                             g_sConfigPath.c_str());
    std::wstring errorMessage;
    return RenameClientProfile(GetSelectedClientNameSanitized(false), newName,
                               errorMessage)
               ? TRUE
               : FALSE;
  }
  case WM_APP_QA_ARCHIVE_CLIENT: {
    if (!g_bQaInstance)
      return FALSE;
    std::wstring errorMessage;
    return ArchiveClientProfile(GetSelectedClientNameSanitized(false),
                                errorMessage)
               ? TRUE
               : FALSE;
  }
  case WM_APP_QA_CAN_EXPORT: {
    if (!g_bQaInstance)
      return FALSE;
    std::wstring errorDetails;
    return ProbeAnySitesProfileInUse(errorDetails) ==
                   ProfileUseState::NotInUse
               ? TRUE
               : FALSE;
  }
  case WM_APP_TASK_COMPLETE: {
    // A synchronous fallback can be sent by the launch worker when the normal
    // owned PostMessage handoff fails. It must not attempt to join itself.
    if (wParam == 0)
      StopLaunchWorker();
    std::wstring clientName;
    if (lParam) {
      std::wstring *pName = reinterpret_cast<std::wstring *>(lParam);
      clientName = *pName;
      delete pName;
    } else {
      clientName = SanitizeName(GetClientInputText());
    }
    SetUiState(true);

    if (!clientName.empty() && _wcsicmp(clientName.c_str(), L"Temp") != 0 &&
        _wcsicmp(clientName.c_str(), L"Default") != 0) {
      g_sClientSel = clientName;
      UpdateClientsComboBox();
      LRESULT idx = SendMessage(g_hComboClient, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                (LPARAM)clientName.c_str());
      if (idx != CB_ERR) {
        SendMessage(g_hComboClient, CB_SETCURSEL, (WPARAM)idx, 0);
      }
      SetClientInputText(clientName);
    }
    UpdateIconPreviewForSelection(true);
    FocusClientEdit();
    if (g_bClosePendingDuringLaunch) {
      g_bClosePendingDuringLaunch = false;
      PostMessageW(hWnd, WM_CLOSE, 0, 0);
    }
    return 0;
  }
  case WM_APP_PROFILE_SHUTDOWN_FAILED: {
    if (!g_bExitWhenProfilesClose)
      return 0;

    bool hasActiveProfiles = false;
    {
      std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
      hasActiveProfiles = !g_activeProfiles.empty();
    }
    if (!hasActiveProfiles) {
      if (g_sessions.empty())
        DestroyWindow(hWnd);
      return 0;
    }

    g_bExitWhenProfilesClose = false;
    SetUiState(true);
    MessageBoxW(
        hWnd,
        L"ctSpaces could not close one or more browser processes. Those "
        L"clients remain open.\n\nClose them manually, then try exiting again.",
        L"Exit ctSpaces", MB_OK | MB_ICONWARNING);
    return 0;
  }
  case WM_CLOSE: {
    if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog))
      DismissQuickTour(false);
    if (g_bArchiveTaskInProgress) {
      MessageBeep(MB_ICONINFORMATION);
      return 0;
    }
    if (g_isLaunchInFlight.load()) {
      g_bClosePendingDuringLaunch = true;
      SetUiState(false);
      return 0;
    }
    bool hasActiveProfiles = false;
    {
      std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
      if (!g_activeProfiles.empty()) {
        hasActiveProfiles = true;
      }
    }
    if (hasActiveProfiles) {
      if (g_bExitWhenProfilesClose)
        return 0;

      const int result = MessageBoxW(
          hWnd,
          L"Close all open client browser windows and exit ctSpaces?\n\n"
          L"ctSpaces will give each browser a moment to save its session and "
          L"sign-ins, then close it without additional browser prompts.\n\n"
          L"Unsaved page changes or active downloads may be interrupted.",
          L"Exit ctSpaces", MB_YESNO | MB_ICONQUESTION);
      if (result == IDYES) {
        g_bExitWhenProfilesClose = true;
        SetUiState(false);
        RequestCloseAllProfiles();
      }
    } else {
      DestroyWindow(hWnd);
    }
    return 0;
  }
  case WM_QUERYENDSESSION:
    if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog))
      DismissQuickTour(false);
    return TRUE;
  case WM_ENDSESSION:
    if (wParam && g_hQuickTourDialog && IsWindow(g_hQuickTourDialog))
      DismissQuickTour(false);
    return 0;
  case WM_DESTROY: {
    DismissQuickTourForOwnerClosing();
    for (auto hbmp : g_menuBitmaps) {
      if (hbmp)
        DeleteObject(hbmp);
    }
    g_menuBitmaps.clear();
    ClearMenuItemData();
    if (g_hConfigMenu) {
      DestroyMenu(g_hConfigMenu);
      g_hConfigMenu = nullptr;
    }
    if (g_hIconBtnTemp) {
      DestroyIcon(g_hIconBtnTemp);
      g_hIconBtnTemp = nullptr;
    }
    if (g_hIconBtnConfig) {
      DestroyIcon(g_hIconBtnConfig);
      g_hIconBtnConfig = nullptr;
    }
    if (g_hIconPreviewHandle) {
      DestroyIcon(g_hIconPreviewHandle);
      g_hIconPreviewHandle = nullptr;
    }
    if (g_hMenuTip) {
      DestroyWindow(g_hMenuTip);
      g_hMenuTip = nullptr;
    }
    if (g_hBtnTmpProfTip) {
      DestroyWindow(g_hBtnTmpProfTip);
      g_hBtnTmpProfTip = nullptr;
    }
    if (g_hBtnConfigTip) {
      DestroyWindow(g_hBtnConfigTip);
      g_hBtnConfigTip = nullptr;
    }
    if (g_hBtnPinTip) {
      DestroyWindow(g_hBtnPinTip);
      g_hBtnPinTip = nullptr;
    }
    if (g_hIconPreviewTip) {
      DestroyWindow(g_hIconPreviewTip);
      g_hIconPreviewTip = nullptr;
    }
    if (g_hFontAboutSmall) {
      DeleteObject(g_hFontAboutSmall);
      g_hFontAboutSmall = nullptr;
    }
    if (g_hFont)
      DeleteObject(g_hFont);
    g_hFont = nullptr;
    if (g_hFontLabel)
      DeleteObject(g_hFontLabel);
    g_hFontLabel = nullptr;
    if (g_hFontStrong)
      DeleteObject(g_hFontStrong);
    g_hFontStrong = nullptr;
    if (g_hFontClient)
      DeleteObject(g_hFontClient);
    g_hFontClient = nullptr;
    ClearThemeBrushes();
    if (g_hIconColorLight) {
      DestroyIcon(g_hIconColorLight);
      g_hIconColorLight = nullptr;
    }
    if (g_hIconColorDark) {
      DestroyIcon(g_hIconColorDark);
      g_hIconColorDark = nullptr;
    }
    if (g_hBmpColorLight) {
      DeleteObject(g_hBmpColorLight);
      g_hBmpColorLight = nullptr;
    }
    if (g_hBmpColorDark) {
      DeleteObject(g_hBmpColorDark);
      g_hBmpColorDark = nullptr;
    }
    g_hGui = nullptr;
    PostQuitMessage(0);
    break;
  }
  case WM_ACTIVATE: {
    if (LOWORD(wParam) != WA_INACTIVE) {
      FocusClientEdit();
    }
    return 0;
  }
  case WM_SETFOCUS: {
    FocusClientEdit();
    return 0;
  }
  case WM_DPICHANGED: {
    const UINT dpi = HIWORD(wParam);
    const RECT *rc = reinterpret_cast<RECT *>(lParam);
    ApplyDpiScaling(hWnd, dpi, rc);
    RefreshQuickTourPlacement();
    return 0;
  }
  case WM_SIZE:
    if (wParam == SIZE_MINIMIZED) {
      if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog))
        DismissQuickTour(false);
      return 0;
    }
    LayoutMainGui(hWnd);
    RefreshQuickTourPlacement();
    return 0;
  case WM_MOVE:
    RefreshQuickTourPlacement();
    return 0;

  case WM_MENUSELECT:
    EnsureMenuTooltips(hWnd);
    HandleMenuSelect(hWnd, wParam, lParam);
    return 0;

  case WM_EXITMENULOOP:
    HideMenuTooltip();
    return 0;

  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

void GuiProfOpen() {
  SetUiState(false);
  std::wstring clientName = GetSelectedClientNameSanitized(false);
  const BrowserKind browser = g_selectedBrowser;
  if (clientName.empty()) {
    MessageBox(g_hGui, L"Please select or enter a valid client name.",
               L"Input Error", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    if (g_activeProfiles.count(ClientBrowserKey{clientName, browser})) {
      const std::wstring message = std::format(
          L"This client is already open in {}.",
          GetBrowserDisplayName(browser));
      MessageBoxW(g_hGui, message.c_str(), L"Already Running",
                  MB_OK | MB_ICONINFORMATION);
      SetUiState(true);
      return;
    }
  }
  LaunchProfileAsync(clientName, false, false, browser);
}

void GuiProfVacuum() {
  SetUiState(false);
  std::wstring clientName = GetSelectedClientNameSanitized(false);

  if (clientName.empty()) {
    MessageBox(g_hGui, L"Please select a client first.", L"Warning",
               MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  fs::path profileRoot;
  if (!TryGetSafeClientProfilePath(clientName, profileRoot)) {
    MessageBoxW(g_hGui,
                L"The selected client folder is not a safe local profile.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  std::wstring profileUseError;
  if (ProbeClientProfilesInUse(clientName, profileUseError) !=
      ProfileUseState::NotInUse) {
    MessageBoxW(
        g_hGui,
        (L"Cannot vacuum this client because ctSpaces could not prove that "
         L"every browser using it is closed.\n\n" +
         profileUseError)
            .c_str(),
        L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  if (MessageBox(
          g_hGui,
          L"This will clear temporary files (Cache, Code Cache, GPUCache, "
          L"etc.) to fix glitches and free space.\n\nYour cookies, "
          L"history, and logins will be PRESERVED.\n\nContinue?",
          L"Confirm Vacuum", MB_YESNO | MB_ICONQUESTION) != IDYES) {
    SetUiState(true);
    return;
  }

  std::vector<ClientBrowserProfileLocation> profileLocations;
  if (!CollectClientBrowserProfileLocations(clientName, profileRoot,
                                             profileLocations)) {
    MessageBoxW(g_hGui,
                L"The client's browser-profile layout is unsafe or "
                L"conflicting. No cache data was removed.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  profileUseError.clear();
  if (ProbeClientProfilesInUse(clientName, profileUseError) !=
      ProfileUseState::NotInUse) {
    MessageBoxW(
        g_hGui,
        (L"A browser became active or could not be reverified after "
         L"confirmation. No cache data was removed.\n\n" +
         profileUseError)
            .c_str(),
        L"Vacuum Cancelled", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  int deletedCount = 0;
  int failedCount = 0;
  for (const auto &location : profileLocations) {
    const std::vector<fs::path> cachePaths =
        IsChromiumBrowser(location.browser)
            ? std::vector<fs::path>{
                  L"Default/Cache", L"Default/Code Cache",
                  L"Default/GPUCache", L"Default/Service Worker/CacheStorage",
                  L"Default/Service Worker/ScriptCache", L"Default/DawnCache",
                  L"Default/GrShaderCache", L"Default/ShaderCache"}
            : std::vector<fs::path>{L"cache2", L"startupCache",
                                    L"shader-cache", L"thumbnails"};
    for (const auto &relativePath : cachePaths) {
      std::vector<ClientBrowserProfileLocation> currentLocations;
      const bool locationStillValid =
          CollectClientBrowserProfileLocations(clientName, profileRoot,
                                               currentLocations) &&
          std::ranges::any_of(
              currentLocations, [&](const ClientBrowserProfileLocation &item) {
                return item.browser == location.browser &&
                       _wcsicmp(item.profileRoot.c_str(),
                                location.profileRoot.c_str()) == 0;
              });
      if (!locationStillValid) {
        ++failedCount;
        continue;
      }
      bool removed = false;
      if (RemoveNestedCacheDirectory(location.profileRoot, relativePath,
                                     removed) &&
          removed) {
        ++deletedCount;
      } else {
        const DWORD attributes = GetFileAttributesW(
            (location.profileRoot / relativePath).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
          ++failedCount;
      }
    }
  }

  std::wstring msg = std::format(
      L"Vacuum complete. Cleared {} cache location{} across {} browser "
      L"profile{}.",
      deletedCount, deletedCount == 1 ? L"" : L"s", profileLocations.size(),
      profileLocations.size() == 1 ? L"" : L"s");
  if (failedCount > 0)
    msg += std::format(L"\n\n{} cache location{} could not be removed safely.",
                       failedCount, failedCount == 1 ? L"" : L"s");
  MessageBoxW(g_hGui, msg.c_str(), failedCount ? L"Vacuum Completed with Warnings"
                                               : L"Success",
              MB_OK | (failedCount ? MB_ICONWARNING : MB_ICONINFORMATION));
  SetUiState(true);
}

static void RetireCachedIcons(const std::wstring &clientName,
                              IconCacheSet &cacheSet) {
  for (auto &[px, hIcon] : cacheSet.byPx) {
    if (hIcon)
      g_retiredIconHandles[clientName].push_back(hIcon);
  }
  cacheSet.byPx.clear();
}

static void DestroyRetiredIconsForClientLocked(
    const std::wstring &clientName) {
  const auto retired = g_retiredIconHandles.find(clientName);
  if (retired == g_retiredIconHandles.end())
    return;
  for (HICON icon : retired->second) {
    if (icon)
      DestroyIcon(icon);
  }
  g_retiredIconHandles.erase(retired);
}

static void DestroyAllRetiredIconsLocked() {
  for (auto &[clientName, icons] : g_retiredIconHandles) {
    for (HICON icon : icons) {
      if (icon)
        DestroyIcon(icon);
    }
  }
  g_retiredIconHandles.clear();
}

static void RefreshIconCacheSource(const std::wstring &clientName,
                                   IconCacheSet &cacheSet,
                                   const fs::path &iconPath) {
  bool sourceExists = false;
  uintmax_t sourceSize = 0;
  fs::file_time_type::duration::rep sourceWriteStamp = 0;
  try {
    sourceExists = IsSafeExistingRegularFile(iconPath);
    if (sourceExists) {
      sourceSize = fs::file_size(iconPath);
      sourceWriteStamp =
          fs::last_write_time(iconPath).time_since_epoch().count();
    }
  } catch (...) {
    sourceExists = false;
  }

  if (!cacheSet.sourceInitialized ||
      cacheSet.sourceExists != sourceExists ||
      cacheSet.sourceSize != sourceSize ||
      cacheSet.sourceWriteStamp != sourceWriteStamp) {
    RetireCachedIcons(clientName, cacheSet);
    cacheSet.sourceInitialized = true;
    cacheSet.sourceExists = sourceExists;
    cacheSet.sourceSize = sourceSize;
    cacheSet.sourceWriteStamp = sourceWriteStamp;
  }
}

static void ClearClientIconCache(const std::wstring &clientName) {
  // The watcher intentionally drops the cache lock before sending HICONs to a
  // browser window. Its active-profile snapshot can therefore outlive the last
  // map entry briefly. Serialize with watcher startup and destroy immediately
  // only when the watcher has proved itself quiescent; otherwise retirement is
  // reclaimed by the watcher's no-active/shutdown path after all raw uses end.
  std::lock_guard<std::mutex> lifecycleLock(g_watcherLifecycleMutex);
  const bool watcherQuiescent = !g_isWatcherRunning.load();
  std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
  auto it = g_iconCache.find(clientName);
  if (it != g_iconCache.end()) {
    RetireCachedIcons(clientName, it->second);
    g_iconCache.erase(it);
  }
  if (watcherQuiescent)
    DestroyRetiredIconsForClientLocked(clientName);
}

static fs::path MakeClientIconWorkPath(const fs::path &destIconPath,
                                       const std::wstring &label,
                                       const std::wstring &extension) {
  return destIconPath.parent_path() /
         std::format(L".client-icon-{}-{}-{}{}", label,
                     GetCurrentProcessId(), GetTickCount64(), extension);
}

static bool CommitPreparedClientIcon(const fs::path &preparedIconPath,
                                     const fs::path &destIconPath,
                                     std::wstring &errorDetails) {
  if (!IsSafeExistingRegularFile(preparedIconPath) ||
      !IsSafeExistingDirectory(destIconPath.parent_path()) ||
      (fs::exists(destIconPath) &&
       !IsSafeExistingRegularFile(destIconPath))) {
    errorDetails = L"The prepared or destination icon path is unsafe.";
    return false;
  }
  HICON probe = LoadIconFromIcoBestDownscale(preparedIconPath, 32);
  if (!probe) {
    errorDetails = L"The selected file did not produce a valid Windows icon.";
    return false;
  }
  DestroyIcon(probe);

  SetFileAttributesW(preparedIconPath.c_str(), FILE_ATTRIBUTE_NORMAL);
  if (fs::exists(destIconPath))
    SetFileAttributesW(destIconPath.c_str(), FILE_ATTRIBUTE_NORMAL);

  if (!MoveFileExW(preparedIconPath.c_str(), destIconPath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    errorDetails = std::format(
        L"Windows could not replace the client icon (error {}).", error);
    return false;
  }
  return true;
}

static std::wstring NormalizeFaviconDomain(std::wstring value) {
  const std::wstring whitespace = L" \t\r\n";
  const size_t first = value.find_first_not_of(whitespace);
  if (first == std::wstring::npos)
    return L"";
  value.erase(0, first);
  value.erase(value.find_last_not_of(whitespace) + 1);

  if (value.size() >= 8 && _wcsnicmp(value.c_str(), L"https://", 8) == 0)
    value.erase(0, 8);
  else if (value.size() >= 7 && _wcsnicmp(value.c_str(), L"http://", 7) == 0)
    value.erase(0, 7);

  const size_t pathStart = value.find_first_of(L"/?#");
  if (pathStart != std::wstring::npos)
    value.erase(pathStart);
  while (!value.empty() && value.back() == L'.')
    value.pop_back();

  if (value.empty() || value.size() > 253 ||
      value.find_first_of(whitespace) != std::wstring::npos)
    return L"";
  std::transform(value.begin(), value.end(), value.begin(), ::towlower);

  size_t labelStart = 0;
  bool hasDot = false;
  while (labelStart < value.size()) {
    const size_t dot = value.find(L'.', labelStart);
    const size_t labelEnd = dot == std::wstring::npos ? value.size() : dot;
    const size_t labelLength = labelEnd - labelStart;
    if (labelLength == 0 || labelLength > 63 ||
        value[labelStart] == L'-' || value[labelEnd - 1] == L'-')
      return L"";
    for (size_t i = labelStart; i < labelEnd; ++i) {
      const wchar_t ch = value[i];
      if (!((ch >= L'a' && ch <= L'z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'-')) {
        return L"";
      }
    }
    if (dot == std::wstring::npos)
      break;
    hasDot = true;
    labelStart = dot + 1;
  }
  if (!hasDot)
    return L"";
  return value;
}

struct WinHttpHandleCloser {
  void operator()(void *handle) const noexcept {
    if (handle)
      WinHttpCloseHandle(handle);
  }
};

using UniqueWinHttpHandle = std::unique_ptr<void, WinHttpHandleCloser>;

static bool DownloadUrlToFile(const std::wstring &url,
                              const fs::path &destination,
                              std::wstring &errorDetails) {
  URL_COMPONENTSW components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = (DWORD)-1;
  components.dwHostNameLength = (DWORD)-1;
  components.dwUrlPathLength = (DWORD)-1;
  components.dwExtraInfoLength = (DWORD)-1;
  if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &components)) {
    errorDetails = std::format(L"The download URL is invalid (Windows error {}).",
                               GetLastError());
    return false;
  }

  if (!components.lpszHostName || components.dwHostNameLength == 0 ||
      (components.dwUrlPathLength > 0 && !components.lpszUrlPath) ||
      (components.dwExtraInfoLength > 0 && !components.lpszExtraInfo)) {
    errorDetails = L"The download URL has incomplete address components.";
    return false;
  }

  const std::wstring host(components.lpszHostName,
                          components.dwHostNameLength);
  std::wstring requestTarget;
  if (components.dwUrlPathLength > 0) {
    requestTarget.assign(components.lpszUrlPath,
                         components.dwUrlPathLength);
  }
  if (components.dwExtraInfoLength > 0) {
    requestTarget.append(components.lpszExtraInfo,
                         components.dwExtraInfoLength);
  }
  if (requestTarget.empty())
    requestTarget = L"/";

  UniqueWinHttpHandle session(WinHttpOpen(
      std::format(L"ctSpaces/{}", APP_VERSION).c_str(),
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    errorDetails = std::format(L"Could not start the downloader (Windows error {}).",
                               GetLastError());
    return false;
  }
  WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 15000);

  UniqueWinHttpHandle connection(
      WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
  if (!connection) {
    errorDetails = std::format(L"Could not connect to the icon service (Windows error {}).",
                               GetLastError());
    return false;
  }

  const DWORD requestFlags =
      components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  const wchar_t *acceptTypes[] = {L"image/*", L"*/*", nullptr};
  UniqueWinHttpHandle request(WinHttpOpenRequest(
      connection.get(), L"GET", requestTarget.c_str(), nullptr,
      WINHTTP_NO_REFERER, acceptTypes, requestFlags));
  if (!request) {
    errorDetails = std::format(L"Could not create the icon request (Windows error {}).",
                               GetLastError());
    return false;
  }

  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    errorDetails = std::format(L"The icon request failed (Windows error {}).",
                               GetLastError());
    return false;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (!WinHttpQueryHeaders(
          request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
          WINHTTP_NO_HEADER_INDEX) ||
      statusCode < 200 || statusCode >= 300) {
    std::wstring finalUrl;
    DWORD urlBytes = 0;
    WinHttpQueryOption(request.get(), WINHTTP_OPTION_URL, nullptr, &urlBytes);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && urlBytes > 0) {
      std::vector<wchar_t> urlBuffer(urlBytes / sizeof(wchar_t) + 1, L'\0');
      if (WinHttpQueryOption(request.get(), WINHTTP_OPTION_URL,
                             urlBuffer.data(), &urlBytes)) {
        finalUrl = urlBuffer.data();
      }
    }
    errorDetails = std::format(
        L"The icon service returned HTTP status {}{}{}.", statusCode,
        finalUrl.empty() ? L"" : L" for ", finalUrl);
    return false;
  }

  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    errorDetails = L"The downloaded icon could not be saved to the client folder.";
    return false;
  }

  constexpr size_t kMaximumIconDownloadBytes = 5 * 1024 * 1024;
  size_t totalBytes = 0;
  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) {
      errorDetails = std::format(L"The icon download was interrupted (Windows error {}).",
                                 GetLastError());
      return false;
    }
    if (available == 0)
      break;
    if (totalBytes + available > kMaximumIconDownloadBytes) {
      errorDetails = L"The downloaded icon was unexpectedly large.";
      return false;
    }

    std::vector<char> buffer(available);
    DWORD bytesRead = 0;
    if (!WinHttpReadData(request.get(), buffer.data(), available, &bytesRead)) {
      errorDetails = std::format(L"The icon download was interrupted (Windows error {}).",
                                 GetLastError());
      return false;
    }
    if (bytesRead == 0)
      break;
    output.write(buffer.data(), bytesRead);
    if (!output) {
      errorDetails = L"The downloaded icon could not be written to disk.";
      return false;
    }
    totalBytes += bytesRead;
  }
  output.close();

  if (totalBytes == 0) {
    errorDetails = L"The icon service returned an empty response.";
    return false;
  }
  return true;
}

void GuiSetIcon() {
  SetUiState(false);

  std::wstring clientName = GetSelectedClientNameSanitized(false);

  if (clientName.empty()) {
    MessageBox(g_hGui, L"Please select a client first.", L"Warning",
               MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }
  fs::path clientRoot;
  if (!TryGetSafeClientProfilePath(clientName, clientRoot) ||
      !IsSafeExistingDirectory(clientRoot)) {
    MessageBoxW(g_hGui, L"Select an existing safe client first.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  std::wstring filter;
  std::wstring allSupportedExtensions = L"*.ico";
  std::vector<std::wstring> types = GetSupportedImageTypes();
  for (const auto &type : types) {
    allSupportedExtensions += L";*." + type;
  }

  filter += L"Supported Image Files (" + allSupportedExtensions + L")";
  filter += L'\0';
  filter += allSupportedExtensions;
  filter += L'\0';
  filter += L"Icon Files (*.ico)";
  filter += L'\0';
  filter += L"*.ico";
  filter += L'\0';
  filter += L"All Files (*.*)";
  filter += L'\0';
  filter += L"*.*";
  filter += L'\0';
  filter += L'\0';

  wchar_t szFile[MAX_PATH] = {0};
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hGui;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
  ofn.lpstrFilter = filter.c_str();
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

  if (GetOpenFileNameW(&ofn)) {
    fs::path sourcePath(ofn.lpstrFile);
    fs::path destIconPath = clientRoot / "client.ico";
    fs::path stagedIconPath;
    bool success = false;
    std::wstring errorDetails;

    try {
      if (!RevalidateSafeClientContainerPath(clientName, clientRoot))
        throw std::runtime_error("The client folder changed before icon staging");
      stagedIconPath =
          MakeClientIconWorkPath(destIconPath, L"manual", L".ico");
      std::error_code staleError;
      fs::remove(stagedIconPath, staleError);

      if (_wcsicmp(sourcePath.extension().c_str(), L".ico") == 0) {
        fs::copy_file(sourcePath, stagedIconPath,
                      fs::copy_options::overwrite_existing);
      } else {
        if (!ConvertImageToIcon(sourcePath, stagedIconPath))
          errorDetails = L"Could not convert image to icon format.";
      }

      if (errorDetails.empty()) {
        if (!RevalidateSafeClientContainerPath(clientName, clientRoot)) {
          errorDetails = L"The client folder changed before icon commit.";
        } else {
          success = CommitPreparedClientIcon(stagedIconPath, destIconPath,
                                             errorDetails);
        }
      }
    } catch (const fs::filesystem_error &e) {
      success = false;
      errorDetails = AnsiToWide(e.what());
    } catch (const std::exception &e) {
      success = false;
      errorDetails = AnsiToWide(e.what());
    } catch (...) {
      success = false;
      errorDetails = L"An unexpected error occurred while preparing the icon.";
    }

    if (!stagedIconPath.empty()) {
      std::error_code cleanupError;
      fs::remove(stagedIconPath, cleanupError);
    }

    if (success) {
      ClearClientIconCache(clientName);
      RefreshOpenClientWindowIcon(clientName, true);
      UpdateIconPreviewForSelection(false);
      MessageBox(g_hGui, L"Icon has been configured.", L"Success",
                 MB_OK | MB_ICONINFORMATION);
    } else {
      std::wstring errorMsg = L"Failed to set icon.";
      if (!errorDetails.empty())
        errorMsg += L"\n\nDetails: " + errorDetails;
      MessageBox(g_hGui, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
  }
  UpdateIconPreviewForSelection(false);
  SetUiState(true);
}

void GuiProfReset() {
  SetUiState(false);
  std::wstring clientName = GetSelectedClientNameSanitized(false);
  if (clientName.empty()) {
    MessageBox(g_hGui, L"Please select a client first.", L"Warning",
               MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }
  fs::path profilePath;
  if (!TryGetSafeClientProfilePath(clientName, profilePath)) {
    MessageBoxW(g_hGui,
                L"The selected client folder is not a safe local profile.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }
  if (IsClientActiveAnyBrowser(clientName)) {
    MessageBox(g_hGui, L"Cannot reset a client that is currently active.",
               L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  std::optional<BrowserKind> legacyBinding;
  const bool recognizedLayout = IsV2ClientContainer(profilePath) ||
                                IsSafeHybridClientRoot(profilePath,
                                                       &legacyBinding);
  MessageBoxW(
      g_hGui,
      recognizedLayout
          ? L"Reset is temporarily unavailable for multi-browser clients. "
            L"ctSpaces left every browser slot unchanged because the old "
            L"whole-client reset would destroy unrelated browser sessions."
          : L"The client layout is not recognized as a safe v2 or hybrid "
            L"container. Nothing was reset.",
      L"Reset Not Available", MB_OK | MB_ICONINFORMATION);
  SetUiState(true);
}

void GuiAutoIcon() {
  SetUiState(false);
  std::wstring clientName = GetSelectedClientNameSanitized(false);
  if (clientName.empty()) {
    MessageBox(g_hGui, L"Please select a client first.", L"Warning",
               MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  std::wstring prompt = L"Website domain";

  std::optional<std::wstring> enteredDomain =
      InputBoxWindow::Show(g_hGui, L"Auto-fetch Icon", prompt);
  if (!enteredDomain) {
    SetUiState(true);
    return; // User cancelled
  }

  std::wstring domain = NormalizeFaviconDomain(*enteredDomain);
  if (domain.empty()) {
    MessageBox(g_hGui, L"Enter a valid website domain, such as microsoft.com.",
               L"Invalid Domain", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }
  fs::path clientRoot;
  if (!TryGetSafeClientProfilePath(clientName, clientRoot) ||
      !IsSafeExistingDirectory(clientRoot)) {
    MessageBoxW(g_hGui, L"Select an existing safe client first.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  const fs::path destIcon = clientRoot / "client.ico";
  fs::path downloadPath;
  fs::path stagedIconPath;
  bool success = false;
  std::wstring errorDetails;

  try {
    if (!RevalidateSafeClientContainerPath(clientName, clientRoot))
      throw std::runtime_error("The client folder changed before icon download");
    downloadPath = MakeClientIconWorkPath(destIcon, L"download", L".png");
    stagedIconPath = MakeClientIconWorkPath(destIcon, L"fetched", L".ico");
    std::error_code staleError;
    fs::remove(downloadPath, staleError);
    fs::remove(stagedIconPath, staleError);

    const std::wstring downloadUrl =
        L"https://www.google.com/s2/favicons?domain=" + domain + L"&sz=64";
    if (!DownloadUrlToFile(downloadUrl, downloadPath, errorDetails)) {
      success = false;
    } else if (!ConvertImageToIcon(downloadPath, stagedIconPath)) {
      errorDetails = L"The downloaded image could not be converted to an icon.";
    } else if (!RevalidateSafeClientContainerPath(clientName, clientRoot)) {
      errorDetails = L"The client folder changed before icon commit.";
    } else {
      success = CommitPreparedClientIcon(stagedIconPath, destIcon, errorDetails);
    }
  } catch (const fs::filesystem_error &e) {
    errorDetails = AnsiToWide(e.what());
  } catch (const std::exception &e) {
    errorDetails = AnsiToWide(e.what());
  } catch (...) {
    errorDetails = L"An unexpected error occurred while fetching the icon.";
  }

  std::error_code cleanupError;
  if (!downloadPath.empty())
    fs::remove(downloadPath, cleanupError);
  if (!stagedIconPath.empty())
    fs::remove(stagedIconPath, cleanupError);

  if (success) {
    ClearClientIconCache(clientName);
    RefreshOpenClientWindowIcon(clientName, true);
    UpdateIconPreviewForSelection(false);
    MessageBox(g_hGui, L"Icon fetched!", L"Success", MB_OK);
  } else {
    std::wstring message = L"Failed to fetch an icon for that domain.";
    if (!errorDetails.empty())
      message += L"\n\nDetails: " + errorDetails;
    MessageBox(g_hGui, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
  }

  UpdateIconPreviewForSelection(false);
  SetUiState(true);
}

void GuiRemoveIcon() {
  SetUiState(false);
  std::wstring clientName = GetSelectedClientNameSanitized(false);

  if (clientName.empty()) {
    MessageBox(g_hGui, L"Please select a client first.", L"Warning",
               MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }
  fs::path clientRoot;
  if (!TryGetSafeClientProfilePath(clientName, clientRoot) ||
      !IsSafeExistingDirectory(clientRoot)) {
    MessageBoxW(g_hGui, L"Select an existing safe client first.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  if (MessageBox(g_hGui, L"Remove custom icon and revert to default?",
                  L"Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
    fs::path destIconPath = clientRoot / "client.ico";
    // Also remove cached png if any?
    fs::path destPngPath = clientRoot / "temp_icon.png";

    bool removed = false;
    try {
      if (!RevalidateSafeClientContainerPath(clientName, clientRoot))
        throw std::runtime_error("The client folder changed before icon removal");
      if (fs::exists(destIconPath)) {
        if (!IsSafeExistingRegularFile(destIconPath))
          throw std::runtime_error("The client icon path is unsafe");
        fs::remove(destIconPath);
        removed = true;
      }
      if (fs::exists(destPngPath)) {
        if (!IsSafeExistingRegularFile(destPngPath))
          throw std::runtime_error("The temporary icon path is unsafe");
        fs::remove(destPngPath); // Cleanup just in case
      }

      if (removed) {
        ClearClientIconCache(clientName);
        RefreshOpenClientWindowIcon(clientName, true);
        UpdateIconPreviewForSelection(false);
        MessageBox(g_hGui, L"Custom icon removed.", L"Success",
                   MB_OK | MB_ICONINFORMATION);
      } else {
        MessageBox(g_hGui, L"No custom icon found to remove.", L"Info",
                   MB_OK | MB_ICONINFORMATION);
      }
    } catch (const fs::filesystem_error &e) {
      MessageBox(g_hGui, AnsiToWide(e.what()).c_str(), L"Error",
                 MB_OK | MB_ICONERROR);
    }
  }

  UpdateIconPreviewForSelection(false);
  SetUiState(true);
}

void GuiOpenDef() {
  SetUiState(false);
  if (g_selectedBrowser == BrowserKind::Firefox) {
    MessageBoxW(g_hGui,
                L"The Default profile editor is Chromium-only for now. "
                L"Select Edge, Chrome, or Brave first.",
                L"Default Profile", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }
  if (IsClientActiveAnyBrowser(L"Default")) {
    MessageBox(g_hGui, L"The Default profile is already open.",
               L"Already Running", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }
  LaunchProfileAsync(L"Default", false, true, g_selectedBrowser);
}

static std::wstring GetBackupTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  struct tm buf {};
  localtime_s(&buf, &in_time_t);
  std::wstringstream ss;
  ss << std::put_time(&buf, L"%Y-%m-%d_%H%M%S");
  return ss.str();
}

static CtBackup::Result RunArchiveTaskWithProgress(
    const std::wstring &status,
    const std::function<CtBackup::Result(_7zUiCtx &)> &operation) {
  _7zUiCtx context;
  context.mainWnd = g_hGui;
  context.status = status;
  CtBackup::Result result{E_FAIL, L"The archive task did not complete."};

  auto invoke = [&]() {
    try {
      result = operation(context);
    } catch (const std::exception &error) {
      result = {E_FAIL, AnsiToWide(error.what())};
    } catch (...) {
      result = {E_FAIL, L"An unexpected archive error occurred."};
    }
  };

  ProgressUI_Show(g_hGui, status, -1);
  g_bArchiveTaskInProgress = true;
  const BOOL ownerWasEnabled = IsWindowEnabled(g_hGui);
  if (ownerWasEnabled)
    EnableWindow(g_hGui, FALSE);

  HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  bool repostQuit = false;
  int quitCode = 0;
  if (!completed) {
    invoke();
  } else {
    std::thread worker;
    try {
      worker = std::thread([&]() {
        invoke();
        SetEvent(completed);
      });

      while (true) {
        const DWORD waitResult = MsgWaitForMultipleObjects(
            1, &completed, FALSE, INFINITE, QS_ALLINPUT);
        if (waitResult == WAIT_OBJECT_0)
          break;
        if (waitResult == WAIT_FAILED) {
          WaitForSingleObject(completed, INFINITE);
          break;
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          if (message.message == WM_QUIT) {
            repostQuit = true;
            quitCode = static_cast<int>(message.wParam);
            continue;
          }
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
      }
    } catch (const std::exception &error) {
      result = {E_FAIL, AnsiToWide(error.what())};
    }
    if (worker.joinable())
      worker.join();
    CloseHandle(completed);
  }

  MSG progressMessage{};
  while (PeekMessageW(&progressMessage, g_hGui, WM_APP_PROGRESS_SHOW,
                      WM_APP_PROGRESS_HIDE, PM_REMOVE)) {
    TranslateMessage(&progressMessage);
    DispatchMessageW(&progressMessage);
  }
  ProgressUI_Hide();
  g_bArchiveTaskInProgress = false;
  if (ownerWasEnabled) {
    EnableWindow(g_hGui, TRUE);
    SetActiveWindow(g_hGui);
  }
  if (repostQuit)
    PostQuitMessage(quitCode);
  return result;
}

static std::wstring QuotePowerShellSingle(const std::wstring &value) {
  std::wstring out = L"'";
  for (wchar_t ch : value) {
    if (ch == L'\'')
      out += L"''";
    else
      out += ch;
  }
  out += L"'";
  return out;
}

struct HiddenCommandResult {
  bool launched = false;
  bool timedOut = false;
  DWORD exitCode = static_cast<DWORD>(-1);
  DWORD systemError = ERROR_SUCCESS;
};

static HiddenCommandResult
RunHiddenCommandAndWait(const fs::path &executable,
                        const std::wstring &arguments,
                        DWORD timeoutMilliseconds) {
  HiddenCommandResult result;
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) {
    result.systemError = GetLastError();
    return result;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
  jobLimits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                               &jobLimits, sizeof(jobLimits))) {
    result.systemError = GetLastError();
    CloseHandle(job);
    return result;
  }

  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi = {};
  std::wstring mutableCmd =
      L"\"" + executable.wstring() + L"\" " + arguments;

  if (!CreateProcessW(executable.c_str(), mutableCmd.data(), NULL, NULL, FALSE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, &si,
                      &pi)) {
    result.systemError = GetLastError();
    CloseHandle(job);
    return result;
  }
  if (!AssignProcessToJobObject(job, pi.hProcess)) {
    result.systemError = GetLastError();
    TerminateProcess(pi.hProcess, result.systemError);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);
    return result;
  }
  if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
    result.systemError = GetLastError();
    CloseHandle(job);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
  }

  result.launched = true;
  const DWORD waitResult =
      WaitForSingleObject(pi.hProcess, timeoutMilliseconds);
  if (waitResult == WAIT_TIMEOUT) {
    result.timedOut = true;
    CloseHandle(job);
    job = nullptr;
    WaitForSingleObject(pi.hProcess, 5000);
  } else if (waitResult == WAIT_OBJECT_0) {
    if (!GetExitCodeProcess(pi.hProcess, &result.exitCode))
      result.systemError = GetLastError();
  } else {
    result.systemError = GetLastError();
    if (result.systemError == ERROR_SUCCESS)
      result.systemError = ERROR_GEN_FAILURE;
    CloseHandle(job);
    job = nullptr;
    WaitForSingleObject(pi.hProcess, 5000);
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (job)
    CloseHandle(job);
  return result;
}

static CtBackup::Result ExtractLegacyZip(const fs::path &zipPath,
                                         const fs::path &destination) {
  constexpr DWORD kLegacyZipTimeoutMilliseconds = 30 * 60 * 1000;
  constexpr unsigned long long kMetadataBytesPerItem = 16ull * 1024ull;
  constexpr unsigned long long kMinimumFreeSpaceReserve =
      5ull * 1024ull * 1024ull * 1024ull;
  if (!fs::is_regular_file(zipPath))
    return {HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            L"The selected legacy ZIP backup does not exist."};

  std::vector<wchar_t> systemDirectory(32768, L'\0');
  const UINT length = GetSystemDirectoryW(
      systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
  if (length == 0)
    return {HRESULT_FROM_WIN32(GetLastError()),
            L"Windows PowerShell could not be located."};
  if (length >= systemDirectory.size())
    return {HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER),
            L"The Windows system-directory path was too long to validate."};

  const fs::path powerShell =
      fs::path(systemDirectory.data()) / L"WindowsPowerShell" / L"v1.0" /
      L"powershell.exe";
  if (!fs::is_regular_file(powerShell))
    return {HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            L"Windows PowerShell is required to restore an older ZIP "
            L"backup."};

  std::wstring script =
      L"$ErrorActionPreference='Stop';"
      L"Add-Type -AssemblyName System.IO.Compression;"
      L"Add-Type -AssemblyName System.IO.Compression.FileSystem;"
      L"$maxItems=[int64]";
  script += std::to_wstring(CtArchiveSafety::kMaxItems);
  script += L";$maxBytes=[int64]";
  script += std::to_wstring(CtArchiveSafety::kMaxUncompressedBytes);
  script += L";$maxDepth=[int32]";
  script += std::to_wstring(CtArchiveSafety::kMaxDirectoryDepth);
  script += L";$metadataPerItem=[int64]";
  script += std::to_wstring(kMetadataBytesPerItem);
  script += L";$minimumReserve=[int64]";
  script += std::to_wstring(kMinimumFreeSpaceReserve);
  script += L";$archive=" + QuotePowerShellSingle(zipPath.wstring());
  script += L";$destination=" +
            QuotePowerShellSingle(destination.wstring()) +
            L";$count=[int64]0;$bytes=[int64]0;"
            L"$seen=[Collections.Generic.HashSet[string]]::new("
            L"[StringComparer]::OrdinalIgnoreCase);"
            L"$stream=[IO.File]::Open($archive,[IO.FileMode]::Open,"
            L"[IO.FileAccess]::Read,[IO.FileShare]::Read);"
            L"try{$zip=[IO.Compression.ZipArchive]::new("
            L"$stream,[IO.Compression.ZipArchiveMode]::Read,$true);"
            L"try{foreach($entry in $zip.Entries){"
            L"$count++;if($count -gt $maxItems){throw 'Too many ZIP entries.'};"
            L"$length=[int64]$entry.Length;"
            L"if($length -lt 0 -or $length -gt ($maxBytes-$bytes)){"
            L"throw 'The ZIP expands beyond the safety limit.'};"
            L"$bytes+=$length;$name=[string]$entry.FullName;"
            L"if([string]::IsNullOrEmpty($name)){throw 'Empty ZIP path.'};"
            L"$name=$name.Replace([char]47,[char]92);"
            L"while($name.EndsWith([string][char]92)){"
            L"$name=$name.Substring(0,$name.Length-1)};"
            L"if($name.Length -eq 0 -or $name.Length -ge 32767 -or "
            L"[IO.Path]::IsPathRooted($name)){throw 'Unsafe ZIP path.'};"
            L"$parts=$name.Split([char[]]@([char]92),"
            L"[StringSplitOptions]::None);"
            L"if($parts.Count -gt $maxDepth){throw 'ZIP path is too deep.'};"
            L"foreach($part in $parts){"
            L"if([string]::IsNullOrEmpty($part) -or $part -eq '.' -or "
            L"$part -eq '..' -or $part.Length -gt 255 -or "
            L"$part.EndsWith(' ') -or $part.EndsWith('.')){"
            L"throw 'Unsafe ZIP path component.'};"
            L"$base=$part.Split([char]46)[0].ToUpperInvariant();"
            L"if($base -match "
            L"'^(CON|PRN|AUX|NUL|CLOCK\\$|CONIN\\$|CONOUT\\$|"
            L"COM(?:[1-9]|\\u00B9|\\u00B2|\\u00B3)|"
            L"LPT(?:[1-9]|\\u00B9|\\u00B2|\\u00B3))$'){"
            L"throw 'Reserved ZIP path component.'};"
            L"foreach($character in $part.ToCharArray()){"
            L"$code=[int]$character;"
            L"if($code -lt 32 -or @(34,42,58,60,62,63,124) -contains $code){"
            L"throw 'Invalid character in ZIP path.'}}};"
            L"if(-not $seen.Add($name)){throw 'Duplicate ZIP path.'};"
            L"$attributes=[int64]$entry.ExternalAttributes;"
            L"if(($attributes -band 1024) -ne 0 -or "
            L"((($attributes -shr 16) -band 61440) -eq 40960)){"
            L"throw 'ZIP reparse points and links are not allowed.'}}}"
            L"finally{$zip.Dispose()};"
            L"if($count -eq 0){throw 'The ZIP is empty.'};"
            L"$drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($destination));"
            L"$reserve=[Math]::Max($minimumReserve,"
            L"[int64]($drive.TotalSize/20));"
            L"$required=$bytes+$count*$metadataPerItem;"
            L"if($drive.AvailableFreeSpace -le $reserve -or "
            L"$required -gt ($drive.AvailableFreeSpace-$reserve)){"
            L"throw 'Insufficient disk space for safe ZIP staging.'};"
            L"Expand-Archive -LiteralPath $archive "
            L"-DestinationPath $destination -Force;"
            L"}finally{$stream.Dispose()};";
  const std::wstring arguments =
      L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
      L"\"" + script + L"\"";
  const HiddenCommandResult commandResult = RunHiddenCommandAndWait(
      powerShell, arguments, kLegacyZipTimeoutMilliseconds);
  if (!commandResult.launched)
    return {HRESULT_FROM_WIN32(commandResult.systemError),
            L"The legacy ZIP validation process could not be started."};
  if (commandResult.timedOut)
    return {HRESULT_FROM_WIN32(ERROR_TIMEOUT),
            L"The legacy ZIP restore exceeded 30 minutes and was stopped."};
  if (commandResult.systemError != ERROR_SUCCESS)
    return {HRESULT_FROM_WIN32(commandResult.systemError),
            L"The legacy ZIP validation process could not be monitored "
            L"safely."};
  if (commandResult.exitCode != 0)
    return {E_FAIL,
            std::format(
                L"Legacy ZIP validation or extraction failed (exit code {}).",
                commandResult.exitCode)};
  return {};
}

static CtBackup::Result
NormalizeLegacyZipStaging(const fs::path &stagingRoot,
                          CtBackup::StagedRestore &stagedRestore) {
  try {
    const fs::path sitesPayload = stagingRoot / L"Sites";
    if (fs::exists(sitesPayload)) {
      if (!fs::is_directory(sitesPayload))
        return {E_INVALIDARG,
                L"The legacy backup contains an invalid Sites entry."};
      for (const fs::directory_entry &entry :
           fs::directory_iterator(stagingRoot)) {
        if (_wcsicmp(entry.path().filename().c_str(), L"Sites") != 0)
          return {E_INVALIDARG,
                  L"The legacy backup contains unexpected root data."};
      }
    } else {
      std::vector<fs::path> rootEntries;
      for (const fs::directory_entry &entry :
           fs::directory_iterator(stagingRoot))
        rootEntries.push_back(entry.path());
      if (rootEntries.empty())
        return {E_INVALIDARG,
                L"The legacy backup did not contain profile data."};

      fs::create_directory(sitesPayload);
      for (const fs::path &entry : rootEntries)
        fs::rename(entry, sitesPayload / entry.filename());
    }

    unsigned long long profileCount = 0;
    for (const fs::directory_entry &entry :
         fs::directory_iterator(sitesPayload)) {
      const DWORD attributes = GetFileAttributesW(entry.path().c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES ||
          (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
          (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return {E_INVALIDARG,
                L"The legacy backup contains a direct Sites item that is not "
                L"a safe client directory."};
      }
      const std::wstring clientName = entry.path().filename().wstring();
      if (!CtBackup::Detail::IsValidClientDirectoryName(clientName)) {
        return {E_INVALIDARG,
                L"The legacy backup contains an invalid or reserved client "
                L"directory name: " +
                    clientName};
      }
      ++profileCount;
    }
    if (profileCount == 0)
      return {E_INVALIDARG,
              L"The legacy backup did not contain any client profiles."};

    stagedRestore = {stagingRoot, sitesPayload, profileCount};
    return {};
  } catch (const std::exception &error) {
    return {E_FAIL, AnsiToWide(error.what())};
  }
}

static fs::path UniqueRecoveryPath(const fs::path &dataDir,
                                   const std::wstring &timestamp) {
  fs::path candidate = dataDir / (L"Sites_PreRestore_" + timestamp);
  for (unsigned suffix = 2; fs::exists(candidate); ++suffix)
    candidate = dataDir /
                std::format(L"Sites_PreRestore_{}-{}", timestamp, suffix);
  return candidate;
}

static std::wstring ArchiveFailureMessage(const wchar_t *heading,
                                          const CtBackup::Result &result) {
  std::wstring message = heading;
  if (!result.details.empty())
    message += L"\n\n" + result.details;
  message += std::format(L"\n\nError: 0x{:08X}",
                         static_cast<unsigned long>(result.code));
  return message;
}

void GuiProfExportAll() {
  SetUiState(false);
  std::wstring profileUseError;
  if (ProbeAnySitesProfileInUse(profileUseError) !=
      ProfileUseState::NotInUse) {
    MessageBoxW(
        g_hGui,
        (L"Close all browsers using ctSpaces client profiles before creating "
         L"a backup. Backup is blocked when process inspection is uncertain."
         L"\n\n" +
         profileUseError)
            .c_str(),
        L"Export Data", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }

  fs::path sourceDir = g_sDataDir / "Sites";
  if (!fs::exists(sourceDir) || !fs::is_directory(sourceDir)) {
    MessageBox(g_hGui, L"No profile data was found to export yet.",
               L"Export Data", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }

  unsigned long long profileCount = 0;
  try {
    for (const fs::directory_entry &entry : fs::directory_iterator(sourceDir)) {
      if (entry.is_directory())
        ++profileCount;
    }
  } catch (...) {
    profileCount = 0;
  }
  if (profileCount == 0) {
    MessageBox(g_hGui, L"No client profiles were found to back up.",
               L"Back Up Data", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }

  std::vector<wchar_t> fileBuffer(32768, L'\0');
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hGui;
  ofn.lpstrFile = fileBuffer.data();
  ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
  ofn.lpstrFilter =
      L"ctSpaces Backup (*.7z)\0*.7z\0All Files (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrDefExt = L"7z";
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

  const std::wstring suggestedName =
      L"ctSpaces_Backup_" + GetBackupTimestamp() + L".7z";
  wcscpy_s(fileBuffer.data(), fileBuffer.size(), suggestedName.c_str());

  if (GetSaveFileNameW(&ofn)) {
    fs::path destPath(ofn.lpstrFile);
    profileUseError.clear();
    if (ProbeAnySitesProfileInUse(profileUseError) !=
        ProfileUseState::NotInUse) {
      MessageBoxW(
          g_hGui,
          (L"A browser began using a ctSpaces client profile or could not be "
           L"reverified after the backup location was selected. No backup was "
           L"created.\n\n" +
           profileUseError)
              .c_str(),
          L"Backup Cancelled", MB_OK | MB_ICONWARNING);
      SetUiState(true);
      return;
    }
    const CtBackup::Result result = RunArchiveTaskWithProgress(
        L"Creating and verifying backup...", [&](auto &context) {
          return CtBackup::CreateValidated7zBackup(
              sourceDir, destPath, CTSPACES_VERSION_TEXT, &_7zProgress,
              &context);
        });
    if (result.ok()) {
      const std::wstring message =
          std::format(L"Backup complete and verified.\n\n{} client profile{} "
                      L"saved to:\n{}",
                      profileCount, profileCount == 1 ? L" was" : L"s were",
                      destPath.wstring());
      MessageBox(g_hGui, message.c_str(), L"Backup Complete",
                 MB_OK | MB_ICONINFORMATION);
    } else {
      const std::wstring message =
          ArchiveFailureMessage(L"The backup could not be created.", result);
      MessageBox(g_hGui, message.c_str(), L"Back Up Data",
                 MB_OK | MB_ICONERROR);
    }
  }
  SetUiState(true);
}

void GuiProfRestoreAll() {
  SetUiState(false);

  std::wstring profileUseError;
  if (ProbeAnySitesProfileInUse(profileUseError) !=
      ProfileUseState::NotInUse) {
    MessageBoxW(
        g_hGui,
        (L"Close all browsers using ctSpaces client profiles before restoring "
         L"a backup. Restore is blocked when process inspection is "
         L"uncertain.\n\n" +
         profileUseError)
            .c_str(),
        L"Restore Data", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }

  std::vector<wchar_t> fileBuffer(32768, L'\0');
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hGui;
  ofn.lpstrFile = fileBuffer.data();
  ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
  ofn.lpstrFilter =
      L"ctSpaces Backup (*.7z;*.zip)\0*.7z;*.zip\0Validated Backup "
      L"(*.7z)\0*.7z\0Legacy Backup (*.zip)\0*.zip\0All Files "
      L"(*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrDefExt = L"7z";
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

  if (!GetOpenFileNameW(&ofn)) {
    SetUiState(true);
    return;
  }

  const fs::path archivePath(ofn.lpstrFile);
  const bool legacyZip =
      _wcsicmp(archivePath.extension().c_str(), L".zip") == 0;
  const bool validated7z =
      _wcsicmp(archivePath.extension().c_str(), L".7z") == 0;
  if (!legacyZip && !validated7z) {
    MessageBox(g_hGui,
               L"Select a ctSpaces .7z backup or an older .zip backup.",
               L"Restore Data", MB_OK | MB_ICONERROR);
    SetUiState(true);
    return;
  }

  const wchar_t *confirmation = legacyZip
      ? L"Restore this older ZIP backup?\n\nZIP restore is retained for "
        L"backward compatibility. Current profile data will be preserved in "
        L"a pre-restore recovery folder first."
      : L"Restore this validated ctSpaces backup?\n\nThe archive will be "
        L"checked before current profile data is moved to a pre-restore "
        L"recovery folder.";
  if (MessageBox(g_hGui, confirmation, L"Restore Data",
                 MB_YESNO | MB_ICONQUESTION) != IDYES) {
    SetUiState(true);
    return;
  }

  profileUseError.clear();
  if (ProbeAnySitesProfileInUse(profileUseError) !=
      ProfileUseState::NotInUse) {
    MessageBoxW(
        g_hGui,
        (L"A browser began using a ctSpaces client profile or could not be "
         L"reverified after confirmation. Nothing was restored.\n\n" +
         profileUseError)
            .c_str(),
        L"Restore Cancelled", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  const std::wstring stamp = GetBackupTimestamp();
  const fs::path tempDir =
      g_sDataDir /
      std::format(L"Restore_Temp_{}-{}-{}", stamp, GetCurrentProcessId(),
                  GetTickCount64());
  const fs::path sitesDir = g_sDataDir / "Sites";
  const fs::path backupDir = UniqueRecoveryPath(g_sDataDir, stamp);

  try {
    fs::create_directories(g_sDataDir);
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);
  } catch (const fs::filesystem_error &e) {
    MessageBox(g_hGui, AnsiToWide(e.what()).c_str(), L"Restore Data",
               MB_OK | MB_ICONERROR);
    SetUiState(true);
    return;
  }

  CtBackup::StagedRestore stagedRestore;
  CtBackup::Result stageResult = RunArchiveTaskWithProgress(
      legacyZip ? L"Reading older ZIP backup..."
                : L"Validating and staging backup...",
      [&](auto &context) {
        if (!legacyZip)
          return CtBackup::StageValidated7zRestore(
              archivePath, tempDir, &_7zProgress, &context, stagedRestore);
        CtBackup::Result result = ExtractLegacyZip(archivePath, tempDir);
        if (!result.ok())
          return result;
        return NormalizeLegacyZipStaging(tempDir, stagedRestore);
      });

  if (!stageResult.ok()) {
    std::error_code cleanupError;
    fs::remove_all(tempDir, cleanupError);
    const std::wstring message = ArchiveFailureMessage(
        L"The selected backup could not be restored.", stageResult);
    MessageBox(g_hGui, message.c_str(), L"Restore Data",
               MB_OK | MB_ICONERROR);
    SetUiState(true);
    return;
  }

  profileUseError.clear();
  if (ProbeAnySitesProfileInUse(profileUseError) !=
      ProfileUseState::NotInUse) {
    std::error_code cleanupError;
    fs::remove_all(tempDir, cleanupError);
    MessageBoxW(
        g_hGui,
        (L"A browser began using a ctSpaces client profile or could not be "
         L"reverified while the backup was staged. Current profile data was "
         L"left unchanged.\n\n" +
         profileUseError)
            .c_str(),
        L"Restore Cancelled", MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  const auto finalRestoreCommitGuard =
      [](void *, std::wstring &failureDetails) -> bool {
    std::wstring finalProfileUseError;
    if (ProbeAnySitesProfileInUse(finalProfileUseError) ==
        ProfileUseState::NotInUse) {
      return true;
    }
    failureDetails =
        L"A browser began using a ctSpaces client profile, or final process "
        L"inspection was uncertain, after the restore path checks completed. "
        L"Current profile data was left unchanged.";
    if (!finalProfileUseError.empty())
      failureDetails += L"\n\n" + finalProfileUseError;
    return false;
  };
  const CtBackup::Result commitResult = CtBackup::CommitStagedRestore(
      stagedRestore, sitesDir, backupDir, finalRestoreCommitGuard, nullptr);
  if (!commitResult.ok()) {
    std::error_code cleanupError;
    fs::remove_all(tempDir, cleanupError);
    const std::wstring message = ArchiveFailureMessage(
        L"The restored data could not be installed. The previous Sites "
        L"folder was left in place or rolled back.",
        commitResult);
    MessageBox(g_hGui, message.c_str(), L"Restore Data",
               MB_OK | MB_ICONERROR);
    SetUiState(true);
    return;
  }

  UpdateClientsComboBox();
  UpdatePinButtonState();
  SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)-1, 0);
  SetClientInputText(L"");
  UpdateIconPreviewForSelection(false);

  std::wstring msg = std::format(
      L"Restore complete.\n\n{} client profile{} restored{}.",
      stagedRestore.profileCount,
      stagedRestore.profileCount == 1 ? L" was" : L"s were",
      legacyZip ? L" from a legacy ZIP backup" : L" from a verified backup");
  if (!commitResult.details.empty())
    msg += L"\n\n" + commitResult.details;
  if (fs::exists(backupDir))
    msg += L"\n\nYour previous profile data was kept here:\n" +
           backupDir.wstring();
  MessageBox(g_hGui, msg.c_str(), L"Restore Data", MB_OK | MB_ICONINFORMATION);
  SetUiState(true);
}

void GuiOpenTmp() {
  SetUiState(false);
  if (IsClientActiveAnyBrowser(L"Temp")) {
    MessageBox(g_hGui, L"A temporary profile is already open.",
               L"Already Running", MB_OK | MB_ICONINFORMATION);
    SetUiState(true);
    return;
  }
  LaunchProfileAsync(L"Temp", true, false, g_selectedBrowser);
}
struct EnumData {
  DWORD processId;
  std::vector<HWND> windows;
};
BOOL CALLBACK EnumWindowsCallback(HWND hWnd, LPARAM lParam) {
  EnumData *pData = (EnumData *)lParam;
  DWORD processId = 0;
  GetWindowThreadProcessId(hWnd, &processId);
  if (pData->processId == processId && IsWindowVisible(hWnd) &&
      GetWindowTextLength(hWnd) > 0) {
    pData->windows.push_back(hWnd);
  }
  return TRUE;
}

static fs::path GetBrowserExecutable(BrowserKind browser) {
  switch (browser) {
  case BrowserKind::Edge:
    return g_sEdgePath;
  case BrowserKind::Chrome:
    return g_sChromePath;
  case BrowserKind::Brave:
    return g_sBravePath;
  case BrowserKind::Firefox:
    return g_sFirefoxPath;
  }
  return {};
}

static bool IsExactBrowserExecutable(const fs::path &exePath,
                                     BrowserKind browser) {
  if (exePath.empty())
    return false;
  const wchar_t *expectedName =
      browser == BrowserKind::Edge      ? L"msedge.exe"
      : browser == BrowserKind::Chrome ? L"chrome.exe"
      : browser == BrowserKind::Brave  ? L"brave.exe"
                                        : L"firefox.exe";
  if (_wcsicmp(exePath.filename().c_str(), expectedName) != 0)
    return false;
  const DWORD attributes = GetFileAttributesW(exePath.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                        FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

static std::wstring QuoteCommandLineArgument(const std::wstring &argument) {
  if (argument.empty())
    return L"\"\"";
  if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    return argument;

  std::wstring quoted = L"\"";
  size_t backslashes = 0;
  for (wchar_t ch : argument) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'\"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(ch);
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(ch);
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

struct NativeCommandLineString {
  USHORT length = 0;
  USHORT maximumLength = 0;
  PWSTR buffer = nullptr;
};

using NtQueryInformationProcessFn =
    LONG(NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static bool TryGetProcessCommandLine(DWORD processId,
                                     std::wstring &commandLine,
                                     std::wstring &errorDetails) {
  commandLine.clear();
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto queryProcess =
      ntdll ? reinterpret_cast<NtQueryInformationProcessFn>(
                  GetProcAddress(ntdll, "NtQueryInformationProcess"))
            : nullptr;
  if (!queryProcess) {
    errorDetails = L"Windows command-line inspection is unavailable.";
    return false;
  }

  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process) {
    errorDetails = std::format(
        L"Browser process {} could not be opened (Windows error {}).", processId,
        GetLastError());
    return false;
  }

  std::vector<BYTE> buffer(4096);
  LONG finalStatus = 0;
  for (unsigned attempt = 0; attempt < 7; ++attempt) {
    ULONG requiredBytes = 0;
    finalStatus = queryProcess(process, 60, buffer.data(),
                               static_cast<ULONG>(buffer.size()),
                               &requiredBytes);
    if (finalStatus >= 0) {
      if (buffer.size() < sizeof(NativeCommandLineString))
        break;
      const auto *nativeText =
          reinterpret_cast<const NativeCommandLineString *>(buffer.data());
      const uintptr_t begin = reinterpret_cast<uintptr_t>(buffer.data());
      const uintptr_t end = begin + buffer.size();
      const uintptr_t text = reinterpret_cast<uintptr_t>(nativeText->buffer);
      if (!nativeText->buffer || nativeText->length == 0 ||
          nativeText->length % sizeof(wchar_t) != 0 || text < begin ||
          text > end || nativeText->length > end - text) {
        break;
      }
      commandLine.assign(nativeText->buffer,
                         nativeText->length / sizeof(wchar_t));
      CloseHandle(process);
      return true;
    }

    size_t nextSize = requiredBytes > buffer.size()
                          ? static_cast<size_t>(requiredBytes)
                          : buffer.size() * 2;
    if (nextSize > 256 * 1024)
      break;
    buffer.resize(nextSize);
  }
  CloseHandle(process);
  errorDetails = std::format(
      L"Browser process {} command line could not be inspected (NTSTATUS "
      L"0x{:08X}).",
      processId, static_cast<unsigned long>(finalStatus));
  return false;
}

static bool IsProcessStillRunning(DWORD processId) {
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
  if (!process)
    return GetLastError() != ERROR_INVALID_PARAMETER;
  const DWORD state = WaitForSingleObject(process, 0);
  CloseHandle(process);
  return state != WAIT_OBJECT_0;
}

static std::optional<BrowserKind>
BrowserKindFromProcessName(const wchar_t *processName) {
  if (!processName)
    return std::nullopt;
  if (_wcsicmp(processName, L"msedge.exe") == 0)
    return BrowserKind::Edge;
  if (_wcsicmp(processName, L"chrome.exe") == 0)
    return BrowserKind::Chrome;
  if (_wcsicmp(processName, L"brave.exe") == 0)
    return BrowserKind::Brave;
  if (_wcsicmp(processName, L"firefox.exe") == 0)
    return BrowserKind::Firefox;
  return std::nullopt;
}

struct BrowserProfileArgument {
  bool present = false;
  bool valid = true;
  fs::path path;
};

static BrowserProfileArgument ExtractBrowserProfileArgument(
    const std::wstring &commandLine, BrowserKind browser) {
  BrowserProfileArgument result;
  int argumentCount = 0;
  LPWSTR *arguments = CommandLineToArgvW(commandLine.c_str(), &argumentCount);
  if (!arguments || argumentCount <= 0) {
    if (arguments)
      LocalFree(arguments);
    result.valid = false;
    return result;
  }

  const wchar_t *longOption = browser == BrowserKind::Firefox
                                  ? L"--profile"
                                  : L"--user-data-dir";
  const size_t longOptionLength = wcslen(longOption);
  for (int index = 1; index < argumentCount; ++index) {
    const std::wstring argument = arguments[index];
    const bool exactLong = _wcsicmp(argument.c_str(), longOption) == 0;
    const bool exactFirefoxShort =
        browser == BrowserKind::Firefox &&
        _wcsicmp(argument.c_str(), L"-profile") == 0;
    if (exactLong || exactFirefoxShort) {
      result.present = true;
      if (index + 1 >= argumentCount || !arguments[index + 1][0]) {
        result.valid = false;
      } else {
        result.path = fs::path(arguments[index + 1]);
      }
      break;
    }

    if (_wcsnicmp(argument.c_str(), longOption, longOptionLength) == 0 &&
        argument.size() > longOptionLength &&
        argument[longOptionLength] == L'=') {
      result.present = true;
      const std::wstring value = argument.substr(longOptionLength + 1);
      if (value.empty())
        result.valid = false;
      else
        result.path = fs::path(value);
      break;
    }
  }
  LocalFree(arguments);
  if (result.present && result.valid && !result.path.is_absolute())
    result.valid = false;
  return result;
}

static ProfileUseState ProbeBrowserProcessesAgainstProfiles(
    const std::vector<fs::path> &exactProfiles,
    const std::optional<fs::path> &sitesScope, std::wstring &errorDetails) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    errorDetails = std::format(
        L"Running browser processes could not be listed (Windows error {}).",
        GetLastError());
    return ProfileUseState::Indeterminate;
  }

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) {
    const DWORD error = GetLastError();
    CloseHandle(snapshot);
    errorDetails = std::format(
        L"Running browser processes could not be inspected (Windows error {}).",
        error);
    return ProfileUseState::Indeterminate;
  }

  do {
    const auto browser = BrowserKindFromProcessName(entry.szExeFile);
    if (!browser)
      continue;

    std::wstring commandLine;
    std::wstring commandError;
    if (!TryGetProcessCommandLine(entry.th32ProcessID, commandLine,
                                  commandError)) {
      if (!IsProcessStillRunning(entry.th32ProcessID))
        continue;
      CloseHandle(snapshot);
      errorDetails = commandError;
      return ProfileUseState::Indeterminate;
    }
    const BrowserProfileArgument profileArgument =
        ExtractBrowserProfileArgument(commandLine, *browser);
    if (!profileArgument.valid) {
      CloseHandle(snapshot);
      errorDetails = std::format(
          L"Browser process {} has a profile argument that could not be "
          L"validated.",
          entry.th32ProcessID);
      return ProfileUseState::Indeterminate;
    }
    if (!profileArgument.present)
      continue;

    bool matches = false;
    if (sitesScope) {
      matches = IsStrictChildPath(profileArgument.path, *sitesScope);
    } else {
      matches = std::ranges::any_of(exactProfiles, [&](const fs::path &profile) {
        return SameExecutablePath(profileArgument.path, profile);
      });
    }
    if (matches) {
      CloseHandle(snapshot);
      errorDetails = std::format(
          L"{} process {} is still using this browser profile.",
          GetBrowserDisplayName(*browser), entry.th32ProcessID);
      return ProfileUseState::InUse;
    }
  } while (Process32NextW(snapshot, &entry));

  const DWORD enumerationEnd = GetLastError();
  const bool closed = CloseHandle(snapshot) != FALSE;
  if (enumerationEnd != ERROR_NO_MORE_FILES || !closed) {
    errorDetails =
        L"Running browser process inspection did not complete cleanly.";
    return ProfileUseState::Indeterminate;
  }
  return ProfileUseState::NotInUse;
}

static ProfileUseState ProbeExactBrowserProfileInUse(
    const fs::path &profilePath, std::wstring &errorDetails) {
  errorDetails.clear();
  if (profilePath.empty() || !profilePath.is_absolute()) {
    errorDetails = L"The browser profile path could not be validated.";
    return ProfileUseState::Indeterminate;
  }

  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    if (std::ranges::any_of(g_activeProfiles, [&](const auto &active) {
          return SameExecutablePath(active.second.profilePath, profilePath);
        })) {
      errorDetails =
          L"ctSpaces is still tracking an open browser for this exact profile.";
      return ProfileUseState::InUse;
    }
  }

  return ProbeBrowserProcessesAgainstProfiles({profilePath}, std::nullopt,
                                               errorDetails);
}

static ProfileUseState ProbeClientProfilesInUse(
    const std::wstring &clientName, std::wstring &errorDetails) {
  errorDetails.clear();
  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    if (std::ranges::any_of(g_activeProfiles, [&](const auto &active) {
          return _wcsicmp(active.first.clientName.c_str(), clientName.c_str()) ==
                 0;
        })) {
      errorDetails = L"ctSpaces is still tracking an open browser for this client.";
      return ProfileUseState::InUse;
    }
  }

  fs::path clientRoot;
  std::vector<ClientBrowserProfileLocation> locations;
  if (!TryGetSafeClientProfilePath(clientName, clientRoot) ||
      !CollectClientBrowserProfileLocations(clientName, clientRoot, locations)) {
    errorDetails =
        L"The client browser-profile layout could not be inspected completely.";
    return ProfileUseState::Indeterminate;
  }
  std::vector<fs::path> profiles;
  profiles.reserve(locations.size());
  for (const auto &location : locations)
    profiles.push_back(location.profileRoot);
  return ProbeBrowserProcessesAgainstProfiles(profiles, std::nullopt,
                                               errorDetails);
}

static ProfileUseState ProbeAnySitesProfileInUse(std::wstring &errorDetails) {
  errorDetails.clear();
  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    if (!g_activeProfiles.empty()) {
      errorDetails = L"ctSpaces is still tracking one or more open browsers.";
      return ProfileUseState::InUse;
    }
  }

  const fs::path sitesRoot = g_sDataDir / L"Sites";
  const DWORD attributes = GetFileAttributesW(sitesRoot.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
      return ProfileUseState::NotInUse;
    errorDetails = std::format(
        L"The Sites folder could not be inspected (Windows error {}).", error);
    return ProfileUseState::Indeterminate;
  }
  if (!IsSafeExistingDirectory(sitesRoot)) {
    errorDetails = L"The Sites folder is a reparse point or is not a directory.";
    return ProfileUseState::Indeterminate;
  }
  return ProbeBrowserProcessesAgainstProfiles({}, sitesRoot, errorDetails);
}

static bool IsValidWebUrl(const std::wstring &url) {
  if (url.empty() || url.size() > 8192 ||
      url.find_first_of(L"\r\n\"") != std::wstring::npos) {
    return false;
  }

  URL_COMPONENTSW components{};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = (DWORD)-1;
  components.dwUrlPathLength = (DWORD)-1;
  components.dwExtraInfoLength = (DWORD)-1;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components))
    return false;
  return (components.nScheme == INTERNET_SCHEME_HTTP ||
          components.nScheme == INTERNET_SCHEME_HTTPS) &&
         components.dwHostNameLength > 0;
}

static bool SetFirefoxStartupPreference(const fs::path &profilePath,
                                        bool restoreTabs) {
  const fs::path preferencesPath = profilePath / L"user.js";
  std::string preferences;
  try {
    if (fs::exists(preferencesPath)) {
      if (!fs::is_regular_file(preferencesPath) ||
          fs::file_size(preferencesPath) > 4ull * 1024ull * 1024ull) {
        return false;
      }
      std::ifstream input(preferencesPath, std::ios::binary);
      if (!input)
        return false;
      preferences.assign(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    }

    const std::string managedPreference =
        std::string("user_pref(\"browser.startup.page\", ") +
        (restoreTabs ? "3" : "1") + ");";
    const std::regex startupPreference(
        R"(user_pref\("browser\.startup\.page"\s*,\s*\d+\s*\);)");
    if (std::regex_search(preferences, startupPreference)) {
      preferences =
          std::regex_replace(preferences, startupPreference, managedPreference);
    } else {
      if (!preferences.empty() && preferences.back() != '\n')
        preferences += "\r\n";
      preferences += managedPreference + "\r\n";
    }

    const fs::path stagedPath =
        profilePath /
        std::format(L".user.js.ctspaces-{}-{}", GetCurrentProcessId(),
                    GetTickCount64());
    {
      std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
      if (!output)
        return false;
      output.write(preferences.data(),
                   static_cast<std::streamsize>(preferences.size()));
      output.flush();
      if (!output)
        return false;
    }
    if (!MoveFileExW(stagedPath.c_str(), preferencesPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      std::error_code cleanupError;
      fs::remove(stagedPath, cleanupError);
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

static DWORD StartBrowserProcess(BrowserKind browser,
                                 const fs::path &profilePath,
                                 bool restoreLastSession,
                                 const std::wstring &startupUrl,
                                 HANDLE *processHandle = nullptr,
                                 const fs::path &boundExecutable = fs::path(),
                                 std::wstring *launchError = nullptr) {
  if (processHandle)
    *processHandle = nullptr;
  if (launchError)
    launchError->clear();

  const fs::path exePath = boundExecutable.empty()
                               ? GetBrowserExecutable(browser)
                               : boundExecutable;
  if (!IsExactBrowserExecutable(exePath, browser)) {
    if (launchError) {
      *launchError = std::format(
          L"The {} executable is unavailable or no longer matches the "
          L"configured browser path.\n\nPath: {}",
          GetBrowserDisplayName(browser), exePath.wstring());
    }
    return 0;
  }

  std::wstring cmdLine = QuoteCommandLineArgument(exePath.wstring());
  if (browser == BrowserKind::Firefox) {
    if (!SetFirefoxStartupPreference(profilePath, restoreLastSession)) {
      if (launchError) {
        *launchError =
            L"Firefox startup preferences could not be updated safely. "
            L"The browser was not started.";
      }
      return 0;
    }
    cmdLine += L" --profile " + QuoteCommandLineArgument(profilePath.wstring()) +
               L" --no-remote --new-instance";
    if (!startupUrl.empty())
      cmdLine += L" --new-window " + QuoteCommandLineArgument(startupUrl);
  } else {
    cmdLine += L" --user-data-dir=" +
               QuoteCommandLineArgument(profilePath.wstring()) +
               L" --no-first-run ";
    if (restoreLastSession)
      cmdLine += L"--restore-last-session ";
    cmdLine += L"--disable-sync --disable-features=SyncPromo "
               L"--edge-skip-compat-layer-relaunch --no-service-autorun "
               L"--disable-background-mode";
    if (!startupUrl.empty()) {
      cmdLine += L" --new-tab ";
      cmdLine += QuoteCommandLineArgument(startupUrl);
    }
  }

  STARTUPINFOW startupInfo{sizeof(startupInfo)};
  PROCESS_INFORMATION processInfo{};
  if (!CreateProcessW(exePath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
                      0, nullptr, nullptr, &startupInfo, &processInfo)) {
    const DWORD error = GetLastError();
    if (launchError) {
      const std::wstring systemMessage =
          AnsiToWide(std::system_category().message(error));
      *launchError = std::format(
          L"Windows could not start {}.\n\nExecutable: {}\nProfile: "
          L"{}\n\nWindows error {}: {}",
          GetBrowserDisplayName(browser), exePath.wstring(),
          profilePath.wstring(), error, systemMessage);
    }
    return 0;
  }
  const DWORD processId = processInfo.dwProcessId;
  CloseHandle(processInfo.hThread);
  if (processHandle)
    *processHandle = processInfo.hProcess;
  else
    CloseHandle(processInfo.hProcess);
  return processId;
}

static bool RemoveProfileDirectoryAndVerify(const fs::path &profilePath,
                                             std::wstring &errorDetails) {
  constexpr int kCleanupAttempts = 20;
  constexpr DWORD kCleanupRetryDelayMs = 50;
  std::error_code lastError;

  for (int attempt = 0; attempt < kCleanupAttempts; ++attempt) {
    std::error_code removeError;
    fs::remove_all(profilePath, removeError);

    std::error_code existsError;
    const bool stillExists = fs::exists(profilePath, existsError);
    if (!removeError && !existsError && !stillExists) {
      errorDetails.clear();
      return true;
    }

    lastError = removeError ? removeError : existsError;
    if (attempt + 1 < kCleanupAttempts)
      Sleep(kCleanupRetryDelayMs);
  }

  errorDetails = L"The disposable browser profile could not be removed.";
  if (lastError)
    errorDetails += L" " + AnsiToWide(lastError.message());
  return false;
}

static bool RemoveTransientProfileIfIdleAndVerify(
    const fs::path &profilePath, std::wstring &errorDetails) {
  std::wstring profileUseError;
  if (ProbeExactBrowserProfileInUse(profilePath, profileUseError) !=
      ProfileUseState::NotInUse) {
    errorDetails =
        L"The disposable browser profile is still open or could not be "
        L"inspected safely. It was left unchanged.";
    if (!profileUseError.empty())
      errorDetails += L"\n\n" + profileUseError;
    return false;
  }
  return RemoveProfileDirectoryAndVerify(profilePath, errorDetails);
}

static bool IsClientActiveAnyBrowser(const std::wstring &clientName) {
  std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
  return std::ranges::any_of(g_activeProfiles, [&](const auto &active) {
    return _wcsicmp(active.first.clientName.c_str(), clientName.c_str()) == 0;
  });
}

static std::optional<ActiveProfileInfo>
GetActiveProfile(const std::wstring &clientName, BrowserKind browser) {
  std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
  const auto active =
      g_activeProfiles.find(ClientBrowserKey{clientName, browser});
  if (active == g_activeProfiles.end())
    return std::nullopt;
  return active->second;
}

static bool CreateUniqueWorkDirectory(const fs::path &base,
                                      const std::wstring &label,
                                      fs::path &workRoot) {
  try {
    fs::create_directories(base);
  } catch (...) {
    return false;
  }
  if (!IsSafeExistingDirectory(base) || !IsStrictChildPath(base, g_sDataDir))
    return false;

  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    workRoot =
        base / std::format(L"{}-{}-{}-{}", label, GetCurrentProcessId(),
                           GetTickCount64(), attempt);
    if (CreateDirectoryW(workRoot.c_str(), nullptr))
      return IsSafeExistingDirectory(workRoot);
    if (GetLastError() != ERROR_ALREADY_EXISTS)
      return false;
  }
  return false;
}

static bool CreateV2ClientSkeleton(const fs::path &clientRoot) {
  if (!IsSafeExistingDirectory(clientRoot))
    return false;
  const fs::path browsersRoot = clientRoot / L"Browsers";
  if (!CreateDirectoryW(browsersRoot.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return false;
  }
  return IsSafeExistingDirectory(browsersRoot) &&
         WriteDurableMarker(clientRoot / kClientSchemaMarkerName,
                            kClientSchemaMarkerText) &&
         IsV2ClientContainer(clientRoot);
}

static bool CreateStagedBrowserSlot(const fs::path &slotRoot,
                                    BrowserKind browser,
                                    const wchar_t *statusText) {
  if (fs::exists(slotRoot) ||
      !CreateDirectoryW(slotRoot.c_str(), nullptr) ||
      !IsSafeExistingDirectory(slotRoot)) {
    return false;
  }

  const fs::path profileRoot = slotRoot / L"Profile";
  bool profileReady = false;
  if (IsChromiumBrowser(browser)) {
    profileReady = extDef(profileRoot, statusText) &&
                   MarkerHasExactContents(profileRoot / L"ctSpaces",
                                          "ctSpaces-profile=2\r\n") &&
                   IsSafeExistingDirectory(profileRoot / L"Default");
  } else {
    profileReady = CreateDirectoryW(profileRoot.c_str(), nullptr) != FALSE &&
                   IsSafeExistingDirectory(profileRoot) &&
                   SetFirefoxStartupPreference(profileRoot, false);
  }
  if (!profileReady)
    return false;

  return WriteDurableMarker(slotRoot / kBrowserSchemaMarkerName,
                            GetBrowserMarkerText(browser)) &&
         IsValidBrowserSlot(slotRoot, profileRoot, browser);
}

static bool EnsureBrowserSlotForClient(const std::wstring &clientName,
                                       const fs::path &clientRoot,
                                       BrowserKind browser,
                                       fs::path &profileRoot,
                                       std::wstring &errorDetails) {
  std::optional<BrowserKind> legacyBinding;
  const bool pureV2 = IsV2ClientContainer(clientRoot);
  if (!pureV2 && !IsSafeHybridClientRoot(clientRoot, &legacyBinding)) {
    errorDetails = L"The client container or hybrid legacy root is unsafe.";
    return false;
  }
  fs::path slotRoot;
  if (!GetNestedBrowserSlotPaths(clientRoot, browser, slotRoot, profileRoot)) {
    errorDetails = L"The client container or browser slot path is unsafe.";
    return false;
  }
  if (fs::exists(slotRoot)) {
    if (!IsValidBrowserSlot(slotRoot, profileRoot, browser)) {
      errorDetails =
          L"This browser slot exists without valid durable v2 markers.";
      return false;
    }
    return true;
  }
  if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
          clientRoot.parent_path(), clientName)) {
    errorDetails =
        L"This existing client's folder path is too long to add another "
        L"browser slot safely. Its existing browser data was left unchanged.";
    return false;
  }

  const fs::path workBase = g_sDataDir / L"_ProfileCreate";
  fs::path workRoot;
  if (!CreateUniqueWorkDirectory(workBase, L"slot", workRoot)) {
    errorDetails =
        L"The ctSpaces data-folder path is too long, or a safe browser-slot "
        L"staging folder could not be created.";
    return false;
  }

  const fs::path stagedSlot = workRoot / L"Slot";
  bool committed = false;
  bool renameCommitted = false;
  try {
    if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
            workRoot, L"Slot")) {
      throw std::runtime_error(
          "The ctSpaces data-folder path leaves insufficient legacy Windows "
          "path room for a temporary browser-slot stage");
    }
    const std::wstring status =
        std::format(L"Creating {} profile...", GetBrowserDisplayName(browser));
    if (!CreateStagedBrowserSlot(stagedSlot, browser, status.c_str()))
      throw std::runtime_error("The staged browser slot failed validation");

    const fs::path browsersRoot = clientRoot / L"Browsers";
    const bool containerStillValid =
        pureV2 ? IsV2ClientContainer(clientRoot)
               : IsSafeHybridClientRoot(clientRoot);
    if (!RevalidateSafeClientContainerPath(clientName, clientRoot) ||
        !containerStillValid) {
      throw std::runtime_error("The client container changed during staging");
    }
    if (!CreateDirectoryW(browsersRoot.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Could not create the browser-slot root");
    }
    if (!IsSafeExistingDirectory(browsersRoot) || fs::exists(slotRoot))
      throw std::runtime_error("The destination browser slot is not empty");
    std::wstring slotPathBudgetError;
    if (!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
            stagedSlot, slotRoot, &slotPathBudgetError)) {
      throw std::runtime_error(
          "The generated browser slot would exceed the legacy Windows path "
          "budget");
    }
    if (!MoveFileExW(stagedSlot.c_str(), slotRoot.c_str(),
                     MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Could not commit the browser slot");
    }
    renameCommitted = true;
    if (!IsValidBrowserSlot(slotRoot, profileRoot, browser))
      throw std::runtime_error("The committed browser slot failed validation");
    if (!pureV2 && !IsSafeHybridClientRoot(clientRoot))
      throw std::runtime_error("The committed hybrid client failed validation");
    committed = true;
  } catch (const std::exception &error) {
    errorDetails = AnsiToWide(error.what());
  }

  if (renameCommitted && !committed) {
    // A successful no-replace move committed our staged directory, but the
    // name can be swapped before a later diagnostic check. Never remove by
    // name here: that could delete an unrelated raced replacement.
    errorDetails +=
        L" The committed browser-slot path was preserved because its "
        L"identity could not be proven safe for rollback. Restart ctSpaces "
        L"before using this client and inspect: " +
        slotRoot.wstring();
  }

  std::error_code cleanupError;
  fs::remove_all(workRoot, cleanupError);
  cleanupError.clear();
  if (fs::is_directory(workBase, cleanupError) &&
      fs::is_empty(workBase, cleanupError)) {
    fs::remove(workBase, cleanupError);
  }
  return committed;
}

static bool CreateNewV2Client(const std::wstring &clientName,
                              BrowserKind browser, fs::path &profileRoot,
                              std::wstring &errorDetails) {
  if (!ValidateSitesRoot(true)) {
    errorDetails = L"The Sites root is missing, unsafe, or a reparse point.";
    return false;
  }
  if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
          g_sDataDir / L"Sites", clientName)) {
    errorDetails =
        L"The client name is too long for this ctSpaces data-folder "
        L"location. Use a shorter name.";
    return false;
  }

  fs::path targetRoot;
  if (!TryGetSafeClientProfilePath(clientName, targetRoot)) {
    errorDetails = L"The requested client path is unsafe.";
    return false;
  }
  if (fs::exists(targetRoot)) {
    errorDetails =
        L"The client folder already exists and was left untouched.";
    return false;
  }

  const fs::path workBase = g_sDataDir / L"_ProfileCreate";
  fs::path workRoot;
  if (!CreateUniqueWorkDirectory(workBase, L"client", workRoot)) {
    errorDetails =
        L"The ctSpaces data-folder path is too long, or a safe client staging "
        L"folder could not be created.";
    return false;
  }

  const fs::path stagedClient = workRoot / L"Client";
  bool committed = false;
  bool renameCommitted = false;
  try {
    if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
            workRoot, L"Client")) {
      throw std::runtime_error(
          "The ctSpaces data-folder path leaves insufficient legacy Windows "
          "path room for a temporary client stage");
    }
    if (!CreateDirectoryW(stagedClient.c_str(), nullptr) ||
        !CreateV2ClientSkeleton(stagedClient)) {
      throw std::runtime_error("The staged client container could not be marked");
    }
    const fs::path stagedSlot =
        stagedClient / L"Browsers" / GetBrowserId(browser);
    const std::wstring status =
        std::format(L"Creating {} profile...", GetBrowserDisplayName(browser));
    if (!CreateStagedBrowserSlot(stagedSlot, browser, status.c_str()))
      throw std::runtime_error("The staged browser profile failed validation");
    if (!IsV2ClientContainer(stagedClient) ||
        !IsValidBrowserSlot(stagedSlot, stagedSlot / L"Profile", browser)) {
      throw std::runtime_error("The staged client failed final validation");
    }
    if (fs::exists(targetRoot))
      throw std::runtime_error("The destination client appeared during staging");
    std::wstring clientPathBudgetError;
    if (!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
            stagedClient, targetRoot, &clientPathBudgetError)) {
      throw std::runtime_error(
          "The generated client would exceed the legacy Windows path budget");
    }
    if (!MoveFileExW(stagedClient.c_str(), targetRoot.c_str(),
                     MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Could not atomically commit the new client");
    }
    renameCommitted = true;
    fs::path slotRoot;
    if (!GetBrowserSlotPaths(targetRoot, browser, slotRoot, profileRoot) ||
        !IsValidBrowserSlot(slotRoot, profileRoot, browser)) {
      throw std::runtime_error("The committed new client failed validation");
    }
    committed = true;
  } catch (const std::exception &error) {
    errorDetails = AnsiToWide(error.what());
  }

  if (renameCommitted && !committed) {
    // See the slot path above: postcommit name-based deletion is not an
    // identity-bound rollback and could erase a raced replacement.
    errorDetails +=
        L" The committed client path was preserved because its identity "
        L"could not be proven safe for rollback. Restart ctSpaces before "
        L"using this client and inspect: " +
        targetRoot.wstring();
  }

  std::error_code cleanupError;
  fs::remove_all(workRoot, cleanupError);
  cleanupError.clear();
  if (fs::is_directory(workBase, cleanupError) &&
      fs::is_empty(workBase, cleanupError)) {
    fs::remove(workBase, cleanupError);
  }
  return committed;
}

static bool ResolveStandardBrowserProfile(
    const std::wstring &clientName, const fs::path &clientRoot,
    BrowserKind browser, fs::path &profileRoot, bool &existingSlot,
    std::wstring &errorDetails) {
  existingSlot = false;
  if (IsV2ClientContainer(clientRoot)) {
    fs::path slotRoot;
    fs::path existingProfile;
    if (!GetNestedBrowserSlotPaths(clientRoot, browser, slotRoot,
                                   existingProfile)) {
      errorDetails = L"The browser slot path is unsafe.";
      return false;
    }
    std::error_code existsError;
    existingSlot = fs::exists(slotRoot, existsError);
    if (existsError) {
      errorDetails = L"The browser slot could not be inspected.";
      return false;
    }
    return EnsureBrowserSlotForClient(clientName, clientRoot, browser,
                                      profileRoot, errorDetails);
  }

  std::optional<BrowserKind> legacyBinding;
  if (!IsSafeHybridClientRoot(clientRoot, &legacyBinding)) {
    errorDetails = L"The legacy client root or its Browsers child is unsafe.";
    return false;
  }

  if (IsChromiumBrowser(browser) && !legacyBinding) {
    std::wstring profileUseError;
    if (!RevalidateSafeClientContainerPath(clientName, clientRoot) ||
        ProbeExactBrowserProfileInUse(clientRoot, profileUseError) !=
            ProfileUseState::NotInUse ||
        !WriteDurableMarkerAtomically(
            clientRoot / kLegacyBrowserBindingMarkerName,
            GetLegacyBrowserBindingText(browser))) {
      errorDetails =
          L"The legacy client could not be bound durably to the selected "
          L"Chromium browser. Its profile data was left in place.";
      if (!profileUseError.empty())
        errorDetails += L"\n\n" + profileUseError;
      return false;
    }
    legacyBinding = browser;
  }

  if (legacyBinding && *legacyBinding == browser) {
    if (!RevalidateSafeClientContainerPath(clientName, clientRoot) ||
        !IsSafeHybridClientRoot(clientRoot)) {
      errorDetails = L"The bound legacy client failed final validation.";
      return false;
    }
    profileRoot = clientRoot;
    existingSlot = true;
    return true;
  }

  fs::path slotRoot;
  fs::path existingProfile;
  if (!GetNestedBrowserSlotPaths(clientRoot, browser, slotRoot,
                                 existingProfile)) {
    errorDetails = L"The hybrid browser slot path is unsafe.";
    return false;
  }
  std::error_code existsError;
  existingSlot = fs::exists(slotRoot, existsError);
  if (existsError) {
    errorDetails = L"The hybrid browser slot could not be inspected.";
    return false;
  }
  return EnsureBrowserSlotForClient(clientName, clientRoot, browser,
                                    profileRoot, errorDetails);
}

DWORD LaunchProfile(const std::wstring &clientName, bool isTemp,
                    bool isDefault, BrowserKind &browser,
                    const std::wstring &startupUrl, HANDLE *processHandle,
                    fs::path *launchedProfilePath,
                    fs::path *launchedExecutablePath,
                    std::wstring *launchError,
                    bool *freshTransientProfile) {
  if (processHandle)
    *processHandle = nullptr;
  if (launchedProfilePath)
    launchedProfilePath->clear();
  if (launchedExecutablePath)
    launchedExecutablePath->clear();
  if (launchError)
    launchError->clear();
  if (freshTransientProfile)
    *freshTransientProfile = false;

  fs::path profilePath;
  const auto profileUseBlocksAction =
      [&](const std::wstring &message) -> bool {
    std::wstring profileUseError;
    if (ProbeExactBrowserProfileInUse(profilePath, profileUseError) ==
        ProfileUseState::NotInUse) {
      return false;
    }

    std::wstring details = message;
    if (!profileUseError.empty())
      details += L"\n\n" + profileUseError;
    if (launchError) {
      *launchError = std::move(details);
    } else {
      MessageBoxW(g_hGui, details.c_str(), L"Cannot Open Profile",
                  MB_OK | MB_ICONWARNING);
    }
    return true;
  };

  if (isDefault && browser == BrowserKind::Firefox) {
    MessageBoxW(g_hGui,
                L"The Default profile editor currently supports Chromium "
                L"browsers only. Select Edge, Chrome, or Brave first.",
                L"Default Profile", MB_OK | MB_ICONINFORMATION);
    return 0;
  }

  bool existingStandardSlot = false;
  if (isTemp) {
    profilePath = g_sDataDir / "Temp";
  } else if (isDefault) {
    profilePath = g_sDataDir / "Default";
  } else {
    fs::path clientRoot;
    if (!TryGetSafeClientProfilePath(clientName, clientRoot)) {
      MessageBoxW(g_hGui,
                  L"The selected client folder is not a safe local profile.",
                  L"Cannot Open Profile", MB_OK | MB_ICONWARNING);
      return 0;
    }

    std::error_code existsError;
    const bool clientExists = fs::exists(clientRoot, existsError);
    if (existsError) {
      MessageBoxW(g_hGui, L"The client folder could not be inspected safely.",
                  L"Cannot Open Profile", MB_OK | MB_ICONERROR);
      return 0;
    }

    std::wstring errorDetails;
    if (!clientExists) {
      if (!CreateNewV2Client(clientName, browser, profilePath, errorDetails)) {
        std::wstring message = L"The new client could not be created safely.";
        if (!errorDetails.empty())
          message += L"\n\n" + errorDetails;
        MessageBoxW(g_hGui, message.c_str(), L"Cannot Open Profile",
                    MB_OK | MB_ICONERROR);
        return 0;
      }
    } else {
      if (!ResolveStandardBrowserProfile(
              clientName, clientRoot, browser, profilePath,
              existingStandardSlot, errorDetails)) {
        std::wstring message =
            L"The browser-specific client profile could not be resolved "
            L"safely. Existing legacy profile data was not moved or copied.";
        if (!errorDetails.empty())
          message += L"\n\n" + errorDetails;
        MessageBoxW(g_hGui, message.c_str(), L"Cannot Open Profile",
                    MB_OK | MB_ICONERROR);
        return 0;
      }
    }
  }

  if (isTemp || isDefault) {
    std::wstring cleanupDetails;
    if (!RemoveTransientProfileIfIdleAndVerify(profilePath, cleanupDetails)) {
      const wchar_t *profileLabel =
          isDefault ? L"Default Profile" : L"Temporary Profile";
      const wchar_t *message =
          isDefault
              ? L"The previous Default editor profile could not be removed. "
                L"Close any browser still using it, wait a moment, and try "
                L"again."
              : L"The previous temporary profile is still in use. Close its "
                L"browser window, wait a moment, and try again.";
      MessageBoxW(g_hGui, message, profileLabel, MB_OK | MB_ICONWARNING);
      return 0;
    }
    // From this point on, any data at this exact transient path was created by
    // this launch attempt. A failure may clean it without risking an older,
    // unsaved Default editor directory that we could not replace.
    if (freshTransientProfile)
      *freshTransientProfile = true;
  }

  if (isTemp || isDefault) {
    bool transientReady = false;
    if (browser == BrowserKind::Firefox) {
      transientReady = !isDefault &&
                       CreateDirectoryW(profilePath.c_str(), nullptr) != FALSE &&
                       IsSafeExistingDirectory(profilePath);
    } else {
      const wchar_t *status = isDefault ? L"Loading Default Profile..."
                                        : L"Loading Temp Profile...";
      transientReady = extDef(profilePath, status);
    }
    if (!transientReady) {
      MessageBoxW(g_hGui,
                  L"The disposable browser profile could not be created "
                  L"safely.",
                  L"Cannot Open Profile", MB_OK | MB_ICONERROR);
      return 0;
    }
  }

  const bool restoreLastSession =
      existingStandardSlot && ShouldRestoreTabsForClient(clientName, browser);
  if (existingStandardSlot && IsChromiumBrowser(browser)) {
    if (profileUseBlocksAction(
            L"The browser profile is already open or could not be inspected "
            L"safely. Its startup preferences were left unchanged.")) {
      return 0;
    }
    if (!SetBrowserStartupPreference(profilePath, restoreLastSession)) {
      MessageBoxW(g_hGui,
                  L"The Chromium startup preferences could not be updated "
                  L"safely. The profile was not opened.",
                  L"Cannot Open Profile", MB_OK | MB_ICONERROR);
      return 0;
    }
  }

  if (profileUseBlocksAction(
          L"The browser profile became active in another process or could "
          L"not be reverified immediately before launch.")) {
    return 0;
  }

  const fs::path executablePath = GetBrowserExecutable(browser);
  if (!isTemp && !isDefault) {
    fs::path activityRoot;
    if (!TryGetSafeClientProfilePath(clientName, activityRoot) ||
        !client_activity::Write(activityRoot, {client_activity::Now(), false})) {
      if (launchError)
        *launchError = L"The client activity date could not be saved safely. "
                       L"The browser was not opened; no client data was deleted.";
      return 0;
    }
  }
  const DWORD pid = StartBrowserProcess(browser, profilePath,
                                        restoreLastSession, startupUrl,
                                        processHandle, executablePath,
                                        launchError);
  if (pid == 0)
    return 0;
  if (launchedProfilePath)
    *launchedProfilePath = profilePath;
  if (launchedExecutablePath)
    *launchedExecutablePath = executablePath;
  return pid;
}

void UpdateClientsComboBox() {
  SendMessage(g_hComboClient, CB_RESETCONTENT, 0, 0);
  PruneArchivedClients();
  fs::path sitesDir = g_sDataDir / "Sites";
  std::vector<std::wstring> clientNames;
  try {
    if (fs::exists(sitesDir) && fs::is_directory(sitesDir)) {
      for (const auto &entry : fs::directory_iterator(sitesDir)) {
        fs::path safeProfilePath;
        const std::wstring clientName = entry.path().filename().wstring();
        if (entry.is_directory() &&
            TryGetSafeClientProfilePath(clientName, safeProfilePath)) {
          // No retrospective guesses from filesystem dates on upgrade.
          client_activity::Write(safeProfilePath,
                                 {client_activity::Now(), true}, true);
          if (!IsClientArchived(clientName))
            clientNames.push_back(clientName);
        }
      }
    }
  } catch (...) {
  }
  std::sort(clientNames.begin(), clientNames.end(), CaseInsensitiveLess{});
  for (const auto &clientName : clientNames)
    SendMessageW(g_hComboClient, CB_ADDSTRING, 0, (LPARAM)clientName.c_str());

  const bool hadPinnedClients = !g_pinnedClients.empty();
  PrunePinnedClients();
  PruneRestoreTabsPreferences();
  UpdatePinButtonState();
  UpdateRestoreTabsToggleState();
  if (g_hGui) {
    if (hadPinnedClients != !g_pinnedClients.empty()) {
      ResizeMainWindowForDpi(g_hGui, GetDpiForWindow(g_hGui));
      InvalidateRect(g_hGui, nullptr, TRUE);
    } else {
      InvalidateRect(g_hGui, &g_rcPinnedArea, TRUE);
    }
  }
}
std::wstring SanitizeName(const std::wstring &name) {
  std::wstring sanitized = name;
  const std::wstring whitespace = L" \t\n\r\f\v";

  const auto trim = [&whitespace](std::wstring &value) {
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::wstring::npos) {
      value.clear();
      return;
    }
    value.erase(0, first);
    const size_t last = value.find_last_not_of(whitespace);
    value.erase(last + 1);
  };

  trim(sanitized);
  std::wregex invalidChars(LR"([\\/:*?"<>|])");
  sanitized = std::regex_replace(sanitized, invalidChars, L"");
  sanitized.erase(
      std::remove_if(sanitized.begin(), sanitized.end(),
                     [](wchar_t ch) { return ch < 32; }),
      sanitized.end());
  while (!sanitized.empty() &&
         (sanitized.back() == L'.' || sanitized.back() == L' ')) {
    sanitized.pop_back();
  }
  trim(sanitized);

  std::wregex reservedNames(
      L"^(CON|PRN|AUX|NUL|CLOCK\\$|CONIN\\$|CONOUT\\$|"
      L"COM(?:[1-9]|\u00B9|\u00B2|\u00B3)|"
      L"LPT(?:[1-9]|\u00B9|\u00B2|\u00B3))(\\..*)?$",
                              std::regex::icase);
  if (sanitized.size() > kMaxClientNameLength ||
      std::regex_match(sanitized, reservedNames) ||
      _wcsicmp(sanitized.c_str(), L"Default") == 0 ||
      _wcsicmp(sanitized.c_str(), L"Temp") == 0) {
    return L"";
  }
  return sanitized;
}

std::wstring ResolveExistingClientName(const std::wstring &name) {
  const std::wstring sanitized = SanitizeName(name);
  if (sanitized.empty())
    return L"";

  const fs::path sitesDir = g_sDataDir / L"Sites";
  try {
    if (fs::exists(sitesDir) && fs::is_directory(sitesDir)) {
      for (const auto &entry : fs::directory_iterator(sitesDir)) {
        if (!entry.is_directory())
          continue;
        const std::wstring existing = entry.path().filename().wstring();
        fs::path safeProfilePath;
        if (_wcsicmp(existing.c_str(), sanitized.c_str()) == 0 &&
            TryGetSafeClientProfilePath(existing, safeProfilePath))
          return existing;
      }
    }
  } catch (...) {
  }

  return sanitized;
}

static bool IsExistingClientProfile(const std::wstring &clientName) {
  if (clientName.empty())
    return false;
  try {
    fs::path profilePath;
    if (!TryGetSafeClientProfilePath(clientName, profilePath))
      return false;
    return fs::exists(profilePath) && fs::is_directory(profilePath);
  } catch (...) {
    return false;
  }
}

static config_persistence::DirectChildDirectoryState
ProbeClientProfileForPruning(const std::wstring &storedName,
                             std::wstring &resolvedName) {
  resolvedName.clear();
  const std::wstring sanitized = SanitizeName(storedName);
  if (sanitized.empty()) {
    return config_persistence::DirectChildDirectoryState::MissingOrInvalid;
  }

  const auto state = config_persistence::ProbeDirectChildDirectory(
      g_sDataDir / L"Sites", sanitized, resolvedName);
  if (state == config_persistence::DirectChildDirectoryState::Present &&
      (resolvedName.empty() || SanitizeName(resolvedName) != resolvedName)) {
    resolvedName.clear();
    return config_persistence::DirectChildDirectoryState::MissingOrInvalid;
  }
  return state;
}

static auto FindPinnedClient(const std::wstring &clientName) {
  return std::find_if(
      g_pinnedClients.begin(), g_pinnedClients.end(),
      [&clientName](const std::wstring &pinnedName) {
        return _wcsicmp(pinnedName.c_str(), clientName.c_str()) == 0;
      });
}

static bool IsClientPinned(const std::wstring &clientName) {
  return FindPinnedClient(clientName) != g_pinnedClients.end();
}

static bool SavePinnedClients() {
  if (g_sConfigPath.empty())
    return false;

  std::vector<config_persistence::IniMutation> mutations;
  mutations.push_back({L"pinned", std::nullopt, std::nullopt});
  const std::wstring count = std::to_wstring(g_pinnedClients.size());
  mutations.push_back(
      {L"pinned", std::wstring(L"count"), count});
  for (size_t i = 0; i < g_pinnedClients.size(); ++i) {
    const std::wstring key = std::format(L"client{}", i);
    mutations.push_back({L"pinned", key, g_pinnedClients[i]});
  }
  return SaveConfigMutations(L"pinned-client list", mutations);
}

static void PrunePinnedClients() {
  bool changed = false;
  std::vector<std::wstring> validClients;
  validClients.reserve(g_pinnedClients.size());

  for (const auto &storedName : g_pinnedClients) {
    std::wstring clientName;
    const auto profileState =
        ProbeClientProfileForPruning(storedName, clientName);
    if (profileState ==
        config_persistence::DirectChildDirectoryState::Indeterminate) {
      return;
    }
    const bool duplicate = std::any_of(
        validClients.begin(), validClients.end(),
        [&clientName](const std::wstring &existing) {
          return _wcsicmp(existing.c_str(), clientName.c_str()) == 0;
        });
    if (profileState ==
            config_persistence::DirectChildDirectoryState::Present &&
        !duplicate &&
        !IsClientArchived(clientName)) {
      validClients.push_back(clientName);
      changed = changed || clientName != storedName;
    } else {
      changed = true;
    }
  }

  if (validClients.size() > MAX_PINNED_CLIENTS) {
    validClients.resize(MAX_PINNED_CLIENTS);
    changed = true;
  }
  if (changed) {
    std::vector<std::wstring> originalClients = g_pinnedClients;
    g_pinnedClients = std::move(validClients);
    if (!SavePinnedClients())
      g_pinnedClients = std::move(originalClients);
  }
}

static void LoadPinnedClients() {
  g_pinnedClients.clear();
  if (g_sConfigPath.empty() || !HasReadableConfigFile())
    return;

  const UINT count = std::min<UINT>(
      GetPrivateProfileIntW(L"pinned", L"count", 0, g_sConfigPath.c_str()),
      (UINT)MAX_PINNED_CLIENTS);
  for (UINT i = 0; i < count; ++i) {
    wchar_t nameBuffer[256]{};
    const std::wstring key = std::format(L"client{}", i);
    GetPrivateProfileStringW(L"pinned", key.c_str(), L"", nameBuffer,
                             (DWORD)std::size(nameBuffer),
                             g_sConfigPath.c_str());
    if (nameBuffer[0])
      g_pinnedClients.emplace_back(nameBuffer);
  }
  PrunePinnedClients();
}

static bool ContainsClientName(const std::vector<std::wstring> &clients,
                               const std::wstring &clientName) {
  return std::any_of(
      clients.begin(), clients.end(),
      [&clientName](const std::wstring &storedName) {
        return _wcsicmp(storedName.c_str(), clientName.c_str()) == 0;
      });
}

static std::wstring MakeClientBrowserPreferenceKey(
    const std::wstring &clientName, BrowserKind browser) {
  return clientName + L"|" + GetBrowserId(browser);
}

static bool TryParseClientBrowserPreferenceKey(
    const std::wstring &storedKey, std::wstring &clientName,
    BrowserKind &browser) {
  const size_t separator = storedKey.rfind(L'|');
  if (separator == std::wstring::npos || separator == 0)
    return false;
  const std::wstring browserId = storedKey.substr(separator + 1);
  for (BrowserKind candidate : {BrowserKind::Edge, BrowserKind::Chrome,
                                BrowserKind::Brave, BrowserKind::Firefox}) {
    if (_wcsicmp(browserId.c_str(), GetBrowserId(candidate)) == 0) {
      clientName = storedKey.substr(0, separator);
      browser = candidate;
      return true;
    }
  }
  return false;
}

static BrowserKind GetBrowserForCurrentSelection() {
  if (g_iSelectedSessionTab > 0 &&
      g_iSelectedSessionTab <= (int)g_sessions.size()) {
    return g_sessions[g_iSelectedSessionTab - 1].browser;
  }
  return g_selectedBrowser;
}

static bool IsClientArchived(const std::wstring &clientName) {
  return ContainsClientName(g_archivedClients, clientName);
}

static bool SaveArchivedClients() {
  if (g_sConfigPath.empty())
    return false;

  std::vector<config_persistence::IniMutation> mutations;
  mutations.push_back({L"archived", std::nullopt, std::nullopt});
  const std::wstring count = std::to_wstring(g_archivedClients.size());
  mutations.push_back(
      {L"archived", std::wstring(L"count"), count});
  for (size_t i = 0; i < g_archivedClients.size(); ++i) {
    const std::wstring key = std::format(L"client{}", i);
    mutations.push_back({L"archived", key, g_archivedClients[i]});
  }
  return SaveConfigMutations(L"archived-client list", mutations);
}

static void PruneArchivedClients() {
  std::vector<std::wstring> validClients;
  validClients.reserve(g_archivedClients.size());
  bool changed = false;
  for (const auto &storedName : g_archivedClients) {
    std::wstring clientName;
    const auto profileState =
        ProbeClientProfileForPruning(storedName, clientName);
    if (profileState ==
        config_persistence::DirectChildDirectoryState::Indeterminate) {
      return;
    }
    if (profileState ==
            config_persistence::DirectChildDirectoryState::Present &&
        !ContainsClientName(validClients, clientName)) {
      validClients.push_back(clientName);
      changed = changed || clientName != storedName;
    } else {
      changed = true;
    }
  }
  if (validClients.size() > MAX_ARCHIVED_CLIENTS) {
    validClients.resize(MAX_ARCHIVED_CLIENTS);
    changed = true;
  }
  if (changed) {
    std::vector<std::wstring> originalClients = g_archivedClients;
    g_archivedClients = std::move(validClients);
    if (!SaveArchivedClients())
      g_archivedClients = std::move(originalClients);
  }
}

static void LoadArchivedClients() {
  g_archivedClients.clear();
  if (g_sConfigPath.empty() || !HasReadableConfigFile())
    return;

  const UINT count = std::min<UINT>(
      GetPrivateProfileIntW(L"archived", L"count", 0,
                            g_sConfigPath.c_str()),
      MAX_ARCHIVED_CLIENTS);
  for (UINT i = 0; i < count; ++i) {
    wchar_t nameBuffer[256]{};
    const std::wstring key = std::format(L"client{}", i);
    GetPrivateProfileStringW(L"archived", key.c_str(), L"", nameBuffer,
                             (DWORD)std::size(nameBuffer),
                             g_sConfigPath.c_str());
    if (nameBuffer[0])
      g_archivedClients.emplace_back(nameBuffer);
  }
  PruneArchivedClients();
}

static bool SaveRestoreTabsPreferences() {
  if (g_sConfigPath.empty())
    return false;

  std::vector<std::wstring> disabledClients;
  {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    disabledClients = g_clientsWithoutTabRestore;
  }

  std::vector<config_persistence::IniMutation> mutations;
  mutations.push_back({L"restore_tabs", std::nullopt, std::nullopt});
  const std::wstring count = std::to_wstring(disabledClients.size());
  mutations.push_back(
      {L"restore_tabs", std::wstring(L"disabled_count"), count});
  for (size_t i = 0; i < disabledClients.size(); ++i) {
    const std::wstring key = std::format(L"disabled_client{}", i);
    mutations.push_back({L"restore_tabs", key, disabledClients[i]});
  }
  return SaveConfigMutations(L"Restore tabs preference", mutations);
}

static void PruneRestoreTabsPreferences() {
  std::vector<std::wstring> storedClients;
  {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    storedClients = g_clientsWithoutTabRestore;
  }

  std::vector<std::wstring> validClients;
  validClients.reserve(storedClients.size());
  for (const auto &storedKey : storedClients) {
    std::wstring storedName;
    BrowserKind browser = g_selectedBrowser;
    const bool browserQualified =
        TryParseClientBrowserPreferenceKey(storedKey, storedName, browser);
    if (!browserQualified)
      storedName = storedKey;
    std::wstring clientName;
    const auto profileState =
        ProbeClientProfileForPruning(storedName, clientName);
    if (profileState ==
        config_persistence::DirectChildDirectoryState::Indeterminate) {
      return;
    }
    const std::wstring normalizedKey =
        MakeClientBrowserPreferenceKey(clientName, browser);
    if (profileState ==
            config_persistence::DirectChildDirectoryState::Present &&
        !ContainsClientName(validClients, normalizedKey)) {
      validClients.push_back(normalizedKey);
    }
  }
  if (validClients.size() > MAX_RESTORE_TAB_PREFERENCES)
    validClients.resize(MAX_RESTORE_TAB_PREFERENCES);

  bool changed = validClients.size() != storedClients.size();
  if (!changed) {
    for (size_t i = 0; i < validClients.size(); ++i) {
      if (validClients[i] != storedClients[i]) {
        changed = true;
        break;
      }
    }
  }
  if (!changed)
    return;

  {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    g_clientsWithoutTabRestore = std::move(validClients);
  }
  if (!SaveRestoreTabsPreferences()) {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    g_clientsWithoutTabRestore = std::move(storedClients);
  }
}

static void LoadRestoreTabsPreferences() {
  std::vector<std::wstring> disabledClients;
  if (!g_sConfigPath.empty() && HasReadableConfigFile()) {
    const UINT count = std::min<UINT>(
        GetPrivateProfileIntW(L"restore_tabs", L"disabled_count", 0,
                              g_sConfigPath.c_str()),
        MAX_RESTORE_TAB_PREFERENCES);
    disabledClients.reserve(count);
    for (UINT i = 0; i < count; ++i) {
      wchar_t nameBuffer[256]{};
      const std::wstring key = std::format(L"disabled_client{}", i);
      GetPrivateProfileStringW(L"restore_tabs", key.c_str(), L"", nameBuffer,
                               (DWORD)std::size(nameBuffer),
                               g_sConfigPath.c_str());
      if (nameBuffer[0] &&
          !ContainsClientName(disabledClients, nameBuffer)) {
        disabledClients.emplace_back(nameBuffer);
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    g_clientsWithoutTabRestore = std::move(disabledClients);
  }
  PruneRestoreTabsPreferences();
}

static bool ShouldRestoreTabsForClient(const std::wstring &clientName,
                                       BrowserKind browser) {
  if (clientName.empty())
    return false;
  const std::wstring preferenceKey =
      MakeClientBrowserPreferenceKey(clientName, browser);
  std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
  return !ContainsClientName(g_clientsWithoutTabRestore, preferenceKey);
}

static void UpdateRestoreTabsToggleState() {
  if (!g_hBtnRestoreTabs)
    return;

  const std::wstring clientName = GetSelectedClientNameSanitized(false);
  const BrowserKind browser = GetBrowserForCurrentSelection();
  g_bRestoreTabsToggleAvailable = IsExistingClientProfile(clientName);
  g_bRestoreTabsForSelection =
      g_bRestoreTabsToggleAvailable &&
      ShouldRestoreTabsForClient(clientName, browser);
  EnableWindow(g_hBtnRestoreTabs,
               g_bUiEnabled && g_bRestoreTabsToggleAvailable);
  RedrawWindow(g_hBtnRestoreTabs, nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
}

static void ToggleRestoreTabsForSelectedClient() {
  const std::wstring clientName = GetSelectedClientNameSanitized(false);
  const BrowserKind browser = GetBrowserForCurrentSelection();
  if (!IsExistingClientProfile(clientName))
    return;
  const std::wstring preferenceKey =
      MakeClientBrowserPreferenceKey(clientName, browser);

  std::vector<std::wstring> originalPreferences;
  {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    originalPreferences = g_clientsWithoutTabRestore;
    const auto disabled = std::find_if(
        g_clientsWithoutTabRestore.begin(),
        g_clientsWithoutTabRestore.end(),
        [&preferenceKey](const std::wstring &storedName) {
          return _wcsicmp(storedName.c_str(), preferenceKey.c_str()) == 0;
        });
    if (disabled == g_clientsWithoutTabRestore.end()) {
      if (g_clientsWithoutTabRestore.size() >=
          MAX_RESTORE_TAB_PREFERENCES) {
        MessageBeep(MB_ICONWARNING);
        return;
      }
      g_clientsWithoutTabRestore.push_back(preferenceKey);
    } else {
      g_clientsWithoutTabRestore.erase(disabled);
    }
  }

  if (!SaveRestoreTabsPreferences()) {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    g_clientsWithoutTabRestore = std::move(originalPreferences);
  }
  UpdateRestoreTabsToggleState();
}

static void UpdatePinButtonState() {
  if (!g_hBtnPin)
    return;

  const std::wstring clientName = GetSelectedClientNameSanitized(false);
  const bool canPin = IsExistingClientProfile(clientName);
  const bool isPinned = canPin && IsClientPinned(clientName);
  const wchar_t *primaryText =
      g_iSelectedSessionTab > 0
          ? L"Show"
          : (!clientName.empty() && !canPin ? L"Create" : L"Open");
  if (g_hBtnGo)
    SetWindowTextW(g_hBtnGo, primaryText);
  EnableWindow(g_hBtnPin, g_bUiEnabled && canPin);
  g_sPinTooltip = isPinned ? L"Remove from pinned clients"
                           : L"Add to pinned clients";

  if (g_hBtnPinTip) {
    TOOLINFOW tip{sizeof(TOOLINFOW)};
    tip.hwnd = g_hGui;
    tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tip.uId = (UINT_PTR)g_hBtnPin;
    tip.lpszText = g_sPinTooltip.data();
    SendMessageW(g_hBtnPinTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&tip);
  }
  RedrawWindow(g_hBtnPin, nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
}

static void ToggleSelectedClientPin() {
  const std::wstring clientName = GetSelectedClientNameSanitized(false);
  if (!IsExistingClientProfile(clientName))
    return;

  const bool hadPinnedClients = !g_pinnedClients.empty();
  const std::vector<std::wstring> originalPinnedClients = g_pinnedClients;
  const auto pinned = FindPinnedClient(clientName);
  if (pinned != g_pinnedClients.end()) {
    g_pinnedClients.erase(pinned);
  } else {
    if (g_pinnedClients.size() >= MAX_PINNED_CLIENTS) {
      MessageBoxW(g_hGui,
                  L"You can pin up to eight clients. Unpin one before adding "
                  L"another.",
                  L"Pinned Clients", MB_OK | MB_ICONINFORMATION);
      return;
    }
    g_pinnedClients.push_back(clientName);
  }

  if (!SavePinnedClients())
    g_pinnedClients = originalPinnedClients;
  UpdatePinButtonState();
  if (g_hGui && hadPinnedClients != !g_pinnedClients.empty()) {
    ResizeMainWindowForDpi(g_hGui, GetDpiForWindow(g_hGui));
    InvalidateRect(g_hGui, nullptr, TRUE);
  } else if (g_hGui) {
    InvalidateRect(g_hGui, &g_rcPinnedArea, TRUE);
  }
}

static bool SelectPinnedClient(const std::wstring &clientName) {
  if (!g_bUiEnabled || !IsExistingClientProfile(clientName))
    return false;

  SelectSessionTab(0, false);
  const LRESULT index = SendMessageW(g_hComboClient, CB_FINDSTRINGEXACT,
                                     (WPARAM)-1, (LPARAM)clientName.c_str());
  if (index != CB_ERR)
    SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)index, 0);
  SetClientInputText(clientName);
  UpdateIconPreviewForSelection(true);
  UpdatePinButtonState();
  return true;
}

static void OpenPinnedClient(const std::wstring &clientName,
                             BrowserKind browser) {
  if (!g_bUiEnabled || !IsExistingClientProfile(clientName))
    return;

  for (int i = 0; i < (int)g_sessions.size(); ++i) {
    if (_wcsicmp(g_sessions[i].clientName.c_str(), clientName.c_str()) == 0 &&
        g_sessions[i].browser == browser) {
      SelectSessionTab(i + 1, true);
      return;
    }
  }

  if (SelectPinnedClient(clientName)) {
    const auto active = GetActiveProfile(clientName, browser);
    if (active)
      BringSessionToFront(active->pid);
    else
      LaunchProfileAsync(clientName, false, false, browser);
  }
}

static bool ReplaceClientName(std::vector<std::wstring> &clients,
                              const std::wstring &oldName,
                              const std::wstring &newName) {
  bool changed = false;
  for (auto &storedName : clients) {
    if (_wcsicmp(storedName.c_str(), oldName.c_str()) == 0) {
      storedName = newName;
      changed = true;
      continue;
    }
    std::wstring qualifiedClient;
    BrowserKind browser = BrowserKind::Edge;
    if (TryParseClientBrowserPreferenceKey(storedName, qualifiedClient,
                                           browser) &&
        _wcsicmp(qualifiedClient.c_str(), oldName.c_str()) == 0) {
      storedName = MakeClientBrowserPreferenceKey(newName, browser);
      changed = true;
    }
  }
  return changed;
}

static bool IsClientActive(const std::wstring &clientName) {
  return IsClientActiveAnyBrowser(clientName);
}

static std::optional<std::wstring> GetClipboardWebUrl() {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) ||
      !OpenClipboard(g_hGui)) {
    return std::nullopt;
  }

  std::optional<std::wstring> result;
  HANDLE clipboardData = GetClipboardData(CF_UNICODETEXT);
  if (clipboardData) {
    const wchar_t *text =
        static_cast<const wchar_t *>(GlobalLock(clipboardData));
    if (text) {
      const size_t length = wcsnlen_s(text, 8193);
      if (length > 0 && length <= 8192) {
        std::wstring url(text, length);
        while (!url.empty() && std::iswspace(url.front()))
          url.erase(url.begin());
        while (!url.empty() && std::iswspace(url.back()))
          url.pop_back();
        if (IsValidWebUrl(url))
          result = std::move(url);
      }
      GlobalUnlock(clipboardData);
    }
  }
  CloseClipboard();
  return result;
}

static bool OpenWebUrlForClient(const std::wstring &clientName,
                                const std::wstring &url,
                                BrowserKind browser) {
  if (!IsExistingClientProfile(clientName) || IsClientArchived(clientName) ||
      !IsValidWebUrl(url)) {
    return false;
  }

  fs::path clientRoot;
  if (!TryGetSafeClientProfilePath(clientName, clientRoot))
    return false;

  const auto active = GetActiveProfile(clientName, browser);
  if (active) {
    if (browser == BrowserKind::Firefox) {
      BringSessionToFront(active->pid);
      MessageBoxW(g_hGui,
                  L"This Firefox client is already open. Firefox's isolated "
                  L"--no-remote session cannot accept another command-line "
                  L"URL, so ctSpaces did not start a second process.",
                  L"Firefox Client Already Open",
                  MB_OK | MB_ICONINFORMATION);
      return false;
    }
    std::wstring launchError;
    if (StartBrowserProcess(browser, active->profilePath, false, url, nullptr,
                            active->executablePath, &launchError) == 0) {
      if (launchError.empty())
        launchError = L"The selected browser could not open the URL.";
      MessageBoxW(g_hGui, launchError.c_str(), L"Cannot Open URL",
                  MB_OK | MB_ICONERROR);
      return false;
    }
    BringSessionToFront(active->pid);
    return true;
  }

  if (!SelectPinnedClient(clientName))
    return false;
  LaunchProfileAsync(clientName, false, false, browser, url);
  return true;
}

static std::wstring GetHistoricalClientShortcutSuffix(
    std::optional<BrowserKind> browser) {
  return browser ? std::wstring(L" - ") + GetBrowserDisplayName(*browser) +
                       L" - ctSpaces.lnk"
                 : L" - ctSpaces.lnk";
}

static std::wstring
GetClientShortcutFileName(const std::wstring &clientName) {
  return clientName + L".lnk";
}

static std::wstring GetHistoricalClientShortcutFileName(
    const std::wstring &clientName, std::optional<BrowserKind> browser) {
  return client_shortcut_name::Build(
      clientName, GetHistoricalClientShortcutSuffix(browser));
}

static std::optional<size_t>
GetClientShortcutFileNameBudget(const fs::path &directory) {
  try {
    std::error_code error;
    const fs::path normalizedDirectory =
        fs::absolute(directory, error).lexically_normal();
    if (error || normalizedDirectory.empty())
      return std::nullopt;

    // Measuring a one-character child accounts for a root/trailing separator
    // exactly, instead of assuming every Desktop path has the same form.
    const size_t oneCharacterChildLength =
        (normalizedDirectory / L"x").native().size();
    if (oneCharacterChildLength == 0 ||
        oneCharacterChildLength - 1 >=
            CtBackup::Detail::kLegacyMaximumFilePathCharacters) {
      return std::nullopt;
    }
    const size_t prefixLength = oneCharacterChildLength - 1;
    return (std::min)(
        client_shortcut_name::kMaxFileNameLength,
        CtBackup::Detail::kLegacyMaximumFilePathCharacters - prefixLength);
  } catch (...) {
    return std::nullopt;
  }
}

static std::optional<fs::path> GetClientShortcutPathInDirectory(
    const fs::path &directory, const std::wstring &clientName) {
  const auto budget = GetClientShortcutFileNameBudget(directory);
  if (!budget)
    return std::nullopt;
  const std::wstring fileName = GetClientShortcutFileName(clientName);
  if (fileName.size() > *budget)
    return std::nullopt;
  return directory / fileName;
}

static std::optional<fs::path> GetHistoricalClientShortcutPathInDirectory(
    const fs::path &directory, const std::wstring &clientName,
    std::optional<BrowserKind> browser) {
  const auto budget = GetClientShortcutFileNameBudget(directory);
  if (!budget)
    return std::nullopt;
  const std::wstring fileName = client_shortcut_name::Build(
      clientName, GetHistoricalClientShortcutSuffix(browser), *budget);
  if (fileName.empty())
    return std::nullopt;
  return directory / fileName;
}

// Compatibility with the short-lived 32-bit truncation scheme. Candidate
// lookup also includes the original full filename below, so installed links
// from every prior format remain renameable/deletable after an upgrade.
static std::wstring GetPriorTruncatedClientShortcutFileName(
    const std::wstring &clientName, std::optional<BrowserKind> browser) {
  std::wstring baseName = clientName;
  if (baseName.size() > 210) {
    uint32_t hash = 2166136261u;
    for (wchar_t ch : clientName) {
      hash ^= static_cast<uint32_t>(towlower(ch));
      hash *= 16777619u;
    }
    baseName = baseName.substr(0, 196) +
               std::format(L"-{:08X}", hash);
  }
  return baseName + GetHistoricalClientShortcutSuffix(browser);
}

static std::optional<fs::path> GetClientShortcutDirectory() {
  if (!g_sShortcutDesktopOverride.empty())
    return g_sShortcutDesktopOverride;

  PWSTR desktopText = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                                  &desktopText)) ||
      !desktopText) {
    return std::nullopt;
  }
  fs::path desktopPath(desktopText);
  CoTaskMemFree(desktopText);
  return desktopPath;
}

static std::optional<fs::path>
GetClientDesktopShortcutPath(const std::wstring &clientName) {
  const auto directory = GetClientShortcutDirectory();
  if (!directory)
    return std::nullopt;
  return GetClientShortcutPathInDirectory(*directory, clientName);
}

static std::vector<fs::path> GetClientDesktopShortcutCandidatePaths(
    const std::wstring &clientName, std::optional<BrowserKind> browser) {
  std::vector<fs::path> candidates;
  const auto directory = GetClientShortcutDirectory();
  if (!directory)
    return candidates;

  const auto appendUnique = [&candidates](const fs::path &candidate) {
    const auto duplicate = std::find_if(
        candidates.begin(), candidates.end(), [&candidate](const fs::path &v) {
          return _wcsicmp(v.c_str(), candidate.c_str()) == 0;
        });
    if (duplicate == candidates.end())
      candidates.push_back(candidate);
  };

  const auto currentPath = GetClientShortcutPathInDirectory(*directory,
                                                            clientName);
  if (currentPath)
    appendUnique(*currentPath);
  const auto historicalPath = GetHistoricalClientShortcutPathInDirectory(
      *directory, clientName, browser);
  if (historicalPath)
    appendUnique(*historicalPath);
  // A shortcut created before Desktop redirection used the filename budget of
  // its former absolute directory. Generate every possible collision-resistant
  // historical truncation so moving the Desktop cannot orphan an owned link.
  const std::wstring historicalSuffix =
      GetHistoricalClientShortcutSuffix(browser);
  constexpr size_t kHistoricalHashTokenLength = 17; // '~' plus 16 hex digits.
  const size_t firstHashedBudget =
      historicalSuffix.size() + kHistoricalHashTokenLength;
  for (size_t budget = firstHashedBudget;
       budget <= client_shortcut_name::kMaxFileNameLength; ++budget) {
    const std::wstring historicalName =
        client_shortcut_name::Build(clientName, historicalSuffix, budget);
    if (!historicalName.empty())
      appendUnique(*directory / historicalName);
  }
  // Compatibility with the prior fixed 240-unit browser-qualified/neutral
  // name. This can be too long at the current Desktop location, in which case
  // Win32 inspection simply fails closed; retaining the candidate lets shorter
  // old paths keep their rename/delete behavior.
  appendUnique(*directory /
               GetHistoricalClientShortcutFileName(clientName, browser));
  appendUnique(*directory /
               GetPriorTruncatedClientShortcutFileName(clientName, browser));
  appendUnique(*directory /
               (clientName + GetHistoricalClientShortcutSuffix(browser)));
  return candidates;
}

static bool IsManagedClientDesktopShortcut(const fs::path &shortcutPath,
                                           const std::wstring &clientName,
                                           std::optional<BrowserKind> browser)
    noexcept {
  try {
    if (!IsSafeExistingRegularFile(shortcutPath))
      return false;

    // Allocate before acquiring COM interfaces so an allocation failure cannot
    // leak a live Shell Link pointer. Every later potentially throwing C++
    // operation runs after the interfaces have been released.
    std::vector<wchar_t> targetText(32768, L'\0');
    std::vector<wchar_t> argumentText(32768, L'\0');

    IShellLinkW *link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&link));
    if (FAILED(result) || !link)
      return false;

    IPersistFile *persist = nullptr;
    result = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(result) && persist) {
      result = persist->Load(shortcutPath.c_str(), STGM_READ);
      persist->Release();
    }

    if (SUCCEEDED(result))
      result = link->GetPath(targetText.data(), (int)targetText.size(), nullptr,
                             SLGP_RAWPATH);
    if (SUCCEEDED(result))
      result = link->GetArguments(argumentText.data(),
                                  (int)argumentText.size());
    link->Release();
    if (FAILED(result) || !targetText[0])
      return false;

    const fs::path currentExecutable = GetCurrentExecutablePath();
    std::wstring expectedArguments =
        L"--client " + QuoteCommandLineArgument(clientName);
    if (browser)
      expectedArguments += L" --browser " +
                           QuoteCommandLineArgument(GetBrowserId(*browser));
    return !currentExecutable.empty() &&
           SameExecutablePath(targetText.data(), currentExecutable) &&
           expectedArguments == argumentText.data();
  } catch (...) {
    // Shortcut ownership is a security boundary. Any indeterminate check must
    // preserve the file and follow the caller's normal recovery path.
    return false;
  }
}

static bool IsManagedClientDesktopShortcutForClient(
    const fs::path &shortcutPath, const std::wstring &clientName) noexcept {
  for (BrowserKind browser : {BrowserKind::Edge, BrowserKind::Chrome,
                              BrowserKind::Brave, BrowserKind::Firefox}) {
    if (IsManagedClientDesktopShortcut(shortcutPath, clientName, browser))
      return true;
  }
  return IsManagedClientDesktopShortcut(shortcutPath, clientName,
                                        std::nullopt);
}

struct SafeRegularFileSignature {
  DWORD volumeSerialNumber = 0;
  DWORD fileIndexHigh = 0;
  DWORD fileIndexLow = 0;
  DWORD fileSizeHigh = 0;
  DWORD fileSizeLow = 0;
  FILETIME creationTime{};
  FILETIME lastWriteTime{};
};

class ScopedKernelHandle {
public:
  explicit ScopedKernelHandle(HANDLE value = INVALID_HANDLE_VALUE)
      : value_(value) {}
  ~ScopedKernelHandle() {
    if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr)
      CloseHandle(value_);
  }
  ScopedKernelHandle(const ScopedKernelHandle &) = delete;
  ScopedKernelHandle &operator=(const ScopedKernelHandle &) = delete;
  HANDLE get() const { return value_; }
  explicit operator bool() const {
    return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
  }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

static bool SameSafeRegularFileSignature(
    const SafeRegularFileSignature &left,
    const SafeRegularFileSignature &right) {
  // Windows file-name tunneling can replace a file's creation time when it is
  // renamed into a name that was just vacated. Creation time is therefore not
  // stable identity across the no-replace shortcut transaction. File ID,
  // size, and last-write time remain required so raced changes still fail
  // closed.
  return left.volumeSerialNumber == right.volumeSerialNumber &&
         left.fileIndexHigh == right.fileIndexHigh &&
         left.fileIndexLow == right.fileIndexLow &&
         left.fileSizeHigh == right.fileSizeHigh &&
         left.fileSizeLow == right.fileSizeLow &&
         CompareFileTime(&left.lastWriteTime, &right.lastWriteTime) == 0;
}

static bool ReadSafeRegularFileSignature(
    HANDLE fileHandle, SafeRegularFileSignature &signature,
    std::wstring *errorDetails = nullptr) noexcept {
  try {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(fileHandle, &info)) {
      if (errorDetails) {
        *errorDetails = std::format(
            L"The file identity could not be read (Windows error {}).",
            GetLastError());
      }
      return false;
    }
    if ((info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
      if (errorDetails)
        *errorDetails = L"The path is not a safe regular file.";
      return false;
    }
    signature.volumeSerialNumber = info.dwVolumeSerialNumber;
    signature.fileIndexHigh = info.nFileIndexHigh;
    signature.fileIndexLow = info.nFileIndexLow;
    signature.fileSizeHigh = info.nFileSizeHigh;
    signature.fileSizeLow = info.nFileSizeLow;
    signature.creationTime = info.ftCreationTime;
    signature.lastWriteTime = info.ftLastWriteTime;
    return true;
  } catch (...) {
    // Callers holding a file handle must be able to close it before reporting
    // an allocation failure from diagnostic construction.
    return false;
  }
}

static bool TryGetSafeRegularFileSignature(
    const fs::path &path, SafeRegularFileSignature &signature,
    std::wstring *errorDetails = nullptr) {
  HANDLE fileHandle = CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (fileHandle == INVALID_HANDLE_VALUE) {
    if (errorDetails) {
      *errorDetails = std::format(
          L"The file could not be opened safely (Windows error {}).",
          GetLastError());
    }
    return false;
  }
  const bool read =
      ReadSafeRegularFileSignature(fileHandle, signature, errorDetails);
  CloseHandle(fileHandle);
  return read;
}

static HANDLE OpenLockedMatchingRegularFile(
    const fs::path &path, const SafeRegularFileSignature &expected,
    std::wstring &errorDetails) {
  // Omitting FILE_SHARE_WRITE and FILE_SHARE_DELETE keeps the validated file
  // from being modified, replaced, renamed, or deleted while the bound-handle
  // operation below is committed.
  HANDLE fileHandle = CreateFileW(
      path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (fileHandle == INVALID_HANDLE_VALUE) {
    errorDetails = std::format(
        L"The verified shortcut could not be locked (Windows error {}).",
        GetLastError());
    return INVALID_HANDLE_VALUE;
  }

  SafeRegularFileSignature current{};
  if (!ReadSafeRegularFileSignature(fileHandle, current, &errorDetails) ||
      !SameSafeRegularFileSignature(current, expected)) {
    CloseHandle(fileHandle);
    if (errorDetails.empty())
      errorDetails = L"The shortcut changed while it was being verified.";
    return INVALID_HANDLE_VALUE;
  }
  return fileHandle;
}

static HANDLE OpenReadGuardMatchingRegularFile(
    const fs::path &path, const SafeRegularFileSignature &expected,
    std::wstring &errorDetails) {
  // A read-attributes handle whose share mode allows only readers blocks
  // writers, renames, and deletes without unnecessarily requesting DELETE
  // access. That remains compatible with ordinary Explorer/AV read handles.
  HANDLE fileHandle = CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (fileHandle == INVALID_HANDLE_VALUE) {
    errorDetails = std::format(
        L"The verified shortcut could not be read-locked (Windows error {}).",
        GetLastError());
    return INVALID_HANDLE_VALUE;
  }

  SafeRegularFileSignature current{};
  if (!ReadSafeRegularFileSignature(fileHandle, current, &errorDetails) ||
      !SameSafeRegularFileSignature(current, expected)) {
    CloseHandle(fileHandle);
    if (errorDetails.empty())
      errorDetails = L"The shortcut changed before its read lock was acquired.";
    return INVALID_HANDLE_VALUE;
  }
  return fileHandle;
}

static HANDLE OpenLockedManagedClientShortcut(
    const fs::path &path, const std::wstring &clientName,
    std::optional<BrowserKind> browser, std::wstring &errorDetails) {
  SafeRegularFileSignature before{};
  SafeRegularFileSignature after{};
  if (!TryGetSafeRegularFileSignature(path, before, &errorDetails) ||
      !IsManagedClientDesktopShortcut(path, clientName, browser) ||
      !TryGetSafeRegularFileSignature(path, after, &errorDetails) ||
      !SameSafeRegularFileSignature(before, after)) {
    if (errorDetails.empty()) {
      errorDetails =
          L"The new shortcut changed while it was being verified.";
    }
    return INVALID_HANDLE_VALUE;
  }
  HANDLE guard = OpenReadGuardMatchingRegularFile(path, after, errorDetails);
  if (guard == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;
  try {
    if (IsManagedClientDesktopShortcut(path, clientName, browser))
      return guard;
  } catch (...) {
    // Treat allocation/COM wrapper exceptions as an indeterminate identity.
  }
  CloseHandle(guard);
  errorDetails =
      L"The read-locked shortcut did not retain its expected client identity.";
  return INVALID_HANDLE_VALUE;
}

static bool MoveKnownRegularFileWithoutReplacement(
    const fs::path &source, const fs::path &destination,
    const SafeRegularFileSignature &expected, bool &moved,
    std::wstring &errorDetails) {
  moved = false;
  const std::wstring destinationName = destination.native();
  if (destinationName.empty() ||
      destinationName.size() >
          ((std::numeric_limits<DWORD>::max)() / sizeof(wchar_t))) {
    errorDetails = L"The shortcut destination path is too long.";
    return false;
  }
  const DWORD destinationNameBytes =
      static_cast<DWORD>(destinationName.size() * sizeof(wchar_t));
  constexpr size_t renameInfoHeaderSize =
      offsetof(FILE_RENAME_INFO, FileName);
  constexpr size_t renameInfoTerminatorSize = sizeof(wchar_t);
  if (destinationNameBytes >
      (std::numeric_limits<DWORD>::max)() - renameInfoHeaderSize -
          renameInfoTerminatorSize) {
    errorDetails = L"The shortcut rename request is too large.";
    return false;
  }
  const size_t renameInfoSize = renameInfoHeaderSize + destinationNameBytes +
                                renameInfoTerminatorSize;

  // Prepare the only post-move diagnostic before acquiring resources or
  // mutating either pathname; swapping it into the caller is non-throwing.
  std::wstring postMoveVerificationError =
      L"The shortcut changed immediately after its atomic rename.";

  HANDLE fileHandle =
      OpenLockedMatchingRegularFile(source, expected, errorDetails);
  if (fileHandle == INVALID_HANDLE_VALUE)
    return false;

  auto *renameInfo = static_cast<FILE_RENAME_INFO *>(
      HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, renameInfoSize));
  if (!renameInfo) {
    CloseHandle(fileHandle);
    errorDetails = L"Windows could not allocate the shortcut rename request.";
    return false;
  }
  renameInfo->ReplaceIfExists = FALSE;
  renameInfo->RootDirectory = nullptr;
  renameInfo->FileNameLength = destinationNameBytes;
  std::memcpy(renameInfo->FileName, destinationName.data(),
              destinationNameBytes);

  // Rename through the already verified handle. Keeping it open with DELETE
  // access and no write/delete sharing prevents a waiting writer from swapping
  // a different source into the name between validation and commit.
  const bool renamed =
      SetFileInformationByHandle(fileHandle, FileRenameInfo, renameInfo,
                                 static_cast<DWORD>(renameInfoSize)) != FALSE;
  const DWORD renameError = renamed ? ERROR_SUCCESS : GetLastError();
  HeapFree(GetProcessHeap(), 0, renameInfo);

  if (!renamed) {
    CloseHandle(fileHandle);
    errorDetails = std::format(
        L"The shortcut could not be renamed without replacement (Windows "
        L"error {}).",
        renameError);
    return false;
  }
  moved = true;

  SafeRegularFileSignature committedHandle{};
  const bool handleVerified =
      ReadSafeRegularFileSignature(fileHandle, committedHandle, nullptr) &&
      SameSafeRegularFileSignature(committedHandle, expected);
  CloseHandle(fileHandle);

  // Some file systems do not publish the destination pathname until the
  // rename handle closes. Reopen it afterward and require the same identity;
  // a raced replacement is detected without ever moving that replacement.
  SafeRegularFileSignature committedPath{};
  const bool pathVerified =
      TryGetSafeRegularFileSignature(destination, committedPath, nullptr) &&
      SameSafeRegularFileSignature(committedPath, expected);
  if (!handleVerified || !pathVerified) {
    errorDetails.swap(postMoveVerificationError);
    return false;
  }
  return true;
}

using ShortcutOwnershipCheck =
    std::function<bool(const fs::path &shortcutPath)>;

static bool EvaluateShortcutOwnershipNoThrow(
    const ShortcutOwnershipCheck &check, const fs::path &path) noexcept {
  try {
    return check(path);
  } catch (...) {
    // Ownership is a security boundary. Allocation or COM wrapper failures
    // make identity indeterminate, so transaction callers must fail closed
    // through their normal rollback/preservation paths instead of unwinding
    // after a shortcut has already moved.
    return false;
  }
}

struct OwnedShortcutQuarantine {
  fs::path path;
  SafeRegularFileSignature signature{};
  bool active = false;
};

static bool RestoreKnownShortcutQuarantine(
    OwnedShortcutQuarantine &quarantine, const fs::path &destination,
    std::wstring &errorDetails) {
  if (!quarantine.active)
    return true;
  bool moved = false;
  if (!MoveKnownRegularFileWithoutReplacement(
          quarantine.path, destination, quarantine.signature, moved,
          errorDetails)) {
    if (moved) {
      SafeRegularFileSignature restored{};
      if (TryGetSafeRegularFileSignature(destination, restored, nullptr) &&
          SameSafeRegularFileSignature(restored, quarantine.signature)) {
        quarantine.active = false;
        return true;
      }
    }
    return false;
  }
  quarantine.active = false;
  return true;
}

static bool QuarantineOwnedShortcut(
    const fs::path &source, const wchar_t *suffix,
    const ShortcutOwnershipCheck &isOwned,
    OwnedShortcutQuarantine &quarantine, std::wstring &errorDetails) {
  quarantine = {};
  SafeRegularFileSignature signature{};
  if (!TryGetSafeRegularFileSignature(source, signature, &errorDetails) ||
      !EvaluateShortcutOwnershipNoThrow(isOwned, source)) {
    if (errorDetails.empty())
      errorDetails = L"The shortcut is no longer owned by ctSpaces.";
    return false;
  }

  fs::path quarantinePath;
  if (!TryMakeUniqueSiblingStagePath(source, suffix, quarantinePath,
                                     errorDetails)) {
    return false;
  }

  // Populate every throwing field before the rename. Once `moved` is true the
  // output record must be able to identify the displaced file even if final
  // verification or later diagnostics fail.
  quarantine.path = quarantinePath;
  quarantine.signature = signature;
  quarantine.active = false;
  bool moved = false;
  if (!MoveKnownRegularFileWithoutReplacement(source, quarantine.path,
                                               signature, moved,
                                               errorDetails)) {
    quarantine.active = moved;
    if (moved) {
      std::wstring recoveryError;
      if (!RestoreKnownShortcutQuarantine(quarantine, source, recoveryError)) {
        errorDetails += L" The original shortcut is preserved at " +
                        quarantine.path.wstring() + L". " + recoveryError;
      }
    }
    return false;
  }

  quarantine.active = true;
  SafeRegularFileSignature quarantinedSignature{};
  if (!TryGetSafeRegularFileSignature(quarantine.path,
                                      quarantinedSignature, nullptr) ||
      !SameSafeRegularFileSignature(quarantinedSignature,
                                    quarantine.signature) ||
      !EvaluateShortcutOwnershipNoThrow(isOwned, quarantine.path)) {
    std::wstring recoveryError;
    if (!RestoreKnownShortcutQuarantine(quarantine, source, recoveryError)) {
      errorDetails = L"The shortcut could not be reverified after quarantine; "
                     L"it is preserved at " + quarantine.path.wstring() +
                     L". " + recoveryError;
    } else {
      errorDetails = L"The shortcut changed while it was being quarantined; "
                     L"the original path was restored.";
    }
    return false;
  }
  return true;
}

static bool DeleteExpectedOwnedShortcutSafely(
    const fs::path &path, const SafeRegularFileSignature &expected,
    const ShortcutOwnershipCheck &isOwned, std::wstring &errorDetails) {
  SafeRegularFileSignature before{};
  if (!TryGetSafeRegularFileSignature(path, before, &errorDetails) ||
      !SameSafeRegularFileSignature(before, expected) ||
      !EvaluateShortcutOwnershipNoThrow(isOwned, path)) {
    if (errorDetails.empty())
      errorDetails = L"The shortcut changed and was not deleted.";
    return false;
  }

  HANDLE fileHandle = OpenLockedMatchingRegularFile(path, expected,
                                                    errorDetails);
  if (fileHandle == INVALID_HANDLE_VALUE)
    return false;
  FILE_DISPOSITION_INFO disposition{TRUE};
  const bool markedForDeletion =
      SetFileInformationByHandle(fileHandle, FileDispositionInfo,
                                 &disposition, sizeof(disposition)) != FALSE;
  const DWORD deleteError = markedForDeletion ? ERROR_SUCCESS : GetLastError();
  CloseHandle(fileHandle);
  if (!markedForDeletion) {
    errorDetails = std::format(
        L"The verified shortcut could not be deleted (Windows error {}).",
        deleteError);
    return false;
  }
  return true;
}

static bool DeleteOwnedShortcutSafely(
    const fs::path &path, const ShortcutOwnershipCheck &isOwned,
    std::wstring &errorDetails) {
  SafeRegularFileSignature expected{};
  if (!TryGetSafeRegularFileSignature(path, expected, &errorDetails)) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return true;
    }
    return false;
  }
  return DeleteExpectedOwnedShortcutSafely(path, expected, isOwned,
                                           errorDetails);
}

static bool CommitStagedOwnedShortcut(
    const fs::path &stagingPath, const fs::path &destination,
    const ShortcutOwnershipCheck &isOwned, std::wstring &errorDetails,
    const ShortcutOwnershipCheck *committedOwnership = nullptr) {
  const ShortcutOwnershipCheck &isCommittedOwned =
      committedOwnership ? *committedOwnership : isOwned;
  SafeRegularFileSignature stagedSignature{};
  if (!TryGetSafeRegularFileSignature(stagingPath, stagedSignature,
                                      &errorDetails) ||
      !EvaluateShortcutOwnershipNoThrow(isOwned, stagingPath)) {
    if (errorDetails.empty())
      errorDetails = L"The staged shortcut is not owned by ctSpaces.";
    return false;
  }

  const auto cleanupUncommittedStage = [&]() {
    std::wstring cleanupError;
    if (!DeleteExpectedOwnedShortcutSafely(
            stagingPath, stagedSignature, isOwned, cleanupError)) {
      errorDetails += L" The staged shortcut was preserved at " +
                      stagingPath.wstring() + L". " + cleanupError;
    }
  };

  if (!EvaluateShortcutOwnershipNoThrow(isCommittedOwned, stagingPath)) {
    errorDetails =
        L"The staged shortcut did not pass its committed identity check.";
    cleanupUncommittedStage();
    return false;
  }

  OwnedShortcutQuarantine previous;
  const DWORD destinationAttributes = GetFileAttributesW(destination.c_str());
  if (destinationAttributes != INVALID_FILE_ATTRIBUTES) {
    if (!QuarantineOwnedShortcut(destination, L"previous.lnk", isOwned,
                                 previous, errorDetails)) {
      cleanupUncommittedStage();
      return false;
    }
  } else {
    const DWORD destinationError = GetLastError();
    if (destinationError != ERROR_FILE_NOT_FOUND &&
        destinationError != ERROR_PATH_NOT_FOUND) {
      errorDetails = std::format(
          L"The shortcut destination could not be inspected (Windows error "
          L"{}).",
          destinationError);
      cleanupUncommittedStage();
      return false;
    }
  }

  bool committed = false;
  const bool commitVerified = MoveKnownRegularFileWithoutReplacement(
      stagingPath, destination, stagedSignature, committed, errorDetails);
  if (!commitVerified && !committed) {
    std::wstring rollbackError;
    if (previous.active &&
        !RestoreKnownShortcutQuarantine(previous, destination,
                                        rollbackError)) {
      errorDetails += L" The previous shortcut is preserved at " +
                      previous.path.wstring() + L". " + rollbackError;
    }
    cleanupUncommittedStage();
    return false;
  }

  SafeRegularFileSignature finalSignature{};
  if (!commitVerified ||
      !TryGetSafeRegularFileSignature(destination, finalSignature, nullptr) ||
      !SameSafeRegularFileSignature(finalSignature, stagedSignature) ||
      !EvaluateShortcutOwnershipNoThrow(isCommittedOwned, destination)) {
    OwnedShortcutQuarantine failedCommit;
    fs::path failedPath;
    std::wstring quarantineError;
    if (TryMakeUniqueSiblingStagePath(destination, L"failed.lnk", failedPath,
                                      quarantineError)) {
      // Copy the throwing path value before the destination is moved away.
      failedCommit.path = failedPath;
      failedCommit.signature = stagedSignature;
      bool moved = false;
      (void)MoveKnownRegularFileWithoutReplacement(
          destination, failedCommit.path, stagedSignature, moved,
          quarantineError);
      failedCommit.active = moved;
    }

    std::wstring rollbackError;
    if (previous.active &&
        !RestoreKnownShortcutQuarantine(previous, destination,
                                        rollbackError)) {
      quarantineError += L" The previous shortcut is preserved at " +
                         previous.path.wstring() + L". " + rollbackError;
    }

    if (failedCommit.active) {
      std::wstring cleanupError;
      if (EvaluateShortcutOwnershipNoThrow(isOwned, failedCommit.path) &&
          DeleteExpectedOwnedShortcutSafely(
              failedCommit.path, failedCommit.signature, isOwned,
              cleanupError)) {
        failedCommit.active = false;
      } else {
        quarantineError += L" The unverified committed file is preserved at " +
                           failedCommit.path.wstring() + L". " + cleanupError;
      }
    }
    errorDetails = L"The committed shortcut did not pass final verification.";
    if (!quarantineError.empty())
      errorDetails += L" " + quarantineError;
    return false;
  }

  if (previous.active) {
    std::wstring cleanupError;
    if (!DeleteExpectedOwnedShortcutSafely(previous.path, previous.signature,
                                           isOwned, cleanupError)) {
      errorDetails = L"The new shortcut was committed, but the verified old "
                     L"shortcut is preserved at " +
                     previous.path.wstring() + L". " + cleanupError;
      // The destination now contains the verified new shortcut. Cleanup is a
      // warning, not a reason to report that the committed operation failed.
      return true;
    }
    previous.active = false;
  }
  return true;
}

static bool CreateClientDesktopShortcut(const std::wstring &clientName,
                                         std::optional<BrowserKind> browser,
                                         bool showConfirmation,
                                         std::wstring *diagnostics) {
  if (diagnostics)
    diagnostics->clear();
  if (!IsExistingClientProfile(clientName) || IsClientArchived(clientName)) {
    if (showConfirmation) {
      MessageBoxW(g_hGui, L"Select a visible existing client first.",
                  L"Desktop Shortcut", MB_OK | MB_ICONINFORMATION);
    }
    return false;
  }

  const fs::path executablePath = GetCurrentExecutablePath();
  fs::path profilePath;
  if (executablePath.empty() ||
      !TryGetSafeClientProfilePath(clientName, profilePath)) {
    return false;
  }

  const auto shortcutPath = GetClientDesktopShortcutPath(clientName);
  if (!shortcutPath) {
    if (showConfirmation) {
      MessageBoxW(g_hGui,
                  L"Windows could not resolve a safe shortcut path. Use a "
                  L"shorter client name or a shorter Desktop folder location.",
                  L"Desktop Shortcut", MB_OK | MB_ICONERROR);
    }
    return false;
  }
  try {
    fs::create_directories(shortcutPath->parent_path());
  } catch (...) {
    return false;
  }
  const DWORD shortcutAttributes = GetFileAttributesW(shortcutPath->c_str());
  if (shortcutAttributes != INVALID_FILE_ATTRIBUTES &&
      !IsManagedClientDesktopShortcutForClient(*shortcutPath, clientName)) {
    if (showConfirmation) {
      MessageBoxW(g_hGui,
                  L"A different or unsafe shortcut already uses this name. "
                  L"ctSpaces left it unchanged.",
                  L"Desktop Shortcut", MB_OK | MB_ICONWARNING);
    }
    return false;
  }
  if (shortcutAttributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD shortcutError = GetLastError();
    if (shortcutError != ERROR_FILE_NOT_FOUND &&
        shortcutError != ERROR_PATH_NOT_FOUND) {
      return false;
    }
  }

  std::wstring shortcutCommitError;
  IShellLinkW *link = nullptr;
  HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
  if (SUCCEEDED(result) && link) {
    result = link->SetPath(executablePath.c_str());
    std::wstring arguments =
        L"--client " + QuoteCommandLineArgument(clientName);
    if (browser) {
      arguments += L" --browser " +
                   QuoteCommandLineArgument(GetBrowserId(*browser));
    }
    if (SUCCEEDED(result))
      result = link->SetArguments(arguments.c_str());
    if (SUCCEEDED(result))
      result = link->SetWorkingDirectory(executablePath.parent_path().c_str());
    const std::wstring description = clientName;
    if (SUCCEEDED(result))
      result = link->SetDescription(description.c_str());

    IPropertyStore *propertyStore = nullptr;
    if (SUCCEEDED(result) && browser)
      result = link->QueryInterface(IID_PPV_ARGS(&propertyStore));
    if (SUCCEEDED(result) && propertyStore) {
      PROPVARIANT appIdValue{};
      const std::wstring appId =
          GetClientAppUserModelId(clientName, *browser);
      result = InitPropVariantFromString(appId.c_str(), &appIdValue);
      if (SUCCEEDED(result))
        result = propertyStore->SetValue(PKEY_AppUserModel_ID, appIdValue);
      if (SUCCEEDED(result))
        result = propertyStore->Commit();
      PropVariantClear(&appIdValue);
      propertyStore->Release();
    }

    const fs::path iconPath = profilePath / L"client.ico";
    if (SUCCEEDED(result)) {
      result = link->SetIconLocation(
          (IsSafeExistingRegularFile(iconPath) ? iconPath : executablePath)
              .c_str(),
          0);
    }

    IPersistFile *persist = nullptr;
    if (SUCCEEDED(result))
      result = link->QueryInterface(IID_PPV_ARGS(&persist));
    fs::path stagingPath;
    std::wstring stagingError;
    if (SUCCEEDED(result) &&
        !TryMakeUniqueSiblingStagePath(*shortcutPath, L"stage.lnk",
                                       stagingPath, stagingError)) {
      result = E_FAIL;
    }
    if (SUCCEEDED(result) && persist) {
      result = persist->Save(stagingPath.c_str(), TRUE);
    }
    if (persist)
      persist->Release();
    link->Release();

    if (SUCCEEDED(result) &&
        !IsManagedClientDesktopShortcut(stagingPath, clientName, browser)) {
      result = E_FAIL;
    }
    bool commitAttempted = false;
    if (SUCCEEDED(result)) {
      commitAttempted = true;
      const ShortcutOwnershipCheck ownsClient =
          [&clientName](const fs::path &candidate) {
            return IsManagedClientDesktopShortcutForClient(candidate,
                                                           clientName);
          };
      const ShortcutOwnershipCheck ownsSelectedBrowser =
          [&clientName, browser](const fs::path &candidate) {
            return IsManagedClientDesktopShortcut(candidate, clientName,
                                                  browser);
          };
      if (!CommitStagedOwnedShortcut(stagingPath, *shortcutPath, ownsClient,
                                     shortcutCommitError,
                                     &ownsSelectedBrowser)) {
        result = E_FAIL;
      }
    }
    if (!commitAttempted)
      RemoveSafeStagingFile(stagingPath);

  }

  if (FAILED(result)) {
    if (shortcutCommitError.empty())
      shortcutCommitError = L"The client shortcut could not be created.";
    if (diagnostics)
      *diagnostics = shortcutCommitError;
    if (showConfirmation) {
      MessageBoxW(g_hGui, shortcutCommitError.c_str(),
                  L"Desktop Shortcut", MB_OK | MB_ICONERROR);
    }
    return false;
  }

  // Earlier releases exposed the browser and product name in the Desktop
  // filename. Once the exact client-only shortcut is committed, retire every
  // verified old-format shortcut for this client so Explorer shows one clean
  // label. Unrelated files and links targeting another ctSpaces executable are
  // never touched.
  std::vector<fs::path> cleanupCandidates;
  std::vector<std::wstring> cleanupFailures;
  {
    std::wstring cleanupGuardError;
    ScopedKernelHandle cleanupGuard(OpenLockedManagedClientShortcut(
        *shortcutPath, clientName, browser, cleanupGuardError));
    if (!cleanupGuard) {
      cleanupFailures.push_back(
          L"Old-format shortcuts were kept because the new client shortcut "
          L"could not be locked and reverified (" +
          cleanupGuardError + L")");
    } else {
      const auto retireHistoricalShortcut =
          [&](const fs::path &candidate,
              std::optional<BrowserKind> candidateBrowser) {
            if (_wcsicmp(candidate.c_str(), shortcutPath->c_str()) == 0)
              return;
            const auto duplicate = std::find_if(
                cleanupCandidates.begin(), cleanupCandidates.end(),
                [&candidate](const fs::path &existing) {
                  return _wcsicmp(existing.c_str(), candidate.c_str()) == 0;
                });
            if (duplicate != cleanupCandidates.end())
              return;
            cleanupCandidates.push_back(candidate);
            if (!IsManagedClientDesktopShortcut(candidate, clientName,
                                                candidateBrowser)) {
              return;
            }
            const ShortcutOwnershipCheck ownsHistorical =
                [&clientName, candidateBrowser](const fs::path &path) {
                  return IsManagedClientDesktopShortcut(path, clientName,
                                                        candidateBrowser);
                };
            std::wstring cleanupError;
            if (!DeleteOwnedShortcutSafely(candidate, ownsHistorical,
                                           cleanupError)) {
              cleanupFailures.push_back(candidate.filename().wstring() +
                                        L" (" + cleanupError + L")");
            }
          };
      for (BrowserKind candidateBrowser :
           {BrowserKind::Edge, BrowserKind::Chrome, BrowserKind::Brave,
            BrowserKind::Firefox}) {
        for (const auto &candidate : GetClientDesktopShortcutCandidatePaths(
                 clientName, candidateBrowser)) {
          retireHistoricalShortcut(candidate, candidateBrowser);
        }
      }
      for (const auto &candidate :
           GetClientDesktopShortcutCandidatePaths(clientName, std::nullopt)) {
        retireHistoricalShortcut(candidate, std::nullopt);
      }
    }
  }
  if (!cleanupFailures.empty()) {
    if (!shortcutCommitError.empty())
      shortcutCommitError += L" ";
    shortcutCommitError +=
        L"The client-only shortcut was created, but one or more verified "
        L"old-format shortcuts could not be removed:";
    for (const auto &failure : cleanupFailures)
      shortcutCommitError += L"\n\n" + failure;
  }
  if (diagnostics)
    *diagnostics = shortcutCommitError;

  if (showConfirmation) {
    if (!shortcutCommitError.empty()) {
      MessageBoxW(g_hGui, shortcutCommitError.c_str(),
                  L"Shortcut Created with Warnings",
                  MB_OK | MB_ICONWARNING);
    } else {
      const std::wstring message =
          browser
              ? std::format(L"Desktop shortcut created for {} in {}.",
                            clientName, GetBrowserDisplayName(*browser))
              : std::format(L"Desktop shortcut created for {}.", clientName);
      MessageBoxW(g_hGui, message.c_str(), L"Desktop Shortcut",
                  MB_OK | MB_ICONINFORMATION);
    }
  }
  return true;
}

struct ClientLaunchRequest {
  std::wstring clientName;
  std::wstring url;
  std::optional<BrowserKind> browser;
};

static std::optional<ClientLaunchRequest>
ParseClientLaunchRequest(const std::wstring &arguments) {
  const std::wstring commandLine = L"ctSpaces.exe " + arguments;
  int argumentCount = 0;
  LPWSTR *argumentValues =
      CommandLineToArgvW(commandLine.c_str(), &argumentCount);
  if (!argumentValues)
    return std::nullopt;

  ClientLaunchRequest request;
  bool browserSeen = false;
  for (int i = 1; i < argumentCount; ++i) {
    const std::wstring argument = argumentValues[i];
    if (_wcsicmp(argument.c_str(), L"--client") == 0 &&
        i + 1 < argumentCount) {
      request.clientName = argumentValues[++i];
    } else if (_wcsnicmp(argument.c_str(), L"--client=", 9) == 0) {
      request.clientName = argument.substr(9);
    } else if (_wcsicmp(argument.c_str(), L"--url") == 0 &&
               i + 1 < argumentCount) {
      request.url = argumentValues[++i];
    } else if (_wcsnicmp(argument.c_str(), L"--url=", 6) == 0) {
      request.url = argument.substr(6);
    } else if (_wcsicmp(argument.c_str(), L"--browser") == 0 &&
               i + 1 < argumentCount && !browserSeen) {
      browserSeen = true;
      request.browser = ParseBrowserKind(argumentValues[++i]);
      if (!request.browser) {
        LocalFree(argumentValues);
        return std::nullopt;
      }
    } else if (_wcsnicmp(argument.c_str(), L"--browser=", 10) == 0 &&
               !browserSeen) {
      browserSeen = true;
      request.browser = ParseBrowserKind(argument.substr(10));
      if (!request.browser) {
        LocalFree(argumentValues);
        return std::nullopt;
      }
    }
  }
  LocalFree(argumentValues);
  if (request.clientName.empty())
    return std::nullopt;
  return request;
}

static bool HasClientLaunchRequest(const std::wstring &arguments) {
  return ParseClientLaunchRequest(arguments).has_value();
}

static bool ProcessClientLaunchRequest(const std::wstring &arguments,
                                       bool showErrors) {
  const auto request = ParseClientLaunchRequest(arguments);
  if (!request)
    return false;

  const std::wstring clientName =
      ResolveExistingClientName(request->clientName);
  if (clientName.empty() || !IsExistingClientProfile(clientName)) {
    if (showErrors) {
      MessageBoxW(g_hGui, L"That client shortcut no longer matches a profile.",
                  L"Client Shortcut", MB_OK | MB_ICONWARNING);
    }
    return false;
  }
  if (IsClientArchived(clientName)) {
    if (showErrors) {
      MessageBoxW(g_hGui,
                  L"This client is archived. Restore it from Settings > "
                  L"Archived Clients first.",
                  L"Client Shortcut", MB_OK | MB_ICONINFORMATION);
    }
    return false;
  }
  if (!g_bUiEnabled) {
    if (showErrors) {
      MessageBoxW(g_hGui, L"ctSpaces is finishing another profile action.",
                  L"Please Wait", MB_OK | MB_ICONINFORMATION);
    }
    return false;
  }
  const BrowserKind browser = request->browser.value_or(g_selectedBrowser);
  if (!request->url.empty()) {
    if (!IsValidWebUrl(request->url)) {
      if (showErrors) {
        MessageBoxW(g_hGui, L"The shortcut contains an invalid website URL.",
                    L"Client Shortcut", MB_OK | MB_ICONWARNING);
      }
      return false;
    }
    return OpenWebUrlForClient(clientName, request->url, browser);
  }

  OpenPinnedClient(clientName, browser);
  return true;
}

static bool HasExactSafeDirectoryEntryName(const fs::path &path) {
  const fs::path parent = path.parent_path();
  if (parent.empty() || !IsSafeExistingDirectory(parent))
    return false;

  std::error_code iteratorError;
  fs::directory_iterator entry(parent, iteratorError);
  const fs::directory_iterator end;
  if (iteratorError)
    return false;
  while (entry != end) {
    const fs::path candidate = entry->path();
    if (candidate.filename().wstring() == path.filename().wstring() &&
        IsSafeExistingDirectory(candidate) &&
        SameExecutablePath(candidate, path)) {
      return true;
    }
    entry.increment(iteratorError);
    if (iteratorError)
      return false;
  }
  return false;
}

enum class StagedDirectoryMoveOutcome {
  DestinationCommitted,
  SourcePreserved,
  Indeterminate
};

using StagedDirectoryPreMoveGuard =
    std::function<bool(std::wstring &guardDetails)>;

static StagedDirectoryMoveOutcome MoveDirectoryThroughUniqueSibling(
    const fs::path &source, const fs::path &destination,
    const wchar_t *stageSuffix, std::wstring &errorDetails,
    const StagedDirectoryPreMoveGuard &preMoveGuard =
        StagedDirectoryPreMoveGuard()) {
  errorDetails.clear();
  if (_wcsicmp(source.parent_path().c_str(),
               destination.parent_path().c_str()) != 0 ||
      source.filename().wstring() == destination.filename().wstring() ||
      _wcsicmp(source.filename().c_str(), destination.filename().c_str()) != 0 ||
      !HasExactSafeDirectoryEntryName(source)) {
    errorDetails = L"The case-only rename source is no longer the exact safe "
                   L"directory entry that was validated.";
    return StagedDirectoryMoveOutcome::SourcePreserved;
  }

  fs::path intermediatePath;
  if (!TryMakeUniqueSiblingStagePath(source, stageSuffix, intermediatePath,
                                     errorDetails)) {
    return HasExactSafeDirectoryEntryName(source)
               ? StagedDirectoryMoveOutcome::SourcePreserved
               : StagedDirectoryMoveOutcome::Indeterminate;
  }
  std::wstring intermediatePathBudgetError;
  if (!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
          source, intermediatePath, &intermediatePathBudgetError)) {
    errorDetails =
        L"The case-only rename staging path would exceed the safe Windows "
        L"path budget. The source was left unchanged.\n\n" +
        intermediatePathBudgetError;
    return HasExactSafeDirectoryEntryName(source)
               ? StagedDirectoryMoveOutcome::SourcePreserved
               : StagedDirectoryMoveOutcome::Indeterminate;
  }

  if (preMoveGuard) {
    std::wstring guardDetails;
    if (!preMoveGuard(guardDetails)) {
      errorDetails =
          L"A final browser-use check blocked the case-only staging move. "
          L"The source was left unchanged.";
      if (!guardDetails.empty())
        errorDetails += L"\n\n" + guardDetails;
      return HasExactSafeDirectoryEntryName(source)
                 ? StagedDirectoryMoveOutcome::SourcePreserved
                 : StagedDirectoryMoveOutcome::Indeterminate;
    }
  }

  if (!MoveFileExW(source.c_str(), intermediatePath.c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    errorDetails = std::format(
        L"The case-only rename could not be staged (Windows error {}).", error);
    return HasExactSafeDirectoryEntryName(source)
               ? StagedDirectoryMoveOutcome::SourcePreserved
               : StagedDirectoryMoveOutcome::Indeterminate;
  }
  if (!HasExactSafeDirectoryEntryName(intermediatePath)) {
    errorDetails = L"The staged case-only directory could not be reverified.";
    return StagedDirectoryMoveOutcome::Indeterminate;
  }

  const auto restoreSource = [&](const fs::path &currentPath,
                                 const std::wstring &failure) {
    fs::path rollbackSource = currentPath;
    if (_wcsicmp(currentPath.c_str(), intermediatePath.c_str()) != 0) {
      if (!MoveFileExW(currentPath.c_str(), intermediatePath.c_str(),
                       MOVEFILE_WRITE_THROUGH) ||
          !HasExactSafeDirectoryEntryName(intermediatePath)) {
        errorDetails = failure +
                       L" The profile could not be returned to its staging "
                       L"path and was left in an indeterminate location.";
        return StagedDirectoryMoveOutcome::Indeterminate;
      }
      rollbackSource = intermediatePath;
    }
    if (MoveFileExW(rollbackSource.c_str(), source.c_str(),
                    MOVEFILE_WRITE_THROUGH) &&
        HasExactSafeDirectoryEntryName(source)) {
      errorDetails = failure + L" The original exact directory name was restored.";
      return StagedDirectoryMoveOutcome::SourcePreserved;
    }
    errorDetails = failure +
                   L" The original exact directory name could not be restored; "
                   L"the profile is preserved at " +
                   rollbackSource.wstring() + L".";
    return StagedDirectoryMoveOutcome::Indeterminate;
  };

  const DWORD destinationAttributes = GetFileAttributesW(destination.c_str());
  if (destinationAttributes != INVALID_FILE_ATTRIBUTES) {
    return restoreSource(
        intermediatePath,
        L"Another item appeared at the case-only rename destination.");
  }
  const DWORD destinationError = GetLastError();
  if (destinationError != ERROR_FILE_NOT_FOUND &&
      destinationError != ERROR_PATH_NOT_FOUND) {
    return restoreSource(
        intermediatePath,
        std::format(L"The case-only rename destination could not be inspected "
                    L"(Windows error {}).",
                    destinationError));
  }

  if (!MoveFileExW(intermediatePath.c_str(), destination.c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    return restoreSource(
        intermediatePath,
        std::format(L"The case-only rename could not be committed (Windows "
                    L"error {}).",
                    error));
  }
  if (!HasExactSafeDirectoryEntryName(destination)) {
    return restoreSource(
        destination,
        L"The committed case-only directory name did not match exactly.");
  }
  return StagedDirectoryMoveOutcome::DestinationCommitted;
}

static bool RenameClientProfile(const std::wstring &oldName,
                                const std::wstring &newName,
                                std::wstring &errorMessage) {
  errorMessage.clear();
  if (!IsExistingClientProfile(oldName)) {
    errorMessage = L"Select an existing client first.";
    return false;
  }
  std::wstring profileUseError;
  if (ProbeClientProfilesInUse(oldName, profileUseError) !=
      ProfileUseState::NotInUse) {
    errorMessage =
        L"Close every browser using this client before renaming it.\n\n" +
        profileUseError;
    return false;
  }
  if (newName.empty() || SanitizeName(newName) != newName) {
    errorMessage =
        L"Enter a valid Windows folder name without leading or trailing "
        L"spaces or any of these characters:\n\\ / : * ? \" < > |";
    return false;
  }
  if (newName == oldName)
    return true;
  if (_wcsicmp(newName.c_str(), oldName.c_str()) != 0 &&
      IsExistingClientProfile(newName)) {
    errorMessage = L"A client with that name already exists.";
    return false;
  }

  fs::path oldPath;
  fs::path newPath;
  if (!TryGetSafeClientProfilePath(oldName, oldPath) ||
      !TryGetSafeClientProfilePath(newName, newPath)) {
    errorMessage = L"The requested client name is not safe.";
    return false;
  }
  if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
          g_sDataDir / L"Sites", newName)) {
    errorMessage =
        L"The new client name is too long for this ctSpaces data-folder "
        L"location. The existing client was left unchanged.";
    return false;
  }
  std::wstring renamePathBudgetError;
  if (!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
          oldPath, newPath, &renamePathBudgetError)) {
    errorMessage =
        L"The renamed client would exceed the safe Windows path budget. "
        L"The existing client was left unchanged.\n\n" +
        renamePathBudgetError;
    return false;
  }
  struct ManagedShortcutToRename {
    fs::path path;
    std::optional<BrowserKind> browser;
  };
  std::vector<ManagedShortcutToRename> shortcutsToRename;
  const auto appendShortcutToRename =
      [&shortcutsToRename](const fs::path &path,
                           std::optional<BrowserKind> browser) {
        const auto duplicate = std::find_if(
            shortcutsToRename.begin(), shortcutsToRename.end(),
            [&path](const ManagedShortcutToRename &existing) {
              return _wcsicmp(existing.path.c_str(), path.c_str()) == 0;
            });
        if (duplicate == shortcutsToRename.end())
          shortcutsToRename.push_back({path, browser});
      };
  for (BrowserKind browser : {BrowserKind::Edge, BrowserKind::Chrome,
                              BrowserKind::Brave, BrowserKind::Firefox}) {
    for (const auto &shortcutPath :
         GetClientDesktopShortcutCandidatePaths(oldName, browser)) {
      if (IsManagedClientDesktopShortcut(shortcutPath, oldName, browser)) {
        appendShortcutToRename(shortcutPath, browser);
      }
    }
  }
  for (const auto &legacyShortcutPath :
       GetClientDesktopShortcutCandidatePaths(oldName, std::nullopt)) {
    if (IsManagedClientDesktopShortcut(legacyShortcutPath, oldName,
                                       std::nullopt)) {
      appendShortcutToRename(legacyShortcutPath, std::nullopt);
    }
  }

  profileUseError.clear();
  if (ProbeClientProfilesInUse(oldName, profileUseError) !=
      ProfileUseState::NotInUse) {
    errorMessage =
        L"A browser became active or could not be reverified before the "
        L"rename. Nothing was renamed.\n\n" +
        profileUseError;
    return false;
  }

  const std::vector<std::wstring> originalPinnedClients = g_pinnedClients;
  const std::vector<std::wstring> originalArchivedClients = g_archivedClients;
  std::vector<std::wstring> originalRestorePreferences;
  {
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    originalRestorePreferences = g_clientsWithoutTabRestore;
  }

  const bool caseOnlyRename =
      oldName != newName && _wcsicmp(oldName.c_str(), newName.c_str()) == 0;
  bool profileMoved = false;
  const auto restoreMemory = [&]() {
    g_pinnedClients = originalPinnedClients;
    g_archivedClients = originalArchivedClients;
    std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
    g_clientsWithoutTabRestore = originalRestorePreferences;
  };
  const auto rollbackProfileMove = [&](std::wstring &rollbackDetails) {
    if (!profileMoved)
      return true;
    if (caseOnlyRename) {
      const StagedDirectoryPreMoveGuard caseOnlyRollbackGuard =
          [&](std::wstring &guardDetails) {
            std::wstring profileUseDetails;
            if (ProbeClientProfilesInUse(newName, profileUseDetails) ==
                ProfileUseState::NotInUse)
              return true;
            guardDetails =
                L"A browser became active, or final process inspection was "
                L"uncertain, before case-only rename rollback.";
            if (!profileUseDetails.empty())
              guardDetails += L"\n\n" + profileUseDetails;
            return false;
          };
      const auto outcome = MoveDirectoryThroughUniqueSibling(
          newPath, oldPath, L"rename-rollback", rollbackDetails,
          caseOnlyRollbackGuard);
      if (outcome == StagedDirectoryMoveOutcome::DestinationCommitted) {
        profileMoved = false;
        return true;
      }
      return false;
    }
    std::wstring rollbackProfileUseError;
    if (ProbeClientProfilesInUse(newName, rollbackProfileUseError) !=
        ProfileUseState::NotInUse) {
      rollbackDetails =
          L"A browser became active, or final process inspection was "
          L"uncertain, before rename rollback. The renamed profile was "
          L"preserved at " +
          newPath.wstring() + L".";
      if (!rollbackProfileUseError.empty())
        rollbackDetails += L"\n\n" + rollbackProfileUseError;
      return false;
    }
    if (MoveFileExW(newPath.c_str(), oldPath.c_str(), MOVEFILE_WRITE_THROUGH) &&
        HasExactSafeDirectoryEntryName(oldPath)) {
      profileMoved = false;
      return true;
    }
    rollbackDetails = std::format(
        L"The renamed profile could not be restored to its original path "
        L"(Windows error {}).",
        GetLastError());
    return false;
  };

  try {
    if (caseOnlyRename) {
      std::wstring moveDetails;
      const StagedDirectoryPreMoveGuard caseOnlyForwardGuard =
          [&](std::wstring &guardDetails) {
            std::wstring profileUseDetails;
            if (ProbeClientProfilesInUse(oldName, profileUseDetails) ==
                ProfileUseState::NotInUse)
              return true;
            guardDetails =
                L"A browser became active, or final process inspection was "
                L"uncertain, before the case-only rename move.";
            if (!profileUseDetails.empty())
              guardDetails += L"\n\n" + profileUseDetails;
            return false;
          };
      const auto outcome = MoveDirectoryThroughUniqueSibling(
          oldPath, newPath, L"rename-stage", moveDetails,
          caseOnlyForwardGuard);
      if (outcome != StagedDirectoryMoveOutcome::DestinationCommitted) {
        profileMoved = outcome == StagedDirectoryMoveOutcome::Indeterminate;
        errorMessage =
            profileMoved
                ? L"The case-only rename could not be completed and the exact "
                  L"profile location is indeterminate. Restart ctSpaces before "
                  L"using this client.\n\n" +
                      moveDetails
                : L"The client could not be renamed. Its existing profile was "
                  L"left in place.\n\n" +
                      moveDetails;
        return false;
      }
    } else {
      std::wstring immediateMoveProfileUseError;
      if (ProbeClientProfilesInUse(oldName, immediateMoveProfileUseError) !=
          ProfileUseState::NotInUse) {
        errorMessage =
            L"A browser became active, or final process inspection was "
            L"uncertain, immediately before the rename. The client was left "
            L"unchanged.";
        if (!immediateMoveProfileUseError.empty())
          errorMessage += L"\n\n" + immediateMoveProfileUseError;
        return false;
      }
      if (!MoveFileExW(oldPath.c_str(), newPath.c_str(),
                       MOVEFILE_WRITE_THROUGH)) {
        errorMessage = std::format(
            L"The client profile could not be renamed safely (Windows error "
            L"{}).",
            GetLastError());
        return false;
      }
      profileMoved = true;
      if (!HasExactSafeDirectoryEntryName(newPath)) {
        std::wstring rollbackDetails;
        const bool rolledBack = rollbackProfileMove(rollbackDetails);
        errorMessage =
            rolledBack
                ? L"The renamed client directory could not be reverified, so "
                  L"its original name was restored."
                : L"The renamed client directory could not be reverified or "
                  L"restored. Restart ctSpaces before using this client.\n\n" +
                      rollbackDetails;
        return false;
      }
    }
    profileMoved = true;

    const bool pinnedChanged =
        ReplaceClientName(g_pinnedClients, oldName, newName);
    bool restoreChanged = false;
    {
      std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
      restoreChanged = ReplaceClientName(g_clientsWithoutTabRestore, oldName,
                                         newName);
    }
    const bool archivedChanged =
        ReplaceClientName(g_archivedClients, oldName, newName);
    std::vector<config_persistence::IniMutation> mutations;
    const auto appendClientList =
        [&mutations](const wchar_t *section, const wchar_t *countKey,
                     const wchar_t *itemPrefix,
                     const std::vector<std::wstring> &items) {
          mutations.push_back({section, std::nullopt, std::nullopt});
          mutations.push_back(
              {section, std::wstring(countKey), std::to_wstring(items.size())});
          for (size_t index = 0; index < items.size(); ++index) {
            mutations.push_back(
                {section, std::format(L"{}{}", itemPrefix, index), items[index]});
          }
        };
    if (pinnedChanged)
      appendClientList(L"pinned", L"count", L"client", g_pinnedClients);
    if (restoreChanged) {
      std::vector<std::wstring> restorePreferences;
      {
        std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);
        restorePreferences = g_clientsWithoutTabRestore;
      }
      appendClientList(L"restore_tabs", L"disabled_count",
                       L"disabled_client", restorePreferences);
    }
    if (archivedChanged)
      appendClientList(L"archived", L"count", L"client", g_archivedClients);
    if (!mutations.empty() &&
        !SaveConfigMutations(L"renamed-client settings", mutations)) {
      restoreMemory();
      std::wstring rollbackDetails;
      const bool rolledBack = rollbackProfileMove(rollbackDetails);
      errorMessage =
          rolledBack
              ? L"The client was not renamed because its saved settings could "
                L"not be committed atomically. Its original profile name and "
                L"settings were restored."
              : L"The saved settings were left unchanged, but the profile "
                L"folder could not be restored to its original name. Restart "
                L"ctSpaces before using this client.\n\n" +
                    rollbackDetails;
      return false;
    }
  } catch (const std::exception &error) {
    const std::wstring details = AnsiToWide(error.what());
    restoreMemory();
    std::wstring rollbackDetails;
    const bool rolledBack = rollbackProfileMove(rollbackDetails);
    errorMessage =
        rolledBack
            ? L"The client could not be renamed. Its original profile and "
              L"saved settings were restored.\n\n" +
                  details
            : L"The profile folder changed, but ctSpaces could not commit the "
              L"saved settings or restore the original folder name. Restart "
              L"ctSpaces before using this client.\n\n" +
                  details + L"\n\n" + rollbackDetails;
    return false;
  } catch (...) {
    restoreMemory();
    std::wstring rollbackDetails;
    const bool rolledBack = rollbackProfileMove(rollbackDetails);
    errorMessage = rolledBack
                       ? L"The client could not be renamed safely. Its original "
                         L"profile and saved settings were restored."
                       : L"The rename failed and the original folder name could "
                         L"not be restored. Restart ctSpaces before using this "
                         L"client.\n\n" +
                             rollbackDetails;
    return false;
  }

  // The profile move and any required config mutation are committed at this
  // point. Cleanup and UI refresh failures are warnings; they must never roll
  // committed in-memory state back against the saved configuration.
  std::vector<std::wstring> shortcutWarnings;
  try {
    ClearClientIconCache(oldName);
  } catch (const std::exception &error) {
    shortcutWarnings.push_back(
        L"The old icon cache could not be retired: " +
        AnsiToWide(error.what()));
  } catch (...) {
    shortcutWarnings.push_back(L"The old icon cache could not be retired.");
  }

  if (!shortcutsToRename.empty()) {
    OwnedShortcutQuarantine renameShortcutQuarantine;
    fs::path renameShortcutRestorePath;
    fs::path renameNewShortcutPath;
    std::optional<BrowserKind> renameBrowserToRecord;
    bool renameShortcutCommitted = false;
    try {
      size_t preferredIndex = 0;
      bool preferredChosen = false;
      const auto oldCanonicalPath = GetClientDesktopShortcutPath(oldName);
      if (oldCanonicalPath) {
        for (size_t index = 0; index < shortcutsToRename.size(); ++index) {
          if (_wcsicmp(shortcutsToRename[index].path.c_str(),
                       oldCanonicalPath->c_str()) == 0) {
            preferredIndex = index;
            preferredChosen = true;
            break;
          }
        }
      }
      if (!preferredChosen) {
        for (size_t index = 0; index < shortcutsToRename.size(); ++index) {
          if (shortcutsToRename[index].browser == g_selectedBrowser) {
            preferredIndex = index;
            preferredChosen = true;
            break;
          }
        }
      }

      const ManagedShortcutToRename &preferredShortcut =
          shortcutsToRename[preferredIndex];
      const auto newShortcutPath = GetClientDesktopShortcutPath(newName);
      if (!newShortcutPath) {
        shortcutWarnings.push_back(
            preferredShortcut.path.filename().wstring() +
            L": the exact client name and Desktop folder leave no safe "
            L"Windows shortcut-path space. The old shortcut was kept.");
      } else {
        const std::optional<BrowserKind> browserToRecord =
            preferredShortcut.browser
                ? preferredShortcut.browser
                : std::optional<BrowserKind>{g_selectedBrowser};
        renameNewShortcutPath = *newShortcutPath;
        renameBrowserToRecord = browserToRecord;
        std::optional<size_t> collidingIndex;
        for (size_t index = 0; index < shortcutsToRename.size(); ++index) {
          if (_wcsicmp(shortcutsToRename[index].path.c_str(),
                       newShortcutPath->c_str()) == 0) {
            collidingIndex = index;
            break;
          }
        }

        bool readyToCreate = true;
        if (collidingIndex) {
          const auto &collidingShortcut =
              shortcutsToRename[*collidingIndex];
          const ShortcutOwnershipCheck ownsCollidingOld =
              [&oldName, &collidingShortcut](const fs::path &candidate) {
                return IsManagedClientDesktopShortcut(
                    candidate, oldName, collidingShortcut.browser);
              };
          std::wstring quarantineError;
          renameShortcutRestorePath = collidingShortcut.path;
          if (!QuarantineOwnedShortcut(
                  collidingShortcut.path, L"rename-backup.lnk",
                  ownsCollidingOld, renameShortcutQuarantine,
                  quarantineError)) {
            shortcutWarnings.push_back(
                collidingShortcut.path.filename().wstring() +
                L": the colliding old shortcut could not be quarantined "
                L"safely. " +
                quarantineError);
            readyToCreate = false;
          }
        }

        std::wstring shortcutDiagnostics;
        const bool shortcutCreated =
            readyToCreate && CreateClientDesktopShortcut(
                                 newName, browserToRecord, false,
                                 &shortcutDiagnostics);
        renameShortcutCommitted = shortcutCreated;
        if (readyToCreate && !shortcutCreated) {
          std::wstring restoreError;
          if (renameShortcutQuarantine.active &&
              !RestoreKnownShortcutQuarantine(
                  renameShortcutQuarantine, renameShortcutRestorePath,
                  restoreError)) {
            shortcutWarnings.push_back(
                renameShortcutRestorePath.filename().wstring() +
                L": the new shortcut failed; the verified old shortcut is "
                L"preserved at " +
                renameShortcutQuarantine.path.wstring() + L". " +
                restoreError);
          } else {
            shortcutWarnings.push_back(
                preferredShortcut.path.filename().wstring() +
                L": the client-only shortcut could not be created; all old "
                L"shortcuts were kept.");
          }
          readyToCreate = false;
        } else if (shortcutCreated && !shortcutDiagnostics.empty()) {
          shortcutWarnings.push_back(shortcutDiagnostics);
        }

        if (readyToCreate) {
          std::wstring cleanupGuardError;
          ScopedKernelHandle cleanupGuard(OpenLockedManagedClientShortcut(
              renameNewShortcutPath, newName, renameBrowserToRecord,
              cleanupGuardError));
          if (!cleanupGuard) {
            std::wstring warning =
                L"The new client-only shortcut was committed, but it could "
                L"not be locked and reverified before old shortcuts were "
                L"retired. The verified old shortcuts were kept. " +
                cleanupGuardError;
            if (renameShortcutQuarantine.active) {
              warning += L" The colliding old shortcut is preserved at " +
                         renameShortcutQuarantine.path.wstring() + L".";
            }
            shortcutWarnings.push_back(std::move(warning));
          } else {
            for (size_t index = 0; index < shortcutsToRename.size(); ++index) {
              const auto &oldShortcut = shortcutsToRename[index];
              const ShortcutOwnershipCheck ownsOld =
                  [&oldName, &oldShortcut](const fs::path &candidate) {
                    return IsManagedClientDesktopShortcut(
                        candidate, oldName, oldShortcut.browser);
                  };
              std::wstring deleteError;
              const bool isQuarantined =
                  collidingIndex && index == *collidingIndex &&
                  renameShortcutQuarantine.active;
              const bool oldRemoved =
                  isQuarantined
                      ? DeleteExpectedOwnedShortcutSafely(
                            renameShortcutQuarantine.path,
                            renameShortcutQuarantine.signature, ownsOld,
                            deleteError)
                      : DeleteOwnedShortcutSafely(oldShortcut.path, ownsOld,
                                                  deleteError);
              if (oldRemoved && isQuarantined)
                renameShortcutQuarantine.active = false;
              if (!oldRemoved) {
                const fs::path preservedPath =
                    isQuarantined ? renameShortcutQuarantine.path
                                  : oldShortcut.path;
                shortcutWarnings.push_back(
                    preservedPath.filename().wstring() +
                    L": the verified old shortcut was preserved. " +
                    deleteError);
              }
            }
          }
        }
      }
    } catch (const std::exception &error) {
      if (!renameShortcutCommitted && !renameNewShortcutPath.empty()) {
        renameShortcutCommitted = IsManagedClientDesktopShortcut(
            renameNewShortcutPath, newName, renameBrowserToRecord);
      }
      std::wstring restoreError;
      if (renameShortcutQuarantine.active && !renameShortcutCommitted &&
          !RestoreKnownShortcutQuarantine(renameShortcutQuarantine,
                                          renameShortcutRestorePath,
                                          restoreError)) {
        shortcutWarnings.push_back(
            L"The verified old shortcut is preserved at " +
            renameShortcutQuarantine.path.wstring() + L". " + restoreError);
      } else if (renameShortcutQuarantine.active &&
                 renameShortcutCommitted) {
        shortcutWarnings.push_back(
            L"The new client-only shortcut was committed; the verified old "
            L"shortcut is preserved at " +
            renameShortcutQuarantine.path.wstring() + L".");
      }
      shortcutWarnings.push_back(
          L"Shortcut migration stopped safely: " + AnsiToWide(error.what()));
    } catch (...) {
      if (!renameShortcutCommitted && !renameNewShortcutPath.empty()) {
        renameShortcutCommitted = IsManagedClientDesktopShortcut(
            renameNewShortcutPath, newName, renameBrowserToRecord);
      }
      std::wstring restoreError;
      if (renameShortcutQuarantine.active && !renameShortcutCommitted &&
          !RestoreKnownShortcutQuarantine(renameShortcutQuarantine,
                                          renameShortcutRestorePath,
                                          restoreError)) {
        shortcutWarnings.push_back(
            L"The verified old shortcut is preserved at " +
            renameShortcutQuarantine.path.wstring() + L". " + restoreError);
      } else if (renameShortcutQuarantine.active &&
                 renameShortcutCommitted) {
        shortcutWarnings.push_back(
            L"The new client-only shortcut was committed; the verified old "
            L"shortcut is preserved at " +
            renameShortcutQuarantine.path.wstring() + L".");
      }
      shortcutWarnings.push_back(L"Shortcut migration stopped safely.");
    }
  }

  try {
    UpdateClientsComboBox();
    SelectSessionTab(0, false);
    const LRESULT index = SendMessageW(g_hComboClient, CB_FINDSTRINGEXACT,
                                       (WPARAM)-1,
                                       (LPARAM)newName.c_str());
    if (index != CB_ERR)
      SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)index, 0);
    SetClientInputText(newName);
    UpdateIconPreviewForSelection(true);
  } catch (const std::exception &error) {
    shortcutWarnings.push_back(
        L"The rename was saved, but the client list could not be refreshed: " +
        AnsiToWide(error.what()));
  } catch (...) {
    shortcutWarnings.push_back(
        L"The rename was saved, but the client list could not be refreshed.");
  }

  if (!shortcutWarnings.empty()) {
    errorMessage =
        L"The client profile and saved settings were renamed, but one or more "
        L"follow-up items need attention:";
    for (const auto &warning : shortcutWarnings)
      errorMessage += L"\n\n" + warning;
  }
  return true;
}

static void GuiRenameClient() {
  const std::wstring oldName = GetSelectedClientNameSanitized(false);
  if (!IsExistingClientProfile(oldName)) {
    MessageBoxW(g_hGui, L"Select an existing client first.", L"Rename Client",
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (IsClientActive(oldName)) {
    MessageBoxW(g_hGui, L"Close this client before renaming it.",
                L"Rename Client", MB_OK | MB_ICONWARNING);
    return;
  }

  const auto entered = InputBoxWindow::Show(
      g_hGui, L"Rename Client", L"Enter the new client name", oldName);
  if (!entered || *entered == oldName)
    return;

  std::wstring errorMessage;
  if (!RenameClientProfile(oldName, *entered, errorMessage)) {
    MessageBoxW(g_hGui, errorMessage.c_str(), L"Rename Client",
                MB_OK | MB_ICONWARNING);
    return;
  }
  if (!errorMessage.empty()) {
    MessageBoxW(g_hGui, errorMessage.c_str(), L"Rename Completed with Warnings",
                MB_OK | MB_ICONWARNING);
  } else {
    MessageBoxW(g_hGui, L"The client was renamed successfully.",
                L"Rename Client", MB_OK | MB_ICONINFORMATION);
  }
}

static bool ArchiveClientProfile(const std::wstring &clientName,
                                 std::wstring &errorMessage) {
  errorMessage.clear();
  if (!IsExistingClientProfile(clientName)) {
    errorMessage = L"Select an existing client first.";
    return false;
  }
  std::wstring profileUseError;
  if (ProbeClientProfilesInUse(clientName, profileUseError) !=
      ProfileUseState::NotInUse) {
    errorMessage =
        L"Close every browser using this client before archiving it.\n\n" +
        profileUseError;
    return false;
  }
  if (IsClientArchived(clientName))
    return true;
  if (g_archivedClients.size() >= MAX_ARCHIVED_CLIENTS) {
    errorMessage = L"The archived-client list is full.";
    return false;
  }

  const std::vector<std::wstring> originalArchivedClients = g_archivedClients;
  const std::vector<std::wstring> originalPinnedClients = g_pinnedClients;
  const bool hadPinnedClients = !originalPinnedClients.empty();
  try {
    g_archivedClients.push_back(clientName);
    const auto pinned = FindPinnedClient(clientName);
    if (pinned != g_pinnedClients.end())
      g_pinnedClients.erase(pinned);

    std::vector<config_persistence::IniMutation> mutations;
    const auto appendClientList =
        [&mutations](const wchar_t *section, const wchar_t *countKey,
                     const std::vector<std::wstring> &clients) {
          mutations.push_back({section, std::nullopt, std::nullopt});
          mutations.push_back(
              {section, std::wstring(countKey),
               std::to_wstring(clients.size())});
          for (size_t i = 0; i < clients.size(); ++i) {
            mutations.push_back(
                {section, std::format(L"client{}", i), clients[i]});
          }
        };
    appendClientList(L"archived", L"count", g_archivedClients);
    appendClientList(L"pinned", L"count", g_pinnedClients);
    if (!SaveConfigMutations(L"archived and pinned client lists", mutations)) {
      g_archivedClients = originalArchivedClients;
      g_pinnedClients = originalPinnedClients;
      errorMessage =
          L"The client was not archived because its saved settings could not "
          L"be committed atomically.";
      return false;
    }
  } catch (const std::exception &error) {
    g_archivedClients = originalArchivedClients;
    g_pinnedClients = originalPinnedClients;
    errorMessage = L"The client could not be archived. Its saved state was "
                   L"restored.\n\n" +
                   AnsiToWide(error.what());
    return false;
  } catch (...) {
    g_archivedClients = originalArchivedClients;
    g_pinnedClients = originalPinnedClients;
    errorMessage = L"The client could not be archived safely. Its saved state "
                   L"was restored.";
    return false;
  }

  // The archived and pinned sections are committed together above. A later UI
  // refresh problem must not restore memory to values that no longer match the
  // atomically saved configuration.
  try {
    SelectSessionTab(0, false);
    SetClientInputText(L"");
    SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)-1, 0);
    UpdateClientsComboBox();
    if (hadPinnedClients != !g_pinnedClients.empty())
      ResizeMainWindowForDpi(g_hGui, GetDpiForWindow(g_hGui));
    return true;
  } catch (const std::exception &error) {
    errorMessage =
        L"The client was archived and its settings were saved, but the client "
        L"list could not be refreshed. Restart ctSpaces to refresh it.\n\n" +
        AnsiToWide(error.what());
    return true;
  } catch (...) {
    errorMessage =
        L"The client was archived and its settings were saved, but the client "
        L"list could not be refreshed. Restart ctSpaces to refresh it.";
    return true;
  }
}

static void GuiArchiveClient() {
  const std::wstring clientName = GetSelectedClientNameSanitized(false);
  if (!IsExistingClientProfile(clientName)) {
    MessageBoxW(g_hGui, L"Select an existing client first.",
                L"Archive Client", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (IsClientActive(clientName)) {
    MessageBoxW(g_hGui, L"Close this client before archiving it.",
                L"Archive Client", MB_OK | MB_ICONWARNING);
    return;
  }
  if (IsClientArchived(clientName))
    return;

  const std::wstring confirmation =
      L"Hide " + clientName +
      L" from the client list?\n\nIts complete browser profile will remain "
      L"saved and can be restored from Archived Clients.";
  if (MessageBoxW(g_hGui, confirmation.c_str(), L"Archive Client",
                  MB_YESNO | MB_ICONQUESTION) != IDYES) {
    return;
  }

  std::wstring errorMessage;
  if (!ArchiveClientProfile(clientName, errorMessage)) {
    MessageBoxW(g_hGui, errorMessage.c_str(), L"Archive Client",
                MB_OK | MB_ICONWARNING);
    return;
  }
  if (!errorMessage.empty()) {
    MessageBoxW(g_hGui, errorMessage.c_str(),
                L"Archive Completed with Warnings",
                MB_OK | MB_ICONWARNING);
    return;
  }
  MessageBoxW(g_hGui,
              L"The client is archived. Restore it from Settings > "
              L"Archived Clients.",
              L"Archive Client", MB_OK | MB_ICONINFORMATION);
}

static bool RestoreArchivedClient(const std::wstring &restoredName,
                                  std::wstring *statusMessage) {
  if (statusMessage)
    statusMessage->clear();
  const auto archived = std::find_if(
      g_archivedClients.begin(), g_archivedClients.end(),
      [&restoredName](const std::wstring &storedName) {
        return _wcsicmp(storedName.c_str(), restoredName.c_str()) == 0;
      });
  if (archived == g_archivedClients.end()) {
    if (statusMessage)
      *statusMessage = L"That client is no longer archived.";
    return false;
  }

  const std::vector<std::wstring> originalArchivedClients = g_archivedClients;
  try {
    g_archivedClients.erase(archived);
    if (!SaveArchivedClients()) {
      g_archivedClients = originalArchivedClients;
      if (statusMessage) {
        *statusMessage =
            L"The client was not restored because the archived-client list "
            L"could not be saved atomically.";
      }
      return false;
    }
  } catch (const std::exception &error) {
    g_archivedClients = originalArchivedClients;
    if (statusMessage) {
      *statusMessage =
          L"The client could not be restored. Its archived state was kept.\n\n" +
          AnsiToWide(error.what());
    }
    return false;
  } catch (...) {
    g_archivedClients = originalArchivedClients;
    if (statusMessage) {
      *statusMessage =
          L"The client could not be restored safely. Its archived state was "
          L"kept.";
    }
    return false;
  }

  // The archived section is committed above. UI failures from this point are
  // warnings and must never put memory back out of sync with saved config.
  try {
    UpdateClientsComboBox();
    SelectSessionTab(0, false);
    const LRESULT index = SendMessageW(g_hComboClient, CB_FINDSTRINGEXACT,
                                       (WPARAM)-1,
                                       (LPARAM)restoredName.c_str());
    if (index != CB_ERR)
      SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)index, 0);
    SetClientInputText(restoredName);
    UpdateIconPreviewForSelection(true);
  } catch (const std::exception &error) {
    if (statusMessage) {
      *statusMessage =
          L"The client was restored and its settings were saved, but the "
          L"client list could not be refreshed. Restart ctSpaces to refresh "
          L"it.\n\n" +
          AnsiToWide(error.what());
    }
  } catch (...) {
    if (statusMessage) {
      *statusMessage =
          L"The client was restored and its settings were saved, but the "
          L"client list could not be refreshed. Restart ctSpaces to refresh "
          L"it.";
    }
  }
  return true;
}

static void ShowArchivedClientsMenu(HWND hWnd) {
  PruneArchivedClients();
  if (g_archivedClients.empty()) {
    MessageBoxW(hWnd, L"There are no archived clients.", L"Archived Clients",
                MB_OK | MB_ICONINFORMATION);
    return;
  }

  std::vector<std::wstring> names = g_archivedClients;
  std::sort(names.begin(), names.end(), CaseInsensitiveLess{});
  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  MENUINFO menuInfo{};
  menuInfo.cbSize = sizeof(menuInfo);
  menuInfo.fMask = MIM_STYLE | MIM_BACKGROUND;
  menuInfo.dwStyle = MNS_CHECKORBMP | MNS_NOCHECK;
  menuInfo.hbrBack =
      g_hbrThemeMenu ? g_hbrThemeMenu : GetSysColorBrush(COLOR_MENU);
  SetMenuInfo(menu, &menuInfo);

  std::vector<MenuItemData> itemData(names.size());
  constexpr UINT commandBase = 47300;
  for (size_t i = 0; i < names.size(); ++i) {
    itemData[i].text = L"Restore " + names[i];
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA;
    item.wID = commandBase + (UINT)i;
    item.fType = MFT_OWNERDRAW;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(&itemData[i]);
    InsertMenuItemW(menu, (UINT)-1, TRUE, &item);
  }

  RECT buttonRect{};
  GetWindowRect(g_hBtnConfig, &buttonRect);
  const int previousMenuMinWidth = g_iMenuMinWidth;
  g_iMenuMinWidth = 0;
  HideMenuTooltip();
  SetForegroundWindow(hWnd);
  const UINT command = TrackPopupMenuEx(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, buttonRect.left,
      buttonRect.bottom, hWnd, nullptr);
  g_iMenuMinWidth = previousMenuMinWidth;
  DestroyMenu(menu);
  PostMessageW(hWnd, WM_NULL, 0, 0);

  if (command < commandBase || command >= commandBase + names.size())
    return;
  std::wstring restoreMessage;
  const bool restored =
      RestoreArchivedClient(names[command - commandBase], &restoreMessage);
  if (!restoreMessage.empty()) {
    MessageBoxW(hWnd, restoreMessage.c_str(),
                restored ? L"Restore Completed with Warnings"
                         : L"Restore Client",
                MB_OK | MB_ICONWARNING);
  }
}

bool IsValidFilenameChar(wchar_t c) {
  const std::wstring invalidChars = L"\\/:*?\"<>|";
  return c >= 32 && invalidChars.find(c) == std::wstring::npos;
}

bool ExtractResourceToFile(UINT resourceID, const fs::path &destPath,
                           bool overwrite) {
  if (fs::exists(destPath) && !overwrite)
    return true;
  HRSRC hRes = FindResource(g_hInst, MAKEINTRESOURCE(resourceID), L"BINARY");
  if (!hRes)
    return false;
  HGLOBAL hResLoad = LoadResource(g_hInst, hRes);
  if (!hResLoad)
    return false;
  void *pRes = LockResource(hResLoad);
  if (!pRes)
    return false;
  DWORD dwSize = SizeofResource(g_hInst, hRes);
  try {
    if (!destPath.parent_path().empty())
      fs::create_directories(destPath.parent_path());
  } catch (...) {
  }
  std::ofstream outFile(destPath, std::ios::binary);
  if (!outFile)
    return false;
  outFile.write(static_cast<char *>(pRes), dwSize);
  return outFile.good();
}

bool EnsureBundledDefaultTemplate() {
  const fs::path archivePath = g_sDataDir / L"Default.7z";
  const fs::path revisionPath = g_sDataDir / L"default-template-revision.txt";

  int installedRevision = 0;
  try {
    if (fs::exists(revisionPath)) {
      std::wifstream revisionFile(revisionPath);
      revisionFile >> installedRevision;
    }
  } catch (...) {
    installedRevision = 0;
  }

  if (installedRevision >= DEFAULT_TEMPLATE_REVISION &&
      IsSafeExistingRegularFile(archivePath)) {
    _7zArchiveInfo archiveInfo{};
    const HRESULT inspectResult =
        _7zInspect7z(archivePath.c_str(), &archiveInfo);
    if (SUCCEEDED(inspectResult) && archiveInfo.fileCount >= 2 &&
        SUCCEEDED(_7zTest7z(archivePath.c_str(), nullptr, nullptr))) {
      return true;
    }
  }

  const fs::path stagedArchive =
      g_sDataDir /
      std::format(L"Default.7z.new-{}-{}", GetCurrentProcessId(),
                  GetTickCount64());
  const fs::path stagedRevision =
      g_sDataDir /
      std::format(L"default-template-revision.txt.new-{}",
                  GetCurrentProcessId());

  try {
    if (!ExtractResourceToFile(IDR_DEFPROF, stagedArchive, true) ||
        !fs::exists(stagedArchive) || fs::file_size(stagedArchive) < 4096) {
      throw std::runtime_error("Bundled profile archive could not be staged");
    }

    _7zArchiveInfo archiveInfo{};
    HRESULT archiveResult = _7zInspect7z(stagedArchive.c_str(), &archiveInfo);
    if (FAILED(archiveResult) || archiveInfo.fileCount < 2 ||
        FAILED(_7zTest7z(stagedArchive.c_str(), nullptr, nullptr))) {
      throw std::runtime_error(
          "Bundled profile archive failed integrity validation");
    }

    const fs::path backupDir = g_sDataDir / L"_DefBak";
    fs::create_directories(backupDir);
    const fs::path backupPath =
        backupDir /
        std::format(L"{}-Sanitized-Default-v{}.7z", GetBackupTimestamp(),
                    DEFAULT_TEMPLATE_REVISION);
    fs::copy_file(stagedArchive, backupPath,
                  fs::copy_options::overwrite_existing);

    if (!MoveFileExW(stagedArchive.c_str(), archivePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Could not replace Default.7z");
    }

    {
      std::wofstream revisionFile(stagedRevision, std::ios::trunc);
      if (!revisionFile)
        throw std::runtime_error("Could not stage template revision marker");
      revisionFile << DEFAULT_TEMPLATE_REVISION << L'\n';
      revisionFile.close();
      if (!revisionFile)
        throw std::runtime_error("Could not write template revision marker");
    }

    if (!MoveFileExW(stagedRevision.c_str(), revisionPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Could not replace template revision marker");
    }

    return true;
  } catch (...) {
    std::error_code ec;
    fs::remove(stagedArchive, ec);
    fs::remove(stagedRevision, ec);
    return false;
  }
}

void SetUiState(bool enabled) {
  // Completion messages can run inside a modal Default-save prompt. Enabling
  // is only safe when both that operation and the launch worker have finished.
  enabled = enabled && !g_bDefaultProfileUiBusy &&
            !g_isLaunchInFlight.load();
  g_bUiEnabled = enabled;
  EnableWindow(g_hComboClient, enabled);
  EnableWindow(g_hClientEdit, enabled);
  EnableWindow(g_hBtnClientIcon, enabled);
  EnableWindow(g_hBtnClientDrop, enabled);
  EnableWindow(g_hBtnGo, enabled);
  EnableWindow(g_hBtnPin, enabled);
  EnableWindow(g_hBtnTmpProf, enabled);
  EnableWindow(g_hBtnConfig, enabled);
  InvalidateSessionTabs(g_hGui);
  InvalidateRect(g_hClientEditSurface, nullptr, TRUE);
  UpdatePinButtonState();
  UpdateRestoreTabsToggleState();

  if (enabled)
    FocusClientEdit();
}

bool extDef(const fs::path &profileDataPath, const wchar_t *statusText) {
  // Default archive lives in your data dir (still extracted from resource
  // once).
  const fs::path default7zPath = g_sDataDir / L"Default.7z";

  if (!fs::exists(default7zPath)) {
    if (!ExtractResourceToFile(IDR_DEFPROF, default7zPath)) {
      MessageBoxW(g_hGui, L"Failed to extract Default.7z resource.", L"Error",
                  MB_OK | MB_ICONERROR);
      return false;
    }
  }

  try {
    if (fs::exists(profileDataPath)) {
      if (!IsSafeExistingDirectory(profileDataPath) ||
          fs::directory_iterator(profileDataPath) !=
              fs::directory_iterator()) {
        MessageBoxW(g_hGui,
                    L"ctSpaces refused to extract the starter over an "
                    L"existing nonempty or unsafe profile directory.",
                    L"Profile Safety", MB_OK | MB_ICONERROR);
        return false;
      }
    } else {
      if (!CreateDirectoryW(profileDataPath.c_str(), nullptr))
        return false;
    }
  } catch (...) {
    return false;
  }

  // Always create our marker file on success (your "profile exists" sentinel).
  const fs::path markerPath = profileDataPath / L"ctSpaces";

  _7zUiCtx ctx;
  ctx.mainWnd = g_hGui;
  ctx.status =
      (statusText && *statusText) ? statusText : L"Loading Default Profile...";

  ProgressUI_PostShow(ctx.mainWnd, ctx.status, -1);

  HRESULT hr = _7zExtra_7z(default7zPath.c_str(), profileDataPath.c_str(),
                           &_7zProgress, &ctx);

  ProgressUI_PostHide(ctx.mainWnd);

  if (FAILED(hr)) {
    MessageBoxW(g_hGui, L"Failed to extract Default.7z (InProc7z).", L"Error",
                MB_OK | MB_ICONERROR);
    return false;
  }

  if (!PrepareFreshProfileState(profileDataPath) ||
      !IsSafeExistingDirectory(profileDataPath / L"Default") ||
      !WriteDurableMarker(markerPath, "ctSpaces-profile=2\r\n")) {
    MessageBoxW(g_hGui,
                L"The starter was extracted, but its durable profile marker "
                L"could not be committed. The profile was not accepted.",
                L"Profile Safety", MB_OK | MB_ICONERROR);
    return false;
  }

  return MarkerHasExactContents(markerPath, "ctSpaces-profile=2\r\n");
}

bool SetBrowserStartupPreference(const fs::path &profilePath,
                                 bool restoreTabs) {
  const auto result = browser_preferences::UpdateStartupPreferencesFile(
      profilePath / L"Default" / L"Preferences", restoreTabs, true);
  return result == browser_preferences::FileUpdateResult::unchanged ||
         result == browser_preferences::FileUpdateResult::updated;
}

bool ClearBrowserStartupState(const fs::path &profilePath) {
  const fs::path defaultDir = profilePath / L"Default";
  if (!IsSafeExistingDirectory(profilePath) ||
      !IsSafeExistingDirectory(defaultDir)) {
    return false;
  }

  const std::vector<std::pair<fs::path, bool>> sessionPaths = {
      {defaultDir / L"Sessions", true},
      {defaultDir / L"Current Session", false},
      {defaultDir / L"Current Tabs", false},
      {defaultDir / L"Last Session", false},
      {defaultDir / L"Last Tabs", false}};

  for (const auto &[path, expectDirectory] : sessionPaths) {
    std::error_code existsError;
    const bool exists = fs::exists(path, existsError);
    if (existsError)
      return false;
    if (!exists)
      continue;
    if (expectDirectory ? !IsSafeExistingDirectory(path)
                        : !IsSafeExistingRegularFile(path)) {
      return false;
    }

    std::error_code removeError;
    fs::remove_all(path, removeError);
    if (removeError)
      return false;
    existsError.clear();
    if (fs::exists(path, existsError) || existsError)
      return false;
  }

  return SetBrowserStartupPreference(profilePath, false);
}

bool PrepareFreshProfileState(const fs::path &profilePath) {
  return ClearBrowserStartupState(profilePath);
}

static std::optional<uint64_t>
GetBoundedFileContentHash(const fs::path &path,
                          uintmax_t maximumBytes =
                              kMaxImportedImageFileBytes) {
  if (!IsSafeExistingRegularFile(path))
    return std::nullopt;

  std::error_code sizeError;
  const uintmax_t expectedSize = fs::file_size(path, sizeError);
  if (sizeError || expectedSize == 0 || expectedSize > maximumBytes)
    return std::nullopt;

  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
  constexpr uint64_t kFnvPrime = 1099511628211ULL;
  uint64_t hash = kFnvOffset;
  uintmax_t totalBytes = 0;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytesRead = input.gcount();
    if (bytesRead < 0 ||
        totalBytes + static_cast<uintmax_t>(bytesRead) > maximumBytes) {
      return std::nullopt;
    }
    for (std::streamsize index = 0; index < bytesRead; ++index) {
      hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(index)]);
      hash *= kFnvPrime;
    }
    totalBytes += static_cast<uintmax_t>(bytesRead);
  }
  if (!input.eof() || totalBytes != expectedSize ||
      !IsSafeExistingRegularFile(path)) {
    return std::nullopt;
  }
  return hash;
}

static fs::path GetTaskbarIconResourcePath(const fs::path &iconPath) {
  if (iconPath.empty() || !IsSafeExistingRegularFile(iconPath) ||
      !IsSafeExistingDirectory(iconPath.parent_path()))
    return fs::path();

  try {
    const auto sourceHash = GetBoundedFileContentHash(iconPath);
    if (!sourceHash)
      return fs::path();
    const fs::path taskbarIcon =
        iconPath.parent_path() /
        std::format(L"ctSpaces-taskbar-{:016X}.ico", *sourceHash);
    if (fs::exists(taskbarIcon) &&
        !IsSafeExistingRegularFile(taskbarIcon))
      return fs::path();
    if (!fs::exists(taskbarIcon) &&
        !CopyFileW(iconPath.c_str(), taskbarIcon.c_str(), TRUE))
      return fs::path();
    const auto copiedHash = GetBoundedFileContentHash(taskbarIcon);
    if (!copiedHash || *copiedHash != *sourceHash)
      return fs::path();
    return taskbarIcon;
  } catch (...) {
    return fs::path();
  }
}

static void CleanupObsoleteTaskbarIconResources(
    const fs::path &clientRoot, const fs::path &keepPath = fs::path()) {
  const fs::path sitesRoot = g_sDataDir / L"Sites";
  if (!ValidateSitesRoot(false) || !IsSafeExistingDirectory(clientRoot) ||
      !IsDirectChildPath(clientRoot, sitesRoot)) {
    return;
  }

  try {
    for (const auto &entry : fs::directory_iterator(clientRoot)) {
      const std::wstring name = entry.path().filename().wstring();
      if (!name.starts_with(L"ctSpaces-taskbar-") ||
          !name.ends_with(L".ico") ||
          (!keepPath.empty() &&
           _wcsicmp(entry.path().c_str(), keepPath.c_str()) == 0)) {
        continue;
      }
      if (!IsSafeExistingRegularFile(entry.path()))
        continue;
      std::error_code cleanupError;
      fs::remove(entry.path(), cleanupError);
    }
  } catch (...) {
  }
}

static bool RecreateTaskbarButton(HWND hWnd) {
  ITaskbarList *taskbarList = nullptr;
  HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&taskbarList));
  if (FAILED(result) || !taskbarList)
    return false;

  result = taskbarList->HrInit();
  if (SUCCEEDED(result)) {
    taskbarList->DeleteTab(hWnd);
    result = taskbarList->AddTab(hWnd);
    if (SUCCEEDED(result))
      taskbarList->ActivateTab(hWnd);
  }
  taskbarList->Release();
  return SUCCEEDED(result);
}

static void LogTaskbarQaResult(const wchar_t *stage, HRESULT result) {
  if (g_sTaskbarQaLogPath.empty())
    return;

  static std::mutex logMutex;
  std::lock_guard<std::mutex> lock(logMutex);
  std::wofstream log(g_sTaskbarQaLogPath, std::ios::app);
  if (log)
    log << GetCurrentThreadId() << L" " << stage << L" 0x" << std::hex
        << std::uppercase << (unsigned long)result << std::dec << L"\n";
}

bool SetWindowAppId(HWND hWnd, const std::wstring &appId,
                    const fs::path &iconPath, const fs::path &clientRoot,
                    bool recreateTaskbar) {
  IPropertyStore *propertyStore = nullptr;
  HRESULT result =
      SHGetPropertyStoreForWindow(hWnd, IID_PPV_ARGS(&propertyStore));
  LogTaskbarQaResult(L"SHGetPropertyStoreForWindow", result);
  if (FAILED(result) || !propertyStore)
    return false;

  fs::path taskbarIconPath;
  std::wstring iconResource;
  if (SUCCEEDED(result) && !iconPath.empty() &&
      IsSafeExistingRegularFile(iconPath)) {
    taskbarIconPath = GetTaskbarIconResourcePath(iconPath);
    iconResource = taskbarIconPath.wstring() + L",0";
    PROPVARIANT iconValue{};
    result = InitPropVariantFromString(iconResource.c_str(), &iconValue);
    LogTaskbarQaResult(L"InitIconResource", result);
    if (SUCCEEDED(result)) {
      result = propertyStore->SetValue(PKEY_AppUserModel_RelaunchIconResource,
                                       iconValue);
      LogTaskbarQaResult(L"SetIconResource", result);
      PropVariantClear(&iconValue);
    }
  } else if (SUCCEEDED(result)) {
    PROPVARIANT emptyValue{};
    PropVariantInit(&emptyValue);
    result = propertyStore->SetValue(PKEY_AppUserModel_RelaunchIconResource,
                                     emptyValue);
    LogTaskbarQaResult(L"ClearIconResource", result);
  }

  // Auxiliary relaunch properties must be established before the AppID. The
  // AppID write is what tells the taskbar to consume the complete identity.
  PROPVARIANT value{};
  if (SUCCEEDED(result)) {
    result = InitPropVariantFromString(appId.c_str(), &value);
    LogTaskbarQaResult(L"InitAppId", result);
  }
  if (SUCCEEDED(result)) {
    result = propertyStore->SetValue(PKEY_AppUserModel_ID, value);
    LogTaskbarQaResult(L"SetAppId", result);
  }
  PropVariantClear(&value);

  if (SUCCEEDED(result)) {
    result = propertyStore->Commit();
    LogTaskbarQaResult(L"Commit", result);
  }

  bool verified = false;
  if (SUCCEEDED(result)) {
    PROPVARIANT currentValue{};
    const HRESULT getAppIdResult =
        propertyStore->GetValue(PKEY_AppUserModel_ID, &currentValue);
    LogTaskbarQaResult(L"GetAppId", getAppIdResult);
    if (SUCCEEDED(getAppIdResult)) {
      PWSTR currentAppId = nullptr;
      const HRESULT readAppIdResult =
          PropVariantToStringAlloc(currentValue, &currentAppId);
      LogTaskbarQaResult(L"ReadAppId", readAppIdResult);
      if (SUCCEEDED(readAppIdResult) && currentAppId) {
        verified = _wcsicmp(currentAppId, appId.c_str()) == 0;
        CoTaskMemFree(currentAppId);
      }
      PropVariantClear(&currentValue);
    }

    if (verified) {
      verified = false;
      PROPVARIANT currentIconValue{};
      const HRESULT getIconResult = propertyStore->GetValue(
          PKEY_AppUserModel_RelaunchIconResource, &currentIconValue);
      LogTaskbarQaResult(L"GetIconResource", getIconResult);
      if (SUCCEEDED(getIconResult)) {
        PWSTR currentIconResource = nullptr;
        const HRESULT readIconResult = PropVariantToStringAlloc(
            currentIconValue, &currentIconResource);
        LogTaskbarQaResult(L"ReadIconResource", readIconResult);
        if (SUCCEEDED(readIconResult) && currentIconResource) {
          verified = iconResource.empty()
                         ? currentIconResource[0] == L'\0'
                         : _wcsicmp(currentIconResource,
                                    iconResource.c_str()) == 0;
          CoTaskMemFree(currentIconResource);
        } else if (iconResource.empty() && currentIconValue.vt == VT_EMPTY) {
          verified = true;
        }
        PropVariantClear(&currentIconValue);
      }
    }
  }

  propertyStore->Release();
  if (verified && !taskbarIconPath.empty()) {
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, taskbarIconPath.c_str(),
                   nullptr);
  }
  if (verified && recreateTaskbar)
    RecreateTaskbarButton(hWnd);
  if (verified)
    CleanupObsoleteTaskbarIconResources(clientRoot, taskbarIconPath);
  return verified;
}

static bool ApplyWindowIcons(HWND hWnd, HICON hSmall, HICON hBig,
                             bool forceNotify) {
  if (!IsWindow(hWnd))
    return false;

  constexpr UINT kMessageTimeoutMs = 350;
  const auto sendWithTimeout = [hWnd](UINT message, WPARAM wParam,
                                      LPARAM lParam,
                                      DWORD_PTR *result) -> bool {
    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(
               hWnd, message, wParam, lParam,
               SMTO_ABORTIFHUNG | SMTO_BLOCK, kMessageTimeoutMs,
               result ? result : &ignored) != 0;
  };
  bool iconChanged = false;
  bool iconsApplied = true;
  const auto updateIcon = [&](WPARAM iconType, HICON icon) {
    DWORD_PTR current = 0;
    const bool currentKnown =
        !forceNotify &&
        sendWithTimeout(WM_GETICON, iconType, 0, &current);
    if (forceNotify || !currentKnown || (HICON)current != icon) {
      if (sendWithTimeout(WM_SETICON, iconType, (LPARAM)icon, nullptr))
        iconChanged = true;
      else
        iconsApplied = false;
    }
  };

  updateIcon(ICON_SMALL, hSmall);
  updateIcon(ICON_BIG, hBig);
  updateIcon(2, hSmall);

  if (forceNotify || GetClassLongPtrW(hWnd, GCLP_HICONSM) != (LONG_PTR)hSmall) {
    SetClassLongPtrW(hWnd, GCLP_HICONSM, (LONG_PTR)hSmall);
    iconChanged = true;
  }
  if (forceNotify || GetClassLongPtrW(hWnd, GCLP_HICON) != (LONG_PTR)hBig) {
    SetClassLongPtrW(hWnd, GCLP_HICON, (LONG_PTR)hBig);
    iconChanged = true;
  }

  if (!forceNotify && !iconChanged)
    return iconsApplied;

  SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  RedrawWindow(hWnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
  return iconsApplied;
}

struct ClientWindowSearch {
  DWORD pid;
  std::vector<HWND> windows;
};

static BOOL CALLBACK EnumClientWindowsCallback(HWND hWnd, LPARAM lParam) {
  auto *search = reinterpret_cast<ClientWindowSearch *>(lParam);
  if (!search || !IsWindowVisible(hWnd) || GetWindowTextLengthW(hWnd) <= 0)
    return TRUE;

  const LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
  if (GetWindow(hWnd, GW_OWNER) != nullptr ||
      (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0) {
    return TRUE;
  }

  DWORD processId = 0;
  GetWindowThreadProcessId(hWnd, &processId);

  if (search->pid != 0 && processId == search->pid &&
      std::find(search->windows.begin(), search->windows.end(), hWnd) ==
          search->windows.end()) {
    search->windows.push_back(hWnd);
  }
  return TRUE;
}

static std::vector<HWND> FindClientWindows(DWORD pid,
                                           const std::wstring &clientName,
                                           BrowserKind browser) {
  (void)clientName;
  (void)browser;
  ClientWindowSearch search{pid, {}};
  EnumWindows(EnumClientWindowsCallback, (LPARAM)&search);
  return search.windows;
}

static void RefreshOpenClientWindowIcon(const std::wstring &clientName,
                                        bool forceNotify) {
  std::vector<std::pair<BrowserKind, DWORD>> sessions;
  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    for (const auto &[key, active] : g_activeProfiles) {
      if (_wcsicmp(key.clientName.c_str(), clientName.c_str()) == 0)
        sessions.emplace_back(key.browser, active.pid);
    }
  }
  if (sessions.empty())
    return;

  fs::path clientRoot;
  if (!TryGetSafeClientProfilePath(clientName, clientRoot))
    return;
  const fs::path iconPath = clientRoot / "client.ico";
  const bool hasIcon = IsSafeExistingRegularFile(iconPath);

  for (const auto &[browser, pid] : sessions) {
    for (HWND ew : FindClientWindows(pid, clientName, browser)) {
    const UINT dpi = GetDpiForWindow(ew);
    const int pxSmall = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const int pxBig = GetSystemMetricsForDpi(SM_CXICON, dpi);

    HICON hSmall = nullptr;
    HICON hBig = nullptr;
    if (hasIcon) {
      std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
      auto &cacheSet = g_iconCache[clientName];
      RefreshIconCacheSource(clientName, cacheSet, iconPath);
      auto getIconPx = [&](int px) -> HICON {
        auto it = cacheSet.byPx.find(px);
        if (it != cacheSet.byPx.end())
          return it->second;
        HICON h = LoadIconFromIcoBestDownscale(iconPath, px);
        cacheSet.byPx[px] = h;
        return h;
      };
      hSmall = getIconPx(pxSmall);
      hBig = getIconPx(pxBig);
    }

    ApplyWindowIcons(ew, hSmall, hBig, forceNotify);

    const std::wstring appId = GetClientAppUserModelId(clientName, browser);
    SetWindowAppId(ew, appId, hasIcon ? iconPath : fs::path(), clientRoot);

    if (forceNotify) {
      RedrawWindow(ew, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    }
    }
  }
}

std::wstring AnsiToWide(const std::string &str) {
  if (str.empty()) {
    return std::wstring();
  }
  int size_needed = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
  if (size_needed == 0) {
    return std::wstring();
  }
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstrTo[0], size_needed);
  if (!wstrTo.empty() && wstrTo.back() == L'\0') {
    wstrTo.pop_back();
  }
  return wstrTo;
}

std::vector<std::wstring> GetSupportedImageTypes() {
  std::vector<std::wstring> supportedTypes;
  UINT numDecoders = 0, sizeBytes = 0;
  Gdiplus::GetImageDecodersSize(&numDecoders, &sizeBytes);
  if (sizeBytes == 0)
    return supportedTypes;

  std::vector<BYTE> buf(sizeBytes);
  auto *p = (Gdiplus::ImageCodecInfo *)buf.data();
  Gdiplus::GetImageDecoders(numDecoders, sizeBytes, p);

  for (UINT i = 0; i < numDecoders; ++i) {
    std::wstring extensions(p[i].FilenameExtension);
    std::wstringstream ss(extensions);
    std::wstring ext;
    while (std::getline(ss, ext, L';')) {
      if (ext.rfind(L"*.", 0) == 0)
        ext = ext.substr(2);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
      if (ext != L"ico" &&
          std::find(supportedTypes.begin(), supportedTypes.end(), ext) ==
              supportedTypes.end()) {
        supportedTypes.push_back(ext);
      }
    }
  }
  return supportedTypes;
}

static std::unique_ptr<Gdiplus::Bitmap>
ResizeBitmap_StretchSquare(Gdiplus::Bitmap *src, int dstW, int dstH) {
  if (!src || dstW <= 0 || dstH <= 0 || dstW > 256 || dstH > 256)
    return nullptr;
  auto dst =
      std::make_unique<Gdiplus::Bitmap>(dstW, dstH, PixelFormat32bppARGB);
  if (!dst || dst->GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  Gdiplus::Graphics g(dst.get());
  if (g.GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

  // AutoIt _GDIPlus_ImageResize() behavior here is effectively a stretch to
  // requested size.
  if (g.DrawImage(src, 0, 0, dstW, dstH) != Gdiplus::Ok)
    return nullptr;
  return dst;
}

bool ConvertImageToIcon(const fs::path &sourceImagePath,
                        const fs::path &destIconPath) {
  std::error_code fileError;
  const uintmax_t sourceBytes = fs::file_size(sourceImagePath, fileError);
  if (fileError || sourceBytes == 0 ||
      sourceBytes > kMaxImportedImageFileBytes)
    return false;

  std::unique_ptr<Gdiplus::Bitmap> src(
      Gdiplus::Bitmap::FromFile(sourceImagePath.c_str()));
  if (!src || src->GetLastStatus() != Gdiplus::Ok)
    return false;

  const UINT w = src->GetWidth();
  const UINT h = src->GetHeight();
  if (w == 0 || h == 0 || w > kMaxImportedImageDimension ||
      h > kMaxImportedImageDimension ||
      static_cast<unsigned long long>(w) * h > kMaxImportedImagePixels)
    return false;
  const int iSize = static_cast<int>((std::max)(w, h));

  // Requested canonical sizes
  const int sizes[] = {16, 32, 48, 64, 128, 256};

  std::vector<HICON> icons;
  icons.reserve(6);

  for (int s : sizes) {
    if (s > iSize)
      continue; // NEVER upscale; only generate sizes <= source square
    auto bmp = ResizeBitmap_StretchSquare(src.get(), s, s);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
      for (HICON icon : icons)
        DestroyIcon(icon);
      return false;
    }

    HICON hIco = NULL;
    if (bmp->GetHICON(&hIco) == Gdiplus::Ok && hIco) {
      icons.push_back(hIco);
    }
  }

  if (icons.empty())
    return false;

  // IMPORTANT: don't skip index 0 (this was dropping your 16px)
  bool ok =
      SaveIconsToFile(destIconPath, icons, /*compress*/ true, /*startIndex*/ 0);

  for (HICON hIco : icons)
    DestroyIcon(hIco);
  return ok;
}

HICON Create32BitHICON(HICON hIcon) {
  ICONINFOEXW iconInfo = {sizeof(ICONINFOEXW)};
  if (!GetIconInfoExW(hIcon, &iconInfo)) {
    return NULL;
  }
  const auto releaseSourceBitmaps = [&iconInfo]() {
    if (iconInfo.hbmColor) {
      DeleteObject(iconInfo.hbmColor);
      iconInfo.hbmColor = nullptr;
    }
    if (iconInfo.hbmMask) {
      DeleteObject(iconInfo.hbmMask);
      iconInfo.hbmMask = nullptr;
    }
  };
  int width = 0;
  int height = 0;
  if (iconInfo.hbmColor) {
    BITMAP bmp = {0};
    if (GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp)) {
      width = bmp.bmWidth;
      height = bmp.bmHeight;
    }
  }
  if (width <= 0 || height <= 0) {
    width = GetSystemMetrics(SM_CXICON);
    height = GetSystemMetrics(SM_CYICON);
  }
  if (iconInfo.hbmColor) {
    BITMAP bmp = {0};
    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp);
    if (bmp.bmBitsPixel == 32 && IsAlphaBitmap(iconInfo.hbmColor)) {
      HICON hCopy = CopyIcon(hIcon);
      releaseSourceBitmaps();
      return hCopy;
    }
  }
  HDC hdcScreen = GetDC(NULL);
  if (!hdcScreen) {
    releaseSourceBitmaps();
    return NULL;
  }
  HDC hdcMem = CreateCompatibleDC(hdcScreen);
  if (!hdcMem) {
    ReleaseDC(NULL, hdcScreen);
    releaseSourceBitmaps();
    return NULL;
  }
  BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *pBits = nullptr;
  HBITMAP hDib =
      CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
  if (!hDib || !pBits) {
    if (hDib)
      DeleteObject(hDib);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    releaseSourceBitmaps();
    return NULL;
  }
  const HGDIOBJ oldBitmap = SelectObject(hdcMem, hDib);
  if (!oldBitmap || oldBitmap == HGDI_ERROR) {
    DeleteObject(hDib);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    releaseSourceBitmaps();
    return NULL;
  }
  const BOOL cleared = PatBlt(hdcMem, 0, 0, width, height, BLACKNESS);
  const BOOL rendered =
      cleared && DrawIconEx(hdcMem, 0, 0, hIcon, width, height, 0, NULL,
                            DI_NORMAL);
  SelectObject(hdcMem, oldBitmap);
  DeleteDC(hdcMem);
  ReleaseDC(NULL, hdcScreen);
  if (!rendered) {
    DeleteObject(hDib);
    releaseSourceBitmaps();
    return NULL;
  }
  HBITMAP hMask = CreateBitmap(width, height, 1, 1, NULL);
  if (!hMask) {
    DeleteObject(hDib);
    releaseSourceBitmaps();
    return NULL;
  }
  ICONINFO newIconInfo = {0};
  newIconInfo.fIcon = TRUE;
  newIconInfo.hbmColor = hDib;
  newIconInfo.hbmMask = hMask;
  HICON hNewIcon = CreateIconIndirect(&newIconInfo);
  DeleteObject(hDib);
  DeleteObject(hMask);
  releaseSourceBitmaps();
  return hNewIcon;
}

CLSID GetEncoderClsid(const WCHAR *format) {
  UINT num = 0;
  UINT size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (size == 0)
    return {0};
  auto pImageCodecInfo = std::make_unique<Gdiplus::ImageCodecInfo[]>(size);
  if (!pImageCodecInfo)
    return {0};
  GetImageEncoders(num, size, pImageCodecInfo.get());
  for (UINT j = 0; j < num; ++j) {
    if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
      return pImageCodecInfo[j].Clsid;
    }
  }
  return {0};
}
bool IsAlphaBitmap(HBITMAP hBitmap) {
  BITMAP bmp{};
  if (!GetObject(hBitmap, sizeof(bmp), &bmp))
    return false;
  if (bmp.bmBitsPixel != 32)
    return false;

  const int dataSize = bmp.bmWidthBytes * bmp.bmHeight;
  if (dataSize <= 0)
    return false;

  std::vector<BYTE> pixels((size_t)dataSize);
  if (GetBitmapBits(hBitmap, dataSize, pixels.data()) == 0)
    return false;

  // AutoIt __AlphaProc(): considers alpha "present" if any A != 0
  for (int i = 3; i < dataSize; i += 4) {
    if (pixels[i] != 0)
      return true;
  }
  return false;
}

std::vector<BYTE> CompressBitmapToPng(HBITMAP hBitmap) {
  std::unique_ptr<Gdiplus::Bitmap> bitmap(
      Gdiplus::Bitmap::FromHBITMAP(hBitmap, NULL));
  if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
    return {};
  }
  CLSID pngClsid = GetEncoderClsid(L"image/png");
  IStream *pStream = NULL;
  if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) != S_OK) {
    return {};
  }
  if (bitmap->Save(pStream, &pngClsid, NULL) != Gdiplus::Ok) {
    pStream->Release();
    return {};
  }
  ULARGE_INTEGER streamSize;
  pStream->Seek({}, STREAM_SEEK_END, &streamSize);
  std::vector<BYTE> buffer(streamSize.QuadPart);
  pStream->Seek({}, STREAM_SEEK_SET, NULL);
  ULONG bytesRead;
  pStream->Read(buffer.data(), (ULONG)buffer.size(), &bytesRead);
  pStream->Release();
  if (bytesRead != buffer.size()) {
    return {};
  }
  return buffer;
}
#pragma pack(push, 1)
struct ICONDIRHDR {
  WORD idReserved;
  WORD idType;
  WORD idCount;
};
#pragma pack(pop)

static bool WriteAll(HANDLE hFile, const void *data, DWORD cb) {
  const BYTE *p = (const BYTE *)data;
  DWORD total = 0;
  while (total < cb) {
    DWORD w = 0;
    if (!WriteFile(hFile, p + total, cb - total, &w, nullptr))
      return false;
    if (w == 0)
      return false;
    total += w;
  }
  return true;
}

static bool WriteBitmapBitsForIcon(HANDLE hFile, const DIBSECTION &ds,
                                   DWORD dataSize) {
  const BYTE *bits = static_cast<const BYTE *>(ds.dsBm.bmBits);
  if (!bits || dataSize == 0)
    return false;

  const bool topDown = ds.dsBmih.biHeight < 0;
  if (!topDown)
    return WriteAll(hFile, bits, dataSize);

  const size_t rowBytes = static_cast<size_t>(ds.dsBm.bmWidthBytes);
  const size_t height = static_cast<size_t>(ds.dsBm.bmHeight);
  const size_t expectedSize = rowBytes * height;
  if (rowBytes == 0 || height == 0 || expectedSize != dataSize)
    return WriteAll(hFile, bits, dataSize);

  for (size_t row = height; row > 0; --row) {
    if (!WriteAll(hFile, bits + ((row - 1) * rowBytes),
                  static_cast<DWORD>(rowBytes))) {
      return false;
    }
  }
  return true;
}

bool SaveIconsToFile(const fs::path &filePath, std::vector<HICON> &icons,
                     bool compressLargeImages, size_t startIndex) {
  if (icons.size() <= startIndex)
    return false;

  const size_t count = icons.size() - startIndex;

  HANDLE hFile =
      CreateFileW(filePath.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ, // matches AutoIt share=4
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  std::vector<ICONDIRENTRY> entries(count);
  ICONDIRHDR hdr{};
  hdr.idReserved = 0;
  hdr.idType = 1;
  hdr.idCount = (WORD)count;

  // AutoIt writes placeholder header+entries, streams image data, then seeks
  // back and rewrites.
  if (!WriteAll(hFile, &hdr, (DWORD)sizeof(hdr))) {
    CloseHandle(hFile);
    DeleteFileW(filePath.c_str());
    return false;
  }
  {
    std::vector<BYTE> zero(sizeof(ICONDIRENTRY) * count, 0);
    if (!WriteAll(hFile, zero.data(), (DWORD)zero.size())) {
      CloseHandle(hFile);
      DeleteFileW(filePath.c_str());
      return false;
    }
  }

  DWORD offset = (DWORD)(sizeof(ICONDIRHDR) + sizeof(ICONDIRENTRY) * count);

  std::vector<HICON> tempIcons; // like AutoIt $aTemp[]
  tempIcons.reserve(count);

  auto fail = [&](void) {
    for (HICON h : tempIcons)
      DestroyIcon(h);
    CloseHandle(hFile);
    DeleteFileW(filePath.c_str());
  };

  for (size_t i = 0; i < count; ++i) {
    HICON hIcon = icons[startIndex + i];
    bool didTemp = false;

    for (int attempt = 0; attempt < 2; ++attempt) {
      ICONINFO ii{};
      if (!GetIconInfo(hIcon, &ii)) {
        fail();
        return false;
      }

      // AutoIt: CopyImage(..., 0x2008 = LR_CREATEDIBSECTION |
      // LR_COPYDELETEORG)
      HBITMAP hMask =
          (HBITMAP)CopyImage(ii.hbmMask, IMAGE_BITMAP, 0, 0,
                             LR_CREATEDIBSECTION | LR_COPYDELETEORG);
      HBITMAP hColor =
          (HBITMAP)CopyImage(ii.hbmColor, IMAGE_BITMAP, 0, 0,
                             LR_CREATEDIBSECTION | LR_COPYDELETEORG);

      // If CopyImage failed, originals weren't deleted; clean them.
      if (!hMask || !hColor) {
        if (ii.hbmMask)
          DeleteObject(ii.hbmMask);
        if (ii.hbmColor)
          DeleteObject(ii.hbmColor);
        if (hMask)
          DeleteObject(hMask);
        if (hColor)
          DeleteObject(hColor);
        fail();
        return false;
      }

      DIBSECTION dsMask{};
      DIBSECTION dsColor{};
      if (GetObject(hMask, sizeof(dsMask), &dsMask) != sizeof(dsMask) ||
          GetObject(hColor, sizeof(dsColor), &dsColor) != sizeof(dsColor)) {
        DeleteObject(hMask);
        DeleteObject(hColor);
        fail();
        return false;
      }

      DWORD maskSize = dsMask.dsBmih.biSizeImage;
      DWORD colorSize = dsColor.dsBmih.biSizeImage;
      if (maskSize == 0)
        maskSize = (DWORD)(dsMask.dsBm.bmWidthBytes * dsMask.dsBm.bmHeight);
      if (colorSize == 0)
        colorSize = (DWORD)(dsColor.dsBm.bmWidthBytes * dsColor.dsBm.bmHeight);

      const int bpp = dsColor.dsBm.bmBitsPixel;
      if (bpp != 16 && bpp != 24 && bpp != 32) {
        DeleteObject(hMask);
        DeleteObject(hColor);
        fail();
        return false;
      }

      // AutoIt: for 32bpp, if not alpha -> try Create32BitHICON once and
      // restart this icon.
      if (bpp == 32) {
        if (!IsAlphaBitmap(hColor)) {
          if (!didTemp) {
            HICON hNew = Create32BitHICON(hIcon);
            if (hNew) {
              tempIcons.push_back(hNew);
              hIcon = hNew;
              didTemp = true;
              DeleteObject(hMask);
              DeleteObject(hColor);
              continue; // retry
            }
          }
        } else {
          // AutoIt compress condition: (colorSize >= 256*256*4) AND compress
          if (compressLargeImages && colorSize >= (256u * 256u * 4u)) {
            std::vector<BYTE> png = CompressBitmapToPng(hColor);
            if (!png.empty()) {
              ICONDIRENTRY &e = entries[i];
              e.bWidth =
                  (dsColor.dsBm.bmWidth < 256) ? (BYTE)dsColor.dsBm.bmWidth : 0;
              e.bHeight = (dsColor.dsBm.bmHeight < 256)
                              ? (BYTE)dsColor.dsBm.bmHeight
                              : 0;
              e.bColorCount = 0;
              e.bReserved = 0;
              e.wPlanes = 1;
              e.wBitCount = 32;
              e.dwBytesInRes = (DWORD)png.size();
              e.dwImageOffset = offset;

              if (!WriteAll(hFile, png.data(), (DWORD)png.size())) {
                DeleteObject(hMask);
                DeleteObject(hColor);
                fail();
                return false;
              }
              offset += (DWORD)png.size();

              DeleteObject(hMask);
              DeleteObject(hColor);
              break; // done with this icon
            }
          }
        }
      }

      // Uncompressed path: BITMAPINFOHEADER + color (XOR) + mask (AND)
      BITMAPINFOHEADER bih{};
      bih.biSize = 40;
      bih.biWidth = dsColor.dsBm.bmWidth;
      bih.biHeight = 2 * dsColor.dsBm.bmHeight;
      bih.biPlanes = 1;
      bih.biBitCount = (WORD)bpp;
      bih.biCompression = BI_RGB;
      bih.biSizeImage = colorSize + maskSize;

      ICONDIRENTRY &e = entries[i];
      e.bWidth = (dsColor.dsBm.bmWidth < 256) ? (BYTE)dsColor.dsBm.bmWidth : 0;
      e.bHeight =
          (dsColor.dsBm.bmHeight < 256) ? (BYTE)dsColor.dsBm.bmHeight : 0;
      e.bColorCount = 0;
      e.bReserved = 0;
      e.wPlanes = 1;
      e.wBitCount = (WORD)bpp;
      e.dwBytesInRes = 40 + colorSize + maskSize;
      e.dwImageOffset = offset;

      if (!WriteAll(hFile, &bih, 40) ||
          !WriteBitmapBitsForIcon(hFile, dsColor, colorSize) ||
          !WriteBitmapBitsForIcon(hFile, dsMask, maskSize)) {
        DeleteObject(hMask);
        DeleteObject(hColor);
        fail();
        return false;
      }

      offset += 40 + colorSize + maskSize;

      DeleteObject(hMask);
      DeleteObject(hColor);
      break;
    }
  }

  // Seek back and write final header + dir entries (AutoIt does this)
  SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
  if (!WriteAll(hFile, &hdr, (DWORD)sizeof(hdr)) ||
      !WriteAll(hFile, entries.data(),
                (DWORD)(entries.size() * sizeof(ICONDIRENTRY)))) {
    for (HICON h : tempIcons)
      DestroyIcon(h);
    CloseHandle(hFile);
    DeleteFileW(filePath.c_str());
    return false;
  }

  CloseHandle(hFile);
  for (HICON h : tempIcons)
    DestroyIcon(h);
  return true;
}

static bool ValidateSanitizedDefaultProfileTree(const fs::path &root,
                                                std::wstring &errorDetails) {
  auto fail = [&errorDetails](const std::wstring &message) {
    errorDetails = message;
    return false;
  };
  auto isReparsePoint = [](const fs::path &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  };

  try {
    if (!fs::is_directory(root) || isReparsePoint(root))
      return fail(L"The sanitized output root is not a safe directory.");

    const fs::path defaultRoot = root / L"Default";
    size_t rootEntryCount = 0;
    for (const auto &entry : fs::directory_iterator(root)) {
      ++rootEntryCount;
      if (entry.path().filename() != L"Default" || !entry.is_directory() ||
          isReparsePoint(entry.path())) {
        return fail(L"The sanitized output contains an unexpected root item.");
      }
    }
    if (rootEntryCount != 1 || !fs::is_directory(defaultRoot))
      return fail(L"The sanitized output does not contain exactly Default.");

    bool foundPreferences = false;
    bool foundSecurePreferences = false;
    bool foundFavicons = false;
    fs::path extensionsRoot;
    for (const auto &entry : fs::directory_iterator(defaultRoot)) {
      if (isReparsePoint(entry.path()))
        return fail(L"The sanitized Default profile contains a reparse point.");

      const std::wstring name = entry.path().filename().wstring();
      if (name == L"Bookmarks") {
        if (!entry.is_regular_file())
          return fail(L"The sanitized Bookmarks item is not a regular file.");
      } else if (name == L"Favicons") {
        constexpr uintmax_t kMaximumFaviconsBytes = 256ULL * 1024ULL * 1024ULL;
        if (!entry.is_regular_file() || entry.file_size() == 0 ||
            entry.file_size() > kMaximumFaviconsBytes) {
          return fail(
              L"The sanitized Favicons item is not a bounded regular file.");
        }
        foundFavicons = true;
      } else if (name == L"Preferences") {
        if (!entry.is_regular_file())
          return fail(L"The sanitized Preferences item is not a regular file.");
        foundPreferences = true;
      } else if (name == L"Secure Preferences") {
        if (!entry.is_regular_file()) {
          return fail(
              L"The sanitized Secure Preferences item is not a regular file.");
        }
        foundSecurePreferences = true;
      } else if (name == L"Extensions") {
        if (!entry.is_directory())
          return fail(L"The sanitized Extensions item is not a directory.");
        extensionsRoot = entry.path();
      } else {
        return fail(L"The sanitized Default profile contains an unexpected "
                    L"top-level item: " +
                    name);
      }
    }
    if (!foundPreferences || !foundSecurePreferences || !foundFavicons) {
      return fail(L"The sanitized Default profile is missing required "
                  L"preference or Favicons files.");
    }

    if (!extensionsRoot.empty()) {
      for (const auto &extension : fs::directory_iterator(extensionsRoot)) {
        const std::wstring id = extension.path().filename().wstring();
        const bool validId =
            id.size() == 32 &&
            std::ranges::all_of(id, [](wchar_t ch) {
              return ch >= L'a' && ch <= L'p';
            });
        if (!validId || !extension.is_directory() ||
            isReparsePoint(extension.path())) {
          return fail(L"The sanitized Extensions folder contains an invalid "
                      L"item.");
        }
        for (const auto &item :
             fs::recursive_directory_iterator(extension.path())) {
          if (isReparsePoint(item.path()) ||
              (!item.is_directory() && !item.is_regular_file())) {
            return fail(
                L"A sanitized extension contains an unsafe filesystem item.");
          }
        }
      }
    }
    return true;
  } catch (const std::exception &e) {
    errorDetails = AnsiToWide(e.what());
    return false;
  }
}

static void HandleClosedDefaultProfile() {
  const fs::path profilePath = g_sDataDir / L"Default";
  const fs::path archivePath = g_sDataDir / L"Default.7z";

  g_bDefaultProfileUiBusy = true;
  struct DefaultProfileUiBusyScope {
    ~DefaultProfileUiBusyScope() {
      g_bDefaultProfileUiBusy = false;
      SetUiState(true);
    }
  } uiBusyScope;
  SetUiState(false);
  std::wstring profileUseError;
  if (ProbeExactBrowserProfileInUse(profilePath, profileUseError) !=
      ProfileUseState::NotInUse) {
    std::wstring message =
        L"The tracked Default editor process closed, but another browser is "
        L"still using its profile or process inspection was inconclusive. "
        L"The profile was left unchanged.";
    if (!profileUseError.empty())
      message += L"\n\n" + profileUseError;
    MessageBoxW(g_hGui, message.c_str(), L"Default Profile",
                MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }
  const int saveChoice = MessageBoxW(
      g_hGui,
      L"Save these changes as the starter for future new clients?\n\n"
      L"Clients that already exist will not be changed.",
      L"Save Default Profile", MB_YESNO | MB_ICONQUESTION | MB_APPLMODAL);

  if (saveChoice != IDYES) {
    std::wstring cleanupDetails;
    const bool removed =
        RemoveTransientProfileIfIdleAndVerify(profilePath, cleanupDetails);
    SetUiState(true);
    if (!removed) {
      std::wstring message =
          L"The Default editor closed, but its disposable profile could not "
          L"be removed. Close any browser still using it before reopening the "
          L"editor.";
      if (!cleanupDetails.empty())
        message += L"\n\nDetails: " + cleanupDetails;
      MessageBoxW(g_hGui, message.c_str(), L"Default Profile",
                  MB_OK | MB_ICONERROR);
    }
    return;
  }

  profileUseError.clear();
  if (ProbeExactBrowserProfileInUse(profilePath, profileUseError) !=
      ProfileUseState::NotInUse) {
    std::wstring message =
        L"The Default profile became active in another browser or could not "
        L"be reverified after confirmation. The previous starter and editor "
        L"profile were left unchanged.";
    if (!profileUseError.empty())
      message += L"\n\n" + profileUseError;
    MessageBoxW(g_hGui, message.c_str(), L"Default Profile",
                MB_OK | MB_ICONWARNING);
    SetUiState(true);
    return;
  }

  bool saved = false;
  std::wstring errorDetails;
  const fs::path workBase = g_sDataDir / L"_DefaultSave";
  const fs::path workRoot =
      workBase /
      std::format(L"{}-{}", GetCurrentProcessId(), GetTickCount64());
  const fs::path sanitizedRoot = workRoot / L"Sanitized";
  const fs::path sanitizerScript =
      workRoot / L"Sanitize-DefaultProfileDirectory.ps1";
  const fs::path stagedArchive = workRoot / L"Default.7z.staged";

  try {
    fs::create_directories(workBase);
    const DWORD workBaseAttributes = GetFileAttributesW(workBase.c_str());
    if (workBaseAttributes == INVALID_FILE_ATTRIBUTES ||
        (workBaseAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (workBaseAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      throw std::runtime_error(
          "The Default-save staging root is not a safe local directory");
    }
    if (!CreateDirectoryW(workRoot.c_str(), nullptr)) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "A unique Default-save work folder could not be "
                              "created");
    }
    if (!ExtractResourceToFile(IDR_DEFAULT_PROFILE_SANITIZER, sanitizerScript,
                               true) ||
        !fs::is_regular_file(sanitizerScript) ||
        fs::file_size(sanitizerScript) == 0) {
      throw std::runtime_error(
          "The embedded Default-profile sanitizer could not be staged");
    }

    std::vector<wchar_t> systemDirectory(32768, L'\0');
    const UINT systemDirectoryLength = GetSystemDirectoryW(
        systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
    if (systemDirectoryLength == 0) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Windows PowerShell could not be located");
    }
    if (systemDirectoryLength >= systemDirectory.size()) {
      throw std::system_error(
          ERROR_INSUFFICIENT_BUFFER, std::system_category(),
          "The Windows system-directory path was too long to validate");
    }
    const fs::path powerShell =
        fs::path(systemDirectory.data()) / L"WindowsPowerShell" / L"v1.0" /
        L"powershell.exe";
    if (!fs::is_regular_file(powerShell)) {
      throw std::runtime_error(
          "Windows PowerShell is required to sanitize the Default profile");
    }

    constexpr DWORD kDefaultSanitizerTimeoutMilliseconds = 10 * 60 * 1000;
    const std::wstring arguments =
        L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        QuoteCommandLineArgument(sanitizerScript.wstring()) +
        L" -SourceProfile " + QuoteCommandLineArgument(profilePath.wstring()) +
        L" -OutputRoot " + QuoteCommandLineArgument(sanitizedRoot.wstring());

    ProgressUI_Show(g_hGui, L"Sanitizing and saving Default Profile...", -1);
    const HiddenCommandResult commandResult = RunHiddenCommandAndWait(
        powerShell, arguments, kDefaultSanitizerTimeoutMilliseconds);
    if (!commandResult.launched) {
      throw std::system_error(
          (int)commandResult.systemError, std::system_category(),
          "The Default-profile sanitizer could not be started");
    }
    if (commandResult.timedOut) {
      throw std::system_error(ERROR_TIMEOUT, std::system_category(),
                              "The Default-profile sanitizer timed out");
    }
    if (commandResult.systemError != ERROR_SUCCESS) {
      throw std::system_error(
          (int)commandResult.systemError, std::system_category(),
          "The Default-profile sanitizer could not be monitored safely");
    }
    if (commandResult.exitCode != 0) {
      throw std::runtime_error(
          std::format("The Default-profile sanitizer failed (exit code {}).",
                      commandResult.exitCode));
    }
    if (!fs::is_directory(sanitizedRoot / L"Default")) {
      throw std::runtime_error(
          "The sanitizer did not create a Default profile payload");
    }
    std::wstring validationError;
    if (!ValidateSanitizedDefaultProfileTree(sanitizedRoot,
                                             validationError)) {
      throw std::runtime_error(
          "The sanitized Default profile failed exact policy validation");
    }

    HRESULT hr =
        _7zCompress7z(stagedArchive.c_str(), sanitizedRoot.c_str(), false,
                      nullptr, nullptr);
    if (FAILED(hr) || !fs::is_regular_file(stagedArchive) ||
        fs::file_size(stagedArchive) == 0) {
      throw std::runtime_error("The new Default.7z archive was not created");
    }

    _7zArchiveInfo archiveInfo{};
    hr = _7zInspect7z(stagedArchive.c_str(), &archiveInfo);
    if (FAILED(hr) || archiveInfo.fileCount < 2) {
      throw std::runtime_error(
          "The new Default.7z archive failed structural validation");
    }
    hr = _7zTest7z(stagedArchive.c_str(), nullptr, nullptr);
    if (FAILED(hr)) {
      throw std::runtime_error(
          "The new Default.7z archive failed its integrity test");
    }

    profileUseError.clear();
    if (ProbeExactBrowserProfileInUse(profilePath, profileUseError) !=
        ProfileUseState::NotInUse) {
      throw std::runtime_error(
          "The Default profile became active while it was being sanitized");
    }

    const fs::path backupDir = g_sDataDir / L"_DefBak";
    fs::create_directories(backupDir);
    const fs::path backupPath =
        backupDir / (GetBackupTimestamp() + L"-Sanitized-Default.7z");
    fs::copy_file(stagedArchive, backupPath,
                  fs::copy_options::overwrite_existing);

    if (!MoveFileExW(stagedArchive.c_str(), archivePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error((int)GetLastError(), std::system_category(),
                              "Could not replace Default.7z");
    }

    saved = true;
  } catch (const std::exception &e) {
    errorDetails = AnsiToWide(e.what());
  } catch (...) {
    errorDetails = L"Unknown error.";
  }

  ProgressUI_Hide();
  std::error_code cleanupError;
  fs::remove(stagedArchive, cleanupError);
  cleanupError.clear();
  fs::remove_all(workRoot, cleanupError);
  cleanupError.clear();
  if (fs::is_directory(workBase, cleanupError) &&
      fs::is_empty(workBase, cleanupError)) {
    fs::remove(workBase, cleanupError);
  }
  std::wstring profileCleanupDetails;
  const bool profileRemoved =
      RemoveTransientProfileIfIdleAndVerify(profilePath, profileCleanupDetails);
  if (saved && profileRemoved) {
    MessageBoxW(g_hGui,
                L"The Default profile was saved. Future new clients will use "
                L"the updated starter.",
                L"Default Profile", MB_OK | MB_ICONINFORMATION);
  } else if (saved) {
    std::wstring message =
        L"The starter was saved, but the disposable Default editor profile "
        L"could not be removed. Close any browser still using it before "
        L"reopening the editor.";
    if (!profileCleanupDetails.empty())
      message += L"\n\nDetails: " + profileCleanupDetails;
    MessageBoxW(g_hGui, message.c_str(), L"Default Profile",
                MB_OK | MB_ICONERROR);
  } else {
    std::wstring message =
        L"The Default profile could not be saved. The previous starter is "
        L"unchanged, and the disposable editor data was not retained.";
    if (!errorDetails.empty())
      message += L"\n\nDetails: " + errorDetails;
    if (!profileRemoved) {
      message +=
          L"\n\nThe disposable Default profile could not be removed. Close any "
          L"browser still using it before reopening the editor.";
      if (!profileCleanupDetails.empty())
        message += L"\n\nCleanup details: " + profileCleanupDetails;
    }
    MessageBoxW(g_hGui, message.c_str(), L"Default Profile",
                MB_OK | MB_ICONERROR);
  }

  SetUiState(true);
}

static bool HandleProfileExitOnUiThread(const ProfileExitPayload &payload) {
  bool claimed = false;
  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    const auto active =
        g_activeProfiles.find(ClientBrowserKey{payload.clientName,
                                               payload.browser});
    if (active == g_activeProfiles.end() || active->second.pid != payload.pid ||
        active->second.type != payload.type) {
      return false;
    }
    g_activeProfiles.erase(active);
    claimed = true;
  }

  if (!claimed)
    return false;

  if (payload.launchRollback) {
    if (payload.type != ProfileType::Standard) {
      std::wstring cleanupDetails;
      const fs::path transientPath =
          g_sDataDir / (payload.type == ProfileType::Default ? L"Default"
                                                             : L"Temp");
      if (!RemoveTransientProfileIfIdleAndVerify(transientPath,
                                                  cleanupDetails)) {
        std::wstring message =
            L"The browser launch was rolled back, but its disposable "
            L"profile could not be removed.";
        if (!cleanupDetails.empty())
          message += L"\n\n" + cleanupDetails;
        MessageBoxW(g_hGui, message.c_str(), L"Profile Cleanup",
                    MB_OK | MB_ICONWARNING);
      }
    }
  } else if (payload.type == ProfileType::Temporary) {
    std::wstring cleanupDetails;
    if (!RemoveTransientProfileIfIdleAndVerify(g_sDataDir / L"Temp",
                                                cleanupDetails)) {
      std::wstring message =
          L"The temporary browser closed, but its disposable profile could "
          L"not be removed.";
      if (!cleanupDetails.empty())
        message += L"\n\n" + cleanupDetails;
      MessageBoxW(g_hGui, message.c_str(), L"Temporary Profile",
                  MB_OK | MB_ICONWARNING);
    }
  } else if (payload.type == ProfileType::Default) {
    HandleClosedDefaultProfile();
  }

  if (payload.sessionAnnounced)
    OnSessionEnded(payload.pid);
  return true;
}

static bool PostProfileExitMessage(DWORD pid,
                                   const std::wstring &clientName,
                                   BrowserKind browser, ProfileType type,
                                   bool launchRollback,
                                   bool sessionAnnounced) {
  std::unique_ptr<ProfileExitPayload> payload;
  try {
    payload = std::make_unique<ProfileExitPayload>(ProfileExitPayload{
        pid, clientName, browser, type, launchRollback, sessionAnnounced});
  } catch (...) {
    return false;
  }

  for (;;) {
    const HWND notifyWindow = g_hGui;
    if (!notifyWindow || !IsWindow(notifyWindow))
      return false;
    if (PostMessageW(notifyWindow, WM_APP_PROFILE_EXITED, 0,
                     reinterpret_cast<LPARAM>(payload.get()))) {
      payload.release();
      return true;
    }
    if (g_isShuttingDown.load())
      return false;
    Sleep(10);
  }
}

static void FinalizeProfileExit(DWORD pid, const std::wstring &clientName,
                                BrowserKind browser, ProfileType type) {
  if (PostProfileExitMessage(pid, clientName, browser, type))
    return;

  const std::wstring message = std::format(
      L"ctSpaces confirmed that {} for '{}' closed, but the main window "
      L"could not claim the exit notification. The profile remains marked "
      L"open to protect its data. Restart ctSpaces before reopening it.",
      GetBrowserDisplayName(browser), clientName);
  PostOwnedStringMessage(WM_APP_PROFILE_MONITOR_FAILED, 0, message, true);
}

static void ReaperThread(DWORD pid, std::wstring clientName,
                         BrowserKind browser, ProfileType type,
                         HANDLE processHandle) {
  if (!processHandle)
    return;

  const DWORD waitResult = WaitForSingleObject(processHandle, INFINITE);
  const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : 0;
  CloseHandle(processHandle);
  if (waitResult == WAIT_OBJECT_0) {
    FinalizeProfileExit(pid, clientName, browser, type);
    return;
  }

  std::wstring message = std::format(
      L"ctSpaces could not confirm whether {} for '{}' closed. The profile "
      L"will remain marked open to protect its data.",
      GetBrowserDisplayName(browser), clientName);
  if (waitResult == WAIT_FAILED)
    message += std::format(L"\n\nWindows error: {}.", waitError);
  else
    message += std::format(L"\n\nUnexpected wait result: 0x{:08X}.",
                           waitResult);
  message += L"\n\nClose the browser manually and restart ctSpaces before "
             L"reopening this profile.";
  PostOwnedStringMessage(WM_APP_PROFILE_MONITOR_FAILED, 0, message, true);
}

static bool StartReaperThread(DWORD pid, const std::wstring &clientName,
                              BrowserKind browser, ProfileType type,
                              HANDLE processHandle) {
  try {
    auto completed = std::make_shared<std::atomic_bool>(false);
    std::lock_guard<std::mutex> lock(g_reaperThreadsMutex);
    std::erase_if(g_reaperThreads, [](const ManagedReaperWorker &worker) {
      return worker.completed &&
             worker.completed->load(std::memory_order_acquire);
    });

    g_reaperThreads.emplace_back();
    ManagedReaperWorker &worker = g_reaperThreads.back();
    worker.completed = completed;
    try {
      worker.thread = std::jthread(
          [pid, clientName, browser, type, processHandle, completed]() {
            try {
              ReaperThread(pid, clientName, browser, type, processHandle);
            } catch (...) {
            }
            completed->store(true, std::memory_order_release);
          });
    } catch (...) {
      g_reaperThreads.pop_back();
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

static void StopReaperThreads() {
  std::vector<ManagedReaperWorker> workers;
  {
    std::lock_guard<std::mutex> lock(g_reaperThreadsMutex);
    workers.swap(g_reaperThreads);
  }
  for (auto &worker : workers) {
    if (worker.thread.joinable())
      worker.thread.join();
  }
}

void EnsureWatcherIsRunning() {
  if (g_isShuttingDown.load())
    return;

  std::lock_guard<std::mutex> lifecycleLock(g_watcherLifecycleMutex);
  if (g_isShuttingDown.load() || g_isWatcherRunning.exchange(true))
    return;

  if (g_watcherThread.joinable())
    g_watcherThread.join();
  try {
    g_watcherThread = std::jthread(WatcherThread);
  } catch (...) {
    g_isWatcherRunning = false;
  }
}

static void StopWatcher() {
  g_isShuttingDown = true;
  {
    std::lock_guard<std::mutex> lifecycleLock(g_watcherLifecycleMutex);
    if (g_watcherThread.joinable()) {
      g_watcherThread.request_stop();
      g_watcherThread.join();
    }
    g_isWatcherRunning = false;
  }

  std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
  for (auto &[name, set] : g_iconCache)
    RetireCachedIcons(name, set);
  g_iconCache.clear();
  DestroyAllRetiredIconsLocked();
}

static std::optional<std::wstring>
GetBrowserPageTitle(const std::wstring &currentTitle,
                    const std::wstring &clientName, BrowserKind browser) {
  const std::wstring sessionLabel =
      std::format(L"{} [{}]", clientName, GetBrowserDisplayName(browser));
  return browser_title::GetPageTitle(
      currentTitle, sessionLabel, APP_ALIAS, browser == BrowserKind::Edge);
}

void WatcherThread(std::stop_token stopToken) {
  try {
  struct ComThreadScope {
    bool uninitialize = false;
    ~ComThreadScope() {
      if (uninitialize)
        CoUninitialize();
    }
  };

  const HRESULT comResult =
      CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
  ComThreadScope comScope{SUCCEEDED(comResult)};
  struct AssignedWindowIdentity {
    std::wstring clientName;
    std::wstring signature;
  };
  std::map<HWND, AssignedWindowIdentity> assignedWindowIdentities;
  while (!stopToken.stop_requested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (stopToken.stop_requested())
      break;

    ActiveProfileMap profiles_copy;
    bool noActiveProfiles = false;
    {
      std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
      if (g_activeProfiles.empty()) {
        g_isWatcherRunning = false;
        noActiveProfiles = true;
      } else {
        profiles_copy = g_activeProfiles;
      }
    }

    if (noActiveProfiles) {
      std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
      for (auto &[name, set] : g_iconCache) {
        for (auto &[px, icon] : set.byPx) {
          if (icon)
            DestroyIcon(icon);
        }
        set.byPx.clear();
      }
      g_iconCache.clear();
      DestroyAllRetiredIconsLocked();
      return;
    }

    std::map<std::wstring, bool> clientIconAssignmentsCurrent;
    std::map<HWND, AssignedWindowIdentity> observedWindowIdentities;
    for (const auto &[key, active] : profiles_copy) {
      (void)active;
      clientIconAssignmentsCurrent.try_emplace(key.clientName, true);
    }

    {
      std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
      for (auto it = g_iconCache.begin(); it != g_iconCache.end();) {
        const bool clientStillActive =
            std::ranges::any_of(profiles_copy, [&](const auto &active) {
              return _wcsicmp(active.first.clientName.c_str(),
                              it->first.c_str()) == 0;
            });
        if (!clientStillActive) {
          RetireCachedIcons(it->first, it->second);
          it = g_iconCache.erase(it);
        } else {
          ++it;
        }
      }
    }

    for (const auto &[key, active] : profiles_copy) {
      if (stopToken.stop_requested())
        break;

      const std::wstring &clientName = key.clientName;
      const BrowserKind browser = key.browser;
      const DWORD pid = active.pid;
      const fs::path &profilePath = active.profilePath;
      fs::path clientRoot;
      if (active.type == ProfileType::Standard) {
        if (!TryGetSafeClientProfilePath(clientName, clientRoot) ||
            !RevalidateSafeClientContainerPath(clientName, clientRoot)) {
          clientIconAssignmentsCurrent[clientName] = false;
          continue;
        }
      } else {
        clientRoot = profilePath;
      }

      fs::path iconPath = clientRoot / "client.ico";
      const bool hasIcon = IsSafeExistingRegularFile(iconPath);
      const std::wstring appId = GetClientAppUserModelId(clientName, browser);
      std::wstring identitySignature = appId;
      try {
        if (hasIcon) {
          identitySignature += std::format(
              L"|{}|{}", fs::file_size(iconPath),
              fs::last_write_time(iconPath).time_since_epoch().count());
        }
      } catch (...) {
        identitySignature += L"|unreadable";
      }

      std::vector<HWND> windows = FindClientWindows(pid, clientName, browser);
      if (windows.empty()) {
        clientIconAssignmentsCurrent[clientName] = false;
        continue;
      }

      for (HWND ew : windows) {
        const AssignedWindowIdentity expectedIdentity{clientName,
                                                      identitySignature};
        observedWindowIdentities[ew] = expectedIdentity;
        auto assigned = assignedWindowIdentities.find(ew);
        if (assigned == assignedWindowIdentities.end() ||
            assigned->second.clientName != clientName ||
            assigned->second.signature != identitySignature) {
          const UINT dpi = GetDpiForWindow(ew);
          const int pxSmall = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
          const int pxBig = GetSystemMetricsForDpi(SM_CXICON, dpi);

          HICON hSmall = nullptr;
          HICON hBig = nullptr;
          {
            std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
            auto &cacheSet = g_iconCache[clientName];
            RefreshIconCacheSource(clientName, cacheSet, iconPath);
            const auto getIconPx = [&](int px) -> HICON {
              auto it = cacheSet.byPx.find(px);
              if (it != cacheSet.byPx.end())
                return it->second;

              HICON icon = nullptr;
              if (hasIcon)
                icon = LoadIconFromIcoBestDownscale(iconPath, px);
              cacheSet.byPx[px] = icon;
              return icon;
            };
            hSmall = getIconPx(pxSmall);
            hBig = getIconPx(pxBig);
          }

          const bool iconsApplied = ApplyWindowIcons(ew, hSmall, hBig);
          const bool identityApplied =
              SetWindowAppId(ew, appId,
                             hasIcon ? iconPath : fs::path(), clientRoot);

          // Browser windows can reject identity updates briefly while they are
          // being created. Cache only a fully applied revision so the watcher
          // retries transient failures on its next pass.
          if (iconsApplied && identityApplied)
            assignedWindowIdentities[ew] = expectedIdentity;
        }

        assigned = assignedWindowIdentities.find(ew);
        if (assigned == assignedWindowIdentities.end() ||
            assigned->second.clientName != clientName ||
            assigned->second.signature != identitySignature) {
          clientIconAssignmentsCurrent[clientName] = false;
        }

        const int titleLength = GetWindowTextLengthW(ew);
        if (titleLength > 0 && titleLength < 4096) {
          std::wstring titleStr((size_t)titleLength + 1, L'\0');
          GetWindowTextW(ew, titleStr.data(), titleLength + 1);
          titleStr.resize((size_t)titleLength);
          const auto pageTitle =
              GetBrowserPageTitle(titleStr, clientName, browser);
          if (!pageTitle || pageTitle->empty())
            continue;

          const std::wstring sessionLabel = std::format(
              L"{} [{}]", clientName, GetBrowserDisplayName(browser));
          const std::wstring newTitle = browser_title::Format(
              *pageTitle, sessionLabel, APP_ALIAS,
              g_bClientTitleFirst.load());
          if (newTitle == titleStr)
            continue;
          DWORD_PTR ignored = 0;
          SendMessageTimeoutW(ew, WM_SETTEXT, 0, (LPARAM)newTitle.c_str(),
                              SMTO_ABORTIFHUNG | SMTO_BLOCK, 350, &ignored);
        }
      }
    }

    for (auto it = assignedWindowIdentities.begin();
         it != assignedWindowIdentities.end();) {
      if (!IsWindow(it->first)) {
        it = assignedWindowIdentities.erase(it);
      } else {
        const auto activeClient = clientIconAssignmentsCurrent.find(
            it->second.clientName);
        const auto observed = observedWindowIdentities.find(it->first);
        if (activeClient == clientIconAssignmentsCurrent.end() ||
            observed == observedWindowIdentities.end() ||
            observed->second.clientName != it->second.clientName ||
            observed->second.signature != it->second.signature) {
          clientIconAssignmentsCurrent[it->second.clientName] = false;
        }
        ++it;
      }
    }

    {
      std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
      std::vector<std::wstring> readyClients;
      readyClients.reserve(g_retiredIconHandles.size());
      for (const auto &[clientName, icons] : g_retiredIconHandles) {
        (void)icons;
        const auto readiness = clientIconAssignmentsCurrent.find(clientName);
        if (readiness == clientIconAssignmentsCurrent.end() ||
            readiness->second) {
          readyClients.push_back(clientName);
        }
      }
      for (const std::wstring &clientName : readyClients)
        DestroyRetiredIconsForClientLocked(clientName);
    }
  }
  } catch (...) {
    // Browser windows and icon files can disappear while metadata is read.
    // Treat that as a stopped watcher rather than terminating ctSpaces.
  }
  g_isWatcherRunning = false;
}

bool FindBrowsers() {
  // 1. Edge
  {
    std::vector<fs::path> searchPaths;
    PWSTR pPath = NULL;
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Microsoft" / "Edge" /
                            "Application" / "msedge.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Microsoft" / "Edge" /
                            "Application" / "msedge.exe");
      CoTaskMemFree(pPath);
    }
    for (const auto &path : searchPaths) {
      if (IsExactBrowserExecutable(path, BrowserKind::Edge)) {
        g_sEdgePath = path;
        break;
      }
    }
  }

  // 2. Chrome
  {
    std::vector<fs::path> searchPaths;
    PWSTR pPath = NULL;
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Google" / "Chrome" /
                            "Application" / "chrome.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Google" / "Chrome" /
                            "Application" / "chrome.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Google" / "Chrome" /
                            "Application" / "chrome.exe");
      CoTaskMemFree(pPath);
    }
    for (const auto &path : searchPaths) {
      if (IsExactBrowserExecutable(path, BrowserKind::Chrome)) {
        g_sChromePath = path;
        break;
      }
    }
  }

  // 3. Brave
  {
    std::vector<fs::path> searchPaths;
    PWSTR pPath = NULL;
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "BraveSoftware" /
                            "Brave-Browser" / "Application" / "brave.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "BraveSoftware" /
                            "Brave-Browser" / "Application" / "brave.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "BraveSoftware" /
                            "Brave-Browser" / "Application" / "brave.exe");
      CoTaskMemFree(pPath);
    }
    for (const auto &path : searchPaths) {
      if (IsExactBrowserExecutable(path, BrowserKind::Brave)) {
        g_sBravePath = path;
        break;
      }
    }
  }
  // 4. Firefox
  {
    std::vector<fs::path> searchPaths;
    PWSTR pPath = NULL;
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Mozilla Firefox" /
                            "firefox.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Mozilla Firefox" /
                            "firefox.exe");
      CoTaskMemFree(pPath);
    }
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &pPath))) {
      searchPaths.push_back(fs::path(pPath) / "Mozilla Firefox" /
                            "firefox.exe");
      CoTaskMemFree(pPath);
    }
    for (const auto &path : searchPaths) {
      if (IsExactBrowserExecutable(path, BrowserKind::Firefox)) {
        g_sFirefoxPath = path;
        break;
      }
    }
  }
  return (!g_sEdgePath.empty() || !g_sChromePath.empty() ||
          !g_sBravePath.empty() || !g_sFirefoxPath.empty());
}

constexpr DWORD kBrowserShutdownGraceMs = 8000;

struct CoordinatedShutdownTarget {
  DWORD pid = 0;
  HANDLE processHandle = nullptr;
};

struct CoordinatedShutdownState {
  HWND notifyWindow = nullptr;
  std::vector<CoordinatedShutdownTarget> targets;
  std::vector<DWORD> retryProcessIds;
};

static bool IsProfileProcessTracked(DWORD pid) {
  std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
  return std::ranges::any_of(g_activeProfiles, [pid](const auto &active) {
    return active.second.pid == pid;
  });
}

static void CloseCoordinatedShutdownHandles(
    const std::shared_ptr<CoordinatedShutdownState> &state) {
  for (const auto &target : state->targets) {
    if (target.processHandle)
      CloseHandle(target.processHandle);
  }
  state->targets.clear();
}

static bool TryTerminateProcessHandle(HANDLE processHandle) {
  const DWORD waitResult = WaitForSingleObject(processHandle, 0);
  if (waitResult == WAIT_OBJECT_0)
    return true;
  if (waitResult != WAIT_TIMEOUT)
    return false;

  const bool terminationRequested =
      TerminateProcess(processHandle, 0) != FALSE;
  constexpr DWORD kTerminationWaitMs = 5000;
  constexpr DWORD kTerminationFailureRaceWaitMs = 100;
  const DWORD finalWait = WaitForSingleObject(
      processHandle, terminationRequested ? kTerminationWaitMs
                                          : kTerminationFailureRaceWaitMs);
  return finalWait == WAIT_OBJECT_0;
}

static bool FinishCoordinatedShutdown(
    const std::shared_ptr<CoordinatedShutdownState> &state) {
  bool allHandled = true;
  for (const auto &target : state->targets) {
    const bool handled = TryTerminateProcessHandle(target.processHandle);
    CloseHandle(target.processHandle);
    if (!handled && IsProfileProcessTracked(target.pid))
      allHandled = false;
  }
  state->targets.clear();

  for (DWORD pid : state->retryProcessIds) {
    if (!IsProfileProcessTracked(pid))
      continue;

    HANDLE processHandle =
        OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    if (!processHandle) {
      allHandled = false;
      continue;
    }

    const bool handled = TryTerminateProcessHandle(processHandle);
    CloseHandle(processHandle);
    if (!handled && IsProfileProcessTracked(pid))
      allHandled = false;
  }
  state->retryProcessIds.clear();
  return allHandled;
}

static void StopShutdownWorker() {
  if (!g_shutdownThread.joinable())
    return;
  g_shutdownThread.request_stop();
  g_shutdownThread.join();
}

void RequestCloseAllProfiles() {
  StopShutdownWorker();

  std::vector<DWORD> processIds;
  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    processIds.reserve(g_activeProfiles.size());
    for (const auto &[key, active] : g_activeProfiles)
      processIds.push_back(active.pid);
  }
  if (processIds.empty())
    return;

  auto state = std::make_shared<CoordinatedShutdownState>();
  state->notifyWindow = g_hGui;
  state->targets.reserve(processIds.size());
  state->retryProcessIds.reserve(processIds.size());
  for (DWORD pid : processIds) {
    HANDLE processHandle =
        OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    if (processHandle)
      state->targets.push_back({pid, processHandle});
    else
      state->retryProcessIds.push_back(pid);
    RequestSessionShutdown(pid);
  }

  try {
    g_shutdownThread = std::jthread([state](std::stop_token stopToken) {
      DWORD waitedMs = 0;
      while (waitedMs < kBrowserShutdownGraceMs &&
             !stopToken.stop_requested()) {
        constexpr DWORD kShutdownPollMs = 100;
        const DWORD delay =
            min(kShutdownPollMs, kBrowserShutdownGraceMs - waitedMs);
        Sleep(delay);
        waitedMs += delay;
      }

      if (stopToken.stop_requested()) {
        CloseCoordinatedShutdownHandles(state);
        return;
      }

      if (!FinishCoordinatedShutdown(state) && state->notifyWindow)
        PostMessageW(state->notifyWindow, WM_APP_PROFILE_SHUTDOWN_FAILED, 0, 0);
    });
  } catch (...) {
    if (!FinishCoordinatedShutdown(state) && state->notifyWindow)
      PostMessageW(state->notifyWindow, WM_APP_PROFILE_SHUTDOWN_FAILED, 0, 0);
  }
}

std::wstring GetExeVersion(const fs::path &filePath);
bool CreateShortcut(const fs::path &targetPath, const fs::path &shortcutPath,
                    const fs::path &workingDir, const fs::path &iconPath,
                    std::wstring *errorDetails = nullptr);

std::wstring GetExeVersion(const fs::path &filePath) {
#ifdef CTSPACES_INSTALLER_TEST_HOOKS
  std::wstring overriddenVersion;
  if (CtSpacesInstallerTestTryGetExeVersion(filePath, overriddenVersion))
    return overriddenVersion;
#endif
  DWORD handle = 0;
  DWORD versionSize = GetFileVersionInfoSizeW(filePath.c_str(), &handle);
  if (versionSize == 0)
    return L"";
  auto versionData = std::make_unique<BYTE[]>(versionSize);
  if (!GetFileVersionInfoW(filePath.c_str(), 0, versionSize, versionData.get()))
    return L"";
  VS_FIXEDFILEINFO *fileInfo = nullptr;
  UINT fileInfoSize = 0;
  if (VerQueryValueW(versionData.get(), L"\\", (LPVOID *)&fileInfo,
                     &fileInfoSize) &&
      fileInfo) {
    return std::format(L"{}.{}.{}.{}", HIWORD(fileInfo->dwFileVersionMS),
                       LOWORD(fileInfo->dwFileVersionMS),
                       HIWORD(fileInfo->dwFileVersionLS),
                       LOWORD(fileInfo->dwFileVersionLS));
  }
  return L"";
}

static int CompareExeVersions(const std::wstring &lhs,
                              const std::wstring &rhs) {
  std::wistringstream leftStream(lhs);
  std::wistringstream rightStream(rhs);

  for (int i = 0; i < 4; ++i) {
    std::wstring leftPart;
    std::wstring rightPart;
    std::getline(leftStream, leftPart, L'.');
    std::getline(rightStream, rightPart, L'.');

    auto parseVersionPart = [](const std::wstring &part) -> int {
      if (part.empty())
        return 0;
      try {
        return std::stoi(part);
      } catch (...) {
        return 0;
      }
    };
    const int leftValue = parseVersionPart(leftPart);
    const int rightValue = parseVersionPart(rightPart);
    if (leftValue != rightValue)
      return leftValue > rightValue ? 1 : -1;
  }

  return 0;
}

static bool IsCorrectedVersionNumberingUpdate(const std::wstring &currentVersion,
                                              const std::wstring &installedVersion) {
  return installedVersion == L"5.0.1.0" &&
         currentVersion.rfind(L"5.0.0.", 0) == 0 &&
         CompareExeVersions(currentVersion, L"5.0.0.0") > 0;
}

static bool ValidateInstalledExecutablePath(const fs::path &installedPath,
                                            bool allowMissing,
                                            std::wstring &errorDetails) {
  if (g_sDataDir.empty() || !IsSafeExistingDirectory(g_sDataDir)) {
    errorDetails =
        L"The ctSpaces application-data folder is missing or is a reparse point.";
    return false;
  }

  const fs::path normalizedDataDir = NormalizePathForScope(g_sDataDir);
  const fs::path normalizedParent =
      NormalizePathForScope(installedPath.parent_path());
  if (_wcsicmp(normalizedDataDir.c_str(), normalizedParent.c_str()) != 0 ||
      !IsDirectChildPath(installedPath, g_sDataDir) ||
      _wcsicmp(installedPath.filename().c_str(), L"ctSpaces.exe") != 0) {
    errorDetails =
        L"The installed executable is not the exact ctSpaces.exe child of the "
        L"application-data folder.";
    return false;
  }

  const DWORD attributes = GetFileAttributesW(installedPath.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (allowMissing &&
        (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
      return true;
    }
    errorDetails =
        std::format(L"The installed executable could not be inspected (Windows "
                    L"error {}).",
                    error);
    return false;
  }
  if ((attributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    errorDetails =
        L"The installed executable path is a directory or reparse point.";
    return false;
  }
  return true;
}

static bool ValidateInstallCopyPaths(const fs::path &sourcePath,
                                     const fs::path &destPath,
                                     std::wstring &errorDetails) {
  const fs::path currentExecutable = GetCurrentExecutablePath();
  if (currentExecutable.empty() || !IsSafeExistingRegularFile(sourcePath) ||
      !SameExecutablePath(sourcePath, currentExecutable)) {
    errorDetails =
        L"The update source is not the current regular ctSpaces executable.";
    return false;
  }
  if (!ValidateInstalledExecutablePath(destPath, true, errorDetails))
    return false;
  if (IsSafeExistingRegularFile(destPath) &&
      SameExecutablePath(sourcePath, destPath)) {
    errorDetails = L"The update source and installed destination are the same file.";
    return false;
  }
  return true;
}

static std::wstring FormatStageCollisionToken(const GUID &guid) {
  return std::format(
      L"{:08x}{:04x}{:04x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
      static_cast<unsigned long>(guid.Data1),
      static_cast<unsigned int>(guid.Data2),
      static_cast<unsigned int>(guid.Data3),
      static_cast<unsigned int>(guid.Data4[0]),
      static_cast<unsigned int>(guid.Data4[1]),
      static_cast<unsigned int>(guid.Data4[2]),
      static_cast<unsigned int>(guid.Data4[3]),
      static_cast<unsigned int>(guid.Data4[4]),
      static_cast<unsigned int>(guid.Data4[5]),
      static_cast<unsigned int>(guid.Data4[6]),
      static_cast<unsigned int>(guid.Data4[7]));
}

static bool TryMakeUniqueSiblingStagePath(const fs::path &destination,
                                          const wchar_t *suffix,
                                          fs::path &stagingPath,
                                          std::wstring &errorDetails) {
  const size_t maximumComponentLength =
      destination.filename().native().size();
  const std::wstring_view suffixView =
      suffix ? std::wstring_view(suffix) : std::wstring_view{};
  const bool retainLinkExtension = suffixView.ends_with(L".lnk");
  const size_t extensionLength =
      retainLinkExtension ? sibling_stage_name::kLinkExtension.size() : 0;
  if (maximumComponentLength <= extensionLength) {
    errorDetails = L"The destination filename leaves no safe staging-name "
                   L"budget.";
    return false;
  }

  const size_t baseBudget = maximumComponentLength - extensionLength;
  const bool singleCharacterBudget = baseBudget == 1;
  const size_t tokenCharacters =
      singleCharacterBudget
          ? 0
          : (std::min)(sibling_stage_name::kFullGuidHexCharacters,
                       baseBudget - 1);
  GUID seedGuid{};
  if (FAILED(CoCreateGuid(&seedGuid))) {
    errorDetails = L"Windows could not create a unique staging-file name.";
    return false;
  }
  const unsigned attemptLimit =
      singleCharacterBudget
          ? static_cast<unsigned>(
                sibling_stage_name::kSingleCharacterAlphabet.size())
          : tokenCharacters <= 2
          ? (1u << static_cast<unsigned>(4 * tokenCharacters))
          : 64u;
  constexpr wchar_t kHexDigits[] = L"0123456789abcdef";

  for (unsigned attempt = 0; attempt < attemptLimit; ++attempt) {
    std::wstring component;
    if (singleCharacterBudget) {
      const size_t alphabetIndex =
          static_cast<size_t>(seedGuid.Data1) + attempt;
      component = sibling_stage_name::BuildSingleCharacter(
          alphabetIndex, retainLinkExtension, maximumComponentLength);
    } else {
      std::wstring collisionToken;
      if (tokenCharacters <= 2) {
        const unsigned namespaceSize = attemptLimit;
        unsigned value =
            (static_cast<unsigned>(seedGuid.Data1) + attempt) % namespaceSize;
        collisionToken.assign(tokenCharacters, L'0');
        for (size_t index = tokenCharacters; index > 0; --index) {
          collisionToken[index - 1] = kHexDigits[value & 0x0f];
          value >>= 4;
        }
      } else {
        GUID candidateGuid = seedGuid;
        if (attempt != 0 && FAILED(CoCreateGuid(&candidateGuid))) {
          errorDetails =
              L"Windows could not create a unique staging-file name.";
          return false;
        }
        collisionToken = FormatStageCollisionToken(candidateGuid);
        if (collisionToken.size() !=
            sibling_stage_name::kFullGuidHexCharacters) {
          errorDetails =
              L"Windows could not format a unique staging-file name.";
          return false;
        }
      }
      component = sibling_stage_name::Build(
          collisionToken, retainLinkExtension, maximumComponentLength);
    }

    if (component.empty()) {
      errorDetails = L"Windows could not format a safe staging-file name.";
      return false;
    }
    stagingPath = destination.parent_path() / component;
    if (CompareStringOrdinal(stagingPath.c_str(), -1, destination.c_str(), -1,
                             TRUE) == CSTR_EQUAL) {
      continue;
    }
    const DWORD attributes = GetFileAttributesW(stagingPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return true;
      errorDetails = std::format(
          L"The staging path could not be inspected (Windows error {}).", error);
      return false;
    }
  }
  errorDetails = L"Windows could not reserve a unique staging-file name.";
  return false;
}

static void RemoveSafeStagingFile(const fs::path &stagingPath) {
  const DWORD attributes = GetFileAttributesW(stagingPath.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0) {
    DeleteFileW(stagingPath.c_str());
  }
}

static bool CopyExecutableToNewStage(const fs::path &sourcePath,
                                     const fs::path &stagingPath,
                                     std::wstring &errorDetails) {
  HANDLE source = CreateFileW(
      sourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_SEQUENTIAL_SCAN,
      nullptr);
  if (source == INVALID_HANDLE_VALUE) {
    errorDetails = std::format(L"The update source could not be opened (Windows "
                               L"error {}).",
                               GetLastError());
    return false;
  }

  BY_HANDLE_FILE_INFORMATION sourceInfo{};
  if (!GetFileInformationByHandle(source, &sourceInfo) ||
      (sourceInfo.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    const DWORD error = GetLastError();
    CloseHandle(source);
    errorDetails = std::format(
        L"The update source is not a safe regular file (Windows error {}).",
        error);
    return false;
  }

  HANDLE staging =
      CreateFileW(stagingPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH |
                      FILE_FLAG_SEQUENTIAL_SCAN,
                  nullptr);
  if (staging == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    CloseHandle(source);
    errorDetails = std::format(
        L"The update staging file could not be created (Windows error {}).",
        error);
    return false;
  }

  bool copied = true;
  DWORD copyError = ERROR_SUCCESS;
  std::vector<BYTE> buffer(1024 * 1024);
  for (;;) {
    DWORD bytesRead = 0;
    if (!ReadFile(source, buffer.data(), (DWORD)buffer.size(), &bytesRead,
                  nullptr)) {
      copied = false;
      copyError = GetLastError();
      break;
    }
    if (bytesRead == 0)
      break;
    DWORD offset = 0;
    while (offset < bytesRead) {
      DWORD bytesWritten = 0;
      if (!WriteFile(staging, buffer.data() + offset, bytesRead - offset,
                     &bytesWritten, nullptr) || bytesWritten == 0) {
        copied = false;
        copyError = bytesWritten == 0 ? ERROR_WRITE_FAULT : GetLastError();
        break;
      }
      offset += bytesWritten;
    }
    if (!copied)
      break;
  }
  if (copied && !FlushFileBuffers(staging)) {
    copied = false;
    copyError = GetLastError();
  }
  const bool stagingClosed = CloseHandle(staging) != FALSE;
  CloseHandle(source);
  if (!stagingClosed && copied) {
    copied = false;
    copyError = GetLastError();
  }
  if (!copied) {
    errorDetails = std::format(
        L"The update staging copy failed (Windows error {}).", copyError);
    return false;
  }
  return true;
}

static bool FilesMatchExactly(const fs::path &leftPath,
                              const fs::path &rightPath,
                              std::wstring &errorDetails) {
  if (!IsSafeExistingRegularFile(leftPath) ||
      !IsSafeExistingRegularFile(rightPath)) {
    errorDetails =
        L"A file could not be verified because it is missing or unsafe.";
    return false;
  }

  HANDLE left = CreateFileW(leftPath.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                                FILE_FLAG_SEQUENTIAL_SCAN,
                            nullptr);
  HANDLE right = CreateFileW(rightPath.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                                 FILE_FLAG_SEQUENTIAL_SCAN,
                             nullptr);
  if (left == INVALID_HANDLE_VALUE || right == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (left != INVALID_HANDLE_VALUE)
      CloseHandle(left);
    if (right != INVALID_HANDLE_VALUE)
      CloseHandle(right);
    errorDetails =
        std::format(L"The staged update could not be verified (Windows error {}).",
                    error);
    return false;
  }

  LARGE_INTEGER leftSize{};
  LARGE_INTEGER rightSize{};
  bool matched = GetFileSizeEx(left, &leftSize) &&
                 GetFileSizeEx(right, &rightSize) &&
                 leftSize.QuadPart == rightSize.QuadPart;
  std::vector<BYTE> leftBuffer(1024 * 1024);
  std::vector<BYTE> rightBuffer(1024 * 1024);
  while (matched) {
    DWORD leftRead = 0;
    DWORD rightRead = 0;
    if (!ReadFile(left, leftBuffer.data(), (DWORD)leftBuffer.size(), &leftRead,
                  nullptr) ||
        !ReadFile(right, rightBuffer.data(), (DWORD)rightBuffer.size(),
                  &rightRead, nullptr)) {
      matched = false;
      break;
    }
    if (leftRead != rightRead ||
        (leftRead != 0 &&
         std::memcmp(leftBuffer.data(), rightBuffer.data(), leftRead) != 0)) {
      matched = false;
      break;
    }
    if (leftRead == 0)
      break;
  }
  CloseHandle(right);
  CloseHandle(left);
  if (!matched) {
    errorDetails =
        L"The staged update is not byte-for-byte identical to the source.";
  }
  return matched;
}

static bool CopyFileWithRetry(const fs::path &sourcePath,
                              const fs::path &destPath,
                              std::wstring *errorDetails = nullptr) {
  std::wstring lastError;
  if (!ValidateInstallCopyPaths(sourcePath, destPath, lastError)) {
    if (errorDetails)
      *errorDetails = lastError;
    return false;
  }

  fs::path stagingPath;
  if (!TryMakeUniqueSiblingStagePath(destPath, L"stage",
                                     stagingPath, lastError) ||
      !CopyExecutableToNewStage(sourcePath, stagingPath, lastError) ||
      !FilesMatchExactly(sourcePath, stagingPath, lastError)) {
    RemoveSafeStagingFile(stagingPath);
    if (errorDetails)
      *errorDetails = lastError;
    return false;
  }

  for (int attempt = 0; attempt < 20; ++attempt) {
    if (!ValidateInstallCopyPaths(sourcePath, destPath, lastError) ||
        !FilesMatchExactly(sourcePath, stagingPath, lastError)) {
      break;
    }

    if (MoveFileExW(stagingPath.c_str(), destPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      if (ValidateInstalledExecutablePath(destPath, false, lastError) &&
          FilesMatchExactly(sourcePath, destPath, lastError)) {
        return true;
      }
      break;
    }

    const DWORD moveError = GetLastError();
    lastError =
        std::format(L"Atomic replacement failed: Windows error {}.", moveError);
    if (moveError != ERROR_ACCESS_DENIED && moveError != ERROR_SHARING_VIOLATION &&
        moveError != ERROR_LOCK_VIOLATION) {
      break;
    }
    Sleep(250);
  }

  RemoveSafeStagingFile(stagingPath);
  if (errorDetails)
    *errorDetails = lastError;
  return false;
}

static bool CopyCurrentBuildToInstalled(const fs::path &currentExePath,
                                        const fs::path &installedExePath) {
  std::wstring copyError;
  if (CopyFileWithRetry(currentExePath, installedExePath, &copyError))
    return true;

  std::wstring message =
      L"Could not replace the installed copy.\n\nIf ctSpaces is open, close it "
      L"and try again. If it is already closed, Windows may still be releasing "
      L"the file or an antivirus tool may be scanning it.";
  auto runningInstalledProcesses = FindProcessesUsingImage(installedExePath);
  if (!runningInstalledProcesses.empty()) {
    message +=
        L"\n\nThe installed ctSpaces.exe still appears to be running as process "
        L"ID(s): " +
        FormatProcessList(runningInstalledProcesses) +
        L"\nClose those from Task Manager, then try again.";
  }
  if (!copyError.empty())
    message += L"\n\nWindows reported:\n" + copyError;

  MessageBox(NULL, message.c_str(), L"Update ctSpaces", MB_OK | MB_ICONERROR);
  return false;
}

static bool LaunchInstalledVersion(const fs::path &installedExePath,
                                   std::wstring *errorDetails = nullptr) {
  std::wstring validationError;
  if (!ValidateInstalledExecutablePath(installedExePath, false,
                                       validationError)) {
    if (errorDetails)
      *errorDetails = validationError;
    return false;
  }

  const std::wstring parameters =
      std::format(L"--wait-for-pid={}", GetCurrentProcessId());
  SHELLEXECUTEINFOW executeInfo{};
  executeInfo.cbSize = sizeof(executeInfo);
  executeInfo.fMask =
      SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
  executeInfo.lpVerb = L"open";
  executeInfo.lpFile = installedExePath.c_str();
  executeInfo.lpParameters = parameters.c_str();
  executeInfo.lpDirectory = g_sDataDir.c_str();
  executeInfo.nShow = SW_SHOW;
  if (!ShellExecuteExW(&executeInfo) || !executeInfo.hProcess) {
    const DWORD error = GetLastError();
    if (executeInfo.hProcess)
      CloseHandle(executeInfo.hProcess);
    if (errorDetails) {
      *errorDetails = std::format(
          L"Windows could not launch the installed copy (error {}).", error);
    }
    return false;
  }

  const DWORD immediateState = WaitForSingleObject(executeInfo.hProcess, 0);
  if (immediateState != WAIT_TIMEOUT) {
    DWORD exitCode = 0;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    const DWORD waitError = immediateState == WAIT_FAILED ? GetLastError() : 0;
    CloseHandle(executeInfo.hProcess);
    if (errorDetails) {
      *errorDetails =
          immediateState == WAIT_FAILED
              ? std::format(L"The installed process could not be verified "
                            L"(Windows error {}).",
                            waitError)
              : std::format(L"The installed process exited immediately with "
                            L"code {}.",
                            exitCode);
    }
    return false;
  }
  CloseHandle(executeInfo.hProcess);
  return true;
}

enum class ManagedApplicationShortcutState {
  Unmanaged,
  LegacyWithoutAppId,
  Current
};

static ManagedApplicationShortcutState
InspectManagedApplicationShortcut(const fs::path &shortcutPath,
                                  const fs::path &targetPath) {
  if (!IsSafeExistingRegularFile(shortcutPath) ||
      !IsSafeExistingRegularFile(targetPath)) {
    return ManagedApplicationShortcutState::Unmanaged;
  }

  IShellLinkW *link = nullptr;
  HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
  if (FAILED(result) || !link)
    return ManagedApplicationShortcutState::Unmanaged;
  IPersistFile *persist = nullptr;
  result = link->QueryInterface(IID_PPV_ARGS(&persist));
  if (SUCCEEDED(result) && persist) {
    result = persist->Load(shortcutPath.c_str(), STGM_READ);
    persist->Release();
  }

  std::vector<wchar_t> targetText(32768, L'\0');
  std::vector<wchar_t> argumentText(32768, L'\0');
  if (SUCCEEDED(result)) {
    result = link->GetPath(targetText.data(), (int)targetText.size(), nullptr,
                           SLGP_RAWPATH);
  }
  if (SUCCEEDED(result))
    result = link->GetArguments(argumentText.data(),
                                (int)argumentText.size());

  if (FAILED(result) || !targetText[0] || argumentText[0] != L'\0' ||
      !SameExecutablePath(targetText.data(), targetPath)) {
    link->Release();
    return ManagedApplicationShortcutState::Unmanaged;
  }

  ManagedApplicationShortcutState state =
      ManagedApplicationShortcutState::Unmanaged;
  IPropertyStore *propertyStore = nullptr;
  result = link->QueryInterface(IID_PPV_ARGS(&propertyStore));
  if (SUCCEEDED(result) && propertyStore) {
    PROPVARIANT appIdValue{};
    result = propertyStore->GetValue(PKEY_AppUserModel_ID, &appIdValue);
    if (SUCCEEDED(result)) {
      if (appIdValue.vt == VT_EMPTY) {
        state = ManagedApplicationShortcutState::LegacyWithoutAppId;
      } else {
        PWSTR currentAppId = nullptr;
        const HRESULT readResult =
            PropVariantToStringAlloc(appIdValue, &currentAppId);
        if (SUCCEEDED(readResult) && currentAppId &&
            wcscmp(currentAppId, LAUNCHER_APP_USER_MODEL_ID) == 0) {
          state = ManagedApplicationShortcutState::Current;
        }
        if (currentAppId)
          CoTaskMemFree(currentAppId);
      }
    }
    PropVariantClear(&appIdValue);
    propertyStore->Release();
  }
  link->Release();
  return state;
}

static bool IsManagedApplicationShortcut(
    const fs::path &shortcutPath, const fs::path &targetPath,
    bool allowLegacyWithoutAppId) {
  const ManagedApplicationShortcutState state =
      InspectManagedApplicationShortcut(shortcutPath, targetPath);
  return state == ManagedApplicationShortcutState::Current ||
         (allowLegacyWithoutAppId &&
          state == ManagedApplicationShortcutState::LegacyWithoutAppId);
}

bool CreateShortcut(const fs::path &targetPath, const fs::path &shortcutPath,
                    const fs::path &workingDir, const fs::path &iconPath,
                    std::wstring *errorDetails) {
  const auto fail = [errorDetails](const std::wstring &message) {
    if (errorDetails)
      *errorDetails = message;
    return false;
  };

  if (!IsSafeExistingRegularFile(targetPath) ||
      !IsSafeExistingDirectory(workingDir) ||
      !IsSafeExistingRegularFile(iconPath) ||
      !IsSafeExistingDirectory(shortcutPath.parent_path())) {
    return fail(L"The shortcut target, icon, working folder, or destination "
                L"folder is missing or unsafe.");
  }

  DWORD attributes = GetFileAttributesW(shortcutPath.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    if (!IsManagedApplicationShortcut(shortcutPath, targetPath, true)) {
      return fail(L"A different or unsafe shortcut already uses this name. It "
                  L"was left unchanged.");
    }
  } else {
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
      return fail(std::format(
          L"The shortcut destination could not be inspected (Windows error {}).",
          error));
    }
  }

  fs::path stagingPath;
  std::wstring stagingError;
  if (!TryMakeUniqueSiblingStagePath(shortcutPath, L"stage.lnk", stagingPath,
                                     stagingError)) {
    return fail(stagingError);
  }

  IShellLinkW *link = nullptr;
  HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
  if (SUCCEEDED(result) && link)
    result = link->SetPath(targetPath.c_str());
  if (SUCCEEDED(result))
    result = link->SetArguments(L"");
  if (SUCCEEDED(result))
    result = link->SetWorkingDirectory(workingDir.c_str());
  if (SUCCEEDED(result))
    result = link->SetIconLocation(iconPath.c_str(), 0);
  if (SUCCEEDED(result))
    result = link->SetDescription(L"Open ctSpaces");

  IPropertyStore *propertyStore = nullptr;
  if (SUCCEEDED(result))
    result = link->QueryInterface(IID_PPV_ARGS(&propertyStore));
  if (SUCCEEDED(result) && !propertyStore)
    result = E_NOINTERFACE;
  PROPVARIANT appIdValue{};
  if (SUCCEEDED(result))
    result = InitPropVariantFromString(LAUNCHER_APP_USER_MODEL_ID, &appIdValue);
  if (SUCCEEDED(result))
    result = propertyStore->SetValue(PKEY_AppUserModel_ID, appIdValue);
  if (SUCCEEDED(result))
    result = propertyStore->Commit();
  PropVariantClear(&appIdValue);
  if (propertyStore)
    propertyStore->Release();

  IPersistFile *persist = nullptr;
  if (SUCCEEDED(result))
    result = link->QueryInterface(IID_PPV_ARGS(&persist));
  if (SUCCEEDED(result) && persist)
    result = persist->Save(stagingPath.c_str(), TRUE);
  if (persist)
    persist->Release();
  if (link)
    link->Release();

  if (FAILED(result) ||
      !IsManagedApplicationShortcut(stagingPath, targetPath, false)) {
    RemoveSafeStagingFile(stagingPath);
    return fail(std::format(
        L"The shortcut could not be staged and verified (HRESULT 0x{:08X}).",
        static_cast<unsigned long>(result)));
  }

  if (!IsSafeExistingDirectory(shortcutPath.parent_path())) {
    RemoveSafeStagingFile(stagingPath);
    return fail(L"The shortcut destination folder changed before commit.");
  }
  const ShortcutOwnershipCheck isOwned =
      [&targetPath](const fs::path &candidate) {
        return IsManagedApplicationShortcut(candidate, targetPath, true);
      };
  const ShortcutOwnershipCheck isCommittedOwned =
      [&targetPath](const fs::path &candidate) {
        return IsManagedApplicationShortcut(candidate, targetPath, false);
      };
  std::wstring commitError;
  if (!CommitStagedOwnedShortcut(stagingPath, shortcutPath, isOwned,
                                 commitError, &isCommittedOwned)) {
    return fail(commitError.empty()
                    ? L"The shortcut could not be committed safely."
                    : commitError);
  }
  return true;
}

enum class AutorunValueState { Missing, Managed, Unrelated, Indeterminate };

static AutorunValueState QueryAutorunValue(HKEY key,
                                           const std::wstring &expectedValue,
                                           std::wstring &errorDetails) {
  DWORD type = 0;
  DWORD byteCount = 0;
  LSTATUS status = RegQueryValueExW(key, L"ctSpaces", nullptr, &type, nullptr,
                                    &byteCount);
  if (status == ERROR_FILE_NOT_FOUND)
    return AutorunValueState::Missing;
  if (status != ERROR_SUCCESS) {
    errorDetails = std::format(
        L"The ctSpaces startup value could not be inspected (Windows error {}).",
        status);
    return AutorunValueState::Indeterminate;
  }
  if (type != REG_SZ || byteCount < sizeof(wchar_t) ||
      byteCount > 64 * 1024 || byteCount % sizeof(wchar_t) != 0) {
    return AutorunValueState::Unrelated;
  }

  std::vector<wchar_t> value(byteCount / sizeof(wchar_t) + 1, L'\0');
  DWORD verifiedType = 0;
  DWORD verifiedBytes = byteCount;
  status = RegQueryValueExW(key, L"ctSpaces", nullptr, &verifiedType,
                            reinterpret_cast<BYTE *>(value.data()),
                            &verifiedBytes);
  if (status != ERROR_SUCCESS) {
    errorDetails = std::format(
        L"The ctSpaces startup value changed while it was read (Windows error "
        L"{}).",
        status);
    return AutorunValueState::Indeterminate;
  }
  if (verifiedType != REG_SZ || verifiedBytes < sizeof(wchar_t) ||
      verifiedBytes % sizeof(wchar_t) != 0) {
    return AutorunValueState::Unrelated;
  }
  const size_t characterCount = verifiedBytes / sizeof(wchar_t);
  if (value[characterCount - 1] != L'\0')
    return AutorunValueState::Unrelated;
  const std::wstring actualValue(value.data(), characterCount - 1);
  if (actualValue.find(L'\0') != std::wstring::npos)
    return AutorunValueState::Unrelated;
  return actualValue == expectedValue ? AutorunValueState::Managed
                                      : AutorunValueState::Unrelated;
}

static AutorunValueState InspectAutorunValue(
    const std::wstring &expectedValue, std::wstring &errorDetails) {
  static constexpr wchar_t kRunKeyPath[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  HKEY key = nullptr;
  const LSTATUS openStatus =
      RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key);
  if (openStatus == ERROR_FILE_NOT_FOUND)
    return AutorunValueState::Missing;
  if (openStatus != ERROR_SUCCESS) {
    errorDetails = std::format(
        L"The Windows startup settings could not be opened (Windows error {}).",
        openStatus);
    return AutorunValueState::Indeterminate;
  }
  const AutorunValueState state =
      QueryAutorunValue(key, expectedValue, errorDetails);
  const LSTATUS closeStatus = RegCloseKey(key);
  if (closeStatus != ERROR_SUCCESS) {
    errorDetails = std::format(
        L"The Windows startup settings could not be closed cleanly (Windows "
        L"error {}).",
        closeStatus);
    return AutorunValueState::Indeterminate;
  }
  return state;
}

static bool ConfigureAutorun(bool enabled, const fs::path &installedExePath,
                             std::wstring &errorDetails) {
  if (!ValidateInstalledExecutablePath(installedExePath, false, errorDetails))
    return false;
  const std::wstring expectedValue =
      L"\"" + installedExePath.wstring() + L"\"";
  AutorunValueState state = InspectAutorunValue(expectedValue, errorDetails);
  if (state == AutorunValueState::Indeterminate)
    return false;
  if (enabled && state == AutorunValueState::Unrelated) {
    errorDetails =
        L"A different Windows startup value already uses the name ctSpaces. It "
        L"was left unchanged.";
    return false;
  }
  if ((enabled && state == AutorunValueState::Managed) ||
      (!enabled &&
       (state == AutorunValueState::Missing ||
        state == AutorunValueState::Unrelated))) {
    return true;
  }

  static constexpr wchar_t kRunKeyPath[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  HKEY key = nullptr;
  DWORD disposition = 0;
  const LSTATUS openStatus = RegCreateKeyExW(
      HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
      KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition);
  if (openStatus != ERROR_SUCCESS || !key) {
    errorDetails = std::format(
        L"The Windows startup settings could not be updated (Windows error {}).",
        openStatus);
    return false;
  }

  const auto finish = [&errorDetails, key](bool succeeded) {
    const LSTATUS closeStatus = RegCloseKey(key);
    if (closeStatus != ERROR_SUCCESS) {
      errorDetails = std::format(
          L"The Windows startup settings could not be closed cleanly (Windows "
          L"error {}).",
          closeStatus);
      return false;
    }
    return succeeded;
  };

  state = QueryAutorunValue(key, expectedValue, errorDetails);
  if (state == AutorunValueState::Indeterminate)
    return finish(false);
  if (enabled) {
    if (state == AutorunValueState::Unrelated) {
      errorDetails =
          L"A different Windows startup value appeared before commit. It was "
          L"left unchanged.";
      return finish(false);
    }
    if (state == AutorunValueState::Missing) {
      const DWORD bytes =
          (DWORD)((expectedValue.size() + 1) * sizeof(wchar_t));
      const LSTATUS setStatus = RegSetValueExW(
          key, L"ctSpaces", 0, REG_SZ,
          reinterpret_cast<const BYTE *>(expectedValue.c_str()), bytes);
      if (setStatus != ERROR_SUCCESS) {
        errorDetails = std::format(
            L"The ctSpaces startup value could not be saved (Windows error {}).",
            setStatus);
        return finish(false);
      }
    }
    state = QueryAutorunValue(key, expectedValue, errorDetails);
    if (state != AutorunValueState::Managed) {
      if (state != AutorunValueState::Indeterminate)
        errorDetails = L"The saved ctSpaces startup value failed verification.";
      return finish(false);
    }
    return finish(true);
  }

  if (state == AutorunValueState::Managed) {
    const LSTATUS deleteStatus = RegDeleteValueW(key, L"ctSpaces");
    if (deleteStatus != ERROR_SUCCESS && deleteStatus != ERROR_FILE_NOT_FOUND) {
      errorDetails = std::format(
          L"The managed ctSpaces startup value could not be removed (Windows "
          L"error {}).",
          deleteStatus);
      return finish(false);
    }
  }
  state = QueryAutorunValue(key, expectedValue, errorDetails);
  if (state != AutorunValueState::Missing) {
    if (state != AutorunValueState::Indeterminate)
      errorDetails =
          L"The managed ctSpaces startup value could not be verified as removed.";
    return finish(false);
  }
  return finish(true);
}

bool doInstall() {
  if (MessageBox(
          NULL,
          L"Set up ctSpaces for this Windows account?\n\nThis copies the "
          L"current build to your local app data folder so future launches use "
          L"the installed copy.",
          L"Set Up ctSpaces", MB_YESNO | MB_ICONQUESTION) == IDYES) {
    fs::path installedExePath = g_sDataDir / L"ctSpaces.exe";
    const fs::path currentExePath = GetCurrentExecutablePath();
    if (currentExePath.empty()) {
      MessageBoxW(NULL,
                  L"Setup could not resolve the full path of this ctSpaces "
                  L"executable. No files were copied.",
                  L"Set Up ctSpaces", MB_OK | MB_ICONERROR);
      return false;
    }
    try {
      std::wstring copyError;
      if (!CopyFileWithRetry(currentExePath, installedExePath, &copyError)) {
        std::wstring message =
            L"Setup could not copy ctSpaces to your local app data folder.";
        if (!copyError.empty())
          message += L"\n\nWindows reported:\n" + copyError;
        MessageBox(NULL, message.c_str(), L"Set Up ctSpaces",
                   MB_OK | MB_ICONERROR);
        return true;
      }
      std::vector<std::wstring> setupWarnings;
      const auto createRequestedShortcut =
          [&](REFKNOWNFOLDERID folderId, const wchar_t *locationName) {
            const std::wstring prompt =
                std::format(L"Create a {} shortcut?", locationName);
            if (MessageBoxW(NULL, prompt.c_str(), L"Set Up ctSpaces",
                            MB_YESNO | MB_ICONQUESTION) != IDYES) {
              return;
            }

            PWSTR folderText = nullptr;
            const HRESULT folderResult =
                SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr,
                                     &folderText);
            if (FAILED(folderResult) || !folderText) {
              setupWarnings.push_back(
                  std::format(L"{} shortcut: Windows could not locate that "
                              L"folder (HRESULT 0x{:08X}).",
                              locationName,
                              static_cast<unsigned long>(folderResult)));
              if (folderText)
                CoTaskMemFree(folderText);
              return;
            }

            const fs::path shortcutPath =
                fs::path(folderText) / L"ctSpaces.lnk";
            CoTaskMemFree(folderText);
            std::wstring shortcutError;
            if (!CreateShortcut(installedExePath, shortcutPath, g_sDataDir,
                                installedExePath, &shortcutError)) {
              setupWarnings.push_back(std::format(
                  L"{} shortcut: {}", locationName, shortcutError));
            }
          };
      createRequestedShortcut(FOLDERID_Programs, L"Start Menu");
      createRequestedShortcut(FOLDERID_Desktop, L"Desktop");

      const bool autorunEnabled =
          MessageBoxW(NULL,
                      L"Start ctSpaces automatically when you sign in?",
                      L"Set Up ctSpaces",
                      MB_YESNO | MB_ICONQUESTION) == IDYES;
      std::wstring autorunError;
      if (!ConfigureAutorun(autorunEnabled, installedExePath, autorunError)) {
        setupWarnings.push_back(L"Windows startup: " + autorunError);
      }

      if (!setupWarnings.empty()) {
        std::wstring warningMessage =
            L"ctSpaces was installed, but one or more optional setup choices "
            L"could not be completed:";
        for (const auto &warning : setupWarnings)
          warningMessage += L"\n\n\u2022 " + warning;
        MessageBoxW(NULL, warningMessage.c_str(), L"Set Up ctSpaces",
                    MB_OK | MB_ICONWARNING);
      }

      std::wstring launchError;
      if (!LaunchInstalledVersion(installedExePath, &launchError)) {
        MessageBoxW(
            NULL,
            (L"ctSpaces was installed, but Windows could not start the "
             L"installed copy. This copy will continue running.\n\n" +
             launchError)
                .c_str(),
            L"Set Up ctSpaces", MB_OK | MB_ICONWARNING);
        return true;
      }
      return false;
    } catch (const std::exception &error) {
      MessageBoxW(NULL,
                  (L"Setup could not be completed. This copy will continue "
                   L"running.\n\n" +
                   AnsiToWide(error.what()))
                      .c_str(),
                  L"Set Up ctSpaces", MB_OK | MB_ICONERROR);
      return true;
    } catch (...) {
      MessageBoxW(NULL,
                  L"Setup could not be completed. This copy will continue "
                  L"running.",
                  L"Set Up ctSpaces", MB_OK | MB_ICONERROR);
      return true;
    }
  } else {
    if (MessageBox(NULL,
                   L"Run this copy once without installing it?",
                   L"Run Once", MB_YESNO | MB_ICONQUESTION) == IDYES) {
      return true;
    } else {
      return false;
    }
  }
}

bool chkUpdate() {
  const fs::path currentExePath = GetCurrentExecutablePath();
  if (currentExePath.empty()) {
    MessageBoxW(NULL,
                L"ctSpaces could not resolve the full path of this "
                L"executable, so update comparison was stopped.",
                L"Update ctSpaces", MB_OK | MB_ICONERROR);
    return false;
  }
  const fs::path installedExePath = g_sDataDir / L"ctSpaces.exe";
  std::error_code equivalentError;
  const bool isInstalledCopy =
      fs::equivalent(currentExePath, installedExePath, equivalentError);
  if (equivalentError) {
    MessageBoxW(NULL,
                L"ctSpaces could not safely compare this executable with "
                L"the installed copy. No update was attempted.",
                L"Update ctSpaces", MB_OK | MB_ICONERROR);
    return false;
  }
  if (isInstalledCopy) {
    return true;
  }

  std::wstring currentVersion = GetExeVersion(currentExePath);
  std::wstring installedVersion = GetExeVersion(installedExePath);
  const int versionCompare = CompareExeVersions(currentVersion, installedVersion);
  const bool correctedVersionUpdate =
      IsCorrectedVersionNumberingUpdate(currentVersion, installedVersion);

  if (versionCompare > 0 || correctedVersionUpdate) {
    std::wstring prompt = correctedVersionUpdate
                              ? std::format(
                                    L"This copy uses the corrected 5.0.0.x "
                                    L"version number and should replace the "
                                    L"earlier 5.0.1.0 build.\n\nInstalled: "
                                    L"{}\nThis copy: {}\n\nUpdate the "
                                    L"installed copy and relaunch from there?",
                                    installedVersion, currentVersion)
                              : std::format(L"This copy is newer than the "
                                            L"installed copy.\n\nInstalled: "
                                            L"{}\nThis copy: {}\n\nUpdate "
                                            L"the installed copy and relaunch "
                                            L"from there?",
                                            installedVersion, currentVersion);
    if (MessageBox(NULL, prompt.c_str(), L"Update ctSpaces",
                   MB_YESNO | MB_ICONQUESTION) == IDYES) {
      if (!CopyCurrentBuildToInstalled(currentExePath, installedExePath)) {
        return true;
      }
      std::wstring launchError;
      if (LaunchInstalledVersion(installedExePath, &launchError))
        return false;
      MessageBoxW(
          NULL,
          (L"The installed copy was updated, but Windows could not start it. "
           L"This copy will continue running.\n\n" +
           launchError)
              .c_str(),
          L"Update ctSpaces", MB_OK | MB_ICONWARNING);
      return true;
    } else {
      if (MessageBox(NULL,
                     L"Run this newer copy once without updating the installed "
                     L"copy?",
                     L"Run Once", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        return true;
      } else {
        return false;
      }
    }
  } else if (versionCompare == 0) {
    std::wstring prompt = std::format(
        L"This copy is not the installed executable, but both report version "
        L"{}.\n\nReplace the installed copy with this build and relaunch from "
        L"there?",
        currentVersion.empty() ? L"unknown" : currentVersion);
    if (MessageBox(NULL, prompt.c_str(), L"Replace Installed Copy",
                   MB_YESNO | MB_ICONQUESTION) == IDYES) {
      if (!CopyCurrentBuildToInstalled(currentExePath, installedExePath)) {
        return true;
      }
      std::wstring launchError;
      if (LaunchInstalledVersion(installedExePath, &launchError))
        return false;
      MessageBoxW(
          NULL,
          (L"The installed copy was replaced, but Windows could not start it. "
           L"This copy will continue running.\n\n" +
           launchError)
              .c_str(),
          L"Replace Installed Copy", MB_OK | MB_ICONWARNING);
      return true;
    }

    if (MessageBox(NULL,
                   L"Run this copy once without replacing the installed copy?",
                   L"Run Once", MB_YESNO | MB_ICONQUESTION) == IDYES) {
      return true;
    }
    return false;
  } else {
    std::wstring prompt =
        std::format(L"The installed copy is newer than this copy.\n\n"
                    L"Installed: {}\nThis copy: {}\n\nRun this older copy "
                    L"once anyway?",
                    installedVersion, currentVersion);
    if (MessageBox(NULL, prompt.c_str(), L"Run Once",
                   MB_YESNO | MB_ICONQUESTION) == IDYES) {
      return true;
    } else {
      return false;
    }
  }
}

// Ref:
// https://learn.microsoft.com/en-us/windows/win32/controls/create-a-tooltip-for-a-control
HWND CreateToolTip(HWND hwndTool, HWND hDlg, PTSTR pszText) {
  if (!hwndTool || !hDlg || !pszText)
    return FALSE;
  HWND hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, hDlg, NULL, g_hInst, NULL);
  if (!hwndTool || !hwndTip)
    return (HWND)NULL;
  TOOLINFO toolInfo = {0};
  toolInfo.cbSize = sizeof(toolInfo);
  toolInfo.hwnd = hDlg;
  toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  toolInfo.uId = (UINT_PTR)hwndTool;
  toolInfo.lpszText = pszText;
  SendMessageW(hwndTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
  SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
  SetWindowTheme(hwndTip, L"", L"");
  SendMessageW(hwndTip, TTM_SETTIPBKCOLOR, g_themeColors.crTip, 0);
  SendMessageW(hwndTip, TTM_SETTIPTEXTCOLOR, g_themeColors.crTipText, 0);
  return hwndTip;
}

static void UpdateGuideIndicators() {
  const bool hasNew =
      guided_walkthrough::HasUnreadAnnouncement(g_guideState);
  g_sConfigTooltip = hasNew
                         ? L"Options / Profile actions (New guide available)"
                         : L"Options / Profile actions";
  if (g_hBtnConfigTip && g_hBtnConfig) {
    TOOLINFOW toolInfo{sizeof(toolInfo)};
    toolInfo.hwnd = g_hGui;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = reinterpret_cast<UINT_PTR>(g_hBtnConfig);
    toolInfo.lpszText = g_sConfigTooltip.data();
    SendMessageW(g_hBtnConfigTip, TTM_UPDATETIPTEXTW, 0,
                 reinterpret_cast<LPARAM>(&toolInfo));
  }
  if (g_hConfigMenu && g_hGui)
    RebuildConfigMenuForTheme(g_hGui);
  if (g_hBtnConfig)
    RedrawWindow(g_hBtnConfig, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
}

static bool PostOwnedStringMessage(UINT message, WPARAM wParam,
                                   const std::wstring &value,
                                   bool retryWhileWindowExists) {
  std::unique_ptr<std::wstring> payload;
  try {
    payload = std::make_unique<std::wstring>(value);
  } catch (...) {
    return false;
  }

  for (;;) {
    const HWND notifyWindow = g_hGui;
    if (!notifyWindow || !IsWindow(notifyWindow))
      return false;
    if (PostMessageW(notifyWindow, message, wParam,
                     reinterpret_cast<LPARAM>(payload.get()))) {
      payload.release();
      return true;
    }
    if (!retryWhileWindowExists || g_isShuttingDown.load())
      return false;
    Sleep(10);
  }
}

static bool PostTaskComplete(const std::wstring &name) {
  return PostOwnedStringMessage(WM_APP_TASK_COMPLETE, 0, name);
}

static bool DeliverTaskComplete(const std::wstring &name) {
  if (PostTaskComplete(name))
    return true;

  // The owned post can fail under message-queue pressure or allocation
  // pressure. A synchronous message carries no borrowed payload and asks the
  // UI handler to skip joining the worker that is currently sending it.
  const HWND notifyWindow = g_hGui;
  if (!notifyWindow || !IsWindow(notifyWindow))
    return false;
  DWORD_PTR messageResult = 0;
  if (SendMessageTimeoutW(notifyWindow, WM_APP_TASK_COMPLETE, 1, 0,
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000,
                          &messageResult)) {
    return true;
  }

  // If the UI was temporarily unable to accept either mechanism, keep trying
  // the ownership-safe post while the window exists. Returning while that
  // window remains alive would leave its controls disabled indefinitely.
  for (;;) {
    if (!g_hGui || !IsWindow(g_hGui) || g_isShuttingDown.load())
      return false;
    if (PostTaskComplete(name))
      return true;
    Sleep(10);
  }
}

static void StopLaunchWorker() {
  if (g_launchThread.joinable())
    g_launchThread.join();
  g_isLaunchInFlight = false;
}

static void LaunchProfileAsync(const std::wstring &name, bool isTemp,
                               bool isDefault, BrowserKind browser,
                               const std::wstring &startupUrl) {
  SetUiState(false);
  StopLaunchWorker();
  g_isLaunchInFlight = true;

  try {
    g_launchThread = std::jthread(
        [name, isTemp, isDefault, browser, startupUrl]() mutable {
          HANDLE processHandle = nullptr;
          fs::path profilePath;
          fs::path executablePath;
          DWORD pid = 0;
          bool activeRegistered = false;
          bool sessionAnnounced = false;
          bool launchFailed = false;
          bool freshTransientProfile = false;
          std::wstring failureMessage;
          std::wstring launchError;
          const ProfileType profileType =
              isTemp ? ProfileType::Temporary
                     : (isDefault ? ProfileType::Default
                                  : ProfileType::Standard);

          try {
            pid = LaunchProfile(name, isTemp, isDefault, browser, startupUrl,
                                &processHandle, &profilePath, &executablePath,
                                &launchError, &freshTransientProfile);
            if (pid > 0) {
              {
                std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
                g_activeProfiles[ClientBrowserKey{name, browser}] =
                    ActiveProfileInfo{pid, executablePath, profilePath,
                                      profileType};
                activeRegistered = true;
              }

              sessionAnnounced = PostOwnedStringMessage(
                  WM_APP_SESSION_STARTED, (WPARAM)pid, name);
              if (!sessionAnnounced) {
                launchFailed = true;
                failureMessage =
                    L"ctSpaces could not register the new browser session. "
                    L"The browser is being closed to keep the profile safe.";
              } else if (StartReaperThread(pid, name, browser, profileType,
                                           processHandle)) {
                processHandle = nullptr;
                EnsureWatcherIsRunning();
              } else {
                launchFailed = true;
                failureMessage =
                    L"ctSpaces could not start the browser lifecycle monitor. "
                    L"The browser is being closed to keep the profile safe.";
              }
            } else {
              launchFailed = true;
              failureMessage = launchError;
            }
          } catch (const std::exception &error) {
            launchFailed = true;
            failureMessage =
                L"The profile launch failed unexpectedly and was stopped "
                L"safely.\n\nDetails: " +
                AnsiToWide(error.what());
          } catch (...) {
            launchFailed = true;
            failureMessage =
                L"The profile launch failed unexpectedly and was stopped "
                L"safely.";
          }

          if (launchFailed) {
            bool processStopped = true;
            if (processHandle) {
              processStopped = TryTerminateProcessHandle(processHandle);
              CloseHandle(processHandle);
              processHandle = nullptr;
            }

            bool profileExitHandedOff = false;
            if (activeRegistered && processStopped) {
              profileExitHandedOff = PostProfileExitMessage(
                  pid, name, browser, profileType, true, sessionAnnounced);
              if (!profileExitHandedOff) {
                if (failureMessage.empty())
                  failureMessage = L"The browser launch could not be completed.";
                failureMessage +=
                    L"\n\nctSpaces could not hand the stopped browser's "
                    L"lifecycle record to the main window. The profile "
                    L"remains marked open to protect its data. Restart "
                    L"ctSpaces before reopening it.";
              }
            }

            std::wstring cleanupDetails;
            bool transientProfileRemoved = true;
            if (processStopped && !activeRegistered &&
                freshTransientProfile &&
                profileType != ProfileType::Standard) {
              const fs::path transientPath =
                  g_sDataDir / (profileType == ProfileType::Default ? L"Default"
                                                                    : L"Temp");
              transientProfileRemoved = RemoveTransientProfileIfIdleAndVerify(
                  transientPath, cleanupDetails);
            }
            if (!processStopped) {
              failureMessage +=
                  L"\n\nThe browser could not be stopped. Close it manually "
                  L"before using this client again.";
            } else if (!transientProfileRemoved) {
              if (failureMessage.empty())
                failureMessage = L"The browser profile could not be opened.";
              failureMessage +=
                  L"\n\nThe disposable profile also could not be removed.";
              if (!cleanupDetails.empty())
                failureMessage += L" " + cleanupDetails;
            }
            if (!failureMessage.empty()) {
              PostOwnedStringMessage(WM_APP_LAUNCH_FAILED, 0, failureMessage,
                                     true);
            }
          } else if (processHandle) {
            CloseHandle(processHandle);
          }

          // This is the worker's final shared-state transition. From here on it
          // only posts owned payloads, so a pending close may safely proceed.
          g_isLaunchInFlight = false;
          DeliverTaskComplete(name);
    });
  } catch (...) {
    g_isLaunchInFlight = false;
    SetUiState(true);
    MessageBoxW(g_hGui, L"The profile launch worker could not be started.",
                L"Cannot Open Profile", MB_OK | MB_ICONERROR);
  }
}

static bool PreflightValidatedClientTreeDelete(const fs::path &root,
                                               std::wstring &errorDetails) {
  errorDetails.clear();
  const auto canOpenForDelete = [&errorDetails](const fs::path &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      errorDetails = L"A client item changed before deletion: " +
                     path.filename().wstring();
      return false;
    }
    const bool isDirectory =
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!isDirectory && (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
      errorDetails = L"A client file is read-only: " +
                     path.filename().wstring();
      return false;
    }
    HANDLE handle = CreateFileW(
        path.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT |
            (isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : 0),
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      errorDetails = std::format(
          L"A client item is locked or cannot be prepared for deletion: {} "
          L"(Windows error {}).",
          path.filename().wstring(), GetLastError());
      return false;
    }
    const bool closed = CloseHandle(handle) != FALSE;
    if (!closed) {
      errorDetails = std::format(
          L"A client-item safety handle could not be closed (Windows error "
          L"{}).",
          GetLastError());
    }
    return closed;
  };

  if (!canOpenForDelete(root))
    return false;
  std::error_code iteratorError;
  fs::recursive_directory_iterator entry(
      root, fs::directory_options::none, iteratorError);
  const fs::recursive_directory_iterator end;
  if (iteratorError) {
    errorDetails = L"The client tree could not be opened for deletion.";
    return false;
  }
  while (entry != end) {
    if (!canOpenForDelete(entry->path()))
      return false;
    entry.increment(iteratorError);
    if (iteratorError) {
      errorDetails =
          L"The client tree changed while deletion readiness was checked.";
      return false;
    }
  }
  return true;
}

static bool SameDeleteAuthorityPath(const fs::path &left,
                                    const fs::path &right) {
  return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

static bool CollectClientDeleteAuthority(
    const fs::path &root, std::vector<fs::path> &authority,
    std::wstring &errorDetails) {
  authority.clear();
  errorDetails.clear();
  const auto add = [&authority](const fs::path &relative) {
    if (std::ranges::none_of(authority, [&](const fs::path &existing) {
          return SameDeleteAuthorityPath(existing, relative);
        })) {
      authority.push_back(relative);
    }
  };

  try {
    if (IsV2ClientContainer(root)) {
      add(kClientSchemaMarkerName);
    } else {
      add(L"Default");
      const fs::path binding = root / kLegacyBrowserBindingMarkerName;
      if (GetFileAttributesW(binding.c_str()) != INVALID_FILE_ATTRIBUTES)
        add(kLegacyBrowserBindingMarkerName);
    }

    const fs::path browsers = root / L"Browsers";
    const DWORD browserAttributes = GetFileAttributesW(browsers.c_str());
    if (browserAttributes != INVALID_FILE_ATTRIBUTES) {
      add(L"Browsers");
      for (const auto &entry : fs::directory_iterator(browsers)) {
        const auto browser =
            ParseBrowserKind(entry.path().filename().wstring());
        if (!browser) {
          errorDetails =
              L"The browser-slot layout changed before deletion.";
          return false;
        }
        const fs::path slot =
            fs::path(L"Browsers") / entry.path().filename();
        const fs::path profile = slot / L"Profile";
        add(slot);
        add(profile);
        add(slot / kBrowserSchemaMarkerName);
        if (IsChromiumBrowser(*browser))
          add(profile / L"ctSpaces");
      }
    } else {
      const DWORD error = GetLastError();
      if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        errorDetails = std::format(
            L"The browser-slot folder could not be inspected (Windows error "
            L"{}).",
            error);
        return false;
      }
    }
    const fs::path activity = root / client_activity::kFileName;
    const DWORD activityAttributes = GetFileAttributesW(activity.c_str());
    if (activityAttributes != INVALID_FILE_ATTRIBUTES) {
      if ((activityAttributes &
           (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        errorDetails = L"The client activity record is not a safe file.";
        return false;
      }
      add(client_activity::kFileName);
    } else {
      const DWORD error = GetLastError();
      if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        errorDetails = std::format(
            L"The client activity record could not be inspected (Windows "
            L"error {}).",
            error);
        return false;
      }
    }
    return true;
  } catch (const std::exception &error) {
    errorDetails = AnsiToWide(error.what());
    return false;
  }
}

static bool ValidateClientDeleteAuthorityEntry(const fs::path &root,
                                               const fs::path &relative,
                                               std::wstring &errorDetails) {
  const fs::path path = root / relative;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    errorDetails = L"Deletion metadata is missing, unreadable, or a reparse "
                   L"point: " +
                   relative.filename().wstring();
    return false;
  }
  const std::wstring filename = relative.filename().wstring();
  const bool expectedFile =
      _wcsicmp(filename.c_str(), kClientSchemaMarkerName) == 0 ||
      _wcsicmp(filename.c_str(), kBrowserSchemaMarkerName) == 0 ||
      _wcsicmp(filename.c_str(), kLegacyBrowserBindingMarkerName) == 0 ||
      _wcsicmp(filename.c_str(), client_activity::kFileName) == 0 ||
      _wcsicmp(filename.c_str(), L"ctSpaces") == 0;
  if (expectedFile == ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) {
    errorDetails = L"Deletion metadata changed type: " + filename;
    return false;
  }
  if (!expectedFile)
    return true;
  if (_wcsicmp(filename.c_str(), kClientSchemaMarkerName) == 0) {
    if (MarkerHasExactContents(path, kClientSchemaMarkerText))
      return true;
    errorDetails = L"The client ownership marker changed.";
    return false;
  }
  if (_wcsicmp(filename.c_str(), kBrowserSchemaMarkerName) == 0) {
    const auto browser =
        ParseBrowserKind(relative.parent_path().filename().wstring());
    if (browser && MarkerHasExactContents(path, GetBrowserMarkerText(*browser)))
      return true;
    errorDetails = L"A browser ownership marker changed.";
    return false;
  }
  if (_wcsicmp(filename.c_str(), L"ctSpaces") == 0) {
    if (MarkerHasExactContents(path, "ctSpaces-profile=2\r\n"))
      return true;
    errorDetails = L"A browser profile marker changed.";
    return false;
  }
  if (_wcsicmp(filename.c_str(), kLegacyBrowserBindingMarkerName) == 0) {
    std::optional<BrowserKind> binding;
    if (TryGetLegacyBrowserBinding(root, binding) && binding)
      return true;
    errorDetails = L"The legacy browser binding marker changed.";
    return false;
  }
  // The activity record is retry authority even when an older/corrupt record
  // cannot be decoded. It must remain a bounded safe regular file.
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
      data.nFileSizeHigh == 0 && data.nFileSizeLow < 96)
    return true;
  errorDetails = L"The client activity record changed unexpectedly.";
  return false;
}

static bool IsClientDeleteAuthority(
    const fs::path &root, const fs::path &path,
    const std::vector<fs::path> &authority) {
  const fs::path relative = path.lexically_relative(root);
  return !relative.empty() &&
         std::ranges::any_of(authority, [&](const fs::path &candidate) {
           return SameDeleteAuthorityPath(relative, candidate);
         });
}

static bool ClientTreeContainsOnlyDeleteAuthority(
    const fs::path &root, const std::vector<fs::path> &authority,
    std::wstring &errorDetails, bool requireEveryAuthority = true) {
  try {
    if (requireEveryAuthority) {
      for (const auto &relative : authority) {
        if (!ValidateClientDeleteAuthorityEntry(root, relative,
                                                errorDetails)) {
          return false;
        }
      }
    }
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
      const DWORD attributes = GetFileAttributesW(entry.path().c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES ||
          (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        errorDetails =
            L"A deletion-staging entry is unreadable or a reparse point: " +
            entry.path().filename().wstring();
        return false;
      }
      const fs::path relative = entry.path().lexically_relative(root);
      if (!IsClientDeleteAuthority(root, entry.path(), authority)) {
        errorDetails = L"Unexpected client payload remained or appeared: " +
                       entry.path().filename().wstring();
        return false;
      }
      if (!ValidateClientDeleteAuthorityEntry(root, relative, errorDetails))
        return false;
    }
    return true;
  } catch (const std::exception &error) {
    errorDetails = AnsiToWide(error.what());
    return false;
  }
}

static bool RemoveClientPayloadPreservingDeleteAuthority(
    const fs::path &root, const std::vector<fs::path> &authority,
    std::wstring &errorDetails) {
  std::vector<fs::path> payload;
  try {
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
      if (!IsClientDeleteAuthority(root, entry.path(), authority))
        payload.push_back(entry.path());
    }
  } catch (const std::exception &error) {
    errorDetails = AnsiToWide(error.what());
    return false;
  }
  std::sort(payload.begin(), payload.end(), [](const auto &left,
                                                const auto &right) {
    return left.native().size() > right.native().size();
  });
  for (const auto &path : payload) {
    std::error_code removeError;
    const bool removed = fs::remove(path, removeError);
    if (removeError) {
      errorDetails = L"The client payload could not be removed: " +
                     path.filename().wstring() + L". " +
                     AnsiToWide(removeError.message());
      return false;
    }
    if (!removed) {
      const DWORD attributes = GetFileAttributesW(path.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES) {
        errorDetails = L"A client payload item remained after deletion: " +
                       path.filename().wstring();
        return false;
      }
      const DWORD absenceError = GetLastError();
      if (absenceError != ERROR_FILE_NOT_FOUND &&
          absenceError != ERROR_PATH_NOT_FOUND) {
        errorDetails = std::format(
            L"A client payload item could not be verified absent (Windows "
            L"error {}).",
            absenceError);
        return false;
      }
    }
  }
  return ClientTreeContainsOnlyDeleteAuthority(root, authority, errorDetails);
}

static bool DeleteEntireClient(
    const std::wstring &clientName, bool preconfirmed,
    const std::optional<client_activity::Record> &expectedActivity,
    std::wstring &outcome) {
  const auto report = [&](HWND owner, const wchar_t *text,
                           const wchar_t *title, UINT flags) {
    outcome = text;
    if (!preconfirmed)
      MessageBoxW(owner, text, title, flags);
  };

  if (clientName.empty()) {
    report(g_hGui, L"Please select a client first.", L"Warning",
               MB_OK | MB_ICONWARNING);
    return false;
  }

  std::wstring profileUseError;
  if (ProbeClientProfilesInUse(clientName, profileUseError) !=
      ProfileUseState::NotInUse) {
    report(
        g_hGui,
        (L"Close every browser open for this client before deleting it. "
         L"ctSpaces will not delete when process inspection is uncertain.\n\n" +
         profileUseError)
            .c_str(),
        L"Client May Be Active", MB_OK | MB_ICONWARNING);
    return false;
  }

  fs::path initialRoot;
  if (!TryGetSafeClientProfilePath(clientName, initialRoot)) {
    report(g_hGui,
                L"The selected client folder is not a safe local profile.",
                L"Action Denied", MB_OK | MB_ICONWARNING);
    return false;
  }
  fs::path validatedRoot;
  std::wstring validationError;
  if (!ValidateWholeClientDeleteTarget(clientName, initialRoot, validatedRoot,
                                       validationError)) {
    std::wstring message =
        L"ctSpaces could not prove that this is a safe, complete client "
        L"profile. Nothing was deleted.";
    if (!validationError.empty())
      message += L"\n\nDetails: " + validationError;
    report(g_hGui, message.c_str(), L"Delete Blocked",
                MB_OK | MB_ICONERROR);
    return false;
  }

  const std::wstring warning =
      L"Permanently delete the entire client \"" + clientName +
      L"\"?\n\nThis removes all legacy profile data and every Edge, Chrome, "
      L"Brave, and Firefox browser slot for this client, including logins, "
      L"cookies, history, bookmarks, extensions, and sessions.\n\nThis cannot "
      L"be undone.";
  if (!preconfirmed &&
      MessageBoxW(g_hGui, warning.c_str(), L"Delete Entire Client",
                  MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
    return false;
  }

  fs::path deleteRoot;
  validationError.clear();
  if (!ValidateWholeClientDeleteTarget(clientName, initialRoot, deleteRoot,
                                       validationError)) {
    std::wstring message =
        L"The client changed after confirmation, so nothing was deleted.";
    if (!validationError.empty())
      message += L"\n\nDetails: " + validationError;
    report(g_hGui, message.c_str(), L"Delete Cancelled",
                MB_OK | MB_ICONERROR);
    return false;
  }

  profileUseError.clear();
  if (ProbeClientProfilesInUse(clientName, profileUseError) !=
      ProfileUseState::NotInUse) {
    report(
        g_hGui,
        (L"A browser for this client became active or could not be reverified "
         L"after confirmation. Nothing was deleted.\n\n" +
         profileUseError)
            .c_str(),
        L"Delete Cancelled", MB_OK | MB_ICONWARNING);
    return false;
  }

  // A preview is not deletion authority for a subsequently reopened client.
  if (expectedActivity &&
      (client_activity::Read(deleteRoot) != expectedActivity ||
       !client_activity::IsInactive(*expectedActivity, client_activity::Now()))) {
    outcome = L"The client's activity date changed or is no longer eligible.";
    return false;
  }

  std::wstring deleteReadinessError;
  if (!PreflightValidatedClientTreeDelete(deleteRoot,
                                          deleteReadinessError)) {
    std::wstring message =
        L"The client could not be prepared for deletion, so no files were "
        L"removed. Close programs using this client and try again.";
    if (!deleteReadinessError.empty())
      message += L"\n\nDetails: " + deleteReadinessError;
    report(g_hGui, message.c_str(), L"Delete Cancelled",
           MB_OK | MB_ICONWARNING);
    return false;
  }
#ifdef CTSPACES_INSTALLER_TEST_HOOKS
  CtSpacesInstallerTestAfterClientDeletePreflight(deleteRoot);
#endif

  std::vector<fs::path> deleteAuthority;
  std::wstring deleteDetails;
  if (!CollectClientDeleteAuthority(deleteRoot, deleteAuthority,
                                    deleteDetails)) {
    std::wstring message =
        L"The client deletion authority could not be captured safely. No "
        L"files were removed.";
    if (!deleteDetails.empty())
      message += L"\n\nDetails: " + deleteDetails;
    report(g_hGui, message.c_str(), L"Delete Cancelled",
           MB_OK | MB_ICONERROR);
    return false;
  }

  const fs::path quarantineBase = g_sDataDir / L"_Delete";
  const DWORD quarantineAttributes = GetFileAttributesW(quarantineBase.c_str());
  if (quarantineAttributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if ((error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) ||
        !CreateDirectoryW(quarantineBase.c_str(), nullptr)) {
      report(g_hGui,
             L"The safe deletion staging folder could not be prepared. No "
             L"files were removed.",
             L"Delete Cancelled", MB_OK | MB_ICONERROR);
      return false;
    }
  }
  if (!IsSafeExistingDirectory(quarantineBase) ||
      !IsDirectChildPath(quarantineBase, g_sDataDir)) {
    report(g_hGui,
           L"The deletion staging folder is not a safe local directory. No "
           L"files were removed.",
           L"Delete Cancelled", MB_OK | MB_ICONERROR);
    return false;
  }
  fs::path quarantineRoot;
  if (!TryMakeUniqueSiblingStagePath(quarantineBase / deleteRoot.filename(),
                                     L"delete", quarantineRoot,
                                     deleteDetails)) {
    std::wstring message =
        L"A unique deletion staging path could not be prepared. No files "
        L"were removed.";
    if (!deleteDetails.empty())
      message += L"\n\nDetails: " + deleteDetails;
    report(g_hGui, message.c_str(), L"Delete Cancelled",
           MB_OK | MB_ICONERROR);
    return false;
  }

  if (!RemoveClientPayloadPreservingDeleteAuthority(
          deleteRoot, deleteAuthority, deleteDetails)) {
    std::wstring message =
        L"The client payload could not be completely removed. The ctSpaces "
        L"ownership metadata was retained so the same client can be retried.";
    if (!deleteDetails.empty())
      message += L"\n\nDetails: " + deleteDetails;
    report(g_hGui, message.c_str(), L"Delete Incomplete",
           MB_OK | MB_ICONERROR);
    return false;
  }

  fs::path finalValidatedRoot;
  deleteDetails.clear();
  std::wstring finalProfileUseError;
  if (!ValidateWholeClientDeleteTarget(clientName, deleteRoot,
                                       finalValidatedRoot, deleteDetails) ||
      !SameExecutablePath(finalValidatedRoot, deleteRoot) ||
      ProbeClientProfilesInUse(clientName, finalProfileUseError) !=
          ProfileUseState::NotInUse ||
      !ClientTreeContainsOnlyDeleteAuthority(
          deleteRoot, deleteAuthority, deleteDetails)) {
    std::wstring message =
        L"The client changed before the final deletion commit. Its ctSpaces "
        L"ownership metadata remains in place so deletion can be retried.";
    if (!deleteDetails.empty())
      message += L"\n\nDetails: " + deleteDetails;
    if (!finalProfileUseError.empty())
      message += L"\n\n" + finalProfileUseError;
    report(g_hGui, message.c_str(), L"Delete Cancelled",
           MB_OK | MB_ICONWARNING);
    return false;
  }

  if (!MoveFileExW(deleteRoot.c_str(), quarantineRoot.c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    report(g_hGui,
           std::format(
               L"The final deletion commit could not be staged (Windows "
               L"error {}). The retryable ctSpaces metadata remains in place.",
               error)
               .c_str(),
           L"Delete Cancelled", MB_OK | MB_ICONERROR);
    return false;
  }

  deleteDetails.clear();
  const DWORD liveRootAttributes = GetFileAttributesW(deleteRoot.c_str());
  const DWORD liveRootAbsenceError =
      liveRootAttributes == INVALID_FILE_ATTRIBUTES ? GetLastError()
                                                    : ERROR_SUCCESS;
  const bool liveRootVerifiedAbsent =
      liveRootAttributes == INVALID_FILE_ATTRIBUTES &&
      (liveRootAbsenceError == ERROR_FILE_NOT_FOUND ||
       liveRootAbsenceError == ERROR_PATH_NOT_FOUND);
  if (!liveRootVerifiedAbsent || !IsSafeExistingDirectory(quarantineRoot) ||
      !ClientTreeContainsOnlyDeleteAuthority(
          quarantineRoot, deleteAuthority, deleteDetails)) {
    std::wstring rollbackDetails;
    const DWORD originalAttributes = GetFileAttributesW(deleteRoot.c_str());
    const DWORD originalError = originalAttributes == INVALID_FILE_ATTRIBUTES
                                    ? GetLastError()
                                    : ERROR_SUCCESS;
    const bool originalAbsent =
        originalAttributes == INVALID_FILE_ATTRIBUTES &&
        (originalError == ERROR_FILE_NOT_FOUND ||
         originalError == ERROR_PATH_NOT_FOUND);
    const bool restored =
        originalAbsent &&
        MoveFileExW(quarantineRoot.c_str(), deleteRoot.c_str(),
                    MOVEFILE_WRITE_THROUGH) &&
        ValidateWholeClientDeleteTarget(clientName, deleteRoot,
                                        finalValidatedRoot, rollbackDetails);
    std::wstring message =
        restored
            ? L"The client changed during the final deletion commit. Its "
              L"retryable ctSpaces metadata was restored."
            : L"The client changed during the final deletion commit and could "
              L"not be restored automatically. No deletion success was "
              L"recorded. Inspect this exact staging folder:\n\n" +
                  quarantineRoot.wstring();
    if (!deleteDetails.empty())
      message += L"\n\nDetails: " + deleteDetails;
    if (!rollbackDetails.empty())
      message += L"\n\nRollback details: " + rollbackDetails;
    report(g_hGui, message.c_str(), L"Delete Incomplete",
           MB_OK | MB_ICONERROR);
    return false;
  }

  std::wstring metadataCleanupWarning;
  std::error_code deleteError;
  fs::remove_all(quarantineRoot, deleteError);
  const DWORD remainingAttributes = GetFileAttributesW(quarantineRoot.c_str());
  const DWORD quarantineAbsenceError =
      remainingAttributes == INVALID_FILE_ATTRIBUTES ? GetLastError()
                                                     : ERROR_SUCCESS;
  const bool quarantineVerifiedAbsent =
      remainingAttributes == INVALID_FILE_ATTRIBUTES &&
      (quarantineAbsenceError == ERROR_FILE_NOT_FOUND ||
       quarantineAbsenceError == ERROR_PATH_NOT_FOUND);
  if (deleteError || !quarantineVerifiedAbsent) {
    std::wstring remainingDetails;
    const bool metadataOnly =
        remainingAttributes != INVALID_FILE_ATTRIBUTES &&
        IsSafeExistingDirectory(quarantineRoot) &&
        ClientTreeContainsOnlyDeleteAuthority(
            quarantineRoot, deleteAuthority, remainingDetails, false);
    if (!metadataOnly) {
      std::wstring message =
          L"The client was removed from the live collection, but ctSpaces "
          L"could not verify that only empty ownership metadata remains. No "
          L"complete deletion was recorded. Inspect this exact staging "
          L"folder:\n\n" +
          quarantineRoot.wstring();
      if (deleteError)
        message += L"\n\nWindows reported: " +
                   AnsiToWide(deleteError.message());
      if (!remainingDetails.empty())
        message += L"\n\nDetails: " + remainingDetails;
      report(g_hGui, message.c_str(), L"Delete Incomplete",
             MB_OK | MB_ICONERROR);
      return false;
    }
    metadataCleanupWarning =
        L"All client browser data was deleted, but an empty ctSpaces metadata "
        L"staging folder could not be removed:\n\n" +
        quarantineRoot.wstring();
  }
  std::error_code baseCleanupError;
  if (fs::is_directory(quarantineBase, baseCleanupError) &&
      fs::is_empty(quarantineBase, baseCleanupError)) {
    fs::remove(quarantineBase, baseCleanupError);
  }

  std::vector<std::wstring> shortcutFailures;
  const auto removeManagedShortcut =
      [&clientName, &shortcutFailures](
          const std::optional<fs::path> &shortcutPath,
          std::optional<BrowserKind> browser) {
        if (!shortcutPath ||
            !IsManagedClientDesktopShortcut(*shortcutPath, clientName,
                                            browser)) {
          return;
        }
        const ShortcutOwnershipCheck isOwned =
            [&clientName, browser](const fs::path &candidate) {
              return IsManagedClientDesktopShortcut(candidate, clientName,
                                                    browser);
            };
        std::wstring deleteError;
        if (!DeleteOwnedShortcutSafely(*shortcutPath, isOwned, deleteError)) {
          shortcutFailures.push_back(shortcutPath->filename().wstring() +
                                     L" (" + deleteError + L")");
        }
      };
  for (BrowserKind browser : {BrowserKind::Edge, BrowserKind::Chrome,
                              BrowserKind::Brave, BrowserKind::Firefox}) {
    for (const auto &shortcutPath :
         GetClientDesktopShortcutCandidatePaths(clientName, browser)) {
      removeManagedShortcut(shortcutPath, browser);
    }
  }
  for (const auto &shortcutPath :
       GetClientDesktopShortcutCandidatePaths(clientName, std::nullopt)) {
    removeManagedShortcut(shortcutPath, std::nullopt);
  }

  ClearClientIconCache(clientName);
  if (!preconfirmed) {
    UpdateClientsComboBox();
    SetClientInputText(L"");
  }
  if (!shortcutFailures.empty() || !metadataCleanupWarning.empty()) {
    std::wstring message =
        L"The entire client and all browser data were deleted.";
    if (!metadataCleanupWarning.empty())
      message += L"\n\n" + metadataCleanupWarning;
    if (!shortcutFailures.empty())
      message += L"\n\nOne or more verified ctSpaces shortcuts could not be removed:";
    for (const auto &failure : shortcutFailures)
      message += L"\n\n" + failure;
    report(g_hGui, message.c_str(), L"Client Deleted",
                MB_OK | MB_ICONWARNING);
  } else {
    report(g_hGui,
                L"The entire client and all Edge, Chrome, Brave, Firefox, and "
                L"legacy profile data were deleted successfully.",
                L"Client Deleted", MB_OK | MB_ICONINFORMATION);
  }
  return true;
}

void GuiProfDel() {
  SetUiState(false);
  struct UiStateRestore {
    ~UiStateRestore() { SetUiState(true); }
  } restoreUi;
  std::wstring outcome;
  DeleteEntireClient(GetSelectedClientNameSanitized(false), false,
                     std::nullopt, outcome);
}

struct InactiveClientCandidate {
  std::wstring name;
  client_activity::Record activity;
};

struct ClientDeletionDialogState {
  const std::vector<InactiveClientCandidate> *clients = nullptr;
  bool manualSelection = false;
  std::vector<int> selected;
};

static void ApplyCleanupDialogTheme(HWND dialog) {
  ApplyThemeToWindow(dialog);
  {
    std::lock_guard<std::mutex> lock(g_themedPopupsMutex);
    if (std::find(g_themedPopups.begin(), g_themedPopups.end(), dialog) == g_themedPopups.end())
      g_themedPopups.push_back(dialog);
  }
  for (const int id : {IDC_INACTIVE_LIST, IDC_CLEANUP_RESULT_TEXT,
                       IDC_GUIDE_TOPICS, IDC_GUIDE_BODY}) {
    HWND control = GetDlgItem(dialog, id);
    if (!control)
      continue;
    SetWindowTheme(control, g_bThemeIsDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowLongPtrW(control, GWL_STYLE,
                     GetWindowLongPtrW(control, GWL_STYLE) & ~WS_BORDER);
    SetWindowLongPtrW(control, GWL_EXSTYLE,
                     GetWindowLongPtrW(control, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
    SetWindowPos(control, nullptr, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  EnumChildWindows(dialog, [](HWND button, LPARAM) -> BOOL {
    wchar_t name[32]{};
    GetClassNameW(button, name, static_cast<int>(std::size(name)));
    if (_wcsicmp(name, L"Edit") == 0)
      ApplyEditContextMenuTheme(button);
    const LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
    const LONG_PTR type = style & BS_TYPEMASK;
    if (_wcsicmp(name, L"Button") == 0 &&
        (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW)) {
      SetWindowLongPtrW(button, GWL_STYLE,
          (style & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW);
      SetWindowSubclass(button, ButtonHotSubclassProc, 1, 0);
    }
    return TRUE;
  }, 0);
  RedrawWindow(dialog, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static std::optional<INT_PTR> HandleCleanupDialogTheme(
    HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_NCDESTROY: {
    std::lock_guard<std::mutex> lock(g_themedPopupsMutex);
    std::erase(g_themedPopups, dialog);
    break;
  }
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLOREDIT:
    return reinterpret_cast<INT_PTR>(HandleThemeCtlColor(
        lParam && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) ==
                       IDC_CLEANUP_RESULT_TEXT ? WM_CTLCOLOREDIT : message,
        reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
  case WM_ERASEBKGND: {
    RECT rect{};
    GetClientRect(dialog, &rect);
    FillRect(reinterpret_cast<HDC>(wParam), &rect, g_hbrThemeWindow);
    for (const int id : {IDC_INACTIVE_LIST, IDC_CLEANUP_RESULT_TEXT,
                         IDC_GUIDE_TOPICS, IDC_GUIDE_BODY}) {
      HWND control = GetDlgItem(dialog, id);
      RECT border{};
      if (!control || !GetWindowRect(control, &border))
        continue;
      MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT *>(&border), 2);
      InflateRect(&border, 1, 1);
      FrameRect(reinterpret_cast<HDC>(wParam), &border, g_hbrThemeBorder);
    }
    return TRUE;
  }
  case WM_MEASUREITEM: {
    auto *measure = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
    if (measure && measure->CtlType == ODT_LISTBOX &&
        measure->CtlID == IDC_INACTIVE_LIST) {
      measure->itemHeight = ScaleByDpi(22, GetDpiForWindow(dialog));
      return TRUE;
    }
    break;
  }
  case WM_DRAWITEM: {
    const auto *draw = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
    if (!draw)
      break;
    if (draw->CtlType == ODT_LISTBOX && draw->CtlID == IDC_INACTIVE_LIST) {
      const bool selected = (draw->itemState & ODS_SELECTED) != 0;
      const COLORREF background = selected ? g_themeColors.crAccent
                                            : g_themeColors.crControl;
      HBRUSH brush = CreateSolidBrush(background);
      FillRect(draw->hDC, &draw->rcItem, brush);
      DeleteObject(brush);
      if (draw->itemID != static_cast<UINT>(-1)) {
        const LRESULT length = SendMessageW(draw->hwndItem, LB_GETTEXTLEN,
                                            draw->itemID, 0);
        if (length >= 0 && length <= 4096) {
          std::wstring text(static_cast<size_t>(length) + 1, L'\0');
          if (SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID,
                           reinterpret_cast<LPARAM>(text.data())) != LB_ERR) {
            HGDIOBJ oldFont = SelectObject(draw->hDC, reinterpret_cast<HFONT>(
                SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0)));
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, selected ? g_themeColors.crAccentText
                                              : g_themeColors.crControlText);
            RECT textRect = draw->rcItem;
            textRect.left += ScaleByDpi(4, GetDpiForWindow(dialog)) -
                                 GetScrollPos(draw->hwndItem, SB_HORZ);
            DrawTextW(draw->hDC, text.c_str(), static_cast<int>(length),
                       &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(draw->hDC, oldFont);
          }
        }
      }
      if ((draw->itemState & (ODS_FOCUS | ODS_NOFOCUSRECT)) == ODS_FOCUS)
        DrawFocusRect(draw->hDC, &draw->rcItem);
      return TRUE;
    }
    if (draw->CtlType == ODT_BUTTON) {
      DrawOwnerDrawItem(*draw);
      return TRUE;
    }
    break;
  }
  case WM_NOTIFY: {
    auto *draw = reinterpret_cast<NMCUSTOMDRAW *>(lParam);
    if (!draw || draw->hdr.idFrom != IDC_INACTIVE_ACK ||
        draw->hdr.code != NM_CUSTOMDRAW || draw->dwDrawStage != CDDS_PREPAINT)
      break;
    const UINT dpi = GetDpiForWindow(dialog);
    FillRect(draw->hdc, &draw->rc, g_hbrThemeWindow);
    const int size = ScaleByDpi(13, dpi);
    RECT box{draw->rc.left, draw->rc.top +
             (draw->rc.bottom - draw->rc.top - size) / 2, draw->rc.left + size, 0};
    box.bottom = box.top + size;
    const bool checked = IsDlgButtonChecked(dialog, IDC_INACTIVE_ACK) == BST_CHECKED;
    DrawRoundedRect(draw->hdc, box,
                    checked ? g_themeColors.crAccent : g_themeColors.crControl,
                    g_themeColors.crControlBorder, ScaleByDpi(2, dpi));
    if (checked) {
      HPEN pen = CreatePen(PS_SOLID, (std::max)(1, ScaleByDpi(2, dpi)),
                           g_themeColors.crAccentText);
      HGDIOBJ oldPen = SelectObject(draw->hdc, pen);
      MoveToEx(draw->hdc, box.left + size / 4, box.top + size / 2, nullptr);
      LineTo(draw->hdc, box.left + size * 2 / 5, box.top + size * 3 / 4);
      LineTo(draw->hdc, box.left + size * 4 / 5, box.top + size / 4);
      SelectObject(draw->hdc, oldPen);
      DeleteObject(pen);
    }
    wchar_t text[256]{};
    GetWindowTextW(draw->hdr.hwndFrom, text, static_cast<int>(std::size(text)));
    RECT textRect = draw->rc;
    textRect.left = box.right + ScaleByDpi(6, dpi);
    SetBkMode(draw->hdc, TRANSPARENT);
    SetTextColor(draw->hdc, g_themeColors.crWindowText);
    HGDIOBJ oldFont = SelectObject(draw->hdc, reinterpret_cast<HFONT>(
        SendMessageW(draw->hdr.hwndFrom, WM_GETFONT, 0, 0)));
    DrawTextW(draw->hdc, text, -1, &textRect,
               DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if ((draw->uItemState & CDIS_FOCUS) &&
        !(SendMessageW(draw->hdr.hwndFrom, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
      DrawFocusRect(draw->hdc, &textRect);
    SelectObject(draw->hdc, oldFont);
    SetWindowLongPtrW(dialog, DWLP_MSGRESULT, CDRF_SKIPDEFAULT);
    return TRUE;
  }
  }
  return std::nullopt;
}

static LRESULT CALLBACK MessageButtonThemeSubclass(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
  // The native dialog manager changes default-button styles as focus moves.
  // Preserve its default ID, but do not let that turn themed painting off.
  if (message == BM_SETSTYLE)
    wParam = (wParam & ~static_cast<WPARAM>(BS_TYPEMASK)) | BS_OWNERDRAW;
  if (message == WM_NCDESTROY)
    RemoveWindowSubclass(window, MessageButtonThemeSubclass, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

static LRESULT CALLBACK MessageThemeSubclass(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
  if (message == WM_PAINT || message == WM_PRINTCLIENT) {
    // Windows paints a separate system-colored button band even when its
    // dialog/control color messages are handled. Paint the whole client area;
    // the native static and button children still draw their own contents.
    PAINTSTRUCT paint{};
    HDC dc = message == WM_PAINT ? BeginPaint(window, &paint)
                                : reinterpret_cast<HDC>(wParam);
    RECT rect{};
    GetClientRect(window, &rect);
    FillRect(dc, &rect, g_hbrThemeWindow);
    if (message == WM_PAINT)
      EndPaint(window, &paint);
    return 0;
  }
  if (const auto themed = HandleCleanupDialogTheme(window, message, wParam, lParam))
    return *themed;
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(window, MessageThemeSubclass, id);
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

struct MessageThemeHookState {
  HHOOK hook = nullptr;
  LPCWSTR title = nullptr;
  bool applied = false;
};
static thread_local MessageThemeHookState *g_messageThemeHook = nullptr;

static LRESULT CALLBACK MessageThemeHook(int code, WPARAM wParam, LPARAM lParam) {
  auto *state = g_messageThemeHook;
  if (code == HCBT_ACTIVATE && state && !state->applied) {
    HWND window = reinterpret_cast<HWND>(wParam);
    wchar_t name[32]{}, title[512]{};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    if (wcscmp(name, L"#32770") == 0 && state->title &&
        wcscmp(title, state->title) == 0) {
      state->applied = true;
      SetWindowSubclass(window, MessageThemeSubclass, 1, 0);
      SetWindowLongPtrW(window, GWL_STYLE,
                       GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
      ApplyCleanupDialogTheme(window);
      EnumChildWindows(window, [](HWND child, LPARAM) -> BOOL {
        wchar_t name[32]{};
        GetClassNameW(child, name, static_cast<int>(std::size(name)));
        if (_wcsicmp(name, L"Button") == 0)
          SetWindowSubclass(child, MessageButtonThemeSubclass, 1, 0);
        return TRUE;
      }, 0);
    }
  }
  return CallNextHookEx(state ? state->hook : nullptr, code, wParam, lParam);
}

static int ShowThemedMessageBox(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags) {
#ifdef CTSPACES_INSTALLER_TEST_HOOKS
  return CtSpacesInstallerTestMessageBox(owner, text, title, flags);
#endif
  // Fatal errors before the user's palette is loaded still use readable
  // Windows styling. Never change a confirmation's native result or default.
  if (!g_hbrThemeWindow)
    return (MessageBoxW)(owner, text, title, flags);
  MessageThemeHookState state{nullptr, title, false};
  auto *previous = g_messageThemeHook;
  g_messageThemeHook = &state;
  state.hook = SetWindowsHookExW(WH_CBT, MessageThemeHook, nullptr, GetCurrentThreadId());
  const int result = (MessageBoxW)(owner, text, title, flags);
  if (state.hook)
    UnhookWindowsHookEx(state.hook);
  g_messageThemeHook = previous;
  return result;
}

struct CleanupResultDialogState {
  const std::wstring &text;
  const wchar_t *title;
};

static bool ShouldInjectCleanupDialogFailure(const wchar_t *dialogKind) {
  if (!g_bQaInstance || g_sConfigPath.empty() || !dialogKind)
    return false;
  wchar_t configuredKind[32]{};
  GetPrivateProfileStringW(L"qa", L"cleanup_dialog_fault", L"",
                           configuredKind,
                           static_cast<DWORD>(std::size(configuredKind)),
                           g_sConfigPath.c_str());
  return _wcsicmp(configuredKind, dialogKind) == 0;
}

static INT_PTR ShowCleanupDialogResource(int resourceId, DLGPROC dialogProc,
                                         LPARAM parameter,
                                         const wchar_t *qaDialogKind) {
  if (ShouldInjectCleanupDialogFailure(qaDialogKind)) {
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return -1;
  }
  return DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(resourceId), g_hGui,
                         dialogProc, parameter);
}

static fs::path GetCleanupQaDecisionLogPath() {
  return g_sConfigPath.empty()
             ? fs::path()
             : g_sConfigPath.parent_path() / L"cleanup-decision.log";
}

static std::string CleanupQaUtf8(std::wstring_view value) {
  if (value.empty())
    return {};
  const int length = WideCharToMultiByte(
      CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
      nullptr, nullptr);
  if (length <= 0)
    return {};
  std::string result(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), length, nullptr, nullptr);
  return result;
}

static std::wstring CleanupQaLogField(std::wstring value) {
  for (wchar_t &character : value) {
    if (character == L'\r' || character == L'\n' || character == L'\t')
      character = L' ';
  }
  constexpr size_t kMaximumFieldCharacters = 512;
  if (value.size() > kMaximumFieldCharacters) {
    value.resize(kMaximumFieldCharacters);
    value += L"...";
  }
  return value;
}

static void ResetCleanupQaDecisionLog(bool manualSelection) {
  if (!g_bQaInstance)
    return;
  try {
    const fs::path path = GetCleanupQaDecisionLogPath();
    if (path.empty())
      return;
    std::ofstream log(path, std::ios::binary | std::ios::trunc);
    if (log) {
      log << "ctSpaces cleanup decision trace\r\nmode="
          << (manualSelection ? "manual" : "inactive") << "\r\n";
    }
  } catch (...) {
  }
}

static void LogCleanupQaDecision(const std::wstring &clientName,
                                 const wchar_t *decision,
                                 const std::wstring &details = {}) {
  if (!g_bQaInstance || !decision)
    return;
  try {
    const fs::path path = GetCleanupQaDecisionLogPath();
    if (path.empty())
      return;
    constexpr uintmax_t kMaximumLogBytes = 128 * 1024;
    std::error_code sizeError;
    if (fs::exists(path, sizeError) && !sizeError &&
        fs::file_size(path, sizeError) >= kMaximumLogBytes) {
      return;
    }
    std::wstring line = L"client=" + CleanupQaLogField(clientName) +
                        L"\tdecision=" + CleanupQaLogField(decision);
    if (!details.empty())
      line += L"\tdetails=" + CleanupQaLogField(details);
    line += L"\r\n";
    const std::string encoded = CleanupQaUtf8(line);
    std::ofstream log(path, std::ios::binary | std::ios::app);
    if (log)
      log.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
  } catch (...) {
  }
}

static INT_PTR CALLBACK CleanupResultDlgProc(HWND dialog, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
  if (const auto themed = HandleCleanupDialogTheme(dialog, message, wParam, lParam))
    return *themed;
  if (message == WM_INITDIALOG) {
    const auto *state = reinterpret_cast<const CleanupResultDialogState *>(lParam);
    if (!state) {
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    SetWindowTextW(dialog, state->title);
    std::wstring text;
    text.reserve(state->text.size());
    for (size_t i = 0; i < state->text.size(); ++i) {
      if (state->text[i] == L'\n' && (i == 0 || state->text[i - 1] != L'\r'))
        text += L'\r';
      text += state->text[i];
    }
    SetDlgItemTextW(dialog, IDC_CLEANUP_RESULT_TEXT, text.c_str());
    ApplyCleanupDialogTheme(dialog);
    HWND details = GetDlgItem(dialog, IDC_CLEANUP_RESULT_TEXT);
    RECT rect{};
    GetClientRect(details, &rect);
    HDC dc = GetDC(details);
    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(details, WM_GETFONT, 0, 0)));
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, oldFont);
    ReleaseDC(details, dc);
    const LRESULT lines = SendMessageW(details, EM_GETLINECOUNT, 0, 0);
    ShowScrollBar(details, SB_VERT, lines * metrics.tmHeight > rect.bottom - ScaleByDpi(4, GetDpiForWindow(dialog)));
    // Read-only details should not gain focus and select all on opening.
    SendDlgItemMessageW(dialog, IDC_CLEANUP_RESULT_TEXT, EM_SETSEL, 0, 0);
    SetFocus(GetDlgItem(dialog, IDOK));
    return FALSE;
  }
  if (message == WM_CLOSE ||
      (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))) {
    EndDialog(dialog, IDOK);
    return TRUE;
  }
  return FALSE;
}

static void ShowCleanupResult(const std::wstring &text, const wchar_t *title) {
  const CleanupResultDialogState state{text, title};
  const INT_PTR result = ShowCleanupDialogResource(
      IDD_CLEANUP_RESULT, CleanupResultDlgProc,
      reinterpret_cast<LPARAM>(&state), L"result");
  if (result == -1) {
    const DWORD dialogError = GetLastError();
    constexpr size_t kMaximumFallbackPageCharacters = 2400;
    constexpr size_t kMaximumFallbackPageLines = 18;
    const auto pages = cleanup_result::Paginate(
        text, kMaximumFallbackPageCharacters, kMaximumFallbackPageLines);
    for (size_t index = 0; index < pages.size(); ++index) {
      std::wstring fallback;
      if (pages.size() > 1) {
        fallback += std::format(L"Result details (page {} of {}):\n\n",
                                index + 1, pages.size());
      }
      fallback += pages[index];
      fallback += L"\n\nThe scrollable details window could not be opened.";
      if (dialogError)
        fallback += std::format(L"\n\nWindows error: {}.", dialogError);
      const std::wstring pageTitle =
          pages.size() > 1
              ? std::format(L"{} ({} of {})", title, index + 1, pages.size())
              : std::wstring(title);
      MessageBoxW(g_hGui, fallback.c_str(), pageTitle.c_str(),
                  MB_OK | MB_ICONWARNING);
    }
  }
}

static INT_PTR CALLBACK InactiveClientsDlgProc(HWND dialog, UINT message,
                                              WPARAM wParam, LPARAM lParam) {
  if (const auto themed = HandleCleanupDialogTheme(dialog, message, wParam, lParam))
    return *themed;
  auto *state = reinterpret_cast<ClientDeletionDialogState *>(
      GetWindowLongPtrW(dialog, DWLP_USER));
  const auto updateSelection = [&]() {
    const LRESULT count = SendDlgItemMessageW(dialog, IDC_INACTIVE_LIST,
                                             LB_GETSELCOUNT, 0, 0);
    EnableWindow(GetDlgItem(dialog, IDOK), count > 0 &&
        IsDlgButtonChecked(dialog, IDC_INACTIVE_ACK) == BST_CHECKED);
    const std::wstring label = std::format(L"Delete Selected ({})",
                                           count > 0 ? count : 0);
    SetDlgItemTextW(dialog, IDOK, label.c_str());
  };
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<ClientDeletionDialogState *>(lParam);
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    const auto *clients = state ? state->clients : nullptr;
    HWND list = GetDlgItem(dialog, IDC_INACTIVE_LIST);
    if (!clients || !list)
      return FALSE;
    ApplyCleanupDialogTheme(dialog);
    int horizontalExtent = 0;
    HDC dc = GetDC(list);
    HFONT oldFont = dc ? static_cast<HFONT>(SelectObject(
        dc, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0))))
        : nullptr;
    for (const auto &client : *clients) {
      const FILETIME utc = client_activity::ToFileTime(client.activity.time);
      FILETIME local{};
      SYSTEMTIME date{};
      FileTimeToLocalFileTime(&utc, &local);
      FileTimeToSystemTime(&local, &date);
      const std::wstring row = client.activity.time == 0
          ? client.name + L"    |    No verified activity date"
          : std::format(
          L"{}    |    {}: {:04}-{:02}-{:02}", client.name,
          client.activity.baseline ? L"Tracking started" : L"Last opened",
          date.wYear, date.wMonth, date.wDay);
      if (SendMessageW(list, LB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(row.c_str())) < 0) {
        if (dc) {
          SelectObject(dc, oldFont);
          ReleaseDC(list, dc);
        }
        EndDialog(dialog, IDCANCEL); // Never approve an incomplete preview.
        return TRUE;
      }
      if (dc) {
        const int rowExtent = owner_draw_ui::HorizontalTextExtent(
            row, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)),
            GetDpiForWindow(dialog), 16);
        horizontalExtent = (std::max)(horizontalExtent, rowExtent);
      }
    }
    if (dc) {
      SelectObject(dc, oldFont);
      ReleaseDC(list, dc);
    }
    SendMessageW(list, LB_SETHORIZONTALEXTENT, horizontalExtent, 0);
    if (!state->manualSelection)
      SendMessageW(list, LB_SETSEL, TRUE, -1);
    if (state->manualSelection)
      SetWindowTextW(dialog, L"Delete Multiple Clients");
    const std::wstring heading = state->manualSelection ? std::format(
        L"{} closed client(s), including archived clients. Click each client "
        L"to select or deselect it. No inactivity waiting period applies.",
        clients->size()) : std::format(
        L"{} client(s) have not been opened in three calendar months. "
        L"Includes archived clients. Click any client to select or deselect it.",
        clients->size());
    SetDlgItemTextW(dialog, IDC_INACTIVE_HEADING, heading.c_str());
    updateSelection();
    SetFocus(GetDlgItem(dialog, IDCANCEL));
    return FALSE;
  }
  if (message == WM_COMMAND) {
    if (LOWORD(wParam) == IDC_INACTIVE_ALL ||
        LOWORD(wParam) == IDC_INACTIVE_NONE) {
      SendDlgItemMessageW(dialog, IDC_INACTIVE_LIST, LB_SETSEL,
                         LOWORD(wParam) == IDC_INACTIVE_ALL, -1);
      updateSelection();
      return TRUE;
    }
    if (LOWORD(wParam) == IDC_INACTIVE_ACK ||
        (LOWORD(wParam) == IDC_INACTIVE_LIST && HIWORD(wParam) == LBN_SELCHANGE)) {
      updateSelection();
      return TRUE;
    }
    if (LOWORD(wParam) == IDOK) {
      const LRESULT count = SendDlgItemMessageW(dialog, IDC_INACTIVE_LIST,
                                               LB_GETSELCOUNT, 0, 0);
      if (state && state->clients && count > 0 &&
          static_cast<size_t>(count) <= state->clients->size() &&
          IsDlgButtonChecked(dialog, IDC_INACTIVE_ACK) == BST_CHECKED) {
        state->selected.resize(static_cast<size_t>(count));
        if (SendDlgItemMessageW(dialog, IDC_INACTIVE_LIST, LB_GETSELITEMS,
                               count, reinterpret_cast<LPARAM>(
                                   state->selected.data())) == count)
          EndDialog(dialog, IDOK);
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
  }
  if (message == WM_CLOSE) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  return FALSE;
}

static void GuiCleanupInactiveClients(bool manualSelection) {
  if (!g_bUiEnabled || g_isLaunchInFlight.load())
    return;
  SetUiState(false);
  struct UiStateRestore {
    ~UiStateRestore() { SetUiState(true); }
  } restoreUi;
  ResetCleanupQaDecisionLog(manualSelection);
  std::vector<InactiveClientCandidate> candidates;
  size_t skipped = 0;
  const auto now = client_activity::Now();
  try {
    if (!ValidateSitesRoot(false))
      throw std::runtime_error("The client collection is unsafe or unavailable.");
    std::error_code error;
    const auto sites = g_sDataDir / L"Sites";
    if (!fs::exists(sites, error) && !error) {
      MessageBoxW(g_hGui, L"There are no clients to clean up.", L"Inactive Clients", MB_OK);
      return;
    }
    for (const auto &entry : fs::directory_iterator(sites)) {
      const auto name = entry.path().filename().wstring();
      fs::path root;
      if (!TryGetSafeClientProfilePath(name, root) ||
          !IsSafeExistingDirectory(root)) {
        ++skipped;
        LogCleanupQaDecision(name, L"skipped-unsafe-root");
        continue;
      }
      // Import/upgrade with no history starts a fresh three-month grace period.
      client_activity::Write(root, {now, true}, true);
      const auto activity = client_activity::Read(root);
      if (!activity && !manualSelection) {
        ++skipped;
        LogCleanupQaDecision(name, L"skipped-unverifiable-activity");
        continue;
      }
      if (!manualSelection && !client_activity::IsInactive(*activity, now)) {
        LogCleanupQaDecision(name, L"not-inactive");
        continue;
      }
      std::wstring details;
      const ProfileUseState profileUse =
          ProbeClientProfilesInUse(name, details);
      if (profileUse != ProfileUseState::NotInUse) {
        ++skipped;
        LogCleanupQaDecision(
            name,
            profileUse == ProfileUseState::InUse ? L"skipped-profile-open"
                                                  : L"skipped-profile-unknown",
            details);
        continue;
      }
      fs::path validatedRoot;
      if (!ValidateWholeClientDeleteTarget(name, root, validatedRoot, details)) {
        ++skipped;
        LogCleanupQaDecision(name, L"skipped-delete-target-invalid", details);
        continue;
      }
      LogCleanupQaDecision(name, L"candidate");
      candidates.push_back({name, activity.value_or(client_activity::Record{})});
    }
  } catch (const std::exception &error) {
    LogCleanupQaDecision(L"(collection)", L"inspection-failed",
                         AnsiToWide(error.what()));
    ShowCleanupResult(L"The collection could not be inspected completely. "
                      L"Nothing was deleted.\n\n" + AnsiToWide(error.what()),
                      L"Cleanup Cancelled");
    return;
  }
  if (candidates.empty()) {
    std::wstring message = manualSelection
        ? L"No closed, safe clients are available to delete."
        : L"No clients have been inactive for three months.\n\n"
          L"Clients with no recorded history start a fresh three-month tracking period.";
    if (skipped)
      message += std::format(L"\n\n{} item(s) skipped: open, unsafe, or unverifiable.", skipped);
    MessageBoxW(g_hGui, message.c_str(), manualSelection ? L"Delete Multiple Clients"
                                               : L"Inactive Clients", MB_OK);
    return;
  }
  std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
    return CaseInsensitiveLess{}(a.name, b.name);
  });
  ClientDeletionDialogState dialogState{&candidates, manualSelection, {}};
  const INT_PTR dialogResult = ShowCleanupDialogResource(
      IDD_INACTIVE_CLIENTS, InactiveClientsDlgProc,
      reinterpret_cast<LPARAM>(&dialogState), L"preview");
  if (dialogResult == -1) {
    const DWORD dialogError = GetLastError();
    std::wstring message =
        L"The cleanup preview could not be opened. Nothing was deleted.";
    if (dialogError)
      message += std::format(L"\n\nWindows error: {}.", dialogError);
    MessageBoxW(g_hGui, message.c_str(), L"Cleanup Unavailable",
                MB_OK | MB_ICONERROR);
    return;
  }
  if (dialogResult != IDOK)
    return;
  size_t deleted = 0;
  std::wstring problems;
  for (const int index : dialogState.selected) {
    if (index < 0 || static_cast<size_t>(index) >= candidates.size()) {
      ++skipped;
      LogCleanupQaDecision(L"(invalid selection)", L"skipped-invalid-index");
      continue;
    }
    const auto &client = candidates[static_cast<size_t>(index)];
    std::wstring outcome;
    const std::optional<client_activity::Record> expectedActivity =
        manualSelection ? std::nullopt
                        : std::optional<client_activity::Record>(client.activity);
    if (DeleteEntireClient(client.name, true, expectedActivity, outcome)) {
      ++deleted;
      LogCleanupQaDecision(client.name, L"deleted", outcome);
      if (outcome.find(L"could not be removed") != std::wstring::npos)
        problems += L"\n" + client.name + L": " + outcome + L"\n";
    } else {
      ++skipped;
      LogCleanupQaDecision(client.name, L"skipped-final-recheck", outcome);
      problems += L"\n" + client.name + L": " + outcome + L"\n";
    }
  }
  // Refresh and prune once, not once per client in a large collection.
  UpdateClientsComboBox();
  SetClientInputText(L"");
  const std::wstring summary = std::format(
      L"Deleted {} client(s), including all stored browser data.\n"
      L"Skipped {} item(s). Open or unverifiable clients are not deleted.",
      deleted, skipped) + problems;
  ShowCleanupResult(summary, L"Cleanup Complete");
}

inline void EnsureMouseVisible() {
  CURSORINFO ci{sizeof(ci)};
  if (GetCursorInfo(&ci) && ci.flags == 0) {
    ShowCursor(TRUE);
  }
}

inline void FocusClientEdit() {
  if (g_hClientEdit && IsWindowEnabled(g_hClientEdit)) {
    SetFocus(g_hClientEdit);
    SetClientInputSelection(0, -1);
  }
}

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
  if (const auto themed = HandleCleanupDialogTheme(hDlg, msg, wParam, lParam))
    return *themed;
  constexpr int kIdIcon = 1001;
  constexpr int kIdTitle = 1002;
  constexpr int kIdBy = 1003;
  constexpr int kIdLink = 1004;
  constexpr int kIdThanks = 1005;

  auto ensureStatic = [&](int id, const wchar_t *text, DWORD style, int x,
                          int y, int width, int height) -> HWND {
    HWND hwnd = GetDlgItem(hDlg, id);
    if (!hwnd) {
      hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | style, x, y,
                           width, height, hDlg, (HMENU)(INT_PTR)id, g_hInst,
                           nullptr);
    } else {
      SetWindowTextW(hwnd, text);
      MoveWindow(hwnd, x, y, width, height, TRUE);
    }
    return hwnd;
  };

  auto ensureLink = [&](int id, const wchar_t *text, int x, int y, int width,
                        int height) -> HWND {
    HWND hwnd = GetDlgItem(hDlg, id);
    if (!hwnd) {
      hwnd = CreateWindowW(WC_LINK, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LWS_TRANSPARENT, x,
                           y, width, height, hDlg, (HMENU)(INT_PTR)id, g_hInst,
                           nullptr);
    } else {
      SetWindowTextW(hwnd, text);
      MoveWindow(hwnd, x, y, width, height, TRUE);
    }
    return hwnd;
  };

  auto relayout = [&](UINT dpi, const RECT *suggested) {
    ResetAboutFonts(dpi);

    const int margin = MulDiv(12, dpi, 96);
    const int iconPx = MulDiv(64, dpi, 96);

    const int dlgW = MulDiv(480, dpi, 96);
    const int dlgH = MulDiv(320, dpi, 96);

    RECT wnd{};
    if (suggested) {
      wnd = *suggested;
    } else {
      GetWindowRect(hDlg, &wnd);
    }
    SetWindowPos(hDlg, nullptr, wnd.left, wnd.top, dlgW, dlgH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rc{};
    GetClientRect(hDlg, &rc);

    const int xIco = margin;
    const int yTop = margin;

    HWND hIco = GetDlgItem(hDlg, kIdIcon);
    if (!hIco) {
      hIco = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON,
                           xIco, yTop, iconPx, iconPx, hDlg, (HMENU)kIdIcon,
                           g_hInst, nullptr);
    } else {
      MoveWindow(hIco, xIco, yTop, iconPx, iconPx, TRUE);
    }

    const int xText = xIco + iconPx + margin;
    const int wText = rc.right - xText - margin;

    HWND hTitle =
        ensureStatic(kIdTitle, APP_TITLE.c_str(), SS_LEFT | SS_NOPREFIX, xText,
                     yTop, wText, MulDiv(24, dpi, 96));

    const wchar_t *byText =
        L"Fork maintained by madrobdestroyer\r\nOriginal by BiatuAutMiahn";
    HWND hBy = ensureStatic(
        kIdBy, byText, SS_LEFT | SS_NOPREFIX, xText + MulDiv(16, dpi, 96),
        yTop + MulDiv(20, dpi, 96), wText - MulDiv(16, dpi, 96), MulDiv(36, dpi, 96));

    const wchar_t *linkText =
        L"<a "
        L"href=\"https://github.com/madrobdestroyer/ctSpaces\">https://"
        L"github.com/madrobdestroyer/ctSpaces</a>";

    HWND hLink =
        ensureLink(kIdLink, linkText, xText, yTop + MulDiv(56, dpi, 96),
                   wText, MulDiv(24, dpi, 96));

    const wchar_t *thanksText = L"Thanks:\r\n"
                                L"  BiatuAutMiahn (Original Author)\r\n"
                                L"  Tim Jaeger (Browser Selection Idea)\r\n"
                                L"  Cameron Kincer (Client Cleanup Idea)\r\n"
                                L"  Igor Pavlov (7-Zip)\r\n"
                                L"  OpenAI (R&D and rapid prototyping)\r\n"
                                L"  Google (Material Icons)";

    HWND hThanks =
        ensureStatic(kIdThanks, thanksText, SS_LEFT | SS_NOPREFIX, xText,
                     yTop + MulDiv(88, dpi, 96), wText, MulDiv(128, dpi, 96));

    HWND hOk = GetDlgItem(hDlg, IDOK);
    if (hOk) {
      RECT br{};
      GetWindowRect(hOk, &br);
      const int bw = br.right - br.left;
      const int bh = br.bottom - br.top;
      MoveWindow(hOk, rc.right - margin - bw, rc.bottom - margin - bh, bw, bh,
                 TRUE);
    }

    HFONT hFont = g_hAboutFont ? g_hAboutFont : g_hFont;
    if (hOk)
      SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (hThanks)
      SendMessageW(hThanks, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (hTitle)
      SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (hBy)
      SendMessageW(hBy, WM_SETFONT, (WPARAM)GetAboutSmallFont(), TRUE);
    if (hLink)
      SendMessageW(hLink, WM_SETFONT, (WPARAM)hFont, TRUE);

    const int sm = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const int bg = GetSystemMetricsForDpi(SM_CXICON, dpi);

    if (g_hAboutDlgSmall) {
      DestroyIcon(g_hAboutDlgSmall);
      g_hAboutDlgSmall = nullptr;
    }
    if (g_hAboutDlgBig) {
      DestroyIcon(g_hAboutDlgBig);
      g_hAboutDlgBig = nullptr;
    }
    if (g_hAboutIcon64) {
      DestroyIcon(g_hAboutIcon64);
      g_hAboutIcon64 = nullptr;
    }

    g_hAboutDlgSmall = LoadIconResBestDownscale(g_hInst, IDI_CTSPACES, sm, sm);
    g_hAboutDlgBig = LoadIconResBestDownscale(g_hInst, IDI_CTSPACES, bg, bg);
    if (g_hAboutDlgSmall)
      SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)g_hAboutDlgSmall);
    if (g_hAboutDlgBig)
      SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)g_hAboutDlgBig);

    g_hAboutIcon64 =
        LoadIconResBestDownscale(g_hInst, IDI_IRND, iconPx, iconPx);
    if (hIco && g_hAboutIcon64) {
      SendMessageW(hIco, STM_SETIMAGE, IMAGE_ICON, (LPARAM)g_hAboutIcon64);
    }
  };

  switch (msg) {
  case WM_INITDIALOG: {
    const UINT dpi = GetDpiForWindow(hDlg);

    // Title
    std::wstring title = std::format(L"About");
    SetWindowTextW(hDlg, title.c_str());

    // Hide ALL existing resource children except OK/CANCEL
    EnumChildWindows(
        hDlg,
        [](HWND c, LPARAM) -> BOOL {
          int id = GetDlgCtrlID(c);
          if (id != IDOK && id != IDCANCEL)
            ShowWindow(c, SW_HIDE);
          return TRUE;
        },
        0);

    relayout(dpi, nullptr);
    ApplyCleanupDialogTheme(hDlg);

    return (INT_PTR)TRUE;
  }

  case WM_DPICHANGED: {
    const UINT dpi = HIWORD(wParam);
    const RECT *rc = reinterpret_cast<RECT *>(lParam);
    relayout(dpi, rc);
    ApplyCleanupDialogTheme(hDlg);
    return (INT_PTR)TRUE;
  }

  case WM_NOTIFY: {
    NMHDR *nm = (NMHDR *)lParam;
    if (!nm)
      break;

    if (nm->idFrom == kIdLink && nm->code == NM_CUSTOMDRAW) {
      auto *draw = reinterpret_cast<NMCUSTOMDRAW *>(lParam);
      if (draw->dwDrawStage == CDDS_PREPAINT) {
        FillRect(draw->hdc, &draw->rc, g_hbrThemeWindow);
        SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
        return TRUE;
      }
      if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
        SetTextColor(draw->hdc, g_themeColors.crWindowText);
        SetBkMode(draw->hdc, TRANSPARENT);
        SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
        return TRUE;
      }
    }

    if ((nm->code == NM_CLICK || nm->code == NM_RETURN) &&
        nm->idFrom == kIdLink) {
      auto *link = (NMLINK *)lParam;
      if (link)
        ShellExecuteW(hDlg, L"open", link->item.szUrl, nullptr, nullptr,
                      SW_SHOWNORMAL);
      return (INT_PTR)TRUE;
    }
    break;
  }

  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
      EndDialog(hDlg, LOWORD(wParam));
      return (INT_PTR)TRUE;
    }
    break;

  case WM_DESTROY:
    if (g_hAboutFont) {
      DeleteObject(g_hAboutFont);
      g_hAboutFont = nullptr;
    }
    if (g_hAboutIcon64) {
      DestroyIcon(g_hAboutIcon64);
      g_hAboutIcon64 = nullptr;
    }
    if (g_hAboutDlgSmall) {
      DestroyIcon(g_hAboutDlgSmall);
      g_hAboutDlgSmall = nullptr;
    }
    if (g_hAboutDlgBig) {
      DestroyIcon(g_hAboutDlgBig);
      g_hAboutDlgBig = nullptr;
    }
    if (g_hFontAboutSmall) {
      DeleteObject(g_hFontAboutSmall);
      g_hFontAboutSmall = nullptr;
    }
    break;
  }
  return (INT_PTR)FALSE;
}

struct GuideDialogState {
  std::vector<size_t> visibleTopics;
  size_t position = 0;
  bool initialWelcome = false;
  bool externallyDismissed = false;
  bool stateChanged = false;
  bool launchQuickTour = false;
  HFONT font = nullptr;
  HFONT strongFont = nullptr;
};

static void ResetGuideFonts(GuideDialogState &state, UINT dpi) {
  if (state.font)
    DeleteObject(state.font);
  if (state.strongFont)
    DeleteObject(state.strongFont);
  state.font = CreateUiFont(dpi);
  state.strongFont = CreateUiStrongFont(dpi);
}

static void LayoutGuideDialog(HWND dialog, GuideDialogState &state, UINT dpi,
                              const RECT *suggested) {
  if (!dpi)
    dpi = USER_DEFAULT_SCREEN_DPI;
  HMONITOR monitor = suggested
                         ? MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST)
                         : MonitorFromWindow(g_hGui ? g_hGui : dialog,
                                             MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitorInfo{sizeof(monitorInfo)};
  GetMonitorInfoW(monitor, &monitorInfo);
  const RECT work = monitorInfo.rcWork;
  const int workWidth = work.right - work.left;
  const int workHeight = work.bottom - work.top;
  const int edge = ScaleByDpi(12, dpi);
  const int outerWidth = min(ScaleByDpi(800, dpi), max(1, workWidth - edge * 2));
  const int outerHeight = min(ScaleByDpi(570, dpi), max(1, workHeight - edge * 2));

  int left = work.left + (workWidth - outerWidth) / 2;
  int top = work.top + (workHeight - outerHeight) / 2;
  if (suggested) {
    left = min(max(suggested->left, work.left), work.right - outerWidth);
    top = min(max(suggested->top, work.top), work.bottom - outerHeight);
  } else if (g_hGui && IsWindow(g_hGui)) {
    RECT owner{};
    if (GetWindowRect(g_hGui, &owner)) {
      left = (owner.left + owner.right - outerWidth) / 2;
      top = (owner.top + owner.bottom - outerHeight) / 2;
      left = min(max(left, work.left), work.right - outerWidth);
      top = min(max(top, work.top), work.bottom - outerHeight);
    }
  }
  SetWindowPos(dialog, nullptr, left, top, outerWidth, outerHeight,
               SWP_NOZORDER | SWP_NOACTIVATE);

  RECT client{};
  GetClientRect(dialog, &client);
  const int margin = ScaleByDpi(16, dpi);
  const int gap = ScaleByDpi(14, dpi);
  const int labelHeight = ScaleByDpi(18, dpi);
  const int buttonHeight = ScaleByDpi(30, dpi);
  const int buttonWidth = ScaleByDpi(82, dpi);
  const int closeWidth = ScaleByDpi(88, dpi);
  const int tourWidth = ScaleByDpi(104, dpi);
  const int topicsWidth = min(ScaleByDpi(240, dpi),
                              max(ScaleByDpi(160, dpi), client.right / 3));
  const int footerTop = client.bottom - margin - buttonHeight;
  const int listTop = margin + labelHeight + ScaleByDpi(4, dpi);
  const int listBottom = footerTop - gap;
  const int contentLeft = margin + topicsWidth + gap;
  const int contentWidth = max(1, client.right - contentLeft - margin);
  const int titleHeight = ScaleByDpi(28, dpi);
  const int locationHeight = ScaleByDpi(42, dpi);
  const int bodyTop = margin + titleHeight + locationHeight;

  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_TOPICS_LABEL), margin, margin,
             topicsWidth, labelHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_TOPICS), margin, listTop,
             topicsWidth, max(1, listBottom - listTop), TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_TITLE), contentLeft, margin,
             contentWidth, titleHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_LOCATION), contentLeft,
             margin + titleHeight, contentWidth, locationHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_BODY), contentLeft, bodyTop,
             contentWidth, max(1, listBottom - bodyTop), TRUE);

  const int closeLeft = client.right - margin - closeWidth;
  const int nextLeft = closeLeft - gap / 2 - buttonWidth;
  const int backLeft = nextLeft - gap / 2 - buttonWidth;
  const int tourLeft = backLeft - gap / 2 - tourWidth;
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_COUNT), margin, footerTop,
             max(1, tourLeft - gap - margin), buttonHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_QUICK_TOUR), tourLeft, footerTop,
             tourWidth, buttonHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_GUIDE_BACK), backLeft, footerTop,
             buttonWidth, buttonHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDOK), nextLeft, footerTop, buttonWidth,
             buttonHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDCANCEL), closeLeft, footerTop, closeWidth,
             buttonHeight, TRUE);

  ResetGuideFonts(state, dpi);
  EnumChildWindows(
      dialog,
      [](HWND child, LPARAM parameter) -> BOOL {
        const auto *dialogState =
            reinterpret_cast<const GuideDialogState *>(parameter);
        const int id = GetDlgCtrlID(child);
        const HFONT font = id == IDC_GUIDE_TITLE && dialogState->strongFont
                               ? dialogState->strongFont
                               : dialogState->font;
        if (font)
          SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&state));
}

static void ApplyGuideDialogTheme(HWND dialog) {
  ApplyCleanupDialogTheme(dialog);
  for (const int id : {IDC_GUIDE_TOPICS, IDC_GUIDE_BODY}) {
    HWND control = GetDlgItem(dialog, id);
    if (control) {
      SetWindowTheme(control,
                     g_bThemeIsDark ? L"DarkMode_Explorer" : L"Explorer",
                     nullptr);
    }
  }
  ApplyEditContextMenuTheme(GetDlgItem(dialog, IDC_GUIDE_BODY));
}

static bool PersistGuideWelcomeHandled(GuideDialogState &state) {
  if (g_guideState.welcomeHandled)
    return true;
  if (!SaveConfigMutations(L"guided-walkthrough welcome state",
                           guided_walkthrough::WelcomeHandledMutations())) {
    return false;
  }
  guided_walkthrough::ApplyWelcomeHandled(g_guideState);
  state.stateChanged = true;
  return true;
}

static bool PersistGuideTopicRead(GuideDialogState &state, size_t topicIndex) {
  const bool handleWelcome = !g_guideState.welcomeHandled;
  const auto mutations = guided_walkthrough::ReadTopicMutations(
      g_guideState, topicIndex, handleWelcome);
  if (!mutations.empty() &&
      !SaveConfigMutations(L"guided-walkthrough progress", mutations)) {
    return false;
  }
  if (handleWelcome)
    guided_walkthrough::ApplyWelcomeHandled(g_guideState);
  guided_walkthrough::ApplyTopicRead(g_guideState, topicIndex);
  state.stateChanged = true;
  return true;
}

static void PopulateGuideTopicList(HWND dialog, GuideDialogState &state) {
  HWND list = GetDlgItem(dialog, IDC_GUIDE_TOPICS);
  if (!list)
    return;
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  const auto &catalog = guided_walkthrough::Catalog();
  for (const size_t topicIndex : state.visibleTopics) {
    if (topicIndex >= catalog.size())
      continue;
    std::wstring label = catalog[topicIndex].title;
    if (catalog[topicIndex].announce &&
        guided_walkthrough::IsUnread(g_guideState, topicIndex)) {
      label += L" (New)";
    }
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label.c_str()));
  }
  if (!state.visibleTopics.empty()) {
    state.position = min(state.position, state.visibleTopics.size() - 1);
    SendMessageW(list, LB_SETCURSEL, state.position, 0);
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

static void UpdateGuideScrollBars(HWND dialog) {
  HWND body = GetDlgItem(dialog, IDC_GUIDE_BODY);
  if (body) {
    RECT rect{};
    GetClientRect(body, &rect);
    HDC dc = GetDC(body);
    TEXTMETRICW metrics{};
    if (dc) {
      HGDIOBJ oldFont = SelectObject(
          dc, reinterpret_cast<HFONT>(SendMessageW(body, WM_GETFONT, 0, 0)));
      GetTextMetricsW(dc, &metrics);
      SelectObject(dc, oldFont);
      ReleaseDC(body, dc);
    }
    const LRESULT lines = SendMessageW(body, EM_GETLINECOUNT, 0, 0);
    const int available = max(0, rect.bottom - rect.top - ScaleByDpi(8, GetDpiForWindow(dialog)));
    ShowScrollBar(body, SB_VERT,
                  metrics.tmHeight > 0 && lines * metrics.tmHeight > available);
  }
  HWND list = GetDlgItem(dialog, IDC_GUIDE_TOPICS);
  if (list) {
    RECT rect{};
    GetClientRect(list, &rect);
    const LRESULT count = SendMessageW(list, LB_GETCOUNT, 0, 0);
    LRESULT itemHeight = SendMessageW(list, LB_GETITEMHEIGHT, 0, 0);
    if (itemHeight <= 0)
      itemHeight = ScaleByDpi(18, GetDpiForWindow(dialog));
    ShowScrollBar(list, SB_VERT,
                  count > 0 && count * itemHeight > rect.bottom - rect.top);
  }
}

static void RefreshGuidePage(HWND dialog, GuideDialogState &state) {
  if (state.visibleTopics.empty())
    return;
  const auto &catalog = guided_walkthrough::Catalog();
  state.position = min(state.position, state.visibleTopics.size() - 1);
  const size_t topicIndex = state.visibleTopics[state.position];
  if (topicIndex >= catalog.size())
    return;
  const auto &topic = catalog[topicIndex];
  SetDlgItemTextW(dialog, IDC_GUIDE_TITLE, topic.title);
  const std::wstring location = L"Find it: " + std::wstring(topic.location);
  SetDlgItemTextW(dialog, IDC_GUIDE_LOCATION, location.c_str());
  SetDlgItemTextW(dialog, IDC_GUIDE_BODY, topic.body);
  HWND body = GetDlgItem(dialog, IDC_GUIDE_BODY);
  SendMessageW(body, EM_SETSEL, 0, 0);
  SendMessageW(body, WM_VSCROLL, SB_TOP, 0);

  const bool unread = guided_walkthrough::IsUnread(g_guideState, topicIndex);
  std::wstring count =
      std::format(L"{} / {}  |  Read-only", state.position + 1,
                  state.visibleTopics.size());
  if (unread)
    count += L"  |  Not yet read";
  SetDlgItemTextW(dialog, IDC_GUIDE_COUNT, count.c_str());
  EnableWindow(GetDlgItem(dialog, IDC_GUIDE_BACK), state.position > 0);
  if (state.position + 1 == state.visibleTopics.size()) {
    SetDlgItemTextW(dialog, IDOK, L"Done");
  } else if (state.initialWelcome && state.position == 0) {
    SetDlgItemTextW(dialog, IDOK, L"Start");
  } else {
    SetDlgItemTextW(dialog, IDOK, L"Next");
  }
  SendDlgItemMessageW(dialog, IDC_GUIDE_TOPICS, LB_SETCURSEL,
                      state.position, 0);
  UpdateGuideScrollBars(dialog);
}

static INT_PTR CALLBACK GuideDlgProc(HWND dialog, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
  auto *state = reinterpret_cast<GuideDialogState *>(
      GetWindowLongPtrW(dialog, DWLP_USER));
  if (const auto themed =
          HandleCleanupDialogTheme(dialog, message, wParam, lParam)) {
    return *themed;
  }
  switch (message) {
  case WM_INITDIALOG: {
    state = reinterpret_cast<GuideDialogState *>(lParam);
    if (!state || state->visibleTopics.empty()) {
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    g_hGuideDialog = dialog;
    SetWindowTextW(dialog, state->initialWelcome
                               ? L"Welcome to ctSpaces"
                               : L"ctSpaces Guided Walkthrough");
    HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_CTSPACES));
    if (icon) {
      SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(icon));
      SendMessageW(dialog, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(icon));
    }
    LayoutGuideDialog(dialog, *state, GetDpiForWindow(dialog), nullptr);
    ApplyGuideDialogTheme(dialog);
    SetDlgItemTextW(dialog, IDCANCEL,
                    state->initialWelcome ? L"Skip" : L"Close");
    PopulateGuideTopicList(dialog, *state);
    RefreshGuidePage(dialog, *state);
    SetFocus(GetDlgItem(dialog, IDC_GUIDE_TOPICS));
    return FALSE;
  }
  case WM_SIZE:
    if (state && wParam != SIZE_MINIMIZED) {
      // WM_SIZE only positions controls. Fonts are recreated on DPI changes.
      RECT client{};
      GetClientRect(dialog, &client);
      const UINT dpi = GetDpiForWindow(dialog);
      const int margin = ScaleByDpi(16, dpi);
      const int gap = ScaleByDpi(14, dpi);
      const int labelHeight = ScaleByDpi(18, dpi);
      const int buttonHeight = ScaleByDpi(30, dpi);
      const int buttonWidth = ScaleByDpi(82, dpi);
      const int closeWidth = ScaleByDpi(88, dpi);
      const int tourWidth = ScaleByDpi(104, dpi);
      const int topicsWidth = min(ScaleByDpi(240, dpi),
                                  max(ScaleByDpi(160, dpi), client.right / 3));
      const int footerTop = client.bottom - margin - buttonHeight;
      const int listTop = margin + labelHeight + ScaleByDpi(4, dpi);
      const int listBottom = footerTop - gap;
      const int contentLeft = margin + topicsWidth + gap;
      const int contentWidth = max(1, client.right - contentLeft - margin);
      const int titleHeight = ScaleByDpi(28, dpi);
      const int locationHeight = ScaleByDpi(42, dpi);
      const int bodyTop = margin + titleHeight + locationHeight;
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_TOPICS_LABEL), margin, margin,
                 topicsWidth, labelHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_TOPICS), margin, listTop,
                 topicsWidth, max(1, listBottom - listTop), TRUE);
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_TITLE), contentLeft, margin,
                 contentWidth, titleHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_LOCATION), contentLeft,
                 margin + titleHeight, contentWidth, locationHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_BODY), contentLeft, bodyTop,
                 contentWidth, max(1, listBottom - bodyTop), TRUE);
      const int closeLeft = client.right - margin - closeWidth;
      const int nextLeft = closeLeft - gap / 2 - buttonWidth;
      const int backLeft = nextLeft - gap / 2 - buttonWidth;
      const int tourLeft = backLeft - gap / 2 - tourWidth;
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_COUNT), margin, footerTop,
                 max(1, tourLeft - gap - margin), buttonHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_QUICK_TOUR), tourLeft,
                 footerTop, tourWidth, buttonHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDC_GUIDE_BACK), backLeft, footerTop,
                 buttonWidth, buttonHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDOK), nextLeft, footerTop, buttonWidth,
                 buttonHeight, TRUE);
      MoveWindow(GetDlgItem(dialog, IDCANCEL), closeLeft, footerTop,
                 closeWidth, buttonHeight, TRUE);
      UpdateGuideScrollBars(dialog);
    }
    return TRUE;
  case WM_DPICHANGED:
    if (state) {
      LayoutGuideDialog(dialog, *state, HIWORD(wParam),
                        reinterpret_cast<const RECT *>(lParam));
      ApplyGuideDialogTheme(dialog);
    }
    return TRUE;
  case WM_GETMINMAXINFO: {
    auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
    if (limits) {
      const UINT dpi = GetDpiForWindow(dialog);
      const HMONITOR monitor =
          MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST);
      MONITORINFO monitorInfo{sizeof(monitorInfo)};
      GetMonitorInfoW(monitor, &monitorInfo);
      const int workWidth =
          monitorInfo.rcWork.right - monitorInfo.rcWork.left;
      const int workHeight =
          monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
      limits->ptMinTrackSize.x =
          min(ScaleByDpi(620, dpi), max(1, workWidth - ScaleByDpi(12, dpi)));
      limits->ptMinTrackSize.y =
          min(ScaleByDpi(440, dpi), max(1, workHeight - ScaleByDpi(12, dpi)));
    }
    return TRUE;
  }
  case WM_MEASUREITEM: {
    auto *measure = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
    if (measure && measure->CtlType == ODT_LISTBOX &&
        measure->CtlID == IDC_GUIDE_TOPICS) {
      measure->itemHeight = ScaleByDpi(24, GetDpiForWindow(dialog));
      return TRUE;
    }
    break;
  }
  case WM_DRAWITEM: {
    const auto *draw = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
    if (!draw || draw->CtlType != ODT_LISTBOX ||
        draw->CtlID != IDC_GUIDE_TOPICS) {
      break;
    }
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    COLORREF background =
        selected ? g_themeColors.crAccent : g_themeColors.crControl;
    COLORREF text =
        selected ? g_themeColors.crAccentText : g_themeColors.crControlText;
    if (disabled) {
      text = BlendColor(text, background, 58);
    }
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(draw->hDC, &draw->rcItem, brush);
    DeleteObject(brush);
    if (draw->itemID != static_cast<UINT>(-1)) {
      const LRESULT length = SendMessageW(draw->hwndItem, LB_GETTEXTLEN,
                                          draw->itemID, 0);
      if (length >= 0 && length <= 4096) {
        std::wstring textValue(static_cast<size_t>(length) + 1, L'\0');
        if (SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID,
                         reinterpret_cast<LPARAM>(textValue.data())) !=
            LB_ERR) {
          HGDIOBJ oldFont = SelectObject(
              draw->hDC, reinterpret_cast<HFONT>(
                             SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0)));
          SetBkMode(draw->hDC, TRANSPARENT);
          SetTextColor(draw->hDC, text);
          RECT textRect = draw->rcItem;
          textRect.left += ScaleByDpi(7, GetDpiForWindow(dialog));
          textRect.right -= ScaleByDpi(5, GetDpiForWindow(dialog));
          DrawTextW(draw->hDC, textValue.c_str(), static_cast<int>(length),
                    &textRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                        DT_NOPREFIX);
          SelectObject(draw->hDC, oldFont);
        }
      }
    }
    if ((draw->itemState & (ODS_FOCUS | ODS_NOFOCUSRECT)) == ODS_FOCUS) {
      RECT focus = draw->rcItem;
      InflateRect(&focus, -ScaleByDpi(2, GetDpiForWindow(dialog)),
                  -ScaleByDpi(2, GetDpiForWindow(dialog)));
      DrawFocusRect(draw->hDC, &focus);
    }
    return TRUE;
  }
  case WM_COMMAND:
    if (!state)
      break;
    if (LOWORD(wParam) == IDC_GUIDE_TOPICS &&
        HIWORD(wParam) == LBN_SELCHANGE) {
      const LRESULT selected =
          SendDlgItemMessageW(dialog, IDC_GUIDE_TOPICS, LB_GETCURSEL, 0, 0);
      if (selected != LB_ERR &&
          static_cast<size_t>(selected) < state->visibleTopics.size()) {
        state->position = static_cast<size_t>(selected);
        RefreshGuidePage(dialog, *state);
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDC_GUIDE_BACK) {
      if (state->position > 0) {
        --state->position;
        RefreshGuidePage(dialog, *state);
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDC_GUIDE_QUICK_TOUR) {
      if (!state->initialWelcome || PersistGuideWelcomeHandled(*state)) {
        state->launchQuickTour = true;
        EndDialog(dialog, IDC_GUIDE_QUICK_TOUR);
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDOK) {
      const size_t topicIndex = state->visibleTopics[state->position];
      if (PersistGuideTopicRead(*state, topicIndex)) {
        PopulateGuideTopicList(dialog, *state);
        if (state->position + 1 < state->visibleTopics.size()) {
          ++state->position;
          RefreshGuidePage(dialog, *state);
        } else {
          EndDialog(dialog, IDOK);
        }
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      PersistGuideWelcomeHandled(*state);
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    break;
  case WM_CLOSE:
    if (state)
      PersistGuideWelcomeHandled(*state);
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  case WM_APP_DISMISS_GUIDE:
    if (state)
      state->externallyDismissed = true;
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  case WM_NCDESTROY:
    if (g_hGuideDialog == dialog)
      g_hGuideDialog = nullptr;
    break;
  case WM_DESTROY:
    if (state) {
      if (state->font) {
        DeleteObject(state->font);
        state->font = nullptr;
      }
      if (state->strongFont) {
        DeleteObject(state->strongFont);
        state->strongFont = nullptr;
      }
    }
    break;
  }
  return FALSE;
}

static void ShowGuidedWalkthrough(bool whatsNewOnly, bool initialWelcome) {
  if (g_hGuideDialog && IsWindow(g_hGuideDialog)) {
    SetForegroundWindow(g_hGuideDialog);
    return;
  }
  if (!g_bUiEnabled || g_bDefaultProfileUiBusy ||
      g_isLaunchInFlight.load() || g_bArchiveTaskInProgress ||
      g_isShuttingDown.load()) {
    if (!initialWelcome) {
      MessageBoxW(g_hGui,
                  L"Finish the current client operation, then open the guide "
                  L"again.",
                  L"Guided Walkthrough", MB_OK | MB_ICONINFORMATION);
    }
    return;
  }

  GuideDialogState state;
  state.initialWelcome = initialWelcome;
  state.visibleTopics = whatsNewOnly
                            ? guided_walkthrough::UnreadAnnouncementIndices(
                                  g_guideState)
                            : guided_walkthrough::AllTopicIndices();
  if (state.visibleTopics.empty()) {
    const int choice = MessageBoxW(
        g_hGui,
        L"You're up to date. There are no unread announced guide topics.\n\n"
        L"Open the full guided walkthrough?",
        L"What's New", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
    if (choice == IDYES)
      ShowGuidedWalkthrough(false, false);
    return;
  }

  DPI_AWARENESS_CONTEXT parentContext = GetWindowDpiAwarenessContext(g_hGui);
  DPI_AWARENESS_CONTEXT previousContext =
      SetThreadDpiAwarenessContext(parentContext);
  SetLastError(ERROR_SUCCESS);
  const INT_PTR result = DialogBoxParamW(
      g_hInst, MAKEINTRESOURCEW(IDD_GUIDED_WALKTHROUGH), g_hGui,
      GuideDlgProc, reinterpret_cast<LPARAM>(&state));
  const DWORD dialogError = GetLastError();
  SetThreadDpiAwarenessContext(previousContext);
  g_hGuideDialog = nullptr;
  if (result == -1) {
    std::wstring message =
        L"The guided walkthrough could not be opened. No guide progress was "
        L"changed.";
    if (dialogError)
      message += std::format(L"\n\nWindows error: {}.", dialogError);
    MessageBoxW(g_hGui, message.c_str(), L"Guided Walkthrough Unavailable",
                MB_OK | MB_ICONERROR);
    return;
  }
  if (state.stateChanged)
    UpdateGuideIndicators();
  if (state.launchQuickTour)
    ShowQuickTour();
}

enum class QuickTourTarget {
  ClientField,
  BrowserSelector,
  PrimaryAction,
  PinnedClients,
  SessionTabs,
  ClientIcon,
  RestoreTabs,
  Temporary,
  Options,
};

struct QuickTourStep {
  const wchar_t *title;
  const wchar_t *body;
  QuickTourTarget target;
};

static constexpr std::array<QuickTourStep, 9> kQuickTourSteps{{
    {L"Choose or name a client",
     L"Use the CLIENT field to choose an existing client or type a new name. "
     L"The tour only points to controls; it never changes this field.",
     QuickTourTarget::ClientField},
    {L"Choose a browser",
     L"The browser selector chooses Edge, Chrome, Brave, or Firefox for the "
     L"current action. Each client keeps a separate slot for each browser.",
     QuickTourTarget::BrowserSelector},
    {L"Create, Open, or Show",
     L"This primary button reflects the current selection. It can create a new "
     L"slot, open an existing one, or show an already-open window.",
     QuickTourTarget::PrimaryAction},
    {L"Pin favorite clients",
     L"The pushpin adds or removes the selected existing client from favorites. "
     L"Pinned clients appear in the row above the CLIENT field.",
     QuickTourTarget::PinnedClients},
    {L"Switch between open sessions",
     L"Open client windows appear as tabs across the top. Select a tab to show "
     L"its window, use x to close it normally, or use overflow for extra tabs.",
     QuickTourTarget::SessionTabs},
    {L"Open the client folder",
     L"For an existing client, the icon at the left of the CLIENT field opens "
     L"its folder in File Explorer. Do not edit profile files while a browser is open.",
     QuickTourTarget::ClientIcon},
    {L"Choose whether to restore tabs",
     L"Restore tabs is saved separately for the selected client and browser. "
     L"Changing it does not erase cookies, sign-ins, or browsing data.",
     QuickTourTarget::RestoreTabs},
    {L"Open a temporary profile",
     L"Temporary opens a disposable profile that ctSpaces removes after it "
     L"closes when Windows releases its files. Do not store important work there.",
     QuickTourTarget::Temporary},
    {L"Open Options",
     L"Options contains icons, client management, backup and restore, themes, "
     L"browser selection, the full guide, and this Quick tour.",
     QuickTourTarget::Options},
}};

struct QuickTourState {
  size_t position = 0;
  bool mainWasEnabled = false;
  bool restoreFocus = false;
  bool ownerClosing = false;
  bool closing = false;
  HFONT font = nullptr;
  HFONT strongFont = nullptr;
};

static QuickTourState g_quickTourState;
static constexpr wchar_t kQuickTourFrameClass[] =
    L"ctSpacesQuickTourHighlight";

static bool QuickTourOperationsAllowEnable() {
  return g_bUiEnabled && !g_bDefaultProfileUiBusy &&
         !g_isLaunchInFlight.load() && !g_bArchiveTaskInProgress &&
         !g_isShuttingDown.load();
}

static void DeleteQuickTourFonts() {
  if (g_quickTourState.font) {
    DeleteObject(g_quickTourState.font);
    g_quickTourState.font = nullptr;
  }
  if (g_quickTourState.strongFont) {
    DeleteObject(g_quickTourState.strongFont);
    g_quickTourState.strongFont = nullptr;
  }
}

static LRESULT CALLBACK QuickTourFrameProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_NCHITTEST:
    return HTTRANSPARENT;
  case WM_ERASEBKGND:
    return TRUE;
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    HBRUSH brush = CreateSolidBrush(g_themeColors.crAccent);
    FillRect(dc, &paint.rcPaint, brush);
    DeleteObject(brush);
    EndPaint(window, &paint);
    return 0;
  }
  default:
    return DefWindowProcW(window, message, wParam, lParam);
  }
}

static bool EnsureQuickTourFrameClass() {
  WNDCLASSEXW existing{sizeof(existing)};
  if (GetClassInfoExW(g_hInst, kQuickTourFrameClass, &existing))
    return true;
  WNDCLASSEXW windowClass{sizeof(windowClass)};
  windowClass.lpfnWndProc = QuickTourFrameProc;
  windowClass.hInstance = g_hInst;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kQuickTourFrameClass;
  return RegisterClassExW(&windowClass) != 0;
}

static bool MainClientRectToScreen(const RECT &clientRect, RECT &screenRect) {
  if (!g_hGui || !IsWindow(g_hGui) || clientRect.right <= clientRect.left ||
      clientRect.bottom <= clientRect.top) {
    return false;
  }
  POINT points[2]{{clientRect.left, clientRect.top},
                  {clientRect.right, clientRect.bottom}};
  SetLastError(ERROR_SUCCESS);
  if (!MapWindowPoints(g_hGui, nullptr, points, 2)) {
    const DWORD error = GetLastError();
    if (error != ERROR_SUCCESS)
      return false;
  }
  screenRect = {points[0].x, points[0].y, points[1].x, points[1].y};
  return true;
}

static bool QuickTourWindowRect(HWND window, RECT &rect) {
  return window && IsWindow(window) && IsWindowVisible(window) &&
         GetWindowRect(window, &rect) && rect.right > rect.left &&
         rect.bottom > rect.top;
}

static bool GetQuickTourTargetRect(QuickTourTarget target, RECT &rect) {
  switch (target) {
  case QuickTourTarget::ClientField:
    return QuickTourWindowRect(g_hClientEditSurface, rect);
  case QuickTourTarget::BrowserSelector:
    return MainClientRectToScreen(g_rcBrowserSelector, rect);
  case QuickTourTarget::PrimaryAction:
    return QuickTourWindowRect(g_hBtnGo, rect);
  case QuickTourTarget::PinnedClients:
    BuildPinnedClientRects();
    if (!g_pinnedClients.empty() &&
        MainClientRectToScreen(g_rcPinnedArea, rect)) {
      return true;
    }
    return QuickTourWindowRect(g_hBtnPin, rect);
  case QuickTourTarget::SessionTabs:
    BuildSessionTabRects(g_hGui, nullptr);
    if (g_sessions.empty() && !g_sessionTabRects.empty() &&
        MainClientRectToScreen(g_sessionTabRects[0], rect)) {
      return true;
    }
    for (size_t index = 1; index < g_sessionTabRects.size(); ++index) {
      if (MainClientRectToScreen(g_sessionTabRects[index], rect))
        return true;
    }
    return MainClientRectToScreen(g_rcSessionTabs, rect);
  case QuickTourTarget::ClientIcon:
    return QuickTourWindowRect(g_hBtnClientIcon, rect);
  case QuickTourTarget::RestoreTabs:
    return QuickTourWindowRect(g_hBtnRestoreTabs, rect);
  case QuickTourTarget::Temporary:
    return QuickTourWindowRect(g_hBtnTmpProf, rect);
  case QuickTourTarget::Options:
    return QuickTourWindowRect(g_hBtnConfig, rect);
  }
  return false;
}

static bool RectsOverlap(const RECT &first, const RECT &second) {
  RECT intersection{};
  return IntersectRect(&intersection, &first, &second) != FALSE;
}

static RECT ClampRectToWorkArea(RECT rect, const RECT &work) {
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  rect.left = min(max(rect.left, work.left), max(work.left, work.right - width));
  rect.top = min(max(rect.top, work.top), max(work.top, work.bottom - height));
  rect.right = rect.left + width;
  rect.bottom = rect.top + height;
  return rect;
}

static RECT ChooseQuickTourCardRect(const RECT &target, int width, int height,
                                    int gap, const RECT &work) {
  const int centeredTop = (target.top + target.bottom - height) / 2;
  const int centeredLeft = (target.left + target.right - width) / 2;
  const std::array<RECT, 4> candidates{{
      {target.right + gap, centeredTop, target.right + gap + width,
       centeredTop + height},
      {target.left - gap - width, centeredTop, target.left - gap,
       centeredTop + height},
      {centeredLeft, target.bottom + gap, centeredLeft + width,
       target.bottom + gap + height},
      {centeredLeft, target.top - gap - height, centeredLeft + width,
       target.top - gap},
  }};
  for (const RECT &candidate : candidates) {
    if (candidate.left >= work.left && candidate.top >= work.top &&
        candidate.right <= work.right && candidate.bottom <= work.bottom &&
        !RectsOverlap(candidate, target)) {
      return candidate;
    }
  }
  for (const RECT &candidate : candidates) {
    RECT clamped = ClampRectToWorkArea(candidate, work);
    if (!RectsOverlap(clamped, target))
      return clamped;
  }
  return ClampRectToWorkArea(candidates[0], work);
}

static std::wstring QuickTourBodyForPosition(size_t position) {
  if (position >= kQuickTourSteps.size())
    return L"";
  std::wstring body = kQuickTourSteps[position].body;
  if (kQuickTourSteps[position].target == QuickTourTarget::PinnedClients) {
    if (g_pinnedClients.empty()) {
      return L"No clients are pinned right now, so the real pushpin is "
             L"highlighted. It adds or removes the selected existing client "
             L"from favorites; the tour will not create a fake client.";
    }
    return L"Click a pinned client to open it in the selected browser or show "
           L"its existing window. Right-click a visible pin for Select, Open, "
           L"Restore tabs, copied-link, and shortcut actions. The pushpin beside "
           L"Create/Open adds or removes the selected existing client.";
  }
  if (kQuickTourSteps[position].target == QuickTourTarget::SessionTabs &&
      g_sessions.empty()) {
    body += L" No client browser is open right now, so the real New tab is "
            L"highlighted.";
  }
  return body;
}

static void LayoutQuickTourDialog(HWND dialog, UINT dpi) {
  if (!dialog)
    return;
  if (!dpi)
    dpi = USER_DEFAULT_SCREEN_DPI;
  RECT client{};
  GetClientRect(dialog, &client);
  const int margin = ScaleByDpi(16, dpi);
  const int titleHeight = ScaleByDpi(28, dpi);
  const int footerHeight = ScaleByDpi(30, dpi);
  const int gap = ScaleByDpi(8, dpi);
  const int footerTop = client.bottom - margin - footerHeight;
  const int buttonWidth = ScaleByDpi(72, dpi);
  const int skipWidth = ScaleByDpi(76, dpi);
  const int skipLeft = client.right - margin - skipWidth;
  const int nextLeft = skipLeft - gap - buttonWidth;
  const int backLeft = nextLeft - gap - buttonWidth;

  MoveWindow(GetDlgItem(dialog, IDC_QUICK_TOUR_TITLE), margin, margin,
             max(1, client.right - margin * 2), titleHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_QUICK_TOUR_BODY), margin,
             margin + titleHeight,
             max(1, client.right - margin * 2),
             max(1, footerTop - gap - margin - titleHeight), TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_QUICK_TOUR_COUNT), margin, footerTop,
             max(1, backLeft - gap - margin), footerHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDC_QUICK_TOUR_BACK), backLeft, footerTop,
             buttonWidth, footerHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDOK), nextLeft, footerTop, buttonWidth,
             footerHeight, TRUE);
  MoveWindow(GetDlgItem(dialog, IDCANCEL), skipLeft, footerTop, skipWidth,
             footerHeight, TRUE);

  DeleteQuickTourFonts();
  g_quickTourState.font = CreateUiFont(dpi);
  g_quickTourState.strongFont = CreateUiStrongFont(dpi);
  EnumChildWindows(
      dialog,
      [](HWND child, LPARAM) -> BOOL {
        const HFONT font =
            GetDlgCtrlID(child) == IDC_QUICK_TOUR_TITLE
                ? g_quickTourState.strongFont
                : g_quickTourState.font;
        if (font)
          SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return TRUE;
      },
      0);
}

static void RefreshQuickTourPage() {
  if (!g_hQuickTourDialog || !IsWindow(g_hQuickTourDialog) ||
      g_quickTourState.position >= kQuickTourSteps.size()) {
    return;
  }
  const auto &step = kQuickTourSteps[g_quickTourState.position];
  SetDlgItemTextW(g_hQuickTourDialog, IDC_QUICK_TOUR_TITLE, step.title);
  const std::wstring body =
      QuickTourBodyForPosition(g_quickTourState.position);
  SetDlgItemTextW(g_hQuickTourDialog, IDC_QUICK_TOUR_BODY, body.c_str());
  const std::wstring count =
      std::format(L"{} / {}", g_quickTourState.position + 1,
                  kQuickTourSteps.size());
  SetDlgItemTextW(g_hQuickTourDialog, IDC_QUICK_TOUR_COUNT, count.c_str());
  EnableWindow(GetDlgItem(g_hQuickTourDialog, IDC_QUICK_TOUR_BACK),
               g_quickTourState.position > 0);
  SetDlgItemTextW(g_hQuickTourDialog, IDOK,
                  g_quickTourState.position + 1 == kQuickTourSteps.size()
                      ? L"Done"
                      : L"Next");
  SetDlgItemTextW(g_hQuickTourDialog, IDCANCEL, L"Skip");
  RefreshQuickTourPlacement();
}

static void RefreshQuickTourPlacement() {
  if (!g_hQuickTourDialog || !IsWindow(g_hQuickTourDialog) ||
      !g_hQuickTourFrame || !IsWindow(g_hQuickTourFrame) ||
      !g_hGui || !IsWindow(g_hGui) || IsIconic(g_hGui) ||
      g_quickTourState.position >= kQuickTourSteps.size()) {
    return;
  }

  RECT target{};
  if (!GetQuickTourTargetRect(
          kQuickTourSteps[g_quickTourState.position].target, target)) {
    return;
  }
  const UINT dpi = GetDpiForWindow(g_hGui);
  const int padding = ScaleByDpi(5, dpi);
  const int thickness = max(2, ScaleByDpi(3, dpi));
  InflateRect(&target, padding, padding);
  const int frameWidth = target.right - target.left;
  const int frameHeight = target.bottom - target.top;
  if (frameWidth <= thickness * 2 || frameHeight <= thickness * 2)
    return;

  HRGN outer = CreateRectRgn(0, 0, frameWidth, frameHeight);
  HRGN inner = CreateRectRgn(thickness, thickness, frameWidth - thickness,
                             frameHeight - thickness);
  if (outer && inner) {
    CombineRgn(outer, outer, inner, RGN_DIFF);
    if (SetWindowRgn(g_hQuickTourFrame, outer, TRUE) != 0)
      outer = nullptr; // The window owns a successfully assigned region.
  }
  if (outer)
    DeleteObject(outer);
  if (inner)
    DeleteObject(inner);

  SetWindowPos(g_hQuickTourFrame, nullptr, target.left, target.top,
               frameWidth, frameHeight,
               SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);

  HMONITOR monitor = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitorInfo{sizeof(monitorInfo)};
  if (!GetMonitorInfoW(monitor, &monitorInfo))
    return;
  const int cardWidth = min(ScaleByDpi(380, dpi),
                            monitorInfo.rcWork.right - monitorInfo.rcWork.left);
  const int cardHeight = min(ScaleByDpi(224, dpi),
                             monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
  const int gap = ScaleByDpi(12, dpi);
  RECT card = ChooseQuickTourCardRect(target, cardWidth, cardHeight, gap,
                                      monitorInfo.rcWork);
  SetWindowPos(g_hQuickTourDialog, nullptr, card.left, card.top,
               card.right - card.left, card.bottom - card.top,
               SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
  InvalidateRect(g_hQuickTourFrame, nullptr, TRUE);
}

static void FinishQuickTourCleanup() {
  const bool restoreFocus = g_quickTourState.restoreFocus;
  const bool enableMain =
      g_quickTourState.mainWasEnabled && !g_quickTourState.ownerClosing &&
      g_hGui && IsWindow(g_hGui) && QuickTourOperationsAllowEnable();
  if (g_hQuickTourFrame && IsWindow(g_hQuickTourFrame))
    DestroyWindow(g_hQuickTourFrame);
  g_hQuickTourFrame = nullptr;
  DeleteQuickTourFonts();
  g_quickTourState.position = 0;
  g_quickTourState.mainWasEnabled = false;
  g_quickTourState.restoreFocus = false;
  g_quickTourState.closing = false;
  if (enableMain) {
    EnableWindow(g_hGui, TRUE);
    if (restoreFocus) {
      SetActiveWindow(g_hGui);
      FocusClientEdit();
    }
  }
}

static void DismissQuickTour(bool restoreFocus) {
  g_quickTourState.restoreFocus = restoreFocus;
  if (g_quickTourState.closing)
    return;
  g_quickTourState.closing = true;
  if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog)) {
    DestroyWindow(g_hQuickTourDialog);
    return;
  }
  g_hQuickTourDialog = nullptr;
  FinishQuickTourCleanup();
}

static void DismissQuickTourForOwnerClosing() {
  g_quickTourState.ownerClosing = true;
  if ((g_hQuickTourDialog && IsWindow(g_hQuickTourDialog)) ||
      (g_hQuickTourFrame && IsWindow(g_hQuickTourFrame))) {
    DismissQuickTour(false);
  }
}

static INT_PTR CALLBACK QuickTourDlgProc(HWND dialog, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
  if (const auto themed =
          HandleCleanupDialogTheme(dialog, message, wParam, lParam)) {
    return *themed;
  }
  switch (message) {
  case WM_INITDIALOG: {
    g_hQuickTourDialog = dialog;
    HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_CTSPACES));
    if (icon) {
      SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(icon));
      SendMessageW(dialog, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(icon));
    }
    LayoutQuickTourDialog(dialog, GetDpiForWindow(dialog));
    ApplyCleanupDialogTheme(dialog);
    return TRUE;
  }
  case WM_SIZE:
    if (wParam != SIZE_MINIMIZED)
      LayoutQuickTourDialog(dialog, GetDpiForWindow(dialog));
    return TRUE;
  case WM_DPICHANGED: {
    const auto *suggested = reinterpret_cast<const RECT *>(lParam);
    if (suggested) {
      SetWindowPos(dialog, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
    LayoutQuickTourDialog(dialog, HIWORD(wParam));
    ApplyCleanupDialogTheme(dialog);
    RefreshQuickTourPlacement();
    return TRUE;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == IDC_QUICK_TOUR_BACK) {
      if (g_quickTourState.position > 0) {
        --g_quickTourState.position;
        RefreshQuickTourPage();
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDOK) {
      if (g_quickTourState.position + 1 < kQuickTourSteps.size()) {
        ++g_quickTourState.position;
        RefreshQuickTourPage();
      } else {
        DismissQuickTour(true);
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      DismissQuickTour(true);
      return TRUE;
    }
    break;
  case WM_CLOSE:
    DismissQuickTour(true);
    return TRUE;
  case WM_NCDESTROY:
    if (g_hQuickTourDialog == dialog)
      g_hQuickTourDialog = nullptr;
    FinishQuickTourCleanup();
    return FALSE;
  }
  return FALSE;
}

static void ShowQuickTour() {
  if (g_hQuickTourDialog && IsWindow(g_hQuickTourDialog)) {
    SetActiveWindow(g_hQuickTourDialog);
    return;
  }
  if (g_hGuideDialog && IsWindow(g_hGuideDialog)) {
    SetForegroundWindow(g_hGuideDialog);
    return;
  }
  if (!g_bUiEnabled || !g_hGui || !IsWindow(g_hGui) ||
      g_bDefaultProfileUiBusy || g_isLaunchInFlight.load() ||
      g_bArchiveTaskInProgress || g_isShuttingDown.load()) {
    MessageBoxW(g_hGui,
                L"Finish the current client operation, then open Quick tour "
                L"again.",
                L"Quick Tour", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (!EnsureQuickTourFrameClass()) {
    MessageBoxW(g_hGui,
                L"Quick tour could not create its highlight. No settings or "
                L"client data were changed.",
                L"Quick Tour Unavailable", MB_OK | MB_ICONERROR);
    return;
  }

  g_quickTourState = {};
  g_hQuickTourFrame = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
      kQuickTourFrameClass, L"", WS_POPUP, 0, 0, 0, 0, g_hGui, nullptr,
      g_hInst, nullptr);
  if (!g_hQuickTourFrame) {
    MessageBoxW(g_hGui,
                L"Quick tour could not create its highlight. No settings or "
                L"client data were changed.",
                L"Quick Tour Unavailable", MB_OK | MB_ICONERROR);
    return;
  }

  DPI_AWARENESS_CONTEXT parentContext = GetWindowDpiAwarenessContext(g_hGui);
  DPI_AWARENESS_CONTEXT previousContext =
      SetThreadDpiAwarenessContext(parentContext);
  HWND dialog = CreateDialogParamW(
      g_hInst, MAKEINTRESOURCEW(IDD_QUICK_TOUR), g_hGui,
      QuickTourDlgProc, 0);
  const DWORD dialogError = GetLastError();
  SetThreadDpiAwarenessContext(previousContext);
  if (!dialog) {
    DestroyWindow(g_hQuickTourFrame);
    g_hQuickTourFrame = nullptr;
    DeleteQuickTourFonts();
    std::wstring message =
        L"Quick tour could not open. No settings or client data were changed.";
    if (dialogError)
      message += std::format(L"\n\nWindows error: {}.", dialogError);
    MessageBoxW(g_hGui, message.c_str(), L"Quick Tour Unavailable",
                MB_OK | MB_ICONERROR);
    return;
  }

  g_hQuickTourDialog = dialog;
  g_quickTourState.mainWasEnabled = IsWindowEnabled(g_hGui) != FALSE;
  if (!g_quickTourState.mainWasEnabled) {
    DismissQuickTour(false);
    return;
  }
  EnableWindow(g_hGui, FALSE);
  RefreshQuickTourPage();
  ShowWindow(g_hQuickTourDialog, SW_SHOW);
  SetActiveWindow(g_hQuickTourDialog);
}

struct ThemeDlgState {
  HWND hCombo = nullptr;
  HWND hApply = nullptr;
  HWND hCancel = nullptr;
  int iInitialMode = 0;
  int iCurrentMode = 0;
  int iLastComboIndex = 0;
  int iPreviewedMode = 0;
  UINT_PTR iPreviewTimer = 0;
};

constexpr UINT_PTR THEME_PREVIEW_TIMER_ID = 0x5450;
constexpr UINT THEME_PREVIEW_DELAY_MS = 100;

static int ThemeComboIndexFromMode(int iMode) {
  if (iMode < 0)
    return 0;
  if (iMode < 3)
    return iMode;
  return g_iThemeSeparatorIndex + 1 + (iMode - 3);
}

static int ThemeModeFromComboIndex(int iSel) {
  if (iSel <= 2)
    return iSel;
  return 3 + (iSel - (g_iThemeSeparatorIndex + 1));
}

static INT_PTR CALLBACK ThemeDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
  auto relayout = [&](ThemeDlgState &aState, UINT iDpi) {
    const int iMargin = MulDiv(12, iDpi, 96);
    const int iGap = MulDiv(8, iDpi, 96);
    const int iComboH = MulDiv(24, iDpi, 96);
    const int iBtnW = MulDiv(84, iDpi, 96);
    const int iBtnH = MulDiv(28, iDpi, 96);
    const int iDlgW = MulDiv(360, iDpi, 96);
    const int iDlgH = MulDiv(140, iDpi, 96);

    RECT rcWnd{};
    GetWindowRect(hDlg, &rcWnd);
    SetWindowPos(hDlg, nullptr, rcWnd.left, rcWnd.top, iDlgW, iDlgH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rc{};
    GetClientRect(hDlg, &rc);

    const int iComboW = rc.right - iMargin * 2;
    const int iComboX = iMargin;
    const int iComboY = iMargin;
    MoveWindow(aState.hCombo, iComboX, iComboY, iComboW, iComboH, TRUE);

    const int iBtnY = rc.bottom - iMargin - iBtnH;
    const int iCancelX = rc.right - iMargin - iBtnW;
    const int iApplyX = iCancelX - iGap - iBtnW;
    MoveWindow(aState.hApply, iApplyX, iBtnY, iBtnW, iBtnH, TRUE);
    MoveWindow(aState.hCancel, iCancelX, iBtnY, iBtnW, iBtnH, TRUE);

    const int iItemHeight =
        (g_iComboItemHeight > 0) ? g_iComboItemHeight : MulDiv(20, iDpi, 96);
    SendMessageW(aState.hCombo, CB_SETITEMHEIGHT, (WPARAM)-1, iItemHeight);
    SendMessageW(aState.hCombo, CB_SETITEMHEIGHT, 0, iItemHeight);
  };

  ThemeDlgState *vState =
      reinterpret_cast<ThemeDlgState *>(GetWindowLongPtrW(hDlg, DWLP_USER));

  auto vCancelPreviewTimer = [&]() {
    if (!vState || !vState->iPreviewTimer)
      return;
    KillTimer(hDlg, THEME_PREVIEW_TIMER_ID);
    vState->iPreviewTimer = 0;
  };

  auto vSchedulePreview = [&]() {
    if (!vState)
      return;
    vCancelPreviewTimer();
    if (vState->iCurrentMode == vState->iPreviewedMode)
      return;
    vState->iPreviewTimer =
        SetTimer(hDlg, THEME_PREVIEW_TIMER_ID, THEME_PREVIEW_DELAY_MS, nullptr);
    if (!vState->iPreviewTimer) {
      ApplyTheme(vState->iCurrentMode, true);
      vState->iPreviewedMode = vState->iCurrentMode;
    }
  };

  switch (msg) {
  case WM_INITDIALOG: {
    const UINT iDpi = GetDpiForWindow(hDlg);
    auto *vStateNew = new ThemeDlgState{};
    vStateNew->iInitialMode = g_iThemeMode;
    vStateNew->iCurrentMode = g_iThemeMode;
    vStateNew->iPreviewedMode = g_iThemeMode;
    SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)vStateNew);
    vState = vStateNew;

    SetWindowTextW(hDlg, L"Themes");

    vState->hCombo = CreateWindowW(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
            CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0, 0, 10, 10, hDlg, (HMENU)IDC_THEME_COMBO, g_hInst, nullptr);
    g_hThemeCombo = vState->hCombo;
    ApplyComboTheme(vState->hCombo);

    SendMessageW(vState->hCombo, CB_ADDSTRING, 0, (LPARAM)L"System (Auto)");
    SendMessageW(vState->hCombo, CB_ADDSTRING, 0,
                 (LPARAM)L"System (Light, Default)");
    SendMessageW(vState->hCombo, CB_ADDSTRING, 0, (LPARAM)L"System (Dark)");
    SendMessageW(vState->hCombo, CB_ADDSTRING, 0, (LPARAM)L"----------");
    for (const auto &theme : g_aCustomThemes) {
      SendMessageW(vState->hCombo, CB_ADDSTRING, 0, (LPARAM)theme.name.c_str());
    }

    int iSel = ThemeComboIndexFromMode(vState->iCurrentMode);
    const int iCount = (int)SendMessageW(vState->hCombo, CB_GETCOUNT, 0, 0);
    if (iSel < 0 || iSel >= iCount)
      iSel = 0;
    vState->iLastComboIndex = iSel;
    SendMessageW(vState->hCombo, CB_SETCURSEL, (WPARAM)iSel, 0);

    vState->hApply = CreateWindowW(
        L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 10, 10, hDlg, (HMENU)IDC_THEME_APPLY, g_hInst, nullptr);
    vState->hCancel = CreateWindowW(
        L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 10, 10, hDlg, (HMENU)IDC_THEME_CANCEL, g_hInst, nullptr);

    if (g_hFont) {
      SendMessageW(vState->hCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
      SendMessageW(vState->hApply, WM_SETFONT, (WPARAM)g_hFont, TRUE);
      SendMessageW(vState->hCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    }

    relayout(*vState, iDpi);
    ApplyThemeToWindow(hDlg);
    return (INT_PTR)TRUE;
  }

  case WM_DPICHANGED: {
    const UINT iDpi = HIWORD(wParam);
    const RECT *rc = reinterpret_cast<RECT *>(lParam);
    if (rc) {
      SetWindowPos(hDlg, nullptr, rc->left, rc->top, rc->right - rc->left,
                   rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (vState)
      relayout(*vState, iDpi);
    return (INT_PTR)TRUE;
  }

  case WM_DRAWITEM: {
    const DRAWITEMSTRUCT *vDraw = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
    if (vDraw)
      DrawOwnerDrawItem(*vDraw);
    return (INT_PTR)TRUE;
  }

  case WM_MEASUREITEM: {
    MEASUREITEMSTRUCT *vMeasure = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
    if (vMeasure && vMeasure->CtlType == ODT_COMBOBOX) {
      const UINT iDpi = GetDpiForWindow(hDlg);
      vMeasure->itemHeight =
          (UINT)((g_iComboItemHeight > 0) ? g_iComboItemHeight
                                          : MulDiv(20, iDpi, 96));
      return (INT_PTR)TRUE;
    }
    break;
  }

  case WM_CTLCOLORDLG:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLORSTATIC:
    return (INT_PTR)HandleThemeCtlColor(msg, (HDC)wParam, (HWND)lParam);

  case WM_ERASEBKGND: {
    HDC hdc = (HDC)wParam;
    RECT rc{};
    GetClientRect(hDlg, &rc);
    FillRect(hdc, &rc,
             g_hbrThemeWindow ? g_hbrThemeWindow
                              : (HBRUSH)GetSysColorBrush(COLOR_BTNFACE));
    return (INT_PTR)TRUE;
  }

  case WM_TIMER:
    if (wParam == THEME_PREVIEW_TIMER_ID && vState) {
      vCancelPreviewTimer();
      if (vState->iCurrentMode != vState->iPreviewedMode) {
        ApplyTheme(vState->iCurrentMode, true);
        vState->iPreviewedMode = vState->iCurrentMode;
      }
      return (INT_PTR)TRUE;
    }
    break;

  case WM_COMMAND: {
    const int iId = LOWORD(wParam);
    const int iEvent = HIWORD(wParam);
    if (iId == IDC_THEME_COMBO && iEvent == CBN_SELCHANGE) {
      const int iSel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
      if (vState) {
        if (iSel < 0)
          return (INT_PTR)TRUE;
        if (iSel == g_iThemeSeparatorIndex) {
          SendMessageW(vState->hCombo, CB_SETCURSEL,
                       (WPARAM)vState->iLastComboIndex, 0);
          return (INT_PTR)TRUE;
        }
        vState->iLastComboIndex = iSel;
        const int iMode = ThemeModeFromComboIndex(iSel);
        if (iMode >= 3) {
          const int iCustom = iMode - 3;
          if (iCustom < 0 || iCustom >= (int)g_aCustomThemes.size()) {
            return (INT_PTR)TRUE;
          }
        }
        vState->iCurrentMode = iMode;
        vSchedulePreview();
      }
      return (INT_PTR)TRUE;
    }
    if (iId == IDC_THEME_APPLY) {
      if (vState) {
        vCancelPreviewTimer();
        ApplyTheme(vState->iCurrentMode, false);
        vState->iPreviewedMode = vState->iCurrentMode;
      }
      EndDialog(hDlg, IDOK);
      return (INT_PTR)TRUE;
    }
    if (iId == IDC_THEME_CANCEL || iId == IDCANCEL) {
      if (vState) {
        vCancelPreviewTimer();
        if (vState->iPreviewedMode != vState->iInitialMode)
          ApplyTheme(vState->iInitialMode, false);
      }
      EndDialog(hDlg, IDCANCEL);
      return (INT_PTR)TRUE;
    }
    break;
  }

  case WM_CLOSE:
    if (vState) {
      vCancelPreviewTimer();
      if (vState->iPreviewedMode != vState->iInitialMode)
        ApplyTheme(vState->iInitialMode, false);
    }
    EndDialog(hDlg, IDCANCEL);
    return (INT_PTR)TRUE;

  case WM_DESTROY:
    if (vState) {
      vCancelPreviewTimer();
      delete vState;
      SetWindowLongPtrW(hDlg, DWLP_USER, 0);
    }
    g_hThemeCombo = nullptr;
    break;
  }
  return (INT_PTR)FALSE;
}

static void ShowAboutDialog() {
  // Ensure the dialog is created under the same DPI awareness context as the
  // main window.
  DPI_AWARENESS_CONTEXT parentCtx = GetWindowDpiAwarenessContext(g_hGui);
  DPI_AWARENESS_CONTEXT oldCtx = SetThreadDpiAwarenessContext(parentCtx);

  DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_ABOUTBOX), g_hGui, AboutDlgProc);

  SetThreadDpiAwarenessContext(oldCtx);
  // DialogBoxW(g_hInst,MAKEINTRESOURCEW(IDD_ABOUTBOX),g_hGui,AboutDlgProc);
}

static void ShowThemeDialog() {
  DPI_AWARENESS_CONTEXT vParentCtx = GetWindowDpiAwarenessContext(g_hGui);
  DPI_AWARENESS_CONTEXT vOldCtx = SetThreadDpiAwarenessContext(vParentCtx);

  DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_THEME), g_hGui, ThemeDlgProc);

  if (g_bThemeMenuRefreshPending) {
    g_bThemeMenuRefreshPending = false;
    RebuildConfigMenuForTheme(g_hGui);
  }

  SetThreadDpiAwarenessContext(vOldCtx);
}
static HBITMAP IconToMenuBitmap(HICON hIcon, int cx, int cy) {
  if (!hIcon)
    return nullptr;

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = cx;
  bmi.bmiHeader.biHeight = -cy; // top-down DIB
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  HDC hdc = GetDC(nullptr);
  HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, hdc);
  if (!hbmp || !bits)
    return nullptr;

  ZeroMemory(bits, (size_t)cx * (size_t)cy * 4);

  HDC memDC = CreateCompatibleDC(nullptr);
  HGDIOBJ old = SelectObject(memDC, hbmp);
  DrawIconEx(memDC, 0, 0, hIcon, cx, cy, 0, nullptr, DI_NORMAL);
  SelectObject(memDC, old);
  DeleteDC(memDC);

  return hbmp;
}

static HICON LoadIconResSized(int iconResId, int cx, int cy) {
  return (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(iconResId), IMAGE_ICON, cx,
                           cy, LR_DEFAULTCOLOR);
}

static HICON LoadIconResScaled(int iconResId, int cx, int cy) {
  HICON h = nullptr;
  if (SUCCEEDED(LoadIconWithScaleDown(g_hInst, MAKEINTRESOURCEW(iconResId), cx,
                                      cy, &h)) &&
      h)
    return h;
  return (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(iconResId), IMAGE_ICON, cx,
                           cy, LR_DEFAULTCOLOR);
}

static bool IsGuideFeatureCommandNew(UINT command) {
  const auto &topics = guided_walkthrough::Catalog();
  for (size_t index = 0; index < topics.size(); ++index) {
    if (topics[index].announce && topics[index].targetCommand == command &&
        guided_walkthrough::IsUnread(g_guideState, index)) {
      return true;
    }
  }
  return false;
}

static void MenuAddItemOD(HMENU hMenu, UINT id, const wchar_t *text,
                          HICON hIcon, bool checked, bool disabled,
                          HMENU hSubMenu = nullptr) {
  bool isSub = (hSubMenu != nullptr);
  const bool newDot = IsGuideFeatureCommandNew(id);
  std::wstring displayText = text ? text : L"";
  if (newDot)
    displayText += L" (New)";
  MenuItemData *vData =
      AddMenuItemData(displayText.c_str(), hIcon, false, isSub, newDot);

  MENUITEMINFOW mi{};
  mi.cbSize = sizeof(mi);
  mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA | MIIM_STATE;

  if (hSubMenu) {
    mi.fMask |= MIIM_SUBMENU;
    mi.hSubMenu = hSubMenu;
  }

  mi.wID = id;
  mi.fType = MFT_OWNERDRAW;
  mi.dwItemData = (ULONG_PTR)vData;
  mi.dwTypeData = displayText.data();

  mi.fState = checked ? MFS_CHECKED : MFS_UNCHECKED;
  if (disabled)
    mi.fState |= (MFS_DISABLED | MFS_GRAYED);

  InsertMenuItemW(hMenu, (UINT)-1, TRUE, &mi);
}

static void MenuAddItemBmp(HMENU hMenu, UINT id, const wchar_t *text,
                           HBITMAP hbmp, HICON hIcon, bool newDot = false) {
  // Legacy helper wrapper
  newDot = newDot || IsGuideFeatureCommandNew(id);
  std::wstring displayText = text ? text : L"";
  if (newDot && displayText.find(L"(New)") == std::wstring::npos)
    displayText += L" (New)";
  MenuItemData *vData =
      AddMenuItemData(displayText.c_str(), hIcon, false, false, newDot);
  MENUITEMINFOW mi{};
  mi.cbSize = sizeof(mi);
  mi.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_FTYPE | MIIM_DATA;
  mi.wID = id;
  mi.dwTypeData = displayText.data();
  mi.hbmpItem = hbmp;
  mi.fType = MFT_OWNERDRAW;
  mi.dwItemData = (ULONG_PTR)vData;
  InsertMenuItemW(hMenu, (UINT)-1, TRUE, &mi);
}

static void MenuAddSep(HMENU hMenu) {
  MenuItemData *vData = AddMenuItemData(L"", nullptr, true);
  MENUITEMINFOW mi{};
  mi.cbSize = sizeof(mi);
  mi.fMask = MIIM_FTYPE | MIIM_DATA;
  mi.fType = MFT_OWNERDRAW | MFT_SEPARATOR;
  mi.dwItemData = (ULONG_PTR)vData;
  InsertMenuItemW(hMenu, (UINT)-1, TRUE, &mi);
}
static void EnsureConfigMenu(HWND hWnd) {
  if (g_hConfigMenu)
    return;

  g_hConfigMenu = CreatePopupMenu();

  const UINT dpi = GetDpiForWindow(hWnd);
  PreloadThemeIcons(dpi);

  // pick a logical size you like for menu icons
  const int iconPx = MulDiv(16, dpi, 96);
  g_iMenuIconPx = iconPx;

  MENUINFO mnuInfo{};
  mnuInfo.cbSize = sizeof(mnuInfo);
  // MIM_APPLYTOSUBMENUS forces the style/brush to cascade
  mnuInfo.fMask = MIM_STYLE | MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
  mnuInfo.dwStyle = MNS_CHECKORBMP | MNS_NOCHECK;

  mnuInfo.hbrBack =
      g_hbrThemeMenu ? g_hbrThemeMenu : GetSysColorBrush(COLOR_MENU);
  SetMenuInfo(g_hConfigMenu, &mnuInfo);

  auto mkbmp = [&](HICON hIcon) -> HBITMAP {
    HBITMAP b = IconToMenuBitmap(hIcon, iconPx, iconPx);
    g_menuBitmaps.push_back(b);
    return b;
  };

  const int iIcoSet = g_bThemeIsDark ? IDI_AICOW : IDI_AICOB;
  const int iIcoRefresh = g_bThemeIsDark ? IDI_UPRFW : IDI_UPRFB;
  const int iIcoReset = g_bThemeIsDark ? IDI_NUKEW : IDI_NUKEB;
  const int iIcoVacuum = g_bThemeIsDark ? IDI_VACW : IDI_VACB;
  const int iIcoDelete = g_bThemeIsDark ? IDI_DPRFW : IDI_DPRFB;
  const int iIcoEdit = g_bThemeIsDark ? IDI_EDPFW : IDI_EDPFB;
  const int iIcoAbout = g_bThemeIsDark ? IDI_INFOW : IDI_INFOB;

  // Precompute minimum menu width from text
  g_iMenuMinWidth = 0;
  {
    const wchar_t *aTexts[] = {L"Set Profile Icon",
                               L"Create Desktop Shortcut",
                               L"Rename Client",
                               L"Archive Client",
                               L"Archived Clients",
                               L"Refresh Profile",
                               L"Reset Profile",
                               L"Delete Profile",
                               L"Clean Up Inactive Clients...",
                               L"Delete Multiple Clients...",
                               L"Edit Default profile",
                               L"Themes",
                               L"Client name first in window titles",
                               L"About",
                               L"Browser Selection",
                               L"Microsoft Edge",
                               L"Google Chrome",
                               L"Back Up All Client Data",
                               L"Restore Client Data",
                               L"Quick tour",
                               L"Guided walkthrough (New)",
                               L"What's new (New)"};
    HDC hdc = GetDC(nullptr);
    HFONT hOld = (HFONT)SelectObject(
        hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
    SIZE sz{};
    int iMaxText = 0;
    for (const auto *txt : aTexts) {
      if (!txt)
        continue;
      GetTextExtentPoint32W(hdc, txt, (int)wcslen(txt), &sz);
      iMaxText = max(iMaxText, sz.cx);
    }
    SelectObject(hdc, hOld);
    ReleaseDC(nullptr, hdc);
    const int iPad = MulDiv(4, dpi, 96);
    const int iIconPad = MulDiv(8, dpi, 96);
    g_iMenuMinWidth =
        iPad + iconPx + iIconPad + iMaxText + iPad + MulDiv(32, dpi, 96);
  }

  auto addItem = [&](UINT id, const wchar_t *text, int iconResId,
                     bool newDot = false) {
    const bool broom = iconResId == IDI_VACW || iconResId == IDI_VACB;
    HICON hIcon = broom ? nullptr : LoadIconResBestDownscale(g_hInst, iconResId, iconPx, iconPx);
    MenuAddItemBmp(g_hConfigMenu, id, text, mkbmp(hIcon), hIcon, newDot);
    g_aMenuItemData.back()->broomGlyph = broom;
  };

  addItem(IDM_CTX_SET_PROFILE_ICON, L"Set Profile Icon", iIcoSet);
  addItem(IDM_CTX_REMOVE_ICON, L"Remove Custom Icon", iIcoDelete);
  addItem(IDM_CTX_CREATE_SHORTCUT, L"Create Desktop Shortcut", iIcoAbout);
  MenuAddSep(g_hConfigMenu);
  addItem(IDM_CTX_RENAME_PROFILE, L"Rename Client", iIcoEdit);
  addItem(IDM_CTX_ARCHIVE_PROFILE, L"Archive Client", iIcoAbout);
  addItem(IDM_CTX_RESET_PROFILE, L"Reset (Nuke)", iIcoReset);
  addItem(IDM_CTX_VACUUM_PROFILE, L"Vacuum (Clear Cache)", iIcoVacuum);
  addItem(IDM_CTX_FETCH_ICON, L"Auto-fetch Icon", iIcoSet);
  addItem(IDM_CTX_DELETE_PROFILE, L"Delete Profile", iIcoDelete);
  addItem(IDM_CTX_CLEANUP_INACTIVE, L"Clean Up Inactive Clients...", iIcoVacuum);
  addItem(IDM_CTX_DELETE_MULTIPLE, L"Delete Multiple Clients...", iIcoDelete);
  MenuAddSep(g_hConfigMenu);
  addItem(IDM_CTX_ARCHIVED_CLIENTS, L"Archived Clients", iIcoRefresh);
  addItem(IDM_CTX_EDIT_DEFAULT_PROFILE, L"Edit Default profile", iIcoEdit);
  MenuAddSep(g_hConfigMenu);
  {
    HICON hColor = nullptr;
    if (g_bThemeIsDark && g_hIconColorDark)
      hColor = CopyIcon(g_hIconColorDark);
    if (!hColor && g_hIconColorLight)
      hColor = CopyIcon(g_hIconColorLight);
    if (!hColor)
      hColor = LoadIconResBestDownscale(
          g_hInst, g_bThemeIsDark ? IDI_COLRW : IDI_COLRB, iconPx, iconPx);
    MenuAddItemBmp(g_hConfigMenu, IDM_CTX_THEME_COLOR, L"Themes",
                   GetThemeColorMenuBitmap(), hColor);
  }
  MenuAddItemOD(g_hConfigMenu, IDM_CTX_CLIENT_TITLE_FIRST,
                L"Client name first in window titles", nullptr,
                g_bClientTitleFirst.load(), false);

  MenuAddSep(g_hConfigMenu);

  // Browser selection - Moved UP before Export/About
  {
    HMENU hSubBrowser = CreatePopupMenu();
    SetMenuInfo(hSubBrowser, &mnuInfo); // Apply same background/style

    MenuAddItemOD(hSubBrowser, IDM_BROWSER_EDGE, L"Microsoft Edge", nullptr,
                  (g_selectedBrowser == BrowserKind::Edge),
                  g_sEdgePath.empty());
    MenuAddItemOD(hSubBrowser, IDM_BROWSER_CHROME, L"Google Chrome", nullptr,
                  (g_selectedBrowser == BrowserKind::Chrome),
                  g_sChromePath.empty());
    MenuAddItemOD(hSubBrowser, IDM_BROWSER_BRAVE, L"Brave Browser", nullptr,
                  (g_selectedBrowser == BrowserKind::Brave),
                  g_sBravePath.empty());
    MenuAddItemOD(hSubBrowser, IDM_BROWSER_FIREFOX, L"Mozilla Firefox", nullptr,
                  (g_selectedBrowser == BrowserKind::Firefox),
                  g_sFirefoxPath.empty());

    // WORKAROUND: Create as strictly Owner Draw Item first (no submenu)
    // Then attach submenu. This forces Windows to treat it as OD.
    HICON hBrowserIcon =
        LoadIconResBestDownscale(g_hInst, IDI_BROWSER, iconPx, iconPx);
    MenuItemData *vData =
        AddMenuItemData(L"Browser Selection", hBrowserIcon, false, true);

    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    // Step 1: Insert as flat OD item
    mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA | MIIM_STATE;
    mi.wID = 1000;
    mi.fType = MFT_OWNERDRAW;
    mi.dwItemData = (ULONG_PTR)vData;
    mi.fState = MFS_UNCHECKED;

    InsertMenuItemW(g_hConfigMenu, (UINT)-1, TRUE, &mi);

    // Step 2: Attach Submenu
    int pos = GetMenuItemCount(g_hConfigMenu) - 1;
    MENUITEMINFOW miSub{};
    miSub.cbSize = sizeof(miSub);
    miSub.fMask = MIIM_SUBMENU;
    miSub.hSubMenu = hSubBrowser;

    SetMenuItemInfoW(g_hConfigMenu, pos, TRUE, &miSub);

    g_hBrowserSubMenu = hSubBrowser;
  }

  MenuAddSep(g_hConfigMenu);
  addItem(IDM_CTX_EXPORT_ALL, L"Back Up All Client Data", iIcoAbout);
  addItem(IDM_CTX_RESTORE_ALL, L"Restore Client Data", iIcoRefresh);
  MenuAddSep(g_hConfigMenu);
  const bool guideNew = guided_walkthrough::HasUnreadAnnouncement(g_guideState);
  addItem(IDM_QUICK_TOUR, L"Quick tour", iIcoAbout);
  addItem(IDM_GUIDED_WALKTHROUGH, L"Guided walkthrough", iIcoAbout);
  addItem(IDM_WHATS_NEW, L"What's new", iIcoAbout, guideNew);
  MenuAddSep(g_hConfigMenu);
  addItem(IDM_ABOUT, L"About", iIcoAbout);
}

static UINT ShowConfigMenuFromButton(HWND hWnd) {
  EnsureConfigMenu(hWnd);
  UpdateConfigMenuEnabledState();
  HideMenuTooltip();
  RECT rc{};
  GetWindowRect(g_hBtnConfig, &rc);

  SetForegroundWindow(hWnd);

  UINT cmd = TrackPopupMenuEx(g_hConfigMenu,
                              TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                              rc.left, rc.bottom, hWnd, nullptr);
  HideMenuTooltip();
  PostMessageW(hWnd, WM_NULL, 0, 0); // allow menu to dismiss cleanly
  return cmd;
}

static UINT ShowBrowserMenuFromSelector(HWND hWnd) {
  EnsureConfigMenu(hWnd);
  UpdateConfigMenuEnabledState();
  HideMenuTooltip();
  if (!g_hBrowserSubMenu)
    return 0;

  POINT menuPoint{g_rcBrowserSelector.left, g_rcBrowserSelector.bottom};
  ClientToScreen(hWnd, &menuPoint);
  SetForegroundWindow(hWnd);
  const UINT command = TrackPopupMenuEx(
      g_hBrowserSubMenu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
      menuPoint.x, menuPoint.y, hWnd, nullptr);
  HideMenuTooltip();
  PostMessageW(hWnd, WM_NULL, 0, 0);
  return command;
}

static void SetButtonIcon(HWND hBtn, int iconResId, HICON &hStore) {
  if (!hBtn)
    return;

  RECT rc{};
  GetClientRect(hBtn, &rc);

  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;

  const UINT dpi = GetDpiForWindow(hBtn);
  const int pad = MulDiv(2, dpi, 96);
  const int s = max(16, min(w, h) - pad); // requested size

  HICON hNew = LoadIconResBestDownscale(g_hInst, iconResId, s, s);

  if (!hNew)
    return;

  HICON hOld = hStore;
  hStore = hNew;
  SendMessageW(hBtn, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hStore);
  if (hOld && hOld != hStore)
    DestroyIcon(hOld);
}

static bool GetIconSizePx(HICON hIcon, int &w, int &h) {
  w = h = 0;
  ICONINFO ii{};
  if (!GetIconInfo(hIcon, &ii))
    return false;

  BITMAP bm{};
  bool ok = false;
  if (ii.hbmColor && GetObject(ii.hbmColor, sizeof(bm), &bm) == sizeof(bm)) {
    w = bm.bmWidth;
    h = bm.bmHeight;
    ok = true;
  } else if (ii.hbmMask &&
             GetObject(ii.hbmMask, sizeof(bm), &bm) == sizeof(bm)) {
    w = bm.bmWidth;
    h = bm.bmHeight / 2;
    ok = true;
  }

  if (ii.hbmColor)
    DeleteObject(ii.hbmColor);
  if (ii.hbmMask)
    DeleteObject(ii.hbmMask);
  return ok;
}

static HICON LoadIconResBestDownscale(HINSTANCE hInst, int groupIconResId,
                                      int cxDesired, int cyDesired) {
  const int want = max(cxDesired, cyDesired);
  if (want <= 0)
    return nullptr;

  HRSRC hGrpRes =
      FindResourceW(hInst, MAKEINTRESOURCEW(groupIconResId), RT_GROUP_ICON);
  if (!hGrpRes)
    return nullptr;

  HGLOBAL hGrp = LoadResource(hInst, hGrpRes);
  if (!hGrp)
    return nullptr;

  const auto *grp = (const GRPICONDIR *)LockResource(hGrp);
  if (!grp || grp->idReserved != 0 || grp->idType != 1 || grp->idCount == 0)
    return nullptr;

  const GRPICONDIRENTRY *bestGE = nullptr;
  int bestGES = (std::numeric_limits<int>::max)();

  const GRPICONDIRENTRY *bestBig = nullptr;
  int bestBigS = 0;

  for (int i = 0; i < (int)grp->idCount; i++) {
    const auto &e = grp->idEntries[i];
    const int s = max(IcoDim(e.bWidth), IcoDim(e.bHeight));

    if (s >= want && s < bestGES) {
      bestGES = s;
      bestGE = &e;
    }
    if (s > bestBigS) {
      bestBigS = s;
      bestBig = &e;
    }
  }

  const GRPICONDIRENTRY *pick = bestGE ? bestGE : bestBig;
  if (!pick)
    return nullptr;

  const int src = max(IcoDim(pick->bWidth), IcoDim(pick->bHeight));

  HRSRC hIconRes = FindResourceW(hInst, MAKEINTRESOURCEW(pick->nID), RT_ICON);
  if (!hIconRes)
    return nullptr;

  DWORD cb = SizeofResource(hInst, hIconRes);
  HGLOBAL hBits = LoadResource(hInst, hIconRes);
  if (!hBits || cb == 0)
    return nullptr;

  BYTE *pBits = (BYTE *)LockResource(hBits);
  if (!pBits)
    return nullptr;

  // IMPORTANT: create at native size (0,0) so we do NOT let this API
  // resample.
  HICON hNative = CreateIconFromResourceEx(pBits, cb, TRUE, 0x00030000, 0, 0,
                                           LR_DEFAULTCOLOR);
  if (!hNative)
    return nullptr;

  // If exact match (or smaller-only), return as-is (no upscale).
  if (src <= want)
    return hNative;

  // Downscale only.
  HICON hScaled = (HICON)CopyImage(hNative, IMAGE_ICON, want, want, 0);
  if (!hScaled) {
    hScaled = ScaleIconDown_HQ(hNative, want, want); // your GDI+ fallback
  }
  DestroyIcon(hNative);
  return hScaled;
}

static DWORD ReadBigEndianDword(const BYTE *value) {
  return (static_cast<DWORD>(value[0]) << 24) |
         (static_cast<DWORD>(value[1]) << 16) |
         (static_cast<DWORD>(value[2]) << 8) |
         static_cast<DWORD>(value[3]);
}

static DWORD ReadLittleEndianDword(const BYTE *value) {
  return static_cast<DWORD>(value[0]) |
         (static_cast<DWORD>(value[1]) << 8) |
         (static_cast<DWORD>(value[2]) << 16) |
         (static_cast<DWORD>(value[3]) << 24);
}

static WORD ReadLittleEndianWord(const BYTE *value) {
  return static_cast<WORD>(value[0] |
                           (static_cast<WORD>(value[1]) << 8));
}

static bool IsSafeIcoPayload(const BYTE *data, size_t size) {
  if (!data)
    return false;

  unsigned long long width = 0;
  unsigned long long height = 0;
  if (size >= 33 && data[0] == 0x89 && data[1] == 0x50 &&
      data[2] == 0x4E && data[3] == 0x47 && data[4] == 0x0D &&
      data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A &&
      ReadBigEndianDword(data + 8) == 13 && data[12] == 'I' &&
      data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
    width = ReadBigEndianDword(data + 16);
    height = ReadBigEndianDword(data + 20);
  } else if (size >= 12) {
    const DWORD headerSize = ReadLittleEndianDword(data);
    unsigned long long encodedHeight = 0;
    if (headerSize == 12) {
      width = ReadLittleEndianWord(data + 4);
      encodedHeight = ReadLittleEndianWord(data + 6);
    } else if (headerSize >= 40 && headerSize <= size && size >= 40) {
      const LONG signedWidth =
          static_cast<LONG>(ReadLittleEndianDword(data + 4));
      const LONG signedHeight =
          static_cast<LONG>(ReadLittleEndianDword(data + 8));
      if (signedWidth <= 0 || signedHeight == 0 ||
          signedHeight == (std::numeric_limits<LONG>::min)())
        return false;
      width = static_cast<unsigned long long>(signedWidth);
      encodedHeight = static_cast<unsigned long long>(
          signedHeight < 0 ? -static_cast<long long>(signedHeight)
                           : signedHeight);
    } else {
      return false;
    }
    if (encodedHeight == 0 || (encodedHeight % 2) != 0)
      return false;
    height = encodedHeight / 2;
  } else {
    return false;
  }

  return width > 0 && height > 0 && width <= kMaxIcoPayloadDimension &&
         height <= kMaxIcoPayloadDimension &&
         width * height <= kMaxIcoPayloadPixels;
}

static HICON LoadIconFromIcoBestDownscale(const fs::path &icoPath,
                                          int pxDesired) {
  if (pxDesired <= 0)
    return nullptr;

  std::ifstream f(icoPath, std::ios::binary | std::ios::ate);
  if (!f)
    return nullptr;

  const std::streamsize sz = f.tellg();
  if (sz < static_cast<std::streamsize>(sizeof(ICONDIRHDR)) ||
      static_cast<unsigned long long>(sz) > kMaxImportedImageFileBytes)
    return nullptr;
  f.seekg(0, std::ios::beg);

  std::vector<BYTE> buf((size_t)sz);
  if (!f.read((char *)buf.data(), sz))
    return nullptr;

  const auto *hdr = (const ICONDIRHDR *)buf.data();
  if (hdr->idReserved != 0 || hdr->idType != 1 || hdr->idCount == 0 ||
      hdr->idCount > kMaxIcoEntries)
    return nullptr;

  const size_t need =
      sizeof(ICONDIRHDR) + (size_t)hdr->idCount * sizeof(ICONDIRENTRY);
  if (buf.size() < need)
    return nullptr;

  const auto *ents = (const ICONDIRENTRY *)(buf.data() + sizeof(ICONDIRHDR));

  const int want = pxDesired;

  const ICONDIRENTRY *bestGE = nullptr;
  int bestGES = (std::numeric_limits<int>::max)();

  const ICONDIRENTRY *bestBig = nullptr;
  int bestBigS = 0;

  for (int i = 0; i < (int)hdr->idCount; i++) {
    const auto &e = ents[i];
    int s = max(IcoDim(e.bWidth), IcoDim(e.bHeight));

    if (s >= want && s < bestGES) {
      bestGES = s;
      bestGE = &e;
    }
    if (s > bestBigS) {
      bestBigS = s;
      bestBig = &e;
    }
  }

  const ICONDIRENTRY *pick = bestGE ? bestGE : bestBig;
  if (!pick)
    return nullptr;

  const size_t off = (size_t)pick->dwImageOffset;
  const size_t cb = (size_t)pick->dwBytesInRes;
  if (off >= buf.size() || cb == 0 || cb > buf.size() - off)
    return nullptr;

  const int src = max(IcoDim(pick->bWidth), IcoDim(pick->bHeight));
  const int out = min(pxDesired, src); // never upscale

  BYTE *pBits = buf.data() + off;
  if (!IsSafeIcoPayload(pBits, cb))
    return nullptr;

  return CreateIconFromResourceEx(pBits, (DWORD)cb, TRUE, 0x00030000, out, out,
                                  LR_DEFAULTCOLOR);
}

static HFONT CreateSmallerFontFrom(HFONT baseFont, int pxHeight, UINT dpi) {
  LOGFONTW lf{};
  if (baseFont && GetObjectW(baseFont, sizeof(lf), &lf) == sizeof(lf)) {
    lf.lfHeight = -MulDiv(pxHeight, dpi, 96); // negative = character height
    return CreateFontIndirectW(&lf);
  }
  // fallback
  return CreateFontW(-MulDiv(pxHeight, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE,
                     FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                     DEFAULT_PITCH | FF_MODERN, L"Consolas");
}

static HICON ScaleIconDown_HQ(HICON hSrc, int dstCx, int dstCy) {
  if (!hSrc || dstCx <= 0 || dstCy <= 0)
    return nullptr;

  Gdiplus::Bitmap src(hSrc);
  if (src.GetLastStatus() != Gdiplus::Ok)
    return nullptr;

  const UINT srcW = src.GetWidth(), srcH = src.GetHeight();
  if ((UINT)dstCx >= srcW && (UINT)dstCy >= srcH)
    return CopyIcon(hSrc);

  Gdiplus::Bitmap dst(dstCx, dstCy, PixelFormat32bppARGB);
  if (dst.GetLastStatus() != Gdiplus::Ok)
    return nullptr;

  Gdiplus::Graphics g(&dst);
  g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
  g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  g.Clear(Gdiplus::Color(0, 0, 0, 0));
  g.DrawImage(&src, Gdiplus::Rect(0, 0, dstCx, dstCy));

  HICON hOut = nullptr;
  if (dst.GetHICON(&hOut) != Gdiplus::Ok)
    return nullptr;
  return hOut;
}

static fs::path GetProfileDirFromName(const std::wstring &name) {
  if (_wcsicmp(name.c_str(), L"Temp") == 0)
    return g_sDataDir / "Temp";
  if (_wcsicmp(name.c_str(), L"Default") == 0)
    return g_sDataDir / "Default";
  fs::path clientRoot;
  return TryGetSafeClientProfilePath(name, clientRoot) ? clientRoot : fs::path();
}

static void OpenSelectedClientFolder() {
  const std::wstring name = GetSelectedClientNameSanitized(false);
  if (name.empty())
    return;

  const fs::path target = GetProfileDirFromName(name);
  if (target.empty() || !IsSafeExistingDirectory(target)) {
    MessageBoxW(g_hGui,
                L"Select an existing safe client before opening its folder.",
                L"Open Client Folder", MB_OK | MB_ICONINFORMATION);
    return;
  }

  SHELLEXECUTEINFOW executeInfo{};
  executeInfo.cbSize = sizeof(executeInfo);
  executeInfo.fMask = SEE_MASK_FLAG_NO_UI;
  executeInfo.hwnd = g_hGui;
  executeInfo.lpVerb = L"open";
  executeInfo.lpFile = target.c_str();
  executeInfo.nShow = SW_SHOW;
  if (!ShellExecuteExW(&executeInfo) ||
      reinterpret_cast<INT_PTR>(executeInfo.hInstApp) <= 32) {
    const DWORD error = GetLastError();
    const std::wstring message =
        L"Windows could not open this client folder:\n\n" + target.wstring() +
        std::format(L"\n\nWindows error: {}.", error);
    MessageBoxW(g_hGui, message.c_str(), L"Open Client Folder",
                MB_OK | MB_ICONERROR);
  }
}

static void UpdateIconPreviewForSelection(bool preferListSelection) {
  if (!g_hGui)
    return;

  const UINT dpi = GetDpiForWindow(g_hGui);
  const int px = MulDiv(36, dpi, 96);

  std::wstring name = GetSelectedClientNameSanitized(preferListSelection);

  HICON hNew = nullptr;
  if (!name.empty()) {
    const fs::path profileRoot = GetProfileDirFromName(name);
    fs::path icoPath = profileRoot / "client.ico";
    if (!profileRoot.empty() && IsSafeExistingRegularFile(icoPath)) {
      hNew = LoadIconFromIcoBestDownscale(icoPath, px);
    }
  }
  if (!hNew) {
    hNew = LoadIconResBestDownscale(
        g_hInst, g_bThemeIsDark ? IDI_NIMGW : IDI_NIMGB, px, px);
  }

  if (g_hIconPreviewHandle && g_hIconPreviewHandle != hNew) {
    DestroyIcon(g_hIconPreviewHandle);
  }
  g_hIconPreviewHandle = hNew;
  if (g_hComboClient)
    RedrawWindow(g_hComboClient, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
  if (g_hBtnClientIcon)
    RedrawWindow(g_hBtnClientIcon, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW);
  if (g_hClientEditSurface)
    RedrawWindow(g_hClientEditSurface, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW);
  UpdatePinButtonState();
  UpdateRestoreTabsToggleState();
}

static void EnsureMenuTooltips(HWND hWnd) {
  if (g_hMenuTip)
    return;

  g_hMenuTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_BALLOON,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, hWnd, nullptr, g_hInst, nullptr);

  if (!g_hMenuTip)
    return;

  SetWindowPos(g_hMenuTip, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SendMessageW(g_hMenuTip, TTM_SETMAXTIPWIDTH, 0, ScaleByDpi(450, g_uiDpi));
  SendMessageW(g_hMenuTip, WM_SETFONT, (WPARAM)g_hFont, TRUE);

  ZeroMemory(&g_menuTi, sizeof(g_menuTi));
  g_menuTi.cbSize = sizeof(g_menuTi);
  g_menuTi.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
  g_menuTi.hwnd = hWnd;
  g_menuTi.uId = 1;
  g_menuTi.lpszText = (LPWSTR)L"";
  SendMessageW(g_hMenuTip, TTM_ADDTOOL, 0, (LPARAM)&g_menuTi);
  UpdateTooltipColors();

  // Tooltip text per menu item
  g_menuTipText.clear();
  g_menuTipText[IDM_CTX_SET_PROFILE_ICON] =
      L"Choose an icon/image for profile.";
  g_menuTipText[IDM_CTX_RESET_PROFILE] =
      L"Delete this profile and recreate from Default.7z.";
  g_menuTipText[IDM_CTX_DELETE_PROFILE] =
      L"Permanently delete this profile folder.";
  g_menuTipText[IDM_CTX_CREATE_SHORTCUT] =
      L"Create a customer-icon shortcut for this client on the desktop.";
  g_menuTipText[IDM_CTX_RENAME_PROFILE] =
      L"Rename this closed client while keeping its profile data.";
  g_menuTipText[IDM_CTX_ARCHIVE_PROFILE] =
      L"Hide this closed client without deleting its profile.";
  g_menuTipText[IDM_CTX_ARCHIVED_CLIENTS] =
      L"Restore a hidden client to the normal client list.";
  g_menuTipText[IDM_CTX_EDIT_DEFAULT_PROFILE] =
      L"Open the Default profile for editing.";
  g_menuTipText[IDM_CTX_THEME_COLOR] = L"Choose the ctSpaces theme.";
  g_menuTipText[IDM_CTX_CLIENT_TITLE_FIRST] =
      L"Put the client name first in browser and Alt+Tab titles.";
  g_menuTipText[IDM_CTX_EXPORT_ALL] =
      L"Create and verify a backup of every client profile.";
  g_menuTipText[IDM_CTX_RESTORE_ALL] =
      L"Restore a verified backup or an older backup ZIP.";
  g_menuTipText[IDM_GUIDED_WALKTHROUGH] =
      L"Open the complete read-only guide (F1).";
  g_menuTipText[IDM_QUICK_TOUR] =
      L"Highlight real controls in a read-only tour.";
  g_menuTipText[IDM_WHATS_NEW] =
      L"Review announced guide topics you have not read yet.";
  g_menuTipText[IDM_ABOUT] = L"About ctSpaces";
}

static void HideMenuTooltip() {
  if (!g_hMenuTip)
    return;
  SendMessageW(g_hMenuTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&g_menuTi);
}

static void HandleMenuSelect(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  // Menu closing / no selection
  const UINT item = (UINT)LOWORD(wParam);
  const UINT flags = (UINT)HIWORD(wParam);

  if (item == 0xFFFF || lParam == 0) {
    HideMenuTooltip();
    return;
  }

  // Popup highlight gives index, not command id
  if (flags & MF_POPUP) {
    HideMenuTooltip();
    return;
  }

  auto it = g_menuTipText.find(item);
  if (it == g_menuTipText.end()) {
    HideMenuTooltip();
    return;
  }

  // Don't show tooltip for disabled items
  if ((flags & MF_DISABLED) || (flags & MF_GRAYED)) {
    HideMenuTooltip();
    return;
  }

  // Show near cursor
  POINT pt{};
  GetCursorPos(&pt);

  g_menuTi.lpszText = (LPWSTR)it->second.c_str();
  SendMessageW(g_hMenuTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&g_menuTi);
  SendMessageW(g_hMenuTip, TTM_TRACKPOSITION, 0,
               MAKELPARAM(pt.x + 12, pt.y + 18));
  SendMessageW(g_hMenuTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&g_menuTi);
}

static void UpdateConfigMenuEnabledState() {
  if (!g_hConfigMenu)
    return;

  std::wstring name = GetSelectedClientNameSanitized(false);
  const bool hasSelection = !name.empty();
  const bool hasExistingClient =
      hasSelection && IsExistingClientProfile(name) &&
      !IsClientArchived(name);
  const bool selectedClientIsClosed =
      hasExistingClient && !IsClientActive(name);
  fs::path selectedClientRoot;
  const bool hasCustomIcon =
      hasExistingClient &&
      TryGetSafeClientProfilePath(name, selectedClientRoot) &&
      IsSafeExistingRegularFile(selectedClientRoot / L"client.ico");

  auto en = [&](UINT id, bool enabled) {
    EnableMenuItem(g_hConfigMenu, id,
                   MF_BYCOMMAND |
                       (enabled ? MF_ENABLED : (MF_DISABLED | MF_GRAYED)));
  };

  // Disable everything requiring a selection when empty; ALWAYS allow
  // Default/About
  en(IDM_CTX_SET_PROFILE_ICON, hasExistingClient);
  en(IDM_CTX_REMOVE_ICON, hasCustomIcon);
  en(IDM_CTX_CREATE_SHORTCUT, hasExistingClient);
  en(IDM_CTX_RENAME_PROFILE, selectedClientIsClosed);
  en(IDM_CTX_ARCHIVE_PROFILE, selectedClientIsClosed);
  en(IDM_CTX_RESET_PROFILE, selectedClientIsClosed);
  en(IDM_CTX_VACUUM_PROFILE, selectedClientIsClosed);
  en(IDM_CTX_FETCH_ICON, hasExistingClient);
  en(IDM_CTX_DELETE_PROFILE, selectedClientIsClosed);
  en(IDM_CTX_CLEANUP_INACTIVE, true);
  en(IDM_CTX_DELETE_MULTIPLE, true);

  en(IDM_CTX_ARCHIVED_CLIENTS, !g_archivedClients.empty());
  en(IDM_CTX_EDIT_DEFAULT_PROFILE, true);
  en(IDM_CTX_THEME_COLOR, true);
  en(IDM_CTX_CLIENT_TITLE_FIRST, true);
  en(IDM_CTX_EXPORT_ALL, true);
  en(IDM_CTX_RESTORE_ALL, true);
  en(IDM_GUIDED_WALKTHROUGH, true);
  en(IDM_QUICK_TOUR, true);
  en(IDM_WHATS_NEW, true);
  en(IDM_ABOUT, true);
  CheckMenuItem(g_hConfigMenu, IDM_CTX_CLIENT_TITLE_FIRST,
                MF_BYCOMMAND |
                    (g_bClientTitleFirst.load() ? MF_CHECKED
                                                : MF_UNCHECKED));

  if (g_hBrowserSubMenu) {
    CheckMenuItem(g_hBrowserSubMenu, IDM_BROWSER_EDGE,
                  MF_BYCOMMAND |
                      (g_selectedBrowser == BrowserKind::Edge ? MF_CHECKED
                                                              : MF_UNCHECKED));
    CheckMenuItem(g_hBrowserSubMenu, IDM_BROWSER_CHROME,
                  MF_BYCOMMAND |
                      (g_selectedBrowser == BrowserKind::Chrome
                           ? MF_CHECKED
                           : MF_UNCHECKED));
    CheckMenuItem(g_hBrowserSubMenu, IDM_BROWSER_BRAVE,
                  MF_BYCOMMAND |
                      (g_selectedBrowser == BrowserKind::Brave ? MF_CHECKED
                                                               : MF_UNCHECKED));
    CheckMenuItem(g_hBrowserSubMenu, IDM_BROWSER_FIREFOX,
                  MF_BYCOMMAND |
                      (g_selectedBrowser == BrowserKind::Firefox
                           ? MF_CHECKED
                           : MF_UNCHECKED));

    EnableMenuItem(
        g_hBrowserSubMenu, IDM_BROWSER_EDGE,
        MF_BYCOMMAND |
            (g_sEdgePath.empty() ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));
    EnableMenuItem(
        g_hBrowserSubMenu, IDM_BROWSER_CHROME,
        MF_BYCOMMAND |
            (g_sChromePath.empty() ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));
    EnableMenuItem(
        g_hBrowserSubMenu, IDM_BROWSER_BRAVE,
        MF_BYCOMMAND |
            (g_sBravePath.empty() ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));
    EnableMenuItem(
        g_hBrowserSubMenu, IDM_BROWSER_FIREFOX,
        MF_BYCOMMAND |
            (g_sFirefoxPath.empty() ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));
  }
}

static HICON GetClientIconForUi(const std::wstring &clientName, int px) {
  if (clientName.empty() || px <= 0)
    return nullptr;

  const fs::path clientRoot = GetProfileDirFromName(clientName);
  if (clientRoot.empty())
    return nullptr;
  const fs::path iconPath = clientRoot / L"client.ico";
  std::lock_guard<std::mutex> cacheLock(g_iconCacheMutex);
  auto &cacheSet = g_iconCache[clientName];
  RefreshIconCacheSource(clientName, cacheSet, iconPath);
  if (!cacheSet.sourceExists)
    return nullptr;

  const auto cached = cacheSet.byPx.find(px);
  if (cached != cacheSet.byPx.end())
    return cached->second;

  HICON icon = LoadIconFromIcoBestDownscale(iconPath, px);
  cacheSet.byPx[px] = icon;
  return icon;
}

static RECT GetPinnedClientRowRect() {
  RECT row = g_rcPinnedArea;
  row.top = min(row.bottom, row.top + ScaleByDpi(15, g_uiDpi));
  return row;
}

static void BuildPinnedClientRects() {
  g_pinnedClientRects.clear();
  g_rcPinnedOverflow = {};
  const RECT row = GetPinnedClientRowRect();
  if (row.right <= row.left || row.bottom <= row.top ||
      g_pinnedClients.empty()) {
    return;
  }

  const int visibleCount =
      min((int)g_pinnedClients.size(), MAX_VISIBLE_PINNED_CLIENTS);
  const bool hasOverflow =
      (int)g_pinnedClients.size() > MAX_VISIBLE_PINNED_CLIENTS;
  const int maxItemW = ScaleByDpi(116, g_uiDpi);
  const int overflowW = hasOverflow ? ScaleByDpi(36, g_uiDpi) : 0;
  const int gap = ScaleByDpi(6, g_uiDpi);
  const int availableW = max(0, (row.right - row.left) - overflowW -
                                    (hasOverflow ? gap : 0));
  const int itemW =
      visibleCount > 0 ? min(maxItemW, availableW / visibleCount) : 0;

  int x = row.left;
  for (int i = 0; i < visibleCount; ++i) {
    const int right = x + itemW;
    g_pinnedClientRects.push_back(RECT{x, row.top, right, row.bottom});
    x = right;
  }

  if (hasOverflow) {
    g_rcPinnedOverflow = {x + gap, row.top, x + gap + overflowW, row.bottom};
  }
}

static void DrawPinnedClients(HDC hdc) {
  if (!hdc || g_pinnedClients.empty() ||
      g_rcPinnedArea.right <= g_rcPinnedArea.left)
    return;

  BuildPinnedClientRects();
  SetBkMode(hdc, TRANSPARENT);
  const COLORREF mutedText = BlendColor(
      g_themeColors.crWindowText, g_themeColors.crWindow,
      g_bThemeIsDark ? 38 : 48);
  const COLORREF divider = BlendColor(
      g_themeColors.crControlBorder, g_themeColors.crWindow, 62);

  const RECT row = GetPinnedClientRowRect();
  HFONT labelOldFont = (HFONT)SelectObject(
      hdc, g_hFontLabel ? g_hFontLabel
                        : (g_hFont ? g_hFont
                                   : GetStockObject(DEFAULT_GUI_FONT)));
  RECT labelRect{g_rcPinnedArea.left, g_rcPinnedArea.top,
                 g_rcPinnedArea.right, row.top};
  SetTextColor(hdc, mutedText);
  DrawTextW(hdc, L"PINNED", -1, &labelRect,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(hdc, labelOldFont);

  HFONT oldFont = (HFONT)SelectObject(
      hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
  const int iconPx = ScaleByDpi(20, g_uiDpi);
  const int sidePad = ScaleByDpi(6, g_uiDpi);

  for (int i = 0; i < (int)g_pinnedClientRects.size(); ++i) {
    const RECT itemRect = g_pinnedClientRects[i];
    if (i == g_iHotPinnedClient && g_bUiEnabled) {
      RECT hotRect = itemRect;
      InflateRect(&hotRect, -ScaleByDpi(2, g_uiDpi),
                  -ScaleByDpi(2, g_uiDpi));
      const COLORREF hotFill = BlendColor(
          g_themeColors.crWindow, g_themeColors.crControlHot, 38);
      const COLORREF hotBorder = BlendColor(
          g_themeColors.crControlBorder, g_themeColors.crWindow, 58);
      DrawRoundedRect(hdc, hotRect, hotFill, hotBorder,
                      ScaleByDpi(4, g_uiDpi));
    }
    if (i > 0) {
      HPEN pen = CreatePen(PS_SOLID, 1, divider);
      HPEN oldPen = (HPEN)SelectObject(hdc, pen);
      const int yPad = ScaleByDpi(6, g_uiDpi);
      MoveToEx(hdc, itemRect.left, itemRect.top + yPad, nullptr);
      LineTo(hdc, itemRect.left, itemRect.bottom - yPad);
      SelectObject(hdc, oldPen);
      DeleteObject(pen);
    }

    const int cy = (itemRect.top + itemRect.bottom) / 2;
    const int iconX = itemRect.left + sidePad;
    const int iconY = cy - iconPx / 2;
    HICON clientIcon = GetClientIconForUi(g_pinnedClients[i], iconPx);
    if (!clientIcon)
      clientIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_CTSPACES));
    if (clientIcon)
      DrawIconEx(hdc, iconX, iconY, clientIcon, iconPx, iconPx, 0, nullptr,
                 DI_NORMAL);

    RECT textRect{iconX + iconPx + ScaleByDpi(8, g_uiDpi), itemRect.top,
                  itemRect.right - sidePad, itemRect.bottom};
    SetTextColor(hdc, g_themeColors.crWindowText);
    DrawTextW(hdc, g_pinnedClients[i].c_str(), -1, &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);

  }

  if (g_rcPinnedOverflow.right > g_rcPinnedOverflow.left) {
    if (g_bHotPinnedOverflow && g_bUiEnabled) {
      RECT hotRect = g_rcPinnedOverflow;
      InflateRect(&hotRect, -ScaleByDpi(2, g_uiDpi),
                  -ScaleByDpi(2, g_uiDpi));
      const COLORREF hotFill = BlendColor(
          g_themeColors.crWindow, g_themeColors.crControlHot, 38);
      const COLORREF hotBorder = BlendColor(
          g_themeColors.crControlBorder, g_themeColors.crWindow, 58);
      DrawRoundedRect(hdc, hotRect, hotFill, hotBorder,
                      ScaleByDpi(4, g_uiDpi));
    }
    const std::wstring overflowText = std::format(
        L"+{}", (int)g_pinnedClients.size() - MAX_VISIBLE_PINNED_CLIENTS);
    RECT overflowRect = g_rcPinnedOverflow;
    SetTextColor(hdc, mutedText);
    DrawTextW(hdc, overflowText.c_str(), -1, &overflowRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  SelectObject(hdc, oldFont);
}

static int HitTestPinnedClient(POINT point) {
  BuildPinnedClientRects();
  for (int i = 0; i < (int)g_pinnedClientRects.size(); ++i) {
    if (PtInRect(&g_pinnedClientRects[i], point))
      return i;
  }
  return -1;
}

static bool HitTestPinnedOverflow(POINT point) {
  BuildPinnedClientRects();
  return g_rcPinnedOverflow.right > g_rcPinnedOverflow.left &&
         PtInRect(&g_rcPinnedOverflow, point);
}

static void ShowPinnedClientOptionsMenu(HWND hWnd,
                                        const std::wstring &clientName,
                                        POINT screenPoint) {
  if (!hWnd || !IsExistingClientProfile(clientName))
    return;

  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  MENUINFO menuInfo{};
  menuInfo.cbSize = sizeof(menuInfo);
  menuInfo.fMask = MIM_STYLE | MIM_BACKGROUND;
  menuInfo.dwStyle = MNS_CHECKORBMP | MNS_NOCHECK;
  menuInfo.hbrBack =
      g_hbrThemeMenu ? g_hbrThemeMenu : GetSysColorBrush(COLOR_MENU);
  SetMenuInfo(menu, &menuInfo);

  MenuItemData selectData;
  selectData.text = L"Select client";
  MenuItemData openData;
  openData.text = L"Open client";
  MenuItemData openClipboardData;
  openClipboardData.text = L"Open copied link";
  MenuItemData separatorData;
  separatorData.separator = true;
  MenuItemData restoreData;
  restoreData.text = L"Restore tabs";
  MenuItemData shortcutData;
  shortcutData.text = L"Create desktop shortcut";
  const auto clipboardUrl = GetClipboardWebUrl();

  auto addItem = [&](UINT id, MenuItemData &data, bool checked = false,
                     bool enabled = true) {
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA | MIIM_STATE;
    item.wID = id;
    item.fType = MFT_OWNERDRAW;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(&data);
    item.fState = (checked ? MFS_CHECKED : MFS_UNCHECKED) |
                  (enabled ? MFS_ENABLED : MFS_DISABLED);
    InsertMenuItemW(menu, (UINT)-1, TRUE, &item);
  };
  auto addSeparator = [&]() {
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE | MIIM_DATA;
    item.fType = MFT_OWNERDRAW | MFT_SEPARATOR;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(&separatorData);
    InsertMenuItemW(menu, (UINT)-1, TRUE, &item);
  };

  constexpr UINT commandSelect = 47100;
  constexpr UINT commandOpen = 47101;
  constexpr UINT commandRestoreTabs = 47102;
  constexpr UINT commandOpenClipboard = 47103;
  constexpr UINT commandCreateShortcut = 47104;
  addItem(commandSelect, selectData);
  addItem(commandOpen, openData);
  addItem(commandOpenClipboard, openClipboardData, false,
          clipboardUrl.has_value());
  addSeparator();
  addItem(commandRestoreTabs, restoreData,
          ShouldRestoreTabsForClient(clientName, g_selectedBrowser));
  addItem(commandCreateShortcut, shortcutData);

  const int previousMenuMinWidth = g_iMenuMinWidth;
  g_iMenuMinWidth = 0;
  HideMenuTooltip();
  SetForegroundWindow(hWnd);
  const UINT command = TrackPopupMenuEx(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, screenPoint.x,
      screenPoint.y, hWnd, nullptr);
  g_iMenuMinWidth = previousMenuMinWidth;
  DestroyMenu(menu);
  PostMessageW(hWnd, WM_NULL, 0, 0);

  if (command == commandSelect) {
    SelectPinnedClient(clientName);
  } else if (command == commandOpen) {
    OpenPinnedClient(clientName);
  } else if (command == commandOpenClipboard && clipboardUrl) {
    OpenWebUrlForClient(clientName, *clipboardUrl);
  } else if (command == commandRestoreTabs &&
             SelectPinnedClient(clientName)) {
    ToggleRestoreTabsForSelectedClient();
  } else if (command == commandCreateShortcut) {
    CreateClientDesktopShortcut(clientName, g_selectedBrowser);
  }
}

static void ShowPinnedOverflowMenu(HWND hWnd, bool selectOnly) {
  if ((int)g_pinnedClients.size() <= MAX_VISIBLE_PINNED_CLIENTS)
    return;

  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  constexpr UINT commandBase = 47000;
  MENUINFO menuInfo{sizeof(menuInfo)};
  menuInfo.fMask = MIM_BACKGROUND;
  menuInfo.hbrBack = g_hbrThemeMenu;
  SetMenuInfo(menu, &menuInfo);
  std::vector<MenuItemData> items(g_pinnedClients.size());
  for (int i = MAX_VISIBLE_PINNED_CLIENTS;
       i < (int)g_pinnedClients.size(); ++i) {
    items[i].text = g_pinnedClients[i];
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA;
    item.fType = MFT_OWNERDRAW;
    item.wID = commandBase + i;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(&items[i]);
    InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &item);
  }

  POINT menuPoint{g_rcPinnedOverflow.left, g_rcPinnedOverflow.bottom};
  ClientToScreen(hWnd, &menuPoint);
  SetForegroundWindow(hWnd);
  const UINT command = TrackPopupMenuEx(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, menuPoint.x,
      menuPoint.y, hWnd, nullptr);
  DestroyMenu(menu);
  PostMessageW(hWnd, WM_NULL, 0, 0);

  if (command >= commandBase) {
    const int index = (int)(command - commandBase);
    if (index >= MAX_VISIBLE_PINNED_CLIENTS &&
        index < (int)g_pinnedClients.size()) {
      if (selectOnly)
        SelectPinnedClient(g_pinnedClients[index]);
      else
        OpenPinnedClient(g_pinnedClients[index]);
    }
  }
}

static void StartMainDragCandidate(HWND hWnd, MainDragKind kind, int index,
                                   POINT point) {
  g_pinnedClientsBeforeDrag.clear();
  g_bPinnedDragSnapshotValid = false;
  if (kind == MainDragKind::PinnedClient) {
    try {
      g_pinnedClientsBeforeDrag = g_pinnedClients;
      g_bPinnedDragSnapshotValid = true;
    } catch (...) {
      return;
    }
  }
  g_mainDragKind = kind;
  g_iMainDragIndex = index;
  g_ptMainDragStart = point;
  g_bMainDragActive = false;
  g_bPinnedOrderChanged = false;
  SetCapture(hWnd);
}

static void FinishPinnedDragPersistence(bool savePinnedOrder) {
  const bool snapshotValid = g_bPinnedDragSnapshotValid;
  std::vector<std::wstring> originalPinnedClients =
      std::move(g_pinnedClientsBeforeDrag);
  g_pinnedClientsBeforeDrag.clear();
  g_bPinnedDragSnapshotValid = false;

  if (!savePinnedOrder)
    return;
  if (SavePinnedClients())
    return;
  if (!snapshotValid)
    return;

  g_pinnedClients = std::move(originalPinnedClients);
  BuildPinnedClientRects();
  UpdatePinButtonState();
  if (g_hGui)
    InvalidateRect(g_hGui, &g_rcPinnedArea, TRUE);
}

static void MovePinnedClientForDrag(int fromIndex, int toIndex) {
  if (fromIndex < 0 || toIndex < 0 ||
      fromIndex >= (int)g_pinnedClients.size() ||
      toIndex >= (int)g_pinnedClients.size() || fromIndex == toIndex) {
    return;
  }

  std::wstring moved = std::move(g_pinnedClients[fromIndex]);
  g_pinnedClients.erase(g_pinnedClients.begin() + fromIndex);
  g_pinnedClients.insert(g_pinnedClients.begin() + toIndex,
                         std::move(moved));
  g_iMainDragIndex = toIndex;
  g_bPinnedOrderChanged = true;
  BuildPinnedClientRects();
  InvalidateRect(g_hGui, &g_rcPinnedArea, TRUE);
}

static void MoveSessionTabForDrag(int fromTab, int toTab) {
  if (fromTab <= 0 || toTab <= 0 || fromTab > (int)g_sessions.size() ||
      toTab > (int)g_sessions.size() || fromTab == toTab) {
    return;
  }

  DWORD selectedPid = 0;
  if (g_iSelectedSessionTab > 0 &&
      g_iSelectedSessionTab <= (int)g_sessions.size()) {
    selectedPid = g_sessions[g_iSelectedSessionTab - 1].pid;
  }

  Session moved = std::move(g_sessions[fromTab - 1]);
  g_sessions.erase(g_sessions.begin() + (fromTab - 1));
  g_sessions.insert(g_sessions.begin() + (toTab - 1), std::move(moved));
  g_iMainDragIndex = toTab;

  if (selectedPid != 0) {
    for (int i = 0; i < (int)g_sessions.size(); ++i) {
      if (g_sessions[i].pid == selectedPid) {
        g_iSelectedSessionTab = i + 1;
        break;
      }
    }
  }

  BuildSessionTabRects(g_hGui, nullptr);
  InvalidateSessionTabs(g_hGui);
}

static bool UpdateMainDrag(HWND hWnd, POINT point, WPARAM keyState) {
  if (g_mainDragKind == MainDragKind::None)
    return false;
  if ((keyState & MK_LBUTTON) == 0) {
    FinishMainDrag(hWnd, point);
    return false;
  }

  if (!g_bMainDragActive) {
    const int dragX = max(2, GetSystemMetrics(SM_CXDRAG));
    const int dragY = max(2, GetSystemMetrics(SM_CYDRAG));
    if (std::abs(point.x - g_ptMainDragStart.x) < dragX &&
        std::abs(point.y - g_ptMainDragStart.y) < dragY) {
      return false;
    }
    g_bMainDragActive = true;
  }

  if (g_mainDragKind == MainDragKind::PinnedClient) {
    int target = HitTestPinnedClient(point);
    if (target < 0 && HitTestPinnedOverflow(point) &&
        !g_pinnedClients.empty()) {
      target = (int)g_pinnedClients.size() - 1;
    }
    if (target >= 0)
      MovePinnedClientForDrag(g_iMainDragIndex, target);
  } else if (g_mainDragKind == MainDragKind::SessionTab) {
    const int target = HitTestSessionTab(hWnd, point);
    if (target > 0)
      MoveSessionTabForDrag(g_iMainDragIndex, target);
  }

  SetCursor(LoadCursorW(nullptr, IDC_HAND));
  return true;
}

static bool IsDesktopDropPoint(POINT screenPoint) {
  HWND target = WindowFromPoint(screenPoint);
  for (int depth = 0; target && depth < 6; ++depth) {
    wchar_t className[128]{};
    GetClassNameW(target, className, (int)std::size(className));
    if (_wcsicmp(className, L"SysListView32") == 0 ||
        _wcsicmp(className, L"SHELLDLL_DefView") == 0 ||
        _wcsicmp(className, L"WorkerW") == 0 ||
        _wcsicmp(className, L"Progman") == 0) {
      return true;
    }
    target = GetParent(target);
  }
  return false;
}

static bool FinishMainDrag(HWND hWnd, POINT point) {
  if (g_mainDragKind == MainDragKind::None)
    return false;

  const bool consumed = g_bMainDragActive;
  const bool savePinnedOrder = g_bPinnedOrderChanged;
  std::wstring desktopShortcutClient;
  if (consumed && g_mainDragKind == MainDragKind::PinnedClient &&
      g_iMainDragIndex >= 0 &&
      g_iMainDragIndex < (int)g_pinnedClients.size()) {
    POINT screenPoint = point;
    ClientToScreen(hWnd, &screenPoint);
    RECT windowRect{};
    GetWindowRect(hWnd, &windowRect);
    if (!PtInRect(&windowRect, screenPoint) &&
        IsDesktopDropPoint(screenPoint)) {
      desktopShortcutClient = g_pinnedClients[g_iMainDragIndex];
    }
  }
  g_mainDragKind = MainDragKind::None;
  g_iMainDragIndex = -1;
  g_bMainDragActive = false;
  g_bPinnedOrderChanged = false;
  if (GetCapture() == hWnd)
    ReleaseCapture();
  FinishPinnedDragPersistence(savePinnedOrder);
  if (!desktopShortcutClient.empty())
    CreateClientDesktopShortcut(desktopShortcutClient, g_selectedBrowser);
  return consumed;
}

static void LayoutMainGui(HWND hWnd) {
  if (!hWnd)
    return;

  const UINT dpi = GetDpiForWindow(hWnd);
  const int m = MulDiv(14, dpi, 96);
  const int gap = MulDiv(8, dpi, 96);
  const int tabH = MulDiv(34, dpi, 96);
  const int footerH = MulDiv(40, dpi, 96);
  const int labelH = MulDiv(14, dpi, 96);
  const int comboVisH = MulDiv(36, dpi, 96);
  const int comboDropH = MulDiv(220, dpi, 96);
  const int btnSmall = MulDiv(30, dpi, 96);
  const int pinW = MulDiv(36, dpi, 96);
  const int openW = MulDiv(78, dpi, 96);
  const int openH = MulDiv(36, dpi, 96);

  RECT rc{};
  GetClientRect(hWnd, &rc);
  const int W = rc.right - rc.left;
  const int H = rc.bottom - rc.top;

  g_rcSessionTabs = {0, 0, W, tabH};
  g_rcUtilityBar = {0, max(tabH, H - footerH), W, H};
  g_rcBrowserSelector = {
      m, g_rcUtilityBar.top + MulDiv(5, dpi, 96),
      m + MulDiv(154, dpi, 96),
      g_rcUtilityBar.bottom - MulDiv(5, dpi, 96)};
  BuildSessionTabRects(hWnd, nullptr);

  const bool hasPinnedClients = !g_pinnedClients.empty();
  if (hasPinnedClients) {
    const int pinnedLabelY = tabH + MulDiv(4, dpi, 96);
    const int pinnedLabelH = MulDiv(12, dpi, 96);
    const int pinnedLabelGap = MulDiv(3, dpi, 96);
    const int pinnedRowH = MulDiv(24, dpi, 96);
    g_rcPinnedArea = {
        m, pinnedLabelY, W - m,
        pinnedLabelY + pinnedLabelH + pinnedLabelGap + pinnedRowH};
    BuildPinnedClientRects();
  } else {
    g_rcPinnedArea = {};
    g_rcPinnedOverflow = {};
    g_pinnedClientRects.clear();
    g_iHotPinnedClient = -1;
    g_bHotPinnedOverflow = false;
  }

  const int labelY =
      hasPinnedClients ? g_rcPinnedArea.bottom + MulDiv(8, dpi, 96)
                       : tabH + MulDiv(8, dpi, 96);
  const int comboY = labelY + labelH + MulDiv(4, dpi, 96);

  const int comboX = m;
  const int openX = W - m - openW;
  const int pinX = openX - gap - pinW;
  const int comboW = max(MulDiv(150, dpi, 96), pinX - gap - comboX);
  HWND hPrompt = GetDlgItem(hWnd, IDC_STATIC_PROMPT);
  if (hPrompt)
    MoveWindow(hPrompt, m, labelY, W - m * 2, labelH, TRUE);

  MoveWindow(g_hComboClient, comboX, comboY, comboW, comboDropH, TRUE);
  HideClientComboChrome(g_hComboClient);
  LayoutClientComboChildren(g_hComboClient);
  StackClientSelectorWindows();
  MoveWindow(g_hBtnPin, pinX, comboY, pinW, comboVisH, TRUE);
  MoveWindow(g_hBtnGo, openX, comboY + (comboVisH - openH) / 2, openW,
             openH, TRUE);

  const int xCfg = W - m - btnSmall;
  const int xTmp = xCfg - gap - btnSmall;
  const int yBtn = g_rcUtilityBar.top +
                   ((g_rcUtilityBar.bottom - g_rcUtilityBar.top) - btnSmall) /
                       2;
  const int restoreToggleW = MulDiv(132, dpi, 96);
  const int restoreToggleH = MulDiv(30, dpi, 96);
  const int restoreToggleX = xTmp - gap - restoreToggleW;
  const int restoreToggleY =
      g_rcUtilityBar.top +
      ((g_rcUtilityBar.bottom - g_rcUtilityBar.top) - restoreToggleH) / 2;
  MoveWindow(g_hBtnRestoreTabs, restoreToggleX, restoreToggleY, restoreToggleW,
             restoreToggleH, TRUE);
  MoveWindow(g_hBtnTmpProf, xTmp, yBtn, btnSmall, btnSmall, TRUE);
  MoveWindow(g_hBtnConfig, xCfg, yBtn, btnSmall, btnSmall, TRUE);

  InvalidateRect(hWnd, &g_rcSessionTabs, TRUE);
  InvalidateRect(hWnd, &g_rcPinnedArea, TRUE);
  if (g_hComboClient)
    InvalidateRect(g_hComboClient, nullptr, TRUE);
  InvalidateRect(hWnd, &g_rcUtilityBar, TRUE);
  QueueClientSelectorRestack();
}

static std::wstring GetSelectedClientNameSanitized(bool preferListSelection) {
  if (g_iSelectedSessionTab > 0 &&
      g_iSelectedSessionTab <= (int)g_sessions.size()) {
    const std::wstring sessionName = ResolveExistingClientName(
        g_sessions[g_iSelectedSessionTab - 1].clientName);
    fs::path profilePath;
    return TryGetSafeClientProfilePath(sessionName, profilePath)
               ? sessionName
               : L"";
  }

  // Edit text (what the user typed)
  const std::wstring editName =
      ResolveExistingClientName(GetClientInputText());

  // Current list selection (what the dropdown is on)
  wchar_t selBuf[256]{};
  std::wstring selName;
  int sel = (int)SendMessageW(g_hComboClient, CB_GETCURSEL, 0, 0);
  if (sel != CB_ERR) {
    SendMessageW(g_hComboClient, CB_GETLBTEXT, sel, (LPARAM)selBuf);
    selName = ResolveExistingClientName(selBuf);
  }

  std::wstring result;
  if (preferListSelection && !selName.empty())
    result = selName;
  else if (!editName.empty())
    result = editName;
  else
    result = selName;

  fs::path profilePath;
  return TryGetSafeClientProfilePath(result, profilePath) ? result : L"";
}

static std::wstring GetSessionTabText(int iTab) {
  if (iTab <= 0)
    return L"New";
  const int iSession = iTab - 1;
  if (iSession >= 0 && iSession < (int)g_sessions.size()) {
    const Session &session = g_sessions[iSession];
    return session.clientName;
  }
  return L"";
}

static int MeasureSessionTabWidth(HDC hdc, const std::wstring &text,
                                  int iMinW, int iMaxW, int iPadX) {
  SIZE sz{};
  if (hdc && !text.empty()) {
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &sz);
  }
  int iWidth = sz.cx + iPadX * 2;
  if (iWidth < iMinW)
    iWidth = iMinW;
  if (iWidth > iMaxW)
    iWidth = iMaxW;
  return iWidth;
}

static void BuildSessionTabRects(HWND hWnd, HDC hdc) {
  const int iCount = 1 + (int)g_sessions.size();
  g_sessionTabRects.assign(iCount, RECT{});
  g_sessionTabCloseRects.assign(iCount, RECT{});
  g_rcSessionOverflow = RECT{};
  g_overflowSessionTabs.clear();

  if (!hWnd || g_rcSessionTabs.right <= g_rcSessionTabs.left ||
      g_rcSessionTabs.bottom <= g_rcSessionTabs.top)
    return;

  const UINT dpi = g_uiDpi ? g_uiDpi : GetDpiForWindow(hWnd);
  const int iPadX = ScaleByDpi(8, dpi);
  const int iGap = ScaleByDpi(4, dpi);
  const int iOuter = ScaleByDpi(8, dpi);
  const int iTabTop = ScaleByDpi(5, dpi);
  const int iTabBottom = ScaleByDpi(3, dpi);
  const int iCloseSize = ScaleByDpi(10, dpi);
  const int iCloseGap = ScaleByDpi(4, dpi);
  const int iOverflowW = ScaleByDpi(42, dpi);
  const int iTabH =
      max(ScaleByDpi(18, dpi),
          (g_rcSessionTabs.bottom - g_rcSessionTabs.top) - iTabTop -
              iTabBottom);
  const int iAvail =
      max(0, (g_rcSessionTabs.right - g_rcSessionTabs.left) - iOuter * 2);

  HDC hMeasure = hdc ? hdc : GetDC(hWnd);
  HFONT hOldFont = nullptr;
  if (hMeasure) {
    hOldFont = (HFONT)SelectObject(
        hMeasure, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
  }

  std::vector<int> widths(iCount, 0);
  widths[0] = MeasureSessionTabWidth(hMeasure, GetSessionTabText(0),
                                     ScaleByDpi(62, dpi), ScaleByDpi(68, dpi),
                                     iPadX);
  for (int i = 1; i < iCount; ++i) {
    widths[i] = MeasureSessionTabWidth(hMeasure, GetSessionTabText(i),
                                       ScaleByDpi(48, dpi),
                                       ScaleByDpi(110, dpi),
                                       iPadX) +
                iCloseSize + iCloseGap;
  }

  int iTotal = (iCount > 0) ? iGap * (iCount - 1) : 0;
  for (int iWidth : widths)
    iTotal += iWidth;

  std::vector<int> visibleTabs;
  if (iTotal <= iAvail) {
    for (int i = 0; i < iCount; ++i)
      visibleTabs.push_back(i);
  } else {
    visibleTabs.push_back(0);

    std::vector<int> candidates;
    if (g_iSelectedSessionTab > 0 && g_iSelectedSessionTab < iCount)
      candidates.push_back(g_iSelectedSessionTab);

    for (int i = iCount - 1; i >= 1; --i) {
      if (i != g_iSelectedSessionTab)
        candidates.push_back(i);
    }

    int iSessionWidth = 0;
    for (int iTab : candidates) {
      const int iCurrentSessionCount = max(0, (int)visibleTabs.size() - 1);
      const int iNextSessionCount = iCurrentSessionCount + 1;
      const int iUsedIfAdded = widths[0] + iSessionWidth + widths[iTab] +
                               iOverflowW + iGap * (iNextSessionCount + 1);
      if (iUsedIfAdded <= iAvail || iCurrentSessionCount == 0) {
        visibleTabs.push_back(iTab);
        iSessionWidth += widths[iTab];
      }
    }

    std::sort(visibleTabs.begin() + 1, visibleTabs.end());
    for (int i = 1; i < iCount; ++i) {
      if (std::find(visibleTabs.begin(), visibleTabs.end(), i) ==
          visibleTabs.end()) {
        g_overflowSessionTabs.push_back(i);
      }
    }
  }

  int x = g_rcSessionTabs.left + iOuter;
  const int iRightLimit = g_rcSessionTabs.right - iOuter;
  const int y = g_rcSessionTabs.top + iTabTop;

  auto placeTab = [&](int i) {
    if (x >= iRightLimit)
      return;
    RECT rc{x, y, min(x + widths[i], iRightLimit), y + iTabH};
    if (rc.right > rc.left) {
      g_sessionTabRects[i] = rc;
      if (i > 0 && rc.right - rc.left > iCloseSize + iPadX * 2) {
        const int iCloseTop = rc.top + ((rc.bottom - rc.top) - iCloseSize) / 2;
        RECT rcClose{rc.right - iPadX - iCloseSize, iCloseTop,
                     rc.right - iPadX, iCloseTop + iCloseSize};
        g_sessionTabCloseRects[i] = rcClose;
      }
    }
    x = rc.right + iGap;
  };

  for (int i : visibleTabs)
    placeTab(i);

  if (!g_overflowSessionTabs.empty() && x < iRightLimit) {
    g_rcSessionOverflow = {x, y, min(x + iOverflowW, iRightLimit), y + iTabH};
  }

  if (hMeasure && hOldFont)
    SelectObject(hMeasure, hOldFont);
  if (!hdc && hMeasure)
    ReleaseDC(hWnd, hMeasure);
}

static void InvalidateSessionTabs(HWND hWnd) {
  if (!hWnd)
    return;
  RECT rc = g_rcSessionTabs;
  if (rc.right <= rc.left || rc.bottom <= rc.top)
    GetClientRect(hWnd, &rc);
  InvalidateRect(hWnd, &rc, TRUE);
}

static int HitTestSessionTab(HWND hWnd, POINT pt) {
  if (!g_bUiEnabled)
    return -1;
  if (g_sessionTabRects.empty())
    BuildSessionTabRects(hWnd, nullptr);
  for (int i = 0; i < (int)g_sessionTabRects.size(); ++i) {
    RECT rc = g_sessionTabRects[i];
    if (rc.right > rc.left && PtInRect(&rc, pt))
      return i;
  }
  return -1;
}

static int HitTestSessionClose(HWND hWnd, POINT pt) {
  if (!g_bUiEnabled)
    return -1;
  if (g_sessionTabCloseRects.empty())
    BuildSessionTabRects(hWnd, nullptr);
  for (int i = 1; i < (int)g_sessionTabCloseRects.size(); ++i) {
    RECT rc = g_sessionTabCloseRects[i];
    if (rc.right > rc.left && PtInRect(&rc, pt))
      return i;
  }
  return -1;
}

static bool HitTestSessionOverflow(HWND hWnd, POINT pt) {
  if (!g_bUiEnabled)
    return false;
  if (g_overflowSessionTabs.empty())
    return false;
  if (g_rcSessionOverflow.right <= g_rcSessionOverflow.left)
    BuildSessionTabRects(hWnd, nullptr);
  return g_rcSessionOverflow.right > g_rcSessionOverflow.left &&
         PtInRect(&g_rcSessionOverflow, pt);
}

static void SwitchToLaunchModeForInput() {
  if (g_iSelectedSessionTab <= 0)
    return;
  g_iSelectedSessionTab = 0;
  SetWindowTextW(g_hBtnGo, L"Open");
  InvalidateSessionTabs(g_hGui);
}

static void DrawSessionTabs(HWND hWnd, HDC hdc) {
  if (!hdc)
    return;

  RECT rcBar = g_rcSessionTabs;
  if (rcBar.right <= rcBar.left || rcBar.bottom <= rcBar.top)
    return;

  const COLORREF crBar = BlendColor(
      g_themeColors.crWindow, g_themeColors.crControl,
      g_bThemeIsDark ? 13 : 26);
  HBRUSH hBar = CreateSolidBrush(crBar);
  FillRect(hdc, &rcBar, hBar);
  DeleteObject(hBar);
  BuildSessionTabRects(hWnd, hdc);

  const COLORREF crLine =
      BlendColor(g_themeColors.crControlBorder, crBar, 48);
  HPEN hSep = CreatePen(PS_SOLID, 1, crLine);
  HPEN hOldPen = (HPEN)SelectObject(hdc, hSep);
  MoveToEx(hdc, rcBar.left, rcBar.bottom - 1, nullptr);
  LineTo(hdc, rcBar.right, rcBar.bottom - 1);
  SelectObject(hdc, hOldPen);
  DeleteObject(hSep);

  HFONT hOldFont = (HFONT)SelectObject(
      hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
  SetBkMode(hdc, TRANSPARENT);

  for (int i = 0; i < (int)g_sessionTabRects.size(); ++i) {
    RECT rc = g_sessionTabRects[i];
    if (rc.right <= rc.left)
      continue;

    const bool bSelected = i == g_iSelectedSessionTab;
    COLORREF crFill =
        bSelected ? g_themeColors.crWindow
                  : BlendColor(crBar, g_themeColors.crControl,
                               g_bThemeIsDark ? 13 : 20);
    COLORREF crBorder =
        bSelected ? BlendColor(g_themeColors.crControlBorder,
                               g_themeColors.crAccent, 20)
                  : BlendColor(g_themeColors.crControlBorder, crBar, 72);
    COLORREF crText =
        bSelected ? g_themeColors.crControlText : g_themeColors.crWindowText;

    if (!g_bUiEnabled) {
      crFill = BlendColor(crFill, g_themeColors.crWindow, 50);
      crBorder = BlendColor(crBorder, g_themeColors.crWindow, 50);
      crText = BlendColor(crText, g_themeColors.crWindow, 55);
    }

    DrawRoundedRect(hdc, rc, crFill, crBorder, ScaleByDpi(6, g_uiDpi));

    if (bSelected && g_bUiEnabled) {
      RECT rcAccent{rc.left + ScaleByDpi(7, g_uiDpi),
                    rc.bottom - ScaleByDpi(3, g_uiDpi),
                    rc.right - ScaleByDpi(7, g_uiDpi), rc.bottom - 1};
      HBRUSH hAccent = CreateSolidBrush(g_themeColors.crAccent);
      FillRect(hdc, &rcAccent, hAccent);
      DeleteObject(hAccent);
    }

    RECT rcText = rc;
    rcText.left += ScaleByDpi(i == 0 ? 22 : 8, g_uiDpi);
    rcText.right -= ScaleByDpi(8, g_uiDpi);
    RECT rcClose{};
    if (i > 0 && i < (int)g_sessionTabCloseRects.size()) {
      rcClose = g_sessionTabCloseRects[i];
      if (rcClose.right > rcClose.left) {
        rcText.right = rcClose.left - ScaleByDpi(4, g_uiDpi);
      }
    }
    SetTextColor(hdc, crText);
    std::wstring text = GetSessionTabText(i);
    DrawTextW(hdc, text.c_str(), -1, &rcText,
              (i == 0 ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);

    if (i == 0) {
      const int iCx = rc.left + ScaleByDpi(13, g_uiDpi);
      const int iCy = (rc.top + rc.bottom) / 2;
      const int iHalf = ScaleByDpi(4, g_uiDpi);
      HPEN hPlusPen = CreatePen(PS_SOLID, max(1, ScaleByDpi(1, g_uiDpi)),
                                crText);
      HPEN hOldPlusPen = (HPEN)SelectObject(hdc, hPlusPen);
      MoveToEx(hdc, iCx - iHalf, iCy, nullptr);
      LineTo(hdc, iCx + iHalf + 1, iCy);
      MoveToEx(hdc, iCx, iCy - iHalf, nullptr);
      LineTo(hdc, iCx, iCy + iHalf + 1);
      SelectObject(hdc, hOldPlusPen);
      DeleteObject(hPlusPen);
    }

    if (rcClose.right > rcClose.left) {
      const COLORREF crClose =
          BlendColor(crText, bSelected ? crFill : g_themeColors.crWindow, 20);
      HPEN hClosePen = CreatePen(PS_SOLID, max(1, ScaleByDpi(1, g_uiDpi)),
                                 crClose);
      HPEN hOldClosePen = (HPEN)SelectObject(hdc, hClosePen);
      const int iInset = ScaleByDpi(3, g_uiDpi);
      MoveToEx(hdc, rcClose.left + iInset, rcClose.top + iInset, nullptr);
      LineTo(hdc, rcClose.right - iInset, rcClose.bottom - iInset);
      MoveToEx(hdc, rcClose.right - iInset, rcClose.top + iInset, nullptr);
      LineTo(hdc, rcClose.left + iInset, rcClose.bottom - iInset);
      SelectObject(hdc, hOldClosePen);
      DeleteObject(hClosePen);
    }
  }

  if (!g_overflowSessionTabs.empty() &&
      g_rcSessionOverflow.right > g_rcSessionOverflow.left) {
    RECT rc = g_rcSessionOverflow;
    const COLORREF crFill =
        BlendColor(crBar, g_themeColors.crControl,
                   g_bThemeIsDark ? 13 : 20);
    const COLORREF crBorder =
        BlendColor(g_themeColors.crControlBorder, crBar, 72);
    COLORREF crText = g_themeColors.crWindowText;

    if (!g_bUiEnabled)
      crText = BlendColor(crText, g_themeColors.crWindow, 55);

    DrawRoundedRect(hdc, rc, crFill, crBorder, ScaleByDpi(6, g_uiDpi));

    RECT rcText = rc;
    rcText.left += ScaleByDpi(8, g_uiDpi);
    rcText.right -= ScaleByDpi(8, g_uiDpi);
    SetTextColor(hdc, crText);
    std::wstring text =
        std::format(L"+{}", (int)g_overflowSessionTabs.size());
    DrawTextW(hdc, text.c_str(), -1, &rcText,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  SelectObject(hdc, hOldFont);
}

static void SelectSessionTab(int iTab, bool bBringToFront) {
  const int iMaxTab = (int)g_sessions.size();
  if (iTab < 0 || iTab > iMaxTab)
    iTab = 0;

  g_iSelectedSessionTab = iTab;
  if (iTab == 0) {
    SetWindowTextW(g_hBtnGo, L"Open");
    FocusClientEdit();
  } else {
    const Session &s = g_sessions[iTab - 1];
    SetClientInputText(s.clientName);
    SetClientInputSelection(0, -1);
    SetWindowTextW(g_hBtnGo, L"Show");
    if (bBringToFront)
      BringSessionToFront(s.pid);
  }

  UpdateIconPreviewForSelection(false);
  InvalidateSessionTabs(g_hGui);
}

static bool RequestSessionClose(DWORD pid) {
  EnumData data{pid};
  EnumWindows(EnumWindowsCallback, (LPARAM)&data);

  bool bPosted = false;
  for (HWND ew : data.windows) {
    if (IsWindowVisible(ew)) {
      PostMessageW(ew, WM_CLOSE, 0, 0);
      bPosted = true;
    }
  }

  if (bPosted)
    return true;

  HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (!hProcess)
    return false;
  const BOOL bTerminated = TerminateProcess(hProcess, 0);
  CloseHandle(hProcess);
  return bTerminated != FALSE;
}

static bool RequestSessionShutdown(DWORD pid) {
  EnumData data{pid};
  EnumWindows(EnumWindowsCallback, (LPARAM)&data);

  HWND hBrowser = nullptr;
  for (HWND hWnd : data.windows) {
    const LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    if (GetWindow(hWnd, GW_OWNER) == nullptr &&
        (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == 0) {
      hBrowser = hWnd;
      break;
    }
  }
  if (!hBrowser && !data.windows.empty())
    hBrowser = data.windows.front();
  if (!hBrowser)
    return false;

  DWORD_PTR queryResult = TRUE;
  SendMessageTimeoutW(hBrowser, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP,
                      SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &queryResult);

  // Chromium saves profile/session data and bypasses per-window close prompts
  // when Windows reports that the application session is ending.
  return PostMessageW(hBrowser, WM_ENDSESSION, TRUE, ENDSESSION_CLOSEAPP) !=
         FALSE;
}

static void CloseSessionTab(int iTab) {
  if (iTab <= 0 || iTab > (int)g_sessions.size())
    return;

  const Session &s = g_sessions[iTab - 1];
  RequestSessionClose(s.pid);
  if (g_iSelectedSessionTab == iTab)
    SelectSessionTab(0, false);
  InvalidateSessionTabs(g_hGui);
}

static void ShowSessionOverflowMenu(HWND hWnd) {
  if (g_overflowSessionTabs.empty())
    return;

  HMENU hMenu = CreatePopupMenu();
  if (!hMenu)
    return;

  constexpr UINT kOverflowShowBase = 44000;
  constexpr UINT kOverflowCloseBase = 45000;
  MENUINFO menuInfo{sizeof(menuInfo)};
  menuInfo.fMask = MIM_BACKGROUND;
  menuInfo.hbrBack = g_hbrThemeMenu;
  SetMenuInfo(hMenu, &menuInfo);
  std::vector<MenuItemData> items;
  items.reserve(g_overflowSessionTabs.size() * 2 + 1);
  const auto append = [&](UINT command, const std::wstring &text, bool separator = false) {
    items.push_back({text, nullptr, separator});
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA;
    item.fType = MFT_OWNERDRAW | (separator ? MFT_SEPARATOR : 0);
    item.wID = command;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(&items.back());
    InsertMenuItemW(hMenu, static_cast<UINT>(-1), TRUE, &item);
  };
  for (int iTab : g_overflowSessionTabs) {
    std::wstring text = GetSessionTabText(iTab);
    if (!text.empty()) {
      append(kOverflowShowBase + iTab, text);
    }
  }

  append(0, L"", true);
  for (int iTab : g_overflowSessionTabs) {
    std::wstring text = GetSessionTabText(iTab);
    if (!text.empty()) {
      std::wstring closeText = L"Close " + text;
      append(kOverflowCloseBase + iTab, closeText);
    }
  }

  POINT pt{g_rcSessionOverflow.left, g_rcSessionOverflow.bottom};
  ClientToScreen(hWnd, &pt);
  SetForegroundWindow(hWnd);
  UINT cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                              pt.x, pt.y, hWnd, nullptr);
  DestroyMenu(hMenu);
  PostMessageW(hWnd, WM_NULL, 0, 0);

  if (cmd >= kOverflowCloseBase) {
    const int iTab = (int)(cmd - kOverflowCloseBase);
    CloseSessionTab(iTab);
  } else if (cmd >= kOverflowShowBase) {
    const int iTab = (int)(cmd - kOverflowShowBase);
    SelectSessionTab(iTab, true);
  }
}

static void BringSessionToFront(DWORD pid) {
  EnumData data{pid};
  EnumWindows(EnumWindowsCallback, (LPARAM)&data);
  for (HWND ew : data.windows) {
    if (IsWindowVisible(ew)) {
      if (IsIconic(ew))
        ShowWindow(ew, SW_RESTORE);
      SetForegroundWindow(ew);
      return;
    }
  }
}

static void OnSessionStarted(DWORD pid, const std::wstring &name) {
  for (const auto &s : g_sessions) {
    if (s.pid == pid)
      return;
  }

  BrowserKind browser = g_selectedBrowser;
  {
    std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
    for (const auto &[key, active] : g_activeProfiles) {
      if (active.pid == pid) {
        browser = key.browser;
        break;
      }
    }
  }
  g_sessions.push_back({name, browser, pid});
  BuildSessionTabRects(g_hGui, nullptr);
  SelectSessionTab((int)g_sessions.size(), false);
}

static void OnSessionEnded(DWORD pid) {
  int iRemovedTab = -1;
  std::wstring removedName;
  for (int i = 0; i < (int)g_sessions.size(); ++i) {
    if (g_sessions[i].pid == pid) {
      iRemovedTab = i + 1;
      removedName = g_sessions[i].clientName;
      g_sessions.erase(g_sessions.begin() + i);
      break;
    }
  }

  if (iRemovedTab < 0)
    return;

  const bool removedSelected = g_iSelectedSessionTab == iRemovedTab;
  if (removedSelected) {
    g_iSelectedSessionTab = 0;
  } else if (g_iSelectedSessionTab > iRemovedTab) {
    --g_iSelectedSessionTab;
  }

  BuildSessionTabRects(g_hGui, nullptr);
  SelectSessionTab(g_iSelectedSessionTab, false);

  const std::wstring currentComboText = GetClientInputText();
  const bool removedSpecialProfile =
      _wcsicmp(removedName.c_str(), L"Temp") == 0 ||
      _wcsicmp(removedName.c_str(), L"Default") == 0;
  if (removedSpecialProfile && g_iSelectedSessionTab == 0 &&
      _wcsicmp(currentComboText.c_str(), removedName.c_str()) == 0) {
    SetClientInputText(L"");
    SendMessageW(g_hComboClient, CB_SETCURSEL, (WPARAM)-1, 0);
    UpdateIconPreviewForSelection(false);
  }

  RedrawWindow(g_hBtnTmpProf, nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
  RedrawWindow(g_hBtnConfig, nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);

  if (g_bExitWhenProfilesClose && g_sessions.empty()) {
    bool hasActiveProfiles = false;
    {
      std::lock_guard<std::mutex> lock(g_activeProfilesMutex);
      hasActiveProfiles = !g_activeProfiles.empty();
    }
    if (!hasActiveProfiles && !g_isLaunchInFlight.load())
      DestroyWindow(g_hGui);
  }
}
