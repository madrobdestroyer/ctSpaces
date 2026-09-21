#include "../OwnerDrawUi.h"
#include "../CleanupResultPages.h"

#include <array>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace {

int DialogWidthPixels(int dialogUnits, const SIZE &alphabet) {
  // Same average-character calculation used by the dialog manager.
  const int average = (alphabet.cx / 26 + 1) / 2;
  return MulDiv(dialogUnits, average, 4);
}

int DialogHeightPixels(int dialogUnits, const TEXTMETRICW &metrics) {
  return MulDiv(dialogUnits, metrics.tmHeight, 8);
}

bool ReadFontMetrics(HFONT font, SIZE &alphabet, TEXTMETRICW &metrics) {
  HDC dc = GetDC(nullptr);
  if (!dc)
    return false;
  HGDIOBJ old = SelectObject(dc, font);
  constexpr wchar_t letters[] =
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  const bool ok =
      GetTextExtentPoint32W(dc, letters, 52, &alphabet) != FALSE &&
      GetTextMetricsW(dc, &metrics) != FALSE;
  SelectObject(dc, old);
  ReleaseDC(nullptr, dc);
  return ok;
}

bool FitsDialogControl(std::wstring_view text, HFONT font, UINT dpi,
                       int widthDialogUnits, int heightDialogUnits,
                       int horizontalPaddingDip = 6) {
  SIZE alphabet{}, textSize{};
  TEXTMETRICW metrics{};
  if (!ReadFontMetrics(font, alphabet, metrics) ||
      !owner_draw_ui::MeasureText(text, font, textSize)) {
    return false;
  }
  const int width = DialogWidthPixels(widthDialogUnits, alphabet) -
                    MulDiv(horizontalPaddingDip, dpi,
                           USER_DEFAULT_SCREEN_DPI);
  const int height = DialogHeightPixels(heightDialogUnits, metrics);
  return textSize.cx <= width && textSize.cy <= height;
}

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int wmain() {
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = TestWindowProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"ctSpacesOwnerDrawUiTest";
  if (!RegisterClassW(&windowClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::wcerr << L"Could not register the UI test window." << std::endl;
    return 1;
  }

  HWND mainWindow = CreateWindowW(windowClass.lpszClassName, L"main",
                                  WS_OVERLAPPED, 0, 0, 400, 200, nullptr,
                                  nullptr, windowClass.hInstance, nullptr);
  HWND secondaryWindow = CreateWindowW(
      windowClass.lpszClassName, L"secondary", WS_OVERLAPPED, 0, 0, 400,
      200, nullptr, nullptr, windowClass.hInstance, nullptr);
  HWND mainButton = CreateWindowW(L"BUTTON", L"Open", WS_CHILD, 0, 0, 100,
                                  30, mainWindow, nullptr,
                                  windowClass.hInstance, nullptr);
  HWND secondaryButton = CreateWindowW(
      L"BUTTON", L"Delete Selected (999)", WS_CHILD, 0, 0, 180, 30,
      secondaryWindow, nullptr, windowClass.hInstance, nullptr);
  HWND secondaryEdit = CreateWindowW(L"EDIT", L"", WS_CHILD, 0, 0, 180, 30,
                                     secondaryWindow, nullptr,
                                     windowClass.hInstance, nullptr);
  HWND acknowledgement = CreateWindowW(
      L"BUTTON",
      L"I understand that all selected clients will be permanently deleted.",
      WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 400, 30, secondaryWindow, nullptr,
      windowClass.hInstance, nullptr);
  HWND clientList = CreateWindowW(L"LISTBOX", L"", WS_CHILD | LBS_EXTENDEDSEL |
                                  WS_HSCROLL, 0, 0, 400, 120,
                                  secondaryWindow, nullptr,
                                  windowClass.hInstance, nullptr);
  if (!mainWindow || !secondaryWindow || !mainButton || !secondaryButton ||
      !secondaryEdit || !acknowledgement || !clientList) {
    std::wcerr << L"Could not create UI test controls." << std::endl;
    return 1;
  }

  constexpr std::array<UINT, 4> testedDpis = {96, 144, 192, 240};
  for (UINT dpi : testedDpis) {
    HFONT regular = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE,
        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT strong = CreateFontW(
        -MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE,
        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (!regular || !strong) {
      std::wcerr << L"Could not create DPI test fonts at " << dpi << L" DPI."
                 << std::endl;
      return 1;
    }
    SendMessageW(mainButton, WM_SETFONT, reinterpret_cast<WPARAM>(regular),
                 FALSE);
    SendMessageW(secondaryButton, WM_SETFONT,
                 reinterpret_cast<WPARAM>(regular), FALSE);
    SendMessageW(secondaryEdit, WM_SETFONT, reinterpret_cast<WPARAM>(regular),
                 FALSE);
    SendMessageW(acknowledgement, WM_SETFONT,
                 reinterpret_cast<WPARAM>(regular), FALSE);
    SendMessageW(clientList, WM_SETFONT, reinterpret_cast<WPARAM>(regular),
                 FALSE);

    if (owner_draw_ui::GetFont(mainButton, mainWindow, true, strong,
                               regular) != strong) {
      std::wcerr << L"The launcher primary font was not preserved at " << dpi
                 << L" DPI." << std::endl;
      return 1;
    }
    if (owner_draw_ui::GetFont(secondaryButton, mainWindow, true, strong,
                               strong) != regular ||
        owner_draw_ui::GetFont(secondaryEdit, mainWindow, false, strong,
                               strong) != regular) {
      std::wcerr << L"A secondary control inherited the launcher font at "
                 << dpi << L" DPI." << std::endl;
      return 1;
    }

    const int menuIcon = MulDiv(16, dpi, USER_DEFAULT_SCREEN_DPI);
    const int fixedMenuWidth = MulDiv(180, dpi, USER_DEFAULT_SCREEN_DPI);
    for (const std::wstring_view label :
         {L"Undo", L"Cut", L"Copy", L"Paste", L"Delete", L"Select All"}) {
      const auto metrics = owner_draw_ui::MeasureMenuItem(
          label, regular, dpi, menuIcon, 0);
      const int textLane = fixedMenuWidth -
                           MulDiv(52, dpi, USER_DEFAULT_SCREEN_DPI);
      if (metrics.height < static_cast<UINT>(MulDiv(24, dpi, 96)) ||
          metrics.text.cx > textLane) {
        std::wcerr << L"The edit menu clips '" << label << L"' at " << dpi
                   << L" DPI." << std::endl;
        return 1;
      }
    }

    const struct {
      std::wstring_view text;
      int widthDlu;
    } buttons[] = {{L"Select All", 65},
                   {L"Clear Selection", 86},
                   {L"Delete Selected (999)", 112},
                   {L"Cancel", 68},
                   {L"OK", 68}};
    for (const auto &button : buttons) {
      if (!FitsDialogControl(button.text, regular, dpi, button.widthDlu, 14)) {
        std::wcerr << L"The cleanup button clips '" << button.text << L"' at "
                   << dpi << L" DPI." << std::endl;
        return 1;
      }
    }
    if (!FitsDialogControl(
            L"I understand that all selected clients will be permanently deleted.",
            regular, dpi, 366, 12, 2)) {
      std::wcerr << L"The cleanup acknowledgement is unreadable at " << dpi
                 << L" DPI." << std::endl;
      return 1;
    }

    SIZE alphabet{}, acknowledgementText{};
    TEXTMETRICW fontMetrics{};
    if (!ReadFontMetrics(regular, alphabet, fontMetrics) ||
        !owner_draw_ui::MeasureText(
            L"I understand that all selected clients will be permanently deleted.",
            regular, acknowledgementText)) {
      return 1;
    }
    const int acknowledgementWidth = DialogWidthPixels(366, alphabet);
    const int acknowledgementHeight = DialogHeightPixels(12, fontMetrics);
    SetWindowPos(acknowledgement, nullptr, 0, 0, acknowledgementWidth,
                 acknowledgementHeight, SWP_NOMOVE | SWP_NOZORDER);
    RECT acknowledgementRect{};
    GetClientRect(acknowledgement, &acknowledgementRect);
    const int checkboxAndGap = MulDiv(18, dpi, USER_DEFAULT_SCREEN_DPI);
    if (acknowledgementText.cx + checkboxAndGap >
            acknowledgementRect.right - acknowledgementRect.left ||
        acknowledgementText.cy >
            acknowledgementRect.bottom - acknowledgementRect.top) {
      std::wcerr << L"The actual acknowledgement control clips at " << dpi
                 << L" DPI." << std::endl;
      return 1;
    }

    const std::wstring longRow =
        std::wstring(240, L'W') + L"    |    Last opened: 2026-09-21";
    SIZE longText{};
    if (!owner_draw_ui::MeasureText(longRow, regular, longText))
      return 1;
    const int horizontalExtent = owner_draw_ui::HorizontalTextExtent(
        longRow, regular, dpi, 16);
    SendMessageW(clientList, LB_RESETCONTENT, 0, 0);
    SendMessageW(clientList, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(longRow.c_str()));
    SendMessageW(clientList, LB_SETHORIZONTALEXTENT, horizontalExtent, 0);
    const int storedExtent = static_cast<int>(
        SendMessageW(clientList, LB_GETHORIZONTALEXTENT, 0, 0));
    if (storedExtent <
        longText.cx + MulDiv(16, dpi, USER_DEFAULT_SCREEN_DPI)) {
      std::wcerr << L"The long-client horizontal extent clips at " << dpi
                 << L" DPI." << std::endl;
      return 1;
    }

    SendMessageW(mainButton, WM_SETFONT, 0, FALSE);
    SendMessageW(secondaryButton, WM_SETFONT, 0, FALSE);
    SendMessageW(secondaryEdit, WM_SETFONT, 0, FALSE);
    SendMessageW(acknowledgement, WM_SETFONT, 0, FALSE);
    SendMessageW(clientList, WM_SETFONT, 0, FALSE);
    DeleteObject(regular);
    DeleteObject(strong);
  }

  std::wstring longCleanupResult =
      L"Deleted 0 client(s).\nSkipped 100 item(s).\n";
  for (int index = 0; index < 100; ++index) {
    longCleanupResult += std::format(
        L"Client {:03}: process inspection was inconclusive, so this client "
        L"was left unchanged.\n",
        index);
  }
  const auto pages = cleanup_result::Paginate(longCleanupResult, 2400, 18);
  if (pages.size() < 2) {
    std::wcerr << L"The long cleanup fallback was not paginated." << std::endl;
    return 1;
  }
  std::wstring reconstructed;
  for (const auto &page : pages) {
    if (page.size() > 2400) {
      std::wcerr << L"A cleanup fallback page exceeded its bound."
                 << std::endl;
      return 1;
    }
    reconstructed += page;
  }
  if (reconstructed != longCleanupResult ||
      reconstructed.find(L"Client 000") == std::wstring::npos ||
      reconstructed.find(L"Client 099") == std::wstring::npos) {
    std::wcerr << L"The paginated cleanup fallback lost outcome details."
               << std::endl;
    return 1;
  }

  std::wstring lineHeavyResult;
  for (int index = 0; index < 100; ++index)
    lineHeavyResult += std::format(L"{}\r\n", index % 10);
  lineHeavyResult += L"Pair: \xD83D\xDE80 done";
  const auto linePages =
      cleanup_result::Paginate(lineHeavyResult, 2400, 18);
  reconstructed.clear();
  for (const auto &page : linePages) {
    if (static_cast<size_t>(std::count(page.begin(), page.end(), L'\n')) > 18 ||
        (!page.empty() && page.back() == L'\r') ||
        (!page.empty() && page.back() >= 0xD800 && page.back() <= 0xDBFF)) {
      std::wcerr << L"A cleanup fallback page split an atomic text unit or "
                    L"exceeded its line bound."
                 << std::endl;
      return 1;
    }
    reconstructed += page;
  }
  if (reconstructed != lineHeavyResult) {
    std::wcerr << L"Line-bounded pagination lost UTF-16 cleanup text."
               << std::endl;
    return 1;
  }

  DestroyWindow(secondaryWindow);
  DestroyWindow(mainWindow);
  std::wcout << L"Owner-draw font and DPI metrics passed at 96, 144, 192, and "
                L"240 DPI; the 100-client fallback remained complete and "
                L"bounded."
             << std::endl;
  return 0;
}
