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

// Writes a validated edit to a unique sibling file, flushes it, and atomically
// replaces the original with ReplaceFileW. Reparse-point and oversized targets
// fail closed with unsafe_target. The original is untouched on error.
FileUpdateResult UpdateStartupPreferencesFile(
    const std::filesystem::path &path, bool restoreTabs,
    bool ensureSessionPreference) noexcept;

} // namespace browser_preferences
