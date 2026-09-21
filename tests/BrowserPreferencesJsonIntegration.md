# Browser preferences JSON integration

The utility is intentionally isolated until the owner of `ctSpaces.cpp` can
replace the old regex implementation without a merge conflict.

1. Add `BrowserPreferencesJson.h` as a `ClInclude` and
   `BrowserPreferencesJson.cpp` as a `ClCompile` in `ctSpaces.vcxproj` (and its
   filters file).
2. Include `BrowserPreferencesJson.h` from `ctSpaces.cpp`.
3. Replace the body of `SetBrowserStartupPreference` with a call for only the
   saved client's `Default/Preferences` file:

   ```cpp
   browser_preferences::UpdateStartupPreferencesFile(
       profilePath / L"Default" / L"Preferences", restoreTabs, true);
   ```

   Chromium startup preferences used here are profile preferences; do not scan
   or rewrite `Local State`.
4. Keep the existing browser command-line restore flag. The JSON preference is
   the persisted companion to that launch behavior.
5. Call this only for a saved standard client whose restore-tabs preference is
   being applied. Do not call it while opening an unsaved Default editor profile
   or a temporary profile, and do not call it from fresh-profile cleanup. A
   Default editor session must affect the template only when the user explicitly
   saves those changes into the default profile/template.

`unsafe_target`, `invalid_json`, and `io_error` deliberately leave the original
file untouched. They should be logged if diagnostics are available, but should
not trigger a fallback regex edit.
