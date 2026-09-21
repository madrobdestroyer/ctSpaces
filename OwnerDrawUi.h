#pragma once

#include <algorithm>
#include <string_view>
#include <windows.h>

namespace owner_draw_ui {

inline UINT GetDpi(HWND sourceWindow, HWND mainWindow) {
  if (sourceWindow && IsWindow(sourceWindow))
    return GetDpiForWindow(sourceWindow);
  return mainWindow && IsWindow(mainWindow) ? GetDpiForWindow(mainWindow)
                                            : USER_DEFAULT_SCREEN_DPI;
}

inline HFONT GetFont(HWND sourceWindow, HWND mainWindow,
                     bool preserveMainStrong, HFONT mainStrong,
                     HFONT mainRegular) {
  if (sourceWindow && IsWindow(sourceWindow)) {
    const bool isMainControl =
        GetAncestor(sourceWindow, GA_ROOT) == mainWindow;
    if (preserveMainStrong && mainStrong && isMainControl)
      return mainStrong;
    const auto controlFont = reinterpret_cast<HFONT>(
        SendMessageW(sourceWindow, WM_GETFONT, 0, 0));
    if (controlFont)
      return controlFont;
    if (!isMainControl)
      return reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
  if (preserveMainStrong && mainStrong)
    return mainStrong;
  return mainRegular
             ? mainRegular
             : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

inline bool MeasureText(std::wstring_view text, HFONT font, SIZE &size) {
  size = {};
  HDC dc = GetDC(nullptr);
  if (!dc)
    return false;
  HGDIOBJ oldFont = SelectObject(
      dc, font ? font : reinterpret_cast<HFONT>(
                             GetStockObject(DEFAULT_GUI_FONT)));
  const bool measured =
      GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()),
                            &size) != FALSE;
  SelectObject(dc, oldFont);
  ReleaseDC(nullptr, dc);
  return measured;
}

inline int HorizontalTextExtent(std::wstring_view text, HFONT font, UINT dpi,
                                int paddingDip) {
  SIZE size{};
  if (!MeasureText(text, font, size))
    return 0;
  return size.cx + MulDiv(paddingDip, dpi, USER_DEFAULT_SCREEN_DPI);
}

struct MenuMetrics {
  UINT width = 0;
  UINT height = 0;
  SIZE text{};
};

inline MenuMetrics MeasureMenuItem(std::wstring_view text, HFONT font, UINT dpi,
                                   int iconPixels, int minimumWidth) {
  MenuMetrics metrics{};
  MeasureText(text, font, metrics.text);
  const int padding = MulDiv(4, dpi, USER_DEFAULT_SCREEN_DPI);
  const int iconPadding = MulDiv(8, dpi, USER_DEFAULT_SCREEN_DPI);
  metrics.height = static_cast<UINT>((std::max)(
      iconPixels + padding,
      MulDiv(24, dpi, USER_DEFAULT_SCREEN_DPI)));
  metrics.width = static_cast<UINT>(
      padding + iconPixels + iconPadding + metrics.text.cx +
      MulDiv(32, dpi, USER_DEFAULT_SCREEN_DPI));
  if (minimumWidth > 0)
    metrics.width =
        (std::max)(metrics.width, static_cast<UINT>(minimumWidth));
  return metrics;
}

} // namespace owner_draw_ui
