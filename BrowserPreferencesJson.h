#pragma once

#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

namespace browser_preferences {

inline constexpr size_t kMaximumPreferencesJsonBytes = 64u * 1024u * 1024u;

enum class JsonEditResult {
  unchanged,
  updated,
  invalid_json,
  unsafe_target,
};

enum class FileUpdateResult {
  missing,
  unchanged,
  updated,
  invalid_json,
  unsafe_target,
  io_error,
};

enum class TabLayoutBrowser { Edge, Chrome, Brave };

// Edits one native profile preference only: Edge edge.vertical_tabs.opened,
// Chrome vertical_tabs.enabled, or Brave brave.tabs.vertical_tabs_enabled.
// Values are JSON booleans. Missing objects/leaf are minimally inserted;
// duplicate (including escaped) target keys, non-object ancestors and existing
// non-boolean leaves fail closed. Other preference bytes are preserved.
JsonEditResult EditTabLayoutPreferencesJson(
    std::string_view input, TabLayoutBrowser browser, bool vertical,
    std::string &output) noexcept;

// Uses the same guarded, bounded publisher as startup preferences. Requires an
// existing Preferences file; it never creates a profile or changes feature flags.
FileUpdateResult UpdateTabLayoutPreferencesFile(
    const std::filesystem::path &path, TabLayoutBrowser browser, bool vertical,
    std::wstring *errorDetails = nullptr) noexcept;

// Updates only these direct JSON paths:
//   session.restore_on_startup
//   session.startup_urls
//   session.urls_to_restore_on_startup
//   profile.exit_type
//   profile.exited_cleanly
//
// Existing bytes outside the replaced values (or the minimal inserted session
// preference) are preserved. Oversized input, duplicate target paths, and
// non-object session or profile values fail closed with unsafe_target.
JsonEditResult EditStartupPreferencesJson(std::string_view input,
                                          bool restoreTabs,
                                          bool ensureSessionPreference,
                                          std::string &output) noexcept;

// Writes a validated edit through the shared native snapshot/ancestor guarded
// publisher. Concurrent writers are rejected or retained in recovery; targets
// and staged output beyond the limit fail closed. Missing files are not created.
FileUpdateResult UpdateStartupPreferencesFile(
    const std::filesystem::path &path, bool restoreTabs,
    bool ensureSessionPreference, std::wstring *errorDetails = nullptr) noexcept;

} // namespace browser_preferences
