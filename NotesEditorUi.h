#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include "Resource.h"

namespace notes_editor_ui {

inline Gdiplus::Color Color(COLORREF value) {
  return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
}

inline bool HasActionIcon(UINT id) {
  return id==IDC_NOTES_FIND_TOGGLE || id==IDC_NOTES_SNIPPETS ||
      id==IDC_NOTES_TEMPLATES || id==IDC_NOTES_WORKLOG || id==IDC_NOTES_FORMAT ||
      id==IDC_NOTES_LIBRARY || id==IDC_NOTES_TRASH;
}

// Compact line icons supplement the existing labels; their controls, hit areas
// and keyboard commands stay unchanged.
inline void DrawActionIcon(HDC dc,const RECT &bounds,UINT id,COLORREF color) {
  using namespace Gdiplus;
  Graphics graphics(dc);graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.TranslateTransform((bounds.left+bounds.right)/2.0f,(bounds.top+bounds.bottom)/2.0f);
  const float scale=(bounds.bottom-bounds.top)/20.0f;graphics.ScaleTransform(scale,scale);
  Pen pen(Color(color),1.7f);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);pen.SetLineJoin(LineJoinRound);
  const auto line=[&](float x1,float y1,float x2,float y2){graphics.DrawLine(&pen,x1,y1,x2,y2);};
  switch(id) {
  case IDC_NOTES_FIND_TOGGLE:
    graphics.DrawEllipse(&pen,-7.0f,-7.0f,10.0f,10.0f);line(2,2,7,7);break;
  case IDC_NOTES_SNIPPETS:
    line(-4,-5,-8,0);line(-8,0,-4,5);line(4,-5,8,0);line(8,0,4,5);line(2,-7,-2,7);break;
  case IDC_NOTES_TEMPLATES:
    graphics.DrawRectangle(&pen,-6.0f,-8.0f,12.0f,16.0f);line(-3,-4,3,-4);line(-3,0,3,0);line(-3,4,1,4);break;
  case IDC_NOTES_LIBRARY:
    graphics.DrawRectangle(&pen,-4.0f,-6.0f,11.0f,14.0f);line(-7,5,-7,-9);line(-7,-9,4,-9);line(-1,-2,4,-2);line(-1,2,4,2);break;
  case IDC_NOTES_WORKLOG:
    graphics.DrawEllipse(&pen,-8.0f,-8.0f,13.0f,13.0f);line(-1.5f,-5,-1.5f,-1.5f);line(-1.5f,-1.5f,1.5f,0);line(5,2,5,8);line(2,5,8,5);break;
  case IDC_NOTES_FORMAT:
    line(-7,-7,7,-7);line(0,-7,0,6);line(-4,6,4,6);break;
  case IDC_NOTES_TRASH:
    line(-7,-5,7,-5);line(-3,-5,-3,-8);line(-3,-8,3,-8);line(3,-8,3,-5);
    line(-5,-3,-4,8);line(-4,8,4,8);line(4,8,5,-3);line(-1.5f,-1,-1.5f,5);line(1.5f,-1,1.5f,5);break;
  }
}

// Subtle vertical lighting gives the reference's controls depth without
// introducing bitmap assets or losing sharpness on another display scale.
inline void DrawSurface(HDC dc, const RECT &bounds, COLORREF top,
                        COLORREF bottom, COLORREF border, float radius,
                        float stroke = 1.0f) {
  using namespace Gdiplus;
  Graphics graphics(dc);
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  const float x = static_cast<float>(bounds.left) + stroke / 2;
  const float y = static_cast<float>(bounds.top) + stroke / 2;
  const float w = static_cast<float>(bounds.right - bounds.left) - stroke;
  const float h = static_cast<float>(bounds.bottom - bounds.top) - stroke;
  if (w <= 0 || h <= 0) return;
  const float d = (std::min)(radius * 2, (std::min)(w, h));
  GraphicsPath path;
  path.AddArc(x, y, d, d, 180, 90);
  path.AddArc(x + w - d, y, d, d, 270, 90);
  path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
  path.AddArc(x, y + h - d, d, d, 90, 90);
  path.CloseFigure();
  LinearGradientBrush fill(PointF(x, y), PointF(x, y + h), Color(top), Color(bottom));
  graphics.FillPath(&fill, &path);
  Pen outline(Color(border), stroke);
  graphics.DrawPath(&outline, &path);
}

