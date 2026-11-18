#pragma once

#include <windows.h>
#include <objbase.h>
#include <oleacc.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Oleacc.lib")

namespace notes_accessibility_detail {

// Exact SDK PROPID_ACC_NAME value from oleacc.h. Keeping the property value
// local avoids INITGUID/DEFINE_GUID changes and works when another header has
// already included oleacc.h without instantiating its external GUID symbols.
inline constexpr MSAAPROPID kNameProperty{
    0x608d3df8, 0x8128, 0x4aa7,
    {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};

struct PropertyServices {
  IAccPropServices *value = nullptr;

  bool Open() noexcept {
    return SUCCEEDED(CoCreateInstance(
        __uuidof(CAccPropServices), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IAccPropServices), reinterpret_cast<void **>(&value))) && value;
  }

  ~PropertyServices() {
    if (value) value->Release();
  }
};

} // namespace notes_accessibility_detail

// Call once for each successfully created native note editor, including cached
// editors. The UI thread already owns its COM apartment. This annotates only
// the native accessible object's name: no window caption, document contents,
// provider, focus, selection or Undo state is replaced.
inline bool SetNoteAccessibleName(HWND editor) noexcept {
  if (!editor || !IsWindow(editor)) return false;
  notes_accessibility_detail::PropertyServices services;
  if (!services.Open()) return false;
  return SUCCEEDED(services.value->SetHwndPropStr(
      editor, OBJID_CLIENT, CHILDID_SELF,
      notes_accessibility_detail::kNameProperty, L"Note content"));
}

// Clear before the editor's native WM_NCDESTROY processing, while its HWND is
// still valid. A failed best-effort annotation/cleanup never blocks editing or
// destruction. Do not call from layout, resize or paint paths.
inline void ClearNoteAccessibleName(HWND editor) noexcept {
  if (!editor || !IsWindow(editor)) return;
  notes_accessibility_detail::PropertyServices services;
  if (!services.Open()) return;
  services.value->ClearHwndProps(
      editor, OBJID_CLIENT, CHILDID_SELF,
      &notes_accessibility_detail::kNameProperty, 1);
}
