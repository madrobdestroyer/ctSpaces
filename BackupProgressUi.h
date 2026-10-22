#pragma once
#include "InProc7z.h"
#include <algorithm>
#include <format>
#include <string>

struct BackupProgressPresentation {
  std::wstring title;
  std::wstring details;
  int percent = -1;
};

inline std::wstring FormatBackupBytes(unsigned long long bytes) {
  if (bytes < 1024) return std::format(L"{} B", bytes);
  if (bytes < 1024ull * 1024ull) return std::format(L"{:.1f} KiB", bytes / 1024.0);
  if (bytes < 1024ull * 1024ull * 1024ull) return std::format(L"{:.1f} MiB", bytes / (1024.0 * 1024.0));
  return std::format(L"{:.2f} GiB", bytes / (1024.0 * 1024.0 * 1024.0));
}

inline BackupProgressPresentation FormatBackupProgress(
    const _7zProgressInfo &info, unsigned long long elapsedMilliseconds) {
  BackupProgressPresentation result;
  switch (info.phase) {
  case _7zProgressPhase::Preparing: result.title=L"Preparing backup"; break;
  case _7zProgressPhase::Scanning: result.title=L"Checking selected backup files"; break;
  case _7zProgressPhase::Compressing: result.title=L"Compressing backup"; break;
  case _7zProgressPhase::CheckingSource: result.title=L"Rechecking selected files"; break;
  case _7zProgressPhase::Verifying: result.title=L"Verifying backup integrity"; break;
  case _7zProgressPhase::Publishing: result.title=L"Saving verified backup"; break;
  }
  const auto seconds=elapsedMilliseconds/1000;
  const auto elapsed=seconds>=3600?std::format(L"{}:{:02}:{:02}",seconds/3600,(seconds/60)%60,seconds%60):
      std::format(L"{}:{:02}",seconds/60,seconds%60);
  const bool measured=(info.phase==_7zProgressPhase::Compressing || info.phase==_7zProgressPhase::Verifying) && info.totalKnown && info.totalBytes;
  if(measured) {
    const auto completed=(std::min)(info.completedBytes,info.totalBytes);
    result.percent=completed>=info.totalBytes?100:static_cast<int>((completed*100)/info.totalBytes);
    result.details=std::format(L"{} of {}  |  {} items  |  Elapsed {}",
        FormatBackupBytes(completed),FormatBackupBytes(info.totalBytes),info.totalItems,elapsed);
  } else if(info.phase==_7zProgressPhase::Scanning || info.phase==_7zProgressPhase::CheckingSource) {
    result.details=std::format(L"{} items checked  |  {} found  |  Elapsed {}",
        info.processedItems,FormatBackupBytes(info.completedBytes),elapsed);
  } else result.details=std::format(L"Elapsed {}",elapsed);
  if(info.currentItem && *info.currentItem)result.details+=L"\n"+std::wstring(info.currentItem);
  return result;
}
