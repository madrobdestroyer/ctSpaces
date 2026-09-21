#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace config_persistence {

enum class ConfigFileState {
  Missing,
  Regular,
  Unavailable,
};

struct IniMutation {
  std::wstring section;
  std::optional<std::wstring> key;
  std::optional<std::wstring> value;
};

using IniStageMutation = std::function<bool(
    const std::filesystem::path &stagedPath, std::wstring &errorDetails)>;

ConfigFileState InspectConfigFile(const std::filesystem::path &configPath,
                                  unsigned long *windowsError = nullptr);

// The staged file is normalized to UTF-16LE with a byte-order mark before the
// callback runs, allowing the Windows wide-character profile APIs to preserve
// every valid client name. BOM-marked UTF-8 is decoded strictly as UTF-8;
// otherwise legacy text is decoded in the active Windows ANSI code page.
// Malformed or unsupported input is rejected without changing the original.
bool MutateIniFileAtomically(const std::filesystem::path &configPath,
                             const IniStageMutation &mutation,
                             std::wstring *errorDetails = nullptr);

bool ApplyIniMutationsAtomically(
    const std::filesystem::path &configPath,
    const std::vector<IniMutation> &mutations,
    std::wstring *errorDetails = nullptr);

enum class DirectChildDirectoryState {
  MissingOrInvalid,
  Present,
  Indeterminate,
};

DirectChildDirectoryState ProbeDirectChildDirectory(
    const std::filesystem::path &parentPath, const std::wstring &childName,
    std::wstring &resolvedName, unsigned long *windowsError = nullptr);

} // namespace config_persistence
