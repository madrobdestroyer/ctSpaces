#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <stdexcept>

// These hooks observe native drawing into an offscreen DIB. No desktop input
// or screenshots are used by the main-window repaint scenarios.
struct ChromeInvalidation { HWND window; RECT bounds; BOOL erase; };
struct ChromeRedrawRequest { HWND window; UINT flags; };
static std::vector<ChromeInvalidation> chromeInvalidations;
static std::vector<ChromeRedrawRequest> chromeRedrawRequests;
static bool chromeRecordRedraws=false;
static unsigned chromeRefusedDesktopPaintCalls=0;
static void ChromeRequireOwnedWindow(HWND window) {
  DWORD pid=0;
  if(!window || !IsWindow(window) || !GetWindowThreadProcessId(window,&pid) || pid!=GetCurrentProcessId()) {
    ++chromeRefusedDesktopPaintCalls;
    throw std::runtime_error("Background main-chrome fixture refused a null, invalid, or foreign painting HWND before calling Windows");
  }
}
static bool chromeRecordInvalidations=false,chromeFailPaintDC=false,chromeFailPaintBitmap=false,chromeFailPaintSelect=false,chromeFailPaintPresent=false;
static HDC chromePaintTarget=nullptr;
static unsigned chromeTargetFills=0,chromePresentations=0;
// The selector keyboard test owns hidden controls. Observe forwarding without
// letting Alt+F4 reach a real close command or changing the user's key state.
static HWND chromeSelectorKeyTarget=nullptr;
static bool chromeSelectorAltDown=false;
static unsigned chromeSelectorKeyForwards=0;
static SHORT WINAPI ChromeGetKeyState(int key) {
  if(chromeSelectorKeyTarget && key==VK_MENU)
    return chromeSelectorAltDown ? static_cast<SHORT>(0x8000) : 0;
  return ::GetKeyState(key);
}
static LRESULT WINAPI ChromeDefSubclassProc(HWND window,UINT message,WPARAM key,LPARAM data) {
  if(window==chromeSelectorKeyTarget &&
      (message==WM_KEYDOWN || message==WM_SYSKEYDOWN) && key==VK_F4) {
    ++chromeSelectorKeyForwards;
    return 0x433;
  }
  return ::DefSubclassProc(window,message,key,data);
}
static BOOL WINAPI ChromeInvalidateRect(HWND window,const RECT *area,BOOL erase) {
  ChromeRequireOwnedWindow(window);
  if(chromeRecordInvalidations) {RECT bounds{};if(area) bounds=*area;else GetClientRect(window,&bounds);chromeInvalidations.push_back({window,bounds,erase});}
  return ::InvalidateRect(window,area,erase);
}
static BOOL WINAPI ChromeRedrawWindow(HWND window,const RECT *area,HRGN region,UINT flags) {
  ChromeRequireOwnedWindow(window);
  if(chromeRecordRedraws) chromeRedrawRequests.push_back({window,flags});
  return ::RedrawWindow(window,area,region,flags);
}
static BOOL WINAPI ChromeValidateRect(HWND window,const RECT *area) {
  ChromeRequireOwnedWindow(window);
  return ::ValidateRect(window,area);
}
static HDC WINAPI ChromeCreateCompatibleDC(HDC target) {return chromeFailPaintDC ? nullptr : ::CreateCompatibleDC(target);}
static HBITMAP WINAPI ChromeCreateCompatibleBitmap(HDC target,int width,int height) {return chromeFailPaintBitmap ? nullptr : ::CreateCompatibleBitmap(target,width,height);}
static HGDIOBJ WINAPI ChromeSelectObject(HDC target,HGDIOBJ object) {
  if(chromeFailPaintSelect && GetObjectType(object)==OBJ_BITMAP) {chromeFailPaintSelect=false;return HGDI_ERROR;}
  return ::SelectObject(target,object);
}
static int WINAPI ChromeFillRect(HDC target,const RECT *area,HBRUSH brush) {
  if(target==chromePaintTarget) ++chromeTargetFills;
  return ::FillRect(target,area,brush);
}
static BOOL WINAPI ChromeBitBlt(HDC target,int x,int y,int width,int height,HDC source,int sx,int sy,DWORD operation) {
  if(target==chromePaintTarget) ++chromePresentations;
  if(chromeFailPaintPresent && target==chromePaintTarget) return FALSE;
  return ::BitBlt(target,x,y,width,height,source,sx,sy,operation);
}
#define InvalidateRect ChromeInvalidateRect
#define RedrawWindow ChromeRedrawWindow
#define ValidateRect ChromeValidateRect
#define CreateCompatibleDC ChromeCreateCompatibleDC
#define CreateCompatibleBitmap ChromeCreateCompatibleBitmap
#define SelectObject ChromeSelectObject
#define FillRect ChromeFillRect
#define BitBlt ChromeBitBlt
#define GetKeyState ChromeGetKeyState
#define DefSubclassProc ChromeDefSubclassProc
#define wWinMain CtSpacesProductionWinMain
#ifdef CTSPACES_REDRAW_BASELINE
#include "../build/redraw-regression-2026-10-15/pinned-repaint/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef InvalidateRect
#undef RedrawWindow
#undef ValidateRect
#undef CreateCompatibleDC
#undef CreateCompatibleBitmap
#undef SelectObject
#undef FillRect
#undef BitBlt
#undef GetKeyState
#undef DefSubclassProc

