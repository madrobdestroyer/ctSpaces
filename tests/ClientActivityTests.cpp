#include "../ClientActivity.h"
#include <iostream>
#include <stdexcept>
#include <fstream>

namespace fs = std::filesystem;
using namespace client_activity;
void Check(bool condition, const char *description) {
  if (!condition)
    throw std::runtime_error(description);
}
unsigned long long Date(WORD year, WORD month, WORD day) {
  SYSTEMTIME date{};
  date.wYear = year;
  date.wMonth = month;
  date.wDay = day;
  FILETIME value{};
  Check(SystemTimeToFileTime(&date, &value) != FALSE, "Test date conversion");
  return ToTicks(value);
}
int main() {
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  const fs::path root = fs::path(temporary) /
      (L"ctSpaces-activity-test-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  try {
    Check(ThreeMonthsBefore(Date(2026, 9, 17)) == Date(2026, 6, 17),
          "Three calendar months");
    Check(ThreeMonthsBefore(Date(2026, 5, 31)) == Date(2026, 2, 28),
          "Month-end clamping");
    Check(ThreeMonthsBefore(Date(2024, 5, 31)) == Date(2024, 2, 29),
          "Leap-year clamping");
    Check(ThreeMonthsBefore(Date(2026, 1, 31)) == Date(2025, 10, 31),
          "Year rollover");
    const auto now = Date(2026, 9, 17);
    const Record boundary{Date(2026, 6, 17), false};
    Check(IsInactive(boundary, now), "Exact cutoff is eligible");
    Check(!IsInactive({boundary.time + 1, false}, now), "Recent is not eligible");
    Check(!IsInactive({now + 1, false}, now), "Future is not eligible");
    Check(!IsInactive({0, false}, now), "Unknown is not eligible");
    Check(IsInactive({boundary.time, true}, now), "Grace period expires");
    Check(Decode(Encode(boundary)) == boundary, "Open date round trip");
    Check(Decode(Encode({now, true})) == Record{now, true}, "Baseline round trip");
    for (const auto &bad : {"", "ctSpaces-activity=2\r\nopened=1\r\n",
                            "ctSpaces-activity=1\r\nopened=-1\r\n",
                            "ctSpaces-activity=1\r\nopened=0\r\n",
                            "ctSpaces-activity=1\r\nopened=18446744073709551616\r\n",
                            "ctSpaces-activity=1\r\nopened=12garbage\r\n"})
      Check(!Decode(bad), "Malformed history rejected");
    Check(CreateDirectoryW(root.c_str(), nullptr) != FALSE, "Disposable root");
    Check(!Read(root), "Missing history is unknown");
    Check(Write(root, {now, true}, true), "Initialize baseline");
    Check(Write(root, {now + 1, true}, true), "Revisit baseline");
    Check(Read(root) == Record{now, true}, "Baseline is not continually reset");
    Check(Write(root, boundary), "Open replaces baseline");
    Check(Read(root) == boundary, "Durable open readback");
    Check(Write(root, {now, false}), "Another browser updates whole-client date");
    Check(!IsInactive(*Read(root), now), "Reopened client protected");
    HANDLE held = CreateFileW((root / kFileName).c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(held != INVALID_HANDLE_VALUE, "Hold marker");
    const bool blockedWrite = Write(root, boundary);
    CloseHandle(held);
    Check(!blockedWrite && Read(root) == Record{now, false},
          "Failed atomic replacement preserves old history");
    const auto renamed = root / L"renamed";
    Check(CreateDirectoryW(renamed.c_str(), nullptr) != FALSE, "Nested test root");
    Check(Write(renamed, boundary), "Renamed marker setup");
    const auto moved = root / L"moved";
    Check(MoveFileW(renamed.c_str(), moved.c_str()) != FALSE, "Client rename");
    Check(Read(moved) == boundary, "Rename preserves activity");
    Check(DeleteFileW((moved / kFileName).c_str()) != FALSE, "Corrupt setup");
    { std::ofstream file(moved / kFileName, std::ios::binary); file << "corrupt"; }
    Check(!Read(moved) && !Write(moved, {now, true}, true),
          "Corrupt history is not guessed or initialized");
    Check(Write(moved, {now, false}), "A new open repairs corrupt history");
    Check(DeleteFileW((moved / kFileName).c_str()) != FALSE, "Unsafe setup");
    Check(CreateDirectoryW((moved / kFileName).c_str(), nullptr) != FALSE,
          "Directory masquerading as marker");
    Check(!Read(moved) && !Write(moved, boundary), "Unsafe marker blocked");
    for (const auto &entry : fs::directory_iterator(root))
      Check(!entry.path().filename().wstring().starts_with(L".ctSpaces-activity-"),
            "No staging files leaked");
    fs::remove_all(root); // Exact test-created unique path only.
    Check(!fs::exists(root), "Whole-client removal also removes activity");
    std::cout << "Client activity: calendar boundaries, history, atomic failure, "
                 "rename, corruption, and unsafe marker tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\nTest files preserved at " << root << '\n';
    return 1;
  }
}
