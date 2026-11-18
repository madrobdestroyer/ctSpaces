// Metadata-only checks execute the production restore capacity policy without
// allocating large payloads, creating archives, or touching a disk destination.
#include "../BackupEngine.h"
#include "../version.h"
#include <climits>
#include <iostream>
#include <stdexcept>

namespace backup_capacity_boundary {
BOOL WINAPI DiskFreeSpace(LPCWSTR, PULARGE_INTEGER, PULARGE_INTEGER,
                         PULARGE_INTEGER);
}

// Backend headers (including Win32 declarations) are already loaded. Only this
// implementation's capacity query is intercepted; the SDK unit stays native.
#define GetDiskFreeSpaceExW backup_capacity_boundary::DiskFreeSpace
#include "../BackupEngine.cpp"
#undef GetDiskFreeSpaceExW

namespace {
constexpr unsigned long long kGiB = 1024ull * 1024ull * 1024ull;
constexpr unsigned long long kMetadata = 16ull * 1024ull;
constexpr unsigned long long kReserve = 5ull * kGiB;
unsigned checks = 0;
struct DiskState {
  unsigned long long available = 40ull * kGiB;
  unsigned long long free = 40ull * kGiB;
  unsigned long long volume = 100ull * 1024ull * kGiB;
  DWORD error = ERROR_SUCCESS;
  unsigned calls = 0;
  bool requestedVolume = false;
  bool requestedAvailable = false;
  bool requestedFree = false;
} disk;

void Check(bool pass, const char* description) {
  ++checks;
  std::cout << (pass ? "PASS " : "FAIL ") << description << '\n';
  if (!pass) throw std::runtime_error(description);
}

_7zArchiveInfo Info(unsigned long long bytes, unsigned long long items = 3) {
  _7zArchiveInfo info;
  info.itemCount = items;
  info.totalUncompressedBytes = bytes;
  return info;
}

CtBackup::Result Evaluate(const _7zArchiveInfo& info,
                         unsigned long long available,
                         unsigned long long free,
                         DWORD error = ERROR_SUCCESS) {
  disk = {};
  disk.available = available;
  disk.free = free;
  disk.error = error;
  // No real I/O reaches this deliberately synthetic path.
  return CtBackup::CheckArchiveLimits(info, L"C:\\ctSpaces-capacity-metadata-only");
}

void Cases() {
  const auto thirty = Info(30ull * kGiB);
  const auto thirtyRequired = thirty.totalUncompressedBytes + 3ull * kMetadata;
  Check(Evaluate(thirty, 40ull * kGiB, 40ull * kGiB).ok(),
        "A 30 GiB declared payload passes with sufficient available staging space");
  Check(disk.calls == 1 && disk.requestedAvailable && disk.requestedFree && !disk.requestedVolume,
        "Production policy queries caller-available and total-free space, not volume capacity");
  Check(Evaluate(Info(4ull * kGiB + 17ull), 10ull * kGiB, 10ull * kGiB).ok(),
        "A payload above four GiB retains its 64-bit size without allocating that payload");
  Check(Evaluate(thirty, thirtyRequired + kReserve, thirtyRequired + kReserve).ok(),
        "Exactly payload plus per-item metadata plus the minimum reserve is sufficient");
  auto result = Evaluate(thirty, thirtyRequired + kReserve - 1, thirtyRequired + kReserve - 1);
  Check(result.code == HRESULT_FROM_WIN32(ERROR_DISK_FULL) && result.details.find(L"space") != std::wstring::npos,
        "One byte below the complete 30 GiB staging requirement fails before extraction");
  Check(Evaluate(thirty, thirty.totalUncompressedBytes + kReserve, thirty.totalUncompressedBytes + kReserve).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "Per-item metadata remains included rather than counting only expanded file bytes");

  const auto emptyPayload = Info(0, 1);
  Check(Evaluate(emptyPayload, kReserve, kReserve).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "Available space equal to the reserve cannot fund even an empty item");
  Check(Evaluate(emptyPayload, kReserve - 1, kReserve - 1).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "Available space below the reserve fails without unsigned subtraction underflow");
  Check(Evaluate(emptyPayload, 0, 0).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "A zero-free-space volume fails safely");
  Check(Evaluate(emptyPayload, kReserve + kMetadata, kReserve + kMetadata).ok(),
        "An empty payload still succeeds when its one-item metadata requirement fits exactly");

  const auto smallInfo = Info(1024, 3);
  const auto required = 1024ull + 3ull * kMetadata;
  const auto totalFree = 1000ull * kGiB;
  const auto proportionalReserve = totalFree / 20ull;
  Check(Evaluate(smallInfo, proportionalReserve + required, totalFree).ok(),
        "The current five-percent-of-total-free reserve accepts its exact threshold");
  Check(Evaluate(smallInfo, proportionalReserve + required - 1, totalFree).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "The proportional reserve rejects one byte below its exact threshold");
  Check(Evaluate(smallInfo, 10ull * kGiB, totalFree).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "Quota-limited caller-available bytes govern staging even when global free space is large");
  Check(Evaluate(smallInfo, 40ull * kGiB, 40ull * kGiB).ok() && disk.volume == 100ull * 1024ull * kGiB,
        "A large volume capacity does not silently replace the current total-free reserve policy");

  Check(CtArchiveSafety::kMaxItems == 500000 && CtArchiveSafety::kMaxUncompressedBytes == 256ull * kGiB,
        "Capacity checks use the unchanged production item and expanded-byte limits");
  Check(Evaluate(Info(CtArchiveSafety::kMaxUncompressedBytes), 300ull * kGiB, 300ull * kGiB).ok(),
        "The exact 256 GiB expanded-byte ceiling is inclusive when staging space fits");
  result = Evaluate(Info(CtArchiveSafety::kMaxUncompressedBytes + 1), ULLONG_MAX, ULLONG_MAX);
  Check(result.code == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) && disk.calls == 0,
        "One byte above the expanded-byte cap is rejected before any capacity query");
  result = Evaluate(Info(ULLONG_MAX), ULLONG_MAX, ULLONG_MAX);
  Check(result.code == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) && disk.calls == 0,
        "An extreme declared byte count cannot wrap into an accepted staging size");
  result = Evaluate(Info(0, 0), ULLONG_MAX, ULLONG_MAX);
  Check(result.code == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) && disk.calls == 0,
        "An empty item inventory is rejected before disk inspection");
  const auto maximumItems = Info(0, CtArchiveSafety::kMaxItems);
  const auto maximumMetadata = maximumItems.itemCount * kMetadata;
  Check(maximumMetadata > 4ull * kGiB &&
        Evaluate(maximumItems, maximumMetadata + kReserve, maximumMetadata + kReserve).ok(),
        "The exact 500000-item ceiling retains more than four GiB of metadata arithmetic");
  Check(Evaluate(maximumItems, maximumMetadata + kReserve - 1, maximumMetadata + kReserve - 1).code == HRESULT_FROM_WIN32(ERROR_DISK_FULL),
        "The maximum-item metadata threshold rejects one-byte-short staging capacity");
  result = Evaluate(Info(0, CtArchiveSafety::kMaxItems + 1), ULLONG_MAX, ULLONG_MAX);
  Check(result.code == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) && disk.calls == 0,
        "One item above the production ceiling is rejected before metadata multiplication");
  result = Evaluate(Info(0, ULLONG_MAX), ULLONG_MAX, ULLONG_MAX);
  Check(result.code == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) && disk.calls == 0,
        "An extreme item count cannot overflow the metadata allowance into an accepted size");
  Check(Evaluate(Info(CtArchiveSafety::kMaxUncompressedBytes, CtArchiveSafety::kMaxItems), ULLONG_MAX, ULLONG_MAX).ok(),
        "Maximum legal item and byte totals plus a huge free-space value do not overflow");

  for (const DWORD error : {DWORD(ERROR_ACCESS_DENIED), DWORD(ERROR_NOT_READY), DWORD(ERROR_INVALID_NAME)}) {
    result = Evaluate(thirty, ULLONG_MAX, ULLONG_MAX, error);
    Check(result.code == HRESULT_FROM_WIN32(error) && disk.calls == 1 &&
          result.details.find(L"checked") != std::wstring::npos,
          "A failed native capacity query preserves its exact HRESULT and capacity diagnostic");
  }
  std::cout << "SYNTHETIC_THIRTY_GIB_CHECKED=1\n"
               "PHYSICAL_PAYLOAD_BYTES=0\n"
               "CAPACITY_POLICY=caller_available_and_total_free\n"
               "OVERFLOW_GUARD_SCOPE=out_of_range_totals_rejected_by_prior_caps\n";
}
} // namespace

namespace backup_capacity_boundary {
BOOL WINAPI DiskFreeSpace(LPCWSTR, PULARGE_INTEGER available,
                         PULARGE_INTEGER volume, PULARGE_INTEGER free) {
  ++disk.calls;
  disk.requestedAvailable = available != nullptr;
  disk.requestedVolume = volume != nullptr;
  disk.requestedFree = free != nullptr;
  if (disk.error != ERROR_SUCCESS) {
    SetLastError(disk.error);
    return FALSE;
  }
  if (available) available->QuadPart = disk.available;
  if (volume) volume->QuadPart = disk.volume;
  if (free) free->QuadPart = disk.free;
  return TRUE;
}
} // namespace backup_capacity_boundary

int wmain(int argc, wchar_t** argv) {
  if (argc != 2 || wcscmp(argv[1], L"--headless") != 0) return 2;
  try {
    Cases();
    std::cout << "VERSION=" << CTSPACES_VERSION_TEXT << "\nCHECKS=" << checks
              << "\nUNEXPECTED_FAILURES=0\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Capacity regression failed: " << error.what() << "\nCHECKS=" << checks
              << "\nUNEXPECTED_FAILURES=1\n";
    return 1;
  }
}