// Draw at a consistent logical size, with smooth strokes at every display DPI.
inline void DrawIcon(HDC dc, const RECT &bounds, UINT id, COLORREF color,
                     COLORREF highlight = RGB(249, 211, 66)) {
  using namespace Gdiplus;
  Graphics graphics(dc);
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
  const float scale = (bounds.bottom - bounds.top) / 44.0f;
  graphics.TranslateTransform((bounds.left + bounds.right) / 2.0f,
                              (bounds.top + bounds.bottom) / 2.0f);
  graphics.ScaleTransform(scale, scale);
  Pen pen(Color(color), 2.1f);
  pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
  SolidBrush ink(Color(color));
  auto line = [&](float x1, float y1, float x2, float y2) {
    graphics.DrawLine(&pen, x1, y1, x2, y2);
  };
  switch (id) {
  case IDCANCEL:
    line(-6, -6, 6, 6); line(6, -6, -6, 6); break;
  case IDC_NOTES_BOLD:
  case IDC_NOTES_ITALIC:
  case IDC_NOTES_UNDERLINE:
  case IDC_NOTES_CLEAR: {
    const wchar_t *text = id == IDC_NOTES_BOLD ? L"B" : id == IDC_NOTES_ITALIC ? L"I" :
                          id == IDC_NOTES_UNDERLINE ? L"U" : L"T";
    const int style = id == IDC_NOTES_BOLD ? FontStyleBold :
                      id == IDC_NOTES_ITALIC || id == IDC_NOTES_CLEAR ? FontStyleItalic : FontStyleRegular;
    Font font(id == IDC_NOTES_ITALIC || id == IDC_NOTES_CLEAR ? L"Cambria" : L"Segoe UI", 27.0f, style, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter); format.SetLineAlignment(StringAlignmentCenter);
    RectF box(-18, -21, id == IDC_NOTES_CLEAR ? 28.0f : 36.0f, 40);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.DrawString(text, 1, &font, box, &format, &ink);
    if (id == IDC_NOTES_UNDERLINE) line(-8, 12, 8, 12);
    if (id == IDC_NOTES_CLEAR) { line(-10, 12, 4, 12); line(6, 5, 13, 12); line(13, 5, 6, 12); }
    break;
  }
  case IDC_NOTES_HIGHLIGHT: {
    GraphicsPath marker;
    marker.AddLine(-8, 6, 5, -10); marker.AddLine(5, -10, 11, -4);
    marker.AddLine(11, -4, -2, 12); marker.CloseFigure();
    graphics.DrawPath(&pen, &marker); line(-5, 2, 2, 8); line(-8, 6, -9, 11);
    SolidBrush yellow(Color(highlight));
    graphics.FillRectangle(&yellow, -10.0f, 14.0f, 22.0f, 3.0f);
    break;
  }
  case IDC_NOTES_BULLETS:
  case IDC_NOTES_NUMBERS:
    for (int i = 0; i != 3; ++i) {
      const float y = -10.0f + i * 10;
      line(-1, y, 12, y);
      if (id == IDC_NOTES_BULLETS) graphics.FillEllipse(&ink, -12.0f, y - 1.8f, 3.6f, 3.6f);
      else {
        Font font(L"Segoe UI", 9.5f, FontStyleRegular, UnitPixel);
        wchar_t digit[] = {static_cast<wchar_t>(L'1' + i), 0};
        graphics.DrawString(digit, 1, &font, PointF(-13.0f, y - 7.0f), &ink);
      }
    }
    break;
  case IDC_NOTES_CHECKLIST: {
    GraphicsPath box;
    box.AddArc(-11, -11, 5, 5, 180, 90); box.AddArc(6, -11, 5, 5, 270, 90);
    box.AddArc(6, 6, 5, 5, 0, 90); box.AddArc(-11, 6, 5, 5, 90, 90); box.CloseFigure();
    graphics.DrawPath(&pen, &box); line(-6, -1, -1, 4); line(-1, 4, 7, -5);
    break;
  }
  case IDC_NOTES_LINK: {
    graphics.RotateTransform(40);
    GraphicsPath first, second;
    first.AddLine(-5, -2, -5, -9); first.AddArc(-5, -14, 10, 10, 180, 180);
    first.AddLine(5, -9, 5, -2);
    second.AddLine(-5, 2, -5, 9); second.AddArc(-5, 4, 10, 10, 180, -180);
    second.AddLine(5, 9, 5, 2);
    graphics.DrawPath(&pen, &first); graphics.DrawPath(&pen, &second);
    line(0, -6, 0, 6); break;
  }
  case IDC_NOTES_UNDO:
  case IDC_NOTES_REDO: {
    if (id == IDC_NOTES_REDO) graphics.ScaleTransform(-1, 1);
    GraphicsPath curve;
    curve.AddBezier(-10, -4, 0, -4, 11, -7, 11, 3);
    curve.AddBezier(11, 3, 11, 10, 4, 12, -2, 10);
    graphics.DrawPath(&pen, &curve); line(-10, -4, -3, -11); line(-10, -4, -3, 3);
    break;
  }
  case IDC_NOTES_ADD_TAB:
    line(-9, 0, 9, 0); line(0, -9, 0, 9); break;
  }
}

} // namespace notes_editor_ui