#include <iostream>

static unsigned chromeChecks=0;
static void ChromeCheck(bool condition,const char *message) {
  if(!condition) throw std::runtime_error(message);
  ++chromeChecks;std::cout << "PASS " << message << std::endl;
}

#include "MainRepaintTests.inl"
#include "ClientSelectorPaintTests.inl"
#include "MainPinnedHoverStress.inl"

int wmain(int argc,wchar_t **argv) {
  try {
    ChromeNativeObserver observer;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hInst=GetModuleHandleW(nullptr);
    g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Chrome regression fixture",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,600,180,nullptr,nullptr,g_hInst,nullptr);
    ChromeCheck(g_hGui!=nullptr,"Create a disposable hidden parent for production button rendering");
    LoadBakedThemesFromHeader();
    const fs::path captures=argc>1 ? fs::absolute(argv[1]) : fs::path{};
    if(!captures.empty()) fs::create_directories(captures);
    for(const wchar_t *themeName:{L"Dark - Gothic",L"Marine"}) {
      const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==themeName;});
      ChromeCheck(theme!=g_aCustomThemes.end(),"Use a real dark or light application theme");
      g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
      for(const auto [id,label]:{std::pair{IDC_BTN_NOTES,L"Notes"},std::pair{IDC_BTN_CLOSE_ALL,L"Close all"}}) {
        HWND button=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|BS_OWNERDRAW,0,0,120,36,g_hGui,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_hInst,nullptr);
        ChromeCheck(button!=nullptr,"Create the main launcher's actual button ID and label");
        SetWindowSubclass(button,ButtonHotSubclassProc,1,0);
        HDC screen=GetDC(button),dc=CreateCompatibleDC(screen);RECT area{};GetClientRect(button,&area);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void *pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto prior=SelectObject(dc,bitmap);
        const auto render=[&](UINT appearance,const wchar_t *suffix) {
          DrawThemedButtonCore(dc,area,id,appearance,button);GdiFlush();
          std::vector<DWORD> result(static_cast<DWORD *>(pixels),static_cast<DWORD *>(pixels)+area.right*area.bottom);
          for(auto &pixel:result) pixel &= 0xFFFFFF;
          if(!captures.empty()) {
            const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
            Gdiplus::Bitmap snapshot(bitmap,nullptr);
            const auto name=std::wstring(g_bThemeIsDark ? L"gothic-" : L"marine-")+std::to_wstring(id)+L"-"+suffix+L".png";
            ChromeCheck(snapshot.Save((captures/name).c_str(),&png,nullptr)==Gdiplus::Ok,"Save production-renderer evidence");
          }
          return result;
        };
        g_hHotButton=nullptr;const auto normal=render(0,L"normal");
        SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(30,18));
        ChromeCheck(g_hHotButton==button,"Actual mouse tracking marks the main button hot");
        ChromeCheck(render(0,L"hover")!=normal,"Main buttons visibly change on hover in both themes");
        SendMessageW(button,WM_MOUSELEAVE,0,0);
        ChromeCheck(g_hHotButton!=button && render(0,L"restored")==normal,"Mouse leave restores the exact resting appearance");
        const auto disabled=render(ODS_DISABLED,L"disabled");
        SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(30,18));
        ChromeCheck(render(ODS_DISABLED,L"disabled-hot")==disabled,"Disabled main buttons ignore hover even if tracking retains a hot HWND");
        SendMessageW(button,WM_MOUSELEAVE,0,0);
        SelectObject(dc,prior);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(button,screen);DestroyWindow(button);
      }
    }
    TestMainRepaint(captures);
    RunClientSelectorPaintTests(captures);
    TestPinnedNativeHoverStress(captures);
    const auto notesStep=std::find_if(kQuickTourSteps.begin(),kQuickTourSteps.end(),[](const auto &step){return std::wstring_view(step.title)==L"Keep Client Notes";});
    ChromeCheck(notesStep!=kQuickTourSteps.end() && notesStep->target==QuickTourTarget::Notes,
        "The Notes tour uses the real Notes button as its highlight target");
    const std::wstring illustration=QuickTourDemoAccessibleText(QuickTourDemo::ClientNotes);
    ChromeCheck(illustration.find(L"Notes beside Close all")!=std::wstring::npos && illustration.find(L"Settings >") == std::wstring::npos,
        "The Notes tour illustration matches the current entry point");
    DestroyWindow(g_hGui);g_hGui=nullptr;Gdiplus::GdiplusShutdown(g_gdiplusToken);
    ChromeCheck(chromeRefusedDesktopPaintCalls==0,"Background production painting never requests a null, invalid, or foreign HWND");
    observer.Audit();
    std::cout << "Main chrome: " << chromeChecks << " checks passed.\n";return 0;
  } catch(const std::exception &error) {std::cerr << "FAIL: " << error.what() << std::endl;return 1;}
}
