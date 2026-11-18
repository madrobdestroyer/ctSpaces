#pragma once
// Test-only composition around the original SDK stream. Only the InProc7z.cpp
// test unit substitutes the class name; the bundled library remains unchanged.
#include <windows.h>
#include "../3p/7zip/CPP/7zip/UI/Client7z/StdAfx.h"
#ifdef CTSPACES_RESTORE_OUTPUT_STREAM_BOUNDARY
// Keep the production unit's GUID-definition order when preloading the header.
#include "../3p/7zip/CPP/Common/MyInitGuid.h"
#endif
#include "../3p/7zip/CPP/7zip/Common/FileStreams.h"
#include "../3p/7zip/CPP/Windows/FileDir.h"

namespace restore_output_boundary {
HRESULT Write(COutFileStream*, const void*, UInt32, UInt32*) noexcept;
HRESULT Close(COutFileStream*) noexcept;
bool ApplyAttributes(CFSTR, DWORD) noexcept;
void WrapperCreated() noexcept;
void WrapperDestroyed() noexcept;
}

Z7_CLASS_IMP_COM_1(RestoreOutputBoundaryStream, IOutStream)
  Z7_IFACE_COM7_IMP(ISequentialOutStream)
private:
  COutFileStream* Native;
  CMyComPtr<IOutStream> Owner;
public:
  NWindows::NFile::NIO::COutFile& File;
  RestoreOutputBoundaryStream() : Native(new COutFileStream), Owner(static_cast<IOutStream*>(Native)), File(Native->File) {
    restore_output_boundary::WrapperCreated();
  }
  ~RestoreOutputBoundaryStream() { restore_output_boundary::WrapperDestroyed(); }
  bool Create_NEW(CFSTR path) { return Native->Create_NEW(path); }
  bool Create_ALWAYS(CFSTR path) { return Native->Create_ALWAYS(path); }
  HRESULT Close() { return restore_output_boundary::Close(Native); }
};

inline Z7_COM7F_IMF(RestoreOutputBoundaryStream::Write(const void* data, UInt32 bytes, UInt32* processed)) {
  return restore_output_boundary::Write(Native, data, bytes, processed);
}
inline Z7_COM7F_IMF(RestoreOutputBoundaryStream::Seek(Int64 offset, UInt32 origin, UInt64* position)) {
  return Owner->Seek(offset, origin, position);
}
inline Z7_COM7F_IMF(RestoreOutputBoundaryStream::SetSize(UInt64 bytes)) {
  return Owner->SetSize(bytes);
}

#ifdef CTSPACES_RESTORE_OUTPUT_STREAM_BOUNDARY
#define COutFileStream RestoreOutputBoundaryStream
#define SetFileAttrib_PosixHighDetect restore_output_boundary::ApplyAttributes
#endif
