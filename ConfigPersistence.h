#pragma once

#include <filesystem>
#include <cstddef>
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
// A source that changes existence, file identity or contents during staging is
// rejected. The final original-file read handle and real ancestor bindings are
// retained through atomic ReplaceFile publication. The displaced object's
// identity/bytes are checked; a competing file is conditionally restored or
// retained with its exact recovery location. No deliberate two-rename gap is
// introduced. This is not a cross-process filesystem compare-and-swap: another
// same-user ReplaceFile/advanced rename may bypass sharing restrictions, and a
// writer which deliberately targets the private native backup name inside
// ReplaceFile itself can overwrite that slot. Recovery paths are private
// transaction artifacts, not safe shared writer destinations.
bool MutateIniFileAtomically(const std::filesystem::path &configPath,
                             const IniStageMutation &mutation,
                             std::wstring *errorDetails = nullptr);

// Reuses the same snapshot, ancestor and displaced-file recovery protections
// for byte-preserving formats. Both the input and staged output are bounded.
// A missing target is created only when allowCreate is explicitly true.
bool MutateRawFileAtomically(const std::filesystem::path &path,
                             const IniStageMutation &mutation,
                             size_t maximumBytes, bool allowCreate,
                             std::wstring *errorDetails = nullptr);

bool ApplyIniMutationsAtomically(
    const std::filesystem::path &configPath,
    const std::vector<IniMutation> &mutations,
    std::wstring *errorDetails = nullptr);

#ifdef CTSPACES_CONFIG_TEST_HOOKS
// The auxiliary path is the backup slot except at BeforeNativePublication,
// where it is the exact staged source pathname handed to the native API.
enum class IniPublicationBoundary { AfterStageCopy, BeforePublish, BeforeNativePublication, AfterPublish, BeforeRollback };
using IniPublicationHook=void(*)(const std::filesystem::path& config,
    const std::filesystem::path& backup,IniPublicationBoundary boundary,void* user);
void SetIniPublicationHookForTest(IniPublicationHook hook,void* user);
enum class IniPublicationFailure { None, OldDisplaced, Installed };
void FailIniPublicationForTest(IniPublicationFailure failure);
#endif

enum class DirectChildDirectoryState {
  MissingOrInvalid,
  Present,
  Indeterminate,
};

DirectChildDirectoryState ProbeDirectChildDirectory(
    const std::filesystem::path &parentPath, const std::wstring &childName,
    std::wstring &resolvedName, unsigned long *windowsError = nullptr);

} // namespace config_persistence
