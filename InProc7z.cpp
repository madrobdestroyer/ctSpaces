#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "InProc7z.h"

#include "3p/7zip/CPP/7zip/UI/Client7z/StdAfx.h"


#include "3p/7zip/CPP/Common/MyWindows.h"
#include "3p/7zip/CPP/Common/MyInitGuid.h"

#include "3p/7zip/CPP/Common/Defs.h"
#include "3p/7zip/CPP/Common/IntToString.h"
#include "3p/7zip/CPP/Common/StringConvert.h"

#include "3p/7zip/CPP/Windows/FileDir.h"
#include "3p/7zip/CPP/Windows/FileFind.h"
#include "3p/7zip/CPP/Windows/FileName.h"
#include "3p/7zip/CPP/Windows/NtCheck.h"
#include "3p/7zip/CPP/Windows/PropVariant.h"
#include "3p/7zip/CPP/Windows/PropVariantConv.h"

#include "3p/7zip/CPP/7zip/Common/FileStreams.h"
#include "3p/7zip/CPP/7zip/Archive/IArchive.h"

#include "3p/7zip/CPP/7zip/Common/CreateCoder.h" // keeps internal codecs available
#include "3p/7zip/CPP/7zip/IPassword.h"

#include "3p/7zip/C/7zVersion.h"

#include <cwctype>
#include <new>
#include <set>
#include <string>
#include <utility>

#ifdef _WIN32
extern HINSTANCE g_hInstance;
HINSTANCE g_hInstance=NULL;
bool g_IsNT=true;
extern "C" {
    HRESULT WINAPI CreateArchiver(const GUID* clsid,const GUID* iid,void** outObject);
}
#endif

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION
// 7z format GUID base: {23170F69-40C1-278A-1000-000110070000}
#define DEFINE_GUID_ARC(name, id) Z7_DEFINE_GUID(name, \
  0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, id, 0x00, 0x00);

enum{ kId_7z=7 };

extern "C" const GUID CLSID_CArchiveHandler=
{0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00 }};

DEFINE_GUID_ARC(CLSID_Format,kId_7z)

using namespace NWindows;
using namespace NFile;
using namespace NDir;


// ----- ctSpaces progress bridge (thread-local) -----
static thread_local _7zProgressCb g__7zProgressCb = nullptr;
static thread_local void* g__7zProgressUser = nullptr;
static thread_local _7zOp g__7zProgressOp = _7zOp::Extract;

struct _7zScopedProgress {
    _7zProgressCb prevCb{};
    void* prevUser{};
    _7zOp prevOp{_7zOp::Extract};
    _7zScopedProgress(_7zOp op, _7zProgressCb cb, void* user){
        prevCb = g__7zProgressCb;
        prevUser = g__7zProgressUser;
        prevOp = g__7zProgressOp;
        g__7zProgressCb = cb;
        g__7zProgressUser = user;
        g__7zProgressOp = op;
    }
    ~_7zScopedProgress(){
        g__7zProgressCb = prevCb;
        g__7zProgressUser = prevUser;
        g__7zProgressOp = prevOp;
    }
};

static void DebugPrintA(const char* s){ if(s) ::OutputDebugStringA(s); }
static void DebugPrintF(const FString& s){ if(s.Ptr()) ::OutputDebugStringW(s.Ptr()); }

static void PrintError(const char* message){
    DebugPrintA("7z: ");
    DebugPrintA(message ? message : "(null)");
    DebugPrintA("\n");
}
static void PrintError(const char* message,const FString& name){
    DebugPrintA("7z: ");
    DebugPrintA(message ? message : "(null)");
    DebugPrintA(" : ");
    DebugPrintF(name);
    DebugPrintA("\n");
}

static bool IsReservedWindowsComponent(const std::wstring &component) {
    const size_t dot = component.find(L'.');
    std::wstring base = component.substr(0, dot);
    for (wchar_t &ch : base)
        ch = (wchar_t)towupper(ch);

    if (base == L"CON" || base == L"PRN" || base == L"AUX" ||
        base == L"NUL" || base == L"CLOCK$" || base == L"CONIN$" ||
        base == L"CONOUT$")
        return true;
    if (base.size() == 4 &&
        ((base.rfind(L"COM", 0) == 0) || (base.rfind(L"LPT", 0) == 0))) {
        const wchar_t suffix=base[3];
        if ((suffix>=L'1' && suffix<=L'9') || suffix==L'\x00B9' ||
            suffix==L'\x00B2' || suffix==L'\x00B3')
            return true;
    }
    return false;
}

static bool IsSafeArchivePath(const UString &archivePath) {
    if (archivePath.IsEmpty() || archivePath.Len() >= 32767)
        return false;

    std::wstring path(archivePath.Ptr(), archivePath.Len());
    if (path == L".")
        return true;
    if (path.size() >= 2 && path[0] == L'.' &&
        (path[1] == L'\\' || path[1] == L'/'))
        path.erase(0, 2);
    if (path.empty())
        return true;
    if (path.front() == L'\\' || path.front() == L'/')
        return false;

    size_t start = 0;
    unsigned componentCount = 0;
    while (start < path.size()) {
        const size_t separator = path.find_first_of(L"\\/", start);
        const size_t end = separator == std::wstring::npos ? path.size() : separator;
        const std::wstring component = path.substr(start, end - start);
        if (++componentCount > CtArchiveSafety::kMaxDirectoryDepth ||
            component.empty() || component == L"." || component == L".." ||
            component.size() > 255 || component.back() == L' ' ||
            component.back() == L'.' || IsReservedWindowsComponent(component))
            return false;

        for (wchar_t ch : component) {
            if (ch < 32 || ch == L':' || ch == L'<' || ch == L'>' ||
                ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*')
                return false;
        }

        if (separator == std::wstring::npos)
            break;
        start = separator + 1;
    }
    return true;
}

struct CArchivePathLess {
    bool operator()(const std::wstring &left,
                    const std::wstring &right) const noexcept {
        const int result=::CompareStringOrdinal(
            left.data(),(int)left.size(),right.data(),(int)right.size(),TRUE);
        return result==CSTR_LESS_THAN;
    }
};

static bool GetArchivePathKey(const UString &archivePath,
                              std::wstring &key) {
    if(!IsSafeArchivePath(archivePath))
        return false;

    key.assign(archivePath.Ptr(),archivePath.Len());
    for(wchar_t &ch:key)
        if(ch==L'/')
            ch=L'\\';
    if(key.size()>=2&&key[0]==L'.'&&key[1]==L'\\')
        key.erase(0,2);
    while(!key.empty()&&key.back()==L'\\')
        key.pop_back();
    if(key.empty())
        key=L".";
    return true;
}

static bool IsUnsafeArchiveAttrib(UInt32 attrib) {
    constexpr UInt32 kPosixFileTypeMask=0xF0000000u;
    constexpr UInt32 kPosixSymbolicLink=0xA0000000u;
    return (attrib&FILE_ATTRIBUTE_REPARSE_POINT)!=0 ||
           (attrib&kPosixFileTypeMask)==kPosixSymbolicLink;
}

static FString ArcPathToRelFsPath(const UString& arcPathIn){
    FString f=us2fs(arcPathIn);

#ifdef _WIN32
    for(unsigned i=0; i<f.Len(); ++i)
        if(f[i]==L'/')
            f.ReplaceOneCharAtPos(i,L'\\');

    // strip Win32 extended prefix: \\?\   <-- NOTE: do NOT end the comment with '\'
    if(f.Len()>=4&&
       f[0]==L'\\'&&f[1]==L'\\'&&f[2]==L'?'&&f[3]==L'\\'){
        f.Delete(0,4);
    }

    // remove ':' anywhere else (invalid in Windows file name)
    for (unsigned i = 0; i < f.Len(); ++i)
        if (f[i] == L':')
            f.ReplaceOneCharAtPos(i, L'_');

    // neuter ".." segments so we can't escape outDir
    for (unsigned i = 0; i + 1 < f.Len(); ++i) {
        if (f[i] == L'.' && f[i + 1] == L'.' &&
            (i == 0 || f[i - 1] == L'\\') &&
            (i + 2 == f.Len() || f[i + 2] == L'\\'))
        {
            f.ReplaceOneCharAtPos(i,   L'_');
            f.ReplaceOneCharAtPos(i+1, L'_');
        }
    }
#endif


    if(f.IsEmpty())
        f=FTEXT("_");

    return f;
}


static inline UString FsToUs(const FString& f){ return fs2us(f); }

// -------------------------
// Directory enumeration helpers (Win32)
// -------------------------
struct CDirItem{
    UString Path_For_Handler; // archive internal path (relative)
    FString FullPath;         // full disk path
    bool IsDir=false;

    UInt64 Size=0;
    DWORD Attrib=0;
    FILETIME CTime{};
    FILETIME ATime{};
    FILETIME MTime{};
};

struct CEnumBudget{
    UInt64 ItemCount=0;
    UInt64 SourceBytes=0;
};

static HRESULT ChargeEnumerationBudget(CEnumBudget& budget,bool isDir,
                                       UInt64 size){
    if(budget.ItemCount>=CtArchiveSafety::kMaxItems)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    if(!isDir){
        if(size>CtArchiveSafety::kMaxUncompressedBytes-budget.SourceBytes)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        budget.SourceBytes+=size;
    }
    ++budget.ItemCount;
    return S_OK;
}

static FString BasenameOfPath(const FString& p){
    // Use 7-Zip helper: reverse find separator and take tail
    int pos=p.ReverseFind_PathSepar();
    if(pos>=0) return p.Ptr(pos+1);
    return p;
}

static HRESULT EnumDirRecursive(
    const FString& rootDirFull,
    const FString& currentDirFull,
    const UString& rootNameInArchive,
    unsigned currentDepth,
    CEnumBudget& budget,
    CObjectVector<CDirItem>& outItems){
#ifdef _WIN32
    // pattern: current\*
    FString pattern=currentDirFull;
    if(!pattern.IsEmpty()){
        wchar_t c=pattern.Back();
        if(c!=L'\\'&&c!=L'/')
            pattern.Add_PathSepar();
    }
    pattern+=FTEXT("*");

    WIN32_FIND_DATAW fd;
    HANDLE h=::FindFirstFileW(pattern.Ptr(),&fd);
    if(h==INVALID_HANDLE_VALUE){
        DWORD e=::GetLastError();
        return HRESULT_FROM_WIN32(e);
    }

    const auto CloseFind=[&](){ ::FindClose(h); };

    do{
        const wchar_t* name=fd.cFileName;
        if(!name||!name[0]) continue;
        if(wcscmp(name,L".")==0||wcscmp(name,L"..")==0) continue;

        if((fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0){
            CloseFind();
            return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);
        }
        const unsigned childDepth=currentDepth+1;
        if(childDepth>CtArchiveSafety::kMaxDirectoryDepth){
            CloseFind();
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        }
        const bool isDir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;

        FString childFull=currentDirFull;
        if(!childFull.IsEmpty()){
            wchar_t c=childFull.Back();
            if(c!=L'\\'&&c!=L'/')
                childFull.Add_PathSepar();
        }
        childFull+=name;

        // Compute relative path from rootDirFull to childFull
        FString rel=childFull;
        if(rel.Len()>=rootDirFull.Len()){
            // Strip rootDirFull prefix (case-insensitive on Windows is typical, but we assume exact form you passed in)
            rel.DeleteFrontal(rootDirFull.Len());
            // strip leading separators
            while(!rel.IsEmpty()&&(rel[0]==L'\\'||rel[0]==L'/'))
                rel.Delete(0);
        }

        // Archive internal path: rootNameInArchive\rel, or just rel if rootNameInArchive is empty
        UString arcPath;
        if(!rootNameInArchive.IsEmpty()){
            arcPath=rootNameInArchive;
            if(!rel.IsEmpty()){
                arcPath.Add_PathSepar();
                arcPath+=FsToUs(rel);
            }
        } else {
            arcPath=FsToUs(rel);
        }

        CDirItem item;
        item.Path_For_Handler=arcPath;
        item.FullPath=childFull;
        item.IsDir=isDir;
        item.Attrib=fd.dwFileAttributes;
        item.CTime=fd.ftCreationTime;
        item.ATime=fd.ftLastAccessTime;
        item.MTime=fd.ftLastWriteTime;
        item.Size=((UInt64)fd.nFileSizeHigh<<32)|(UInt64)fd.nFileSizeLow;

        HRESULT hr=ChargeEnumerationBudget(budget,isDir,item.Size);
        if(FAILED(hr)){ CloseFind(); return hr; }
        outItems.Add(item);

        if(isDir){
            hr=EnumDirRecursive(rootDirFull,childFull,rootNameInArchive,
                                childDepth,budget,outItems);
            if(FAILED(hr)){ CloseFind(); return hr; }
        }

    } while(::FindNextFileW(h,&fd));

    const DWORD last=::GetLastError();
    CloseFind();
    if(last!=ERROR_NO_MORE_FILES){
        return HRESULT_FROM_WIN32(last);
    }
    return S_OK;
#else
    (void)rootDirFull; (void)currentDirFull; (void)rootNameInArchive;
    (void)currentDepth; (void)budget; (void)outItems;
    return E_NOTIMPL;
#endif
}

// -------------------------
// Extract callback (no password)
// -------------------------
static const wchar_t* const kEmptyFileAlias=L"[Content]";

class CArchiveExtractCallback Z7_final:
    public IArchiveExtractCallback,
    public CMyUnknownImp{
    Z7_IFACES_IMP_UNK_1(IArchiveExtractCallback)
        Z7_IFACE_COM7_IMP(IProgress)

        CMyComPtr<IInArchive> _archiveHandler;
    FString _outDir;        // normalized dir prefix (ends with \)
    UString _filePath;      // path inside archive
    FString _diskFilePath;  // full output path on disk
    bool _extractMode=false;

    struct CProcessedFileInfo{
        UInt32 Attrib=0;
        bool isDir=false;
        bool Attrib_Defined=false;
    } _processed{};

    COutFileStream* _outFileStreamSpec=nullptr;
    CMyComPtr<ISequentialOutStream> _outFileStream;
    std::set<std::wstring,CArchivePathLess> _seenPaths;

public:
    void Init(IInArchive* archiveHandler,const FString& outDir){
        NumErrors=0;
        TotalBytes=0;
        CompletedBytes=0;
        LastPercent=(UInt32)(Int32)-1;
        LastTick=0;

        _archiveHandler=archiveHandler;
        _outDir=outDir;
        _seenPaths.clear();
        NName::NormalizeDirPathPrefix(_outDir);
        if(!_outDir.IsEmpty())
            CreateComplexDir(_outDir);
    }
    UInt64 NumErrors=0;

    void EndProgressLine();

private:
    UInt64 TotalBytes=0;
    UInt64 CompletedBytes=0;
    UInt32 LastPercent=(UInt32)(Int32)-1;
    ULONGLONG LastTick=0;

    void PrintProgress(bool force);
    UString CurrentItem;
    unsigned LastLineLen=0;


};

Z7_COM7F_IMF(CArchiveExtractCallback::SetTotal(UInt64 size)){
    TotalBytes=size;
    CompletedBytes=0;
    LastPercent=(UInt32)(Int32)-1;
    LastTick=0;
    PrintProgress(true);
    return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::SetCompleted(const UInt64* completeValue)){
    if(completeValue)
        CompletedBytes=*completeValue;
    PrintProgress(false);
    return S_OK;
}

void CArchiveExtractCallback::PrintProgress(bool force){
    if(!g__7zProgressCb) return;
    if(TotalBytes==0){
        if(force){
            g__7zProgressCb(g__7zProgressUser, g__7zProgressOp, 0, CurrentItem.Ptr());
        }
        return;
    }

    const ULONGLONG tick=::GetTickCount64();
    UInt32 percent=(UInt32)((CompletedBytes*100)/TotalBytes);
    if(percent>100) percent=100;
    if(!force){
        if(percent==LastPercent&&(tick-LastTick)<150)
            return;
    }
    LastPercent=percent;
    LastTick=tick;

    g__7zProgressCb(g__7zProgressUser, g__7zProgressOp, (unsigned)percent, CurrentItem.Ptr());
}


void CArchiveExtractCallback::EndProgressLine(){
    TotalBytes=0;
    LastLineLen=0;
}


Z7_COM7F_IMF(CArchiveExtractCallback::GetStream(UInt32 index,ISequentialOutStream** outStream,Int32 askExtractMode)){
    *outStream=NULL;
    _outFileStream.Release();
    _outFileStreamSpec=nullptr;
    _processed={};

    // Path
    {
        NCOM::CPropVariant prop;
        RINOK(_archiveHandler->GetProperty(index,kpidPath,&prop))
            if(prop.vt==VT_EMPTY)
                _filePath=kEmptyFileAlias;
            else{
                if(prop.vt!=VT_BSTR){
                    NumErrors++;
                    PrintError("WARNING: bad kpidPath type (skipping)");
                    return S_FALSE; // skip this item, continue
                }
                _filePath=prop.bstrVal;
            }
    }
    CurrentItem=_filePath;

    std::wstring pathKey;
    if(!GetArchivePathKey(_filePath,pathKey)){
        NumErrors++;
        PrintError("Unsafe archive path",ArcPathToRelFsPath(_filePath));
        return E_INVALIDARG;
    }
    if(!_seenPaths.insert(std::move(pathKey)).second){
        NumErrors++;
        PrintError("Duplicate archive path",ArcPathToRelFsPath(_filePath));
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    if(askExtractMode!=NArchive::NExtract::NAskMode::kExtract)
        return S_OK;

    // Attrib
    {
        NCOM::CPropVariant prop;
        RINOK(_archiveHandler->GetProperty(index,kpidAttrib,&prop))

            if(prop.vt==VT_EMPTY){
                _processed.Attrib=0;
                _processed.Attrib_Defined=false;
            } else if(prop.vt==VT_UI4){
                if(IsUnsafeArchiveAttrib(prop.ulVal)){
                    NumErrors++;
                    PrintError("Unsafe archive attributes",
                               ArcPathToRelFsPath(_filePath));
                    return E_INVALIDARG;
                }
                _processed.Attrib=prop.ulVal;
                _processed.Attrib_Defined=true;
            } else{
                // was: return E_FAIL;
                _processed.Attrib=0;
                _processed.Attrib_Defined=false;
                NumErrors++;
                PrintError("WARNING: unexpected kpidAttrib type (continuing)");
            }
    }


    // IsDir
    {
        NCOM::CPropVariant prop;
        RINOK(_archiveHandler->GetProperty(index,kpidIsDir,&prop))

            if(prop.vt==VT_BOOL){
                _processed.isDir=VARIANT_BOOLToBool(prop.boolVal);
            } else if(prop.vt==VT_EMPTY){
                _processed.isDir=false;
            } else{
                // was: return E_FAIL;
                _processed.isDir=false;
                NumErrors++;
                PrintError("WARNING: unexpected kpidIsDir type (continuing)");
            }
    }


    // Ensure parent dirs exist
    {
        int slashPos=_filePath.ReverseFind_PathSepar();
        if(slashPos>=0){
            const FString parent=_outDir+ArcPathToRelFsPath(_filePath.Left(slashPos));
            if(!CreateComplexDir(parent)){
                NumErrors++;
                PrintError("WARNING: can't create directory (skipping)",parent);
                return S_FALSE;
            }
        }
    }

    _diskFilePath=_outDir+ArcPathToRelFsPath(_filePath);

    if(_processed.isDir){
        if(!CreateComplexDir(_diskFilePath)){
            NumErrors++;
            PrintError("WARNING: can't create directory",_diskFilePath);
        }
        return S_OK;
    }

    // Replace if exists
    {
        NFind::CFileInfo fi;
        if(fi.Find(_diskFilePath)){
            if(!DeleteFileAlways(_diskFilePath)){
                NumErrors++;
                PrintError("WARNING: cannot delete output file (skipping)",_diskFilePath);
                return S_FALSE;
            }
        }
    }

    _outFileStreamSpec=new COutFileStream;
    CMyComPtr<ISequentialOutStream> outLoc(_outFileStreamSpec);
    if(!_outFileStreamSpec->Create_ALWAYS(_diskFilePath)){
        NumErrors++;
        PrintError("WARNING: cannot open output file (skipping)",_diskFilePath);
        _outFileStreamSpec=nullptr;
        return S_FALSE;
    }

    _outFileStream=outLoc;
    *outStream=outLoc.Detach();
    return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)){
    _extractMode=(askExtractMode==NArchive::NExtract::NAskMode::kExtract);
    return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::SetOperationResult(Int32 operationResult)){
    if(operationResult!=NArchive::NExtract::NOperationResult::kOK){
        NumErrors++;
        PrintError("Extract error (operationResult != OK)");
    }

    if(_outFileStream&&_outFileStreamSpec){
        const HRESULT hrClose=_outFileStreamSpec->Close();
        if(hrClose!=S_OK){
            NumErrors++;
            PrintError("WARNING: cannot close output file",_diskFilePath);
        }
    }
    _outFileStream.Release();
    _outFileStreamSpec=nullptr;

    if(_extractMode&&_processed.Attrib_Defined){
        SetFileAttrib_PosixHighDetect(_diskFilePath,_processed.Attrib);
    }
    return S_OK;
}


// -------------------------
// Update callback (no password)
// -------------------------
class CArchiveUpdateCallback Z7_final:
    public IArchiveUpdateCallback2,
    public CMyUnknownImp{
    Z7_IFACES_IMP_UNK_1(IArchiveUpdateCallback2)
        Z7_IFACE_COM7_IMP(IProgress)
        Z7_IFACE_COM7_IMP(IArchiveUpdateCallback)

public:
    const CObjectVector<CDirItem>* Items=NULL;

    void Init(const CObjectVector<CDirItem>* items){
        Items=items;
        FailedFiles.Clear();
        FailedCodes.Clear();
        NumErrors=0;
        TotalBytes=0;
        CompletedBytes=0;
        LastPercent=(UInt32)(Int32)-1;
        LastTick=0;
    }
    FStringVector FailedFiles;
    CRecordVector<HRESULT> FailedCodes;

    UInt64 TotalBytes=0;
    UInt64 CompletedBytes=0;
    UInt32 LastPercent=(UInt32)(Int32)-1;
    ULONGLONG LastTick=0;

    void EndProgressLine();

    UInt64 NumErrors=0;
private:
    void PrintProgress(bool force);
    UString CurrentItem;
    unsigned LastLineLen=0;







    // volumes unused
};

Z7_COM7F_IMF(CArchiveUpdateCallback::SetTotal(UInt64 size)){
    TotalBytes=size;
    CompletedBytes=0;
    LastPercent=(UInt32)(Int32)-1;
    LastTick=0;
    PrintProgress(true);
    return S_OK;
}

Z7_COM7F_IMF(CArchiveUpdateCallback::SetCompleted(const UInt64* completeValue)){
    if(completeValue)
        CompletedBytes=*completeValue;
    PrintProgress(false);
    return S_OK;
}

void CArchiveUpdateCallback::PrintProgress(bool force){
    if(!g__7zProgressCb) return;
    if(TotalBytes==0){
        if(force){
            g__7zProgressCb(g__7zProgressUser, g__7zProgressOp, 0, CurrentItem.Ptr());
        }
        return;
    }

    const ULONGLONG tick=::GetTickCount64();
    UInt32 percent=(UInt32)((CompletedBytes*100)/TotalBytes);
    if(percent>100) percent=100;
    if(!force){
        if(percent==LastPercent&&(tick-LastTick)<150)
            return;
    }
    LastPercent=percent;
    LastTick=tick;

    g__7zProgressCb(g__7zProgressUser, g__7zProgressOp, (unsigned)percent, CurrentItem.Ptr());
}


void CArchiveUpdateCallback::EndProgressLine(){
    TotalBytes=0;
    LastLineLen=0;
}

Z7_COM7F_IMF(CArchiveUpdateCallback::GetUpdateItemInfo(UInt32 /*index*/,Int32* newData,Int32* newProps,UInt32* indexInArchive)){
    if(newData) *newData=BoolToInt(true);
    if(newProps) *newProps=BoolToInt(true);
    if(indexInArchive) *indexInArchive=(UInt32)(Int32)-1;
    return S_OK;
}

Z7_COM7F_IMF(CArchiveUpdateCallback::GetProperty(UInt32 index,PROPID propID,PROPVARIANT* value)){
    NCOM::CPropVariant prop;
    const CDirItem& it=(*Items)[index];

    switch(propID){
    case kpidPath:   prop=it.Path_For_Handler; break;
    case kpidIsDir:  prop=it.IsDir; break;
    case kpidSize:   prop=it.Size; break;
    case kpidAttrib: prop=(UInt32)it.Attrib; break;
    case kpidCTime:  PropVariant_SetFrom_FiTime(prop,it.CTime); break;
    case kpidATime:  PropVariant_SetFrom_FiTime(prop,it.ATime); break;
    case kpidMTime:  PropVariant_SetFrom_FiTime(prop,it.MTime); break;
    default: break;
    }

    prop.Detach(value);
    return S_OK;
}

Z7_COM7F_IMF(CArchiveUpdateCallback::GetStream(UInt32 index,ISequentialInStream** inStream)){
    *inStream=NULL;
    const CDirItem& it=(*Items)[index];
    CurrentItem=it.Path_For_Handler;

    if(it.IsDir)
        return S_OK;

    NFind::CFileInfo currentInfo;
    DWORD validationError=ERROR_SUCCESS;
    if(!currentInfo.Find(it.FullPath)){
        validationError=::GetLastError();
        if(validationError==ERROR_SUCCESS)
            validationError=ERROR_FILE_NOT_FOUND;
    } else if(currentInfo.IsDir() ||
            (currentInfo.Attrib&FILE_ATTRIBUTE_REPARSE_POINT)!=0)
        validationError=ERROR_CANT_ACCESS_FILE;
    else if(currentInfo.Size!=it.Size)
        validationError=ERROR_FILE_INVALID;
    if(validationError!=ERROR_SUCCESS){
        const HRESULT hr=HRESULT_FROM_WIN32(validationError);
        FailedFiles.Add(it.FullPath);
        FailedCodes.Add(hr);
        PrintError("WARNING: source file changed before backup",it.FullPath);
        return S_FALSE;
    }

    CInFileStream* inSpec=new CInFileStream;
    CMyComPtr<ISequentialInStream> inLoc(inSpec);

#ifdef _WIN32
    if(!inSpec->OpenShared(it.FullPath,true))  // allow reading even if someone else has it open
#else
    if(!inSpec->Open(it.FullPath))
#endif
    {
#ifdef _WIN32
        const DWORD e=::GetLastError();
#else
        const DWORD e=::GetLastError();
#endif
        const HRESULT hr=HRESULT_FROM_WIN32(e);
        FailedFiles.Add(it.FullPath);
        FailedCodes.Add(hr);
        PrintError("WARNING: can't open file (skipping)",it.FullPath);
        return S_FALSE; // skip and continue
    }

    *inStream=inLoc.Detach();
    return S_OK;

}

Z7_COM7F_IMF(CArchiveUpdateCallback::SetOperationResult(Int32 operationResult)){
    if(operationResult!=0) // 0 == OK in 7-Zip update results
        NumErrors++;
    return S_OK; // keep going
}

Z7_COM7F_IMF(CArchiveUpdateCallback::GetVolumeSize(UInt32 /*index*/,UInt64* /*size*/)){ return S_FALSE; }

Z7_COM7F_IMF(CArchiveUpdateCallback::GetVolumeStream(UInt32 /*index*/,ISequentialOutStream** /*volumeStream*/)){ return S_FALSE; }


// -------------------------
// Property setup helper (your CLI equivalent)
// -------------------------
static HRESULT Apply7zUltraProps(IOutArchive* outArchive){
    // Mirrors these 7z.exe switches (for 7z format):
    //   -mx=9 -m0=lzma2 -md=256m -mfb=64 -ms=16g -mmt=on -mmemuse=p80
    //
    // Important detail: 7-Zip's console parses "-m<name>=<value>" and passes
    // just "<name>" into ISetProperties. For example "-m0=lzma2" becomes name "0".
    CMyComPtr<ISetProperties> setProps;
    outArchive->QueryInterface(IID_ISetProperties,(void**)&setProps);
    if(!setProps)
        return E_NOINTERFACE;

    const wchar_t* const names[]=
    {
      L"x",       // -mx=9
      L"0",       // -m0=lzma2  (method name for method slot 0)
      L"d",       // -md=256m   (dictionary)
      L"fb",      // -mfb=64
      L"s",       // -ms=16g    (solid block size)
      L"mt",      // -mmt=on
      L"memuse"   // -mmemuse=p80
    };

    const unsigned kNumProps=Z7_ARRAY_SIZE(names);
    NCOM::CPropVariant vals[kNumProps];
    unsigned n=0;

    vals[n++]=(UInt32)9;
    vals[n++]=L"lzma2";
    vals[n++]=L"256m";
    vals[n++]=(UInt32)64;
    vals[n++]=L"16g";
    vals[n++]=L"on";
    vals[n++]=L"p80";

    return setProps->SetProperties(names,vals,kNumProps);
}

static HRESULT Apply7zBackupProps(IOutArchive* outArchive){
    CMyComPtr<ISetProperties> setProps;
    outArchive->QueryInterface(IID_ISetProperties,(void**)&setProps);
    if(!setProps)
        return E_NOINTERFACE;

    const wchar_t* const names[]={L"x",L"0",L"d",L"s",L"mt",L"memuse"};
    NCOM::CPropVariant vals[Z7_ARRAY_SIZE(names)];
    unsigned n=0;
    vals[n++]=(UInt32)5;
    vals[n++]=L"lzma2";
    vals[n++]=L"64m";
    vals[n++]=L"2g";
    vals[n++]=L"on";
    vals[n++]=L"p50";
    return setProps->SetProperties(names,vals,Z7_ARRAY_SIZE(names));
}


// -------------------------
// Public functions you asked for
// -------------------------
static HRESULT AppendFileItem(const FString &filePath, const UString &archivePath,
                              CEnumBudget &budget,
                              CObjectVector<CDirItem> &items) {
    NFind::CFileInfo fi;
    if(!fi.Find(filePath))
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if(fi.IsDir())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    if((fi.Attrib&FILE_ATTRIBUTE_REPARSE_POINT)!=0)
        return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);

    CDirItem item;
    item.Path_For_Handler=archivePath;
    item.FullPath=filePath;
    item.IsDir=false;
    item.Attrib=fi.Attrib;
    item.CTime=fi.CTime;
    item.ATime=fi.ATime;
    item.MTime=fi.MTime;
    item.Size=fi.Size;
    HRESULT hr=ChargeEnumerationBudget(budget,false,item.Size);
    if(FAILED(hr))
        return hr;
    items.Add(item);
    return S_OK;
}

static HRESULT CompressDirectory7zImpl(
    const FString& archivePath,const FString& folderPath,bool includeTopDirectory,
    const FString *extraFilePath=nullptr,const UString *extraArchivePath=nullptr,
    bool useBackupProps=false){
    // Normalize folderPath: must exist and be directory
    NFind::CFileInfo fi;
    if(!fi.Find(folderPath))
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    if(!fi.IsDir())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    if((fi.Attrib&FILE_ATTRIBUTE_REPARSE_POINT)!=0)
        return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);

    CObjectVector<CDirItem> items;
    CEnumBudget budget;
    HRESULT hr=S_OK;

    UString rootName;
    if(includeTopDirectory){
        // Include the folder name as the top-level entry (equivalent to: 7za a Archive.7z "folder")
        FString rootNameFs=BasenameOfPath(folderPath);
        rootName=FsToUs(rootNameFs);

        // Add the root directory entry itself (helps preserve an empty root dir)
        CDirItem root;
        root.Path_For_Handler=rootName;
        root.FullPath=folderPath;
        root.IsDir=true;
        root.Attrib=fi.Attrib;
        root.CTime=fi.CTime;
        root.ATime=fi.ATime;
        root.MTime=fi.MTime;
        root.Size=0;
        hr=ChargeEnumerationBudget(budget,true,0);
        if(FAILED(hr))
            return hr;
        items.Add(root);
    } else {
        // Do NOT include the folder name in the archive; add only its contents at the root
        // (equivalent to: 7za a Archive.7z "folder\*")
        rootName.Empty();
    }

    // Enumerate children recursively
    const unsigned initialDepth=rootName.IsEmpty()?0:1;
    hr=EnumDirRecursive(folderPath,folderPath,rootName,initialDepth,budget,
                        items);
    if(FAILED(hr))
        return hr;

    if(extraFilePath && extraArchivePath){
        if(!IsSafeArchivePath(*extraArchivePath))
            return E_INVALIDARG;
        hr=AppendFileItem(*extraFilePath,*extraArchivePath,budget,items);
        if(FAILED(hr))
            return hr;
    }

    // Create output archive stream
    COutFileStream* outSpec=new COutFileStream;
    CMyComPtr<IOutStream> outStream=outSpec;
    if(!outSpec->Create_NEW(archivePath))
        return HRESULT_FROM_WIN32(::GetLastError());

    // Create 7z out archive handler
    CMyComPtr<IOutArchive> outArchive;
    hr=CreateArchiver(&CLSID_Format,&IID_IOutArchive,(void**)&outArchive);
    if(FAILED(hr)||!outArchive)
        return FAILED(hr)?hr:E_FAIL;

    // Apply your compression settings
    hr=useBackupProps ? Apply7zBackupProps(outArchive)
                      : Apply7zUltraProps(outArchive);
    if(FAILED(hr))
        return hr;

    // Update callback
    CArchiveUpdateCallback* cbSpec=new CArchiveUpdateCallback;
    CMyComPtr<IArchiveUpdateCallback2> cb(cbSpec);
    cbSpec->Init(&items);

    hr=outArchive->UpdateItems(outStream,items.Size(),cb);
    cbSpec->EndProgressLine();
    if(hr==S_FALSE || cbSpec->NumErrors!=0 || cbSpec->FailedFiles.Size()!=0)
        return E_FAIL;
    return hr;
}

static HRESULT OpenArchive7z(const FString& archivePath,
                             CMyComPtr<IInStream>& inStream,
                             CMyComPtr<IInArchive>& inArchive){
    CInFileStream* inSpec=new CInFileStream;
    inStream=inSpec;
    if(!inSpec->Open(archivePath))
        return HRESULT_FROM_WIN32(::GetLastError());

    HRESULT hr=CreateArchiver(&CLSID_Format,&IID_IInArchive,(void**)&inArchive);
    if(FAILED(hr)||!inArchive)
        return FAILED(hr)?hr:E_FAIL;

    // Open archive (no password callback)
    const UInt64 scanSize=(UInt64)1<<23;
    hr=inArchive->Open(inStream,&scanSize,NULL);
    if(FAILED(hr))
        return hr;
    return S_OK;
}

static HRESULT ExtractOrTestOpenedArchive7z(IInArchive* inArchive,
                                             const FString& outDir,
                                             bool testMode){
    if(!inArchive)
        return E_INVALIDARG;
    // Ensure output directory exists
    if(!outDir.IsEmpty())
        CreateComplexDir(outDir);

    // Extract callback
    CArchiveExtractCallback* cbSpec=new CArchiveExtractCallback;
    CMyComPtr<IArchiveExtractCallback> cb(cbSpec);
    cbSpec->Init(inArchive,outDir);

    HRESULT hr=inArchive->Extract(NULL,(UInt32)(Int32)(-1),testMode,cb);
    cbSpec->EndProgressLine();
    if(hr==S_FALSE || cbSpec->NumErrors!=0)
        return E_FAIL;
    return hr;
}

static HRESULT InspectOpenedArchive7z(IInArchive *inArchive,
                                      _7zArchiveInfo &info){
    info={};
    if(!inArchive)
        return E_INVALIDARG;

    UInt32 itemCount=0;
    RINOK(inArchive->GetNumberOfItems(&itemCount))
    if(itemCount==0 || itemCount>CtArchiveSafety::kMaxItems)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    info.itemCount=itemCount;
    std::set<std::wstring,CArchivePathLess> seenPaths;

    for(UInt32 index=0;index<itemCount;++index){
        NCOM::CPropVariant pathProp;
        RINOK(inArchive->GetProperty(index,kpidPath,&pathProp))
        if(pathProp.vt!=VT_BSTR)
            return E_INVALIDARG;
        const UString archivePath(pathProp.bstrVal);
        std::wstring pathKey;
        if(!GetArchivePathKey(archivePath,pathKey))
            return E_INVALIDARG;
        if(!seenPaths.insert(std::move(pathKey)).second)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);

        bool isDir=false;
        NCOM::CPropVariant dirProp;
        RINOK(inArchive->GetProperty(index,kpidIsDir,&dirProp))
        if(dirProp.vt==VT_BOOL)
            isDir=VARIANT_BOOLToBool(dirProp.boolVal);
        else if(dirProp.vt!=VT_EMPTY)
            return E_INVALIDARG;

        if(isDir){
            ++info.directoryCount;
        } else {
            ++info.fileCount;
        }

        NCOM::CPropVariant attribProp;
        RINOK(inArchive->GetProperty(index,kpidAttrib,&attribProp))
        if(attribProp.vt==VT_UI4){
            if(IsUnsafeArchiveAttrib(attribProp.ulVal))
                return E_INVALIDARG;
        } else if(attribProp.vt!=VT_EMPTY){
            return E_INVALIDARG;
        }

        if(isDir)
            continue;

        NCOM::CPropVariant sizeProp;
        RINOK(inArchive->GetProperty(index,kpidSize,&sizeProp))
        UInt64 size=0;
        if(sizeProp.vt==VT_UI8)
            size=sizeProp.uhVal.QuadPart;
        else if(sizeProp.vt==VT_UI4)
            size=sizeProp.ulVal;
        else if(sizeProp.vt!=VT_EMPTY)
            return E_INVALIDARG;
        if(size>CtArchiveSafety::kMaxUncompressedBytes-
                    info.totalUncompressedBytes)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        info.totalUncompressedBytes+=size;
    }
    return S_OK;
}

struct _7zRestoreSession {
    CMyComPtr<IInStream> InStream;
    CMyComPtr<IInArchive> InArchive;
};



// -------------------------
// Public API
// -------------------------

void _7zSetHInstance(HINSTANCE hInst){
#ifdef _WIN32
    g_hInstance = hInst;
#else
    (void)hInst;
#endif
}

HRESULT _7zExtra_7z(
    const wchar_t* archivePath,
    const wchar_t* outDir,
    _7zProgressCb progressCb,
    void* progressUser)
{
    if(!archivePath || !outDir) return E_INVALIDARG;
    _7zArchiveInfo info{};
    _7zRestoreSession *session=nullptr;
    HRESULT hr=_7zOpenRestoreSession(archivePath,&info,&session);
    if(SUCCEEDED(hr))
        hr=_7zExtractRestoreSession(session,outDir,progressCb,progressUser);
    _7zCloseRestoreSession(session);
    return hr;
}

HRESULT _7zInspect7z(const wchar_t *archivePath,_7zArchiveInfo *info){
    if(!archivePath || !info) return E_INVALIDARG;
    _7zRestoreSession *session=nullptr;
    const HRESULT hr=_7zOpenRestoreSession(archivePath,info,&session);
    _7zCloseRestoreSession(session);
    return hr;
}

HRESULT _7zOpenRestoreSession(const wchar_t *archivePath,
                              _7zArchiveInfo *info,
                              _7zRestoreSession **session){
    if(!archivePath || !info || !session) return E_INVALIDARG;
    *session=nullptr;
#ifdef _WIN32
    NT_CHECK
#endif
    const FString arc=us2fs(UString(archivePath));
    _7zRestoreSession *candidate=new(std::nothrow) _7zRestoreSession;
    if(!candidate)
        return E_OUTOFMEMORY;
    HRESULT hr=OpenArchive7z(arc,candidate->InStream,candidate->InArchive);
    if(SUCCEEDED(hr))
        hr=InspectOpenedArchive7z(candidate->InArchive,*info);
    if(FAILED(hr)){
        if(candidate->InArchive)
            candidate->InArchive->Close();
        delete candidate;
        return hr;
    }
    *session=candidate;
    return S_OK;
}

HRESULT _7zExtractRestoreSession(_7zRestoreSession *session,
                                 const wchar_t *outDir,
                                 _7zProgressCb progressCb,
                                 void *progressUser){
    if(!session || !session->InArchive || !outDir) return E_INVALIDARG;
    _7zScopedProgress sp(_7zOp::Extract,progressCb,progressUser);
    const FString out=us2fs(UString(outDir));
    HRESULT hr=ExtractOrTestOpenedArchive7z(session->InArchive,out,false);
    if(SUCCEEDED(hr) && progressCb)
        progressCb(progressUser,_7zOp::Extract,100,nullptr);
    return hr;
}

void _7zCloseRestoreSession(_7zRestoreSession *session){
    if(!session)
        return;
    if(session->InArchive)
        session->InArchive->Close();
    delete session;
}

HRESULT _7zTest7z(const wchar_t *archivePath,_7zProgressCb progressCb,
                  void *progressUser){
    if(!archivePath) return E_INVALIDARG;
    _7zArchiveInfo info{};
    _7zRestoreSession *session=nullptr;
    HRESULT hr=_7zOpenRestoreSession(archivePath,&info,&session);
    _7zScopedProgress sp(_7zOp::Extract,progressCb,progressUser);
    if(SUCCEEDED(hr))
        hr=ExtractOrTestOpenedArchive7z(session->InArchive,FString(),true);
    if(SUCCEEDED(hr) && progressCb)
        progressCb(progressUser,_7zOp::Extract,100,nullptr);
    _7zCloseRestoreSession(session);
    return hr;
}

HRESULT _7zCompress7z(
    const wchar_t* archivePath,
    const wchar_t* folderPath,
    bool includeTopDirectory,
    _7zProgressCb progressCb,
    void* progressUser)
{
    if(!archivePath || !folderPath) return E_INVALIDARG;
#ifdef _WIN32
    NT_CHECK
#endif
    _7zScopedProgress sp(_7zOp::Compress, progressCb, progressUser);
    const FString arc = us2fs(UString(archivePath));
    const FString dir = us2fs(UString(folderPath));
    HRESULT hr = CompressDirectory7zImpl(arc, dir, includeTopDirectory);
    if(SUCCEEDED(hr) && progressCb){
        progressCb(progressUser, _7zOp::Compress, 100, nullptr);
    }
    return hr;
}


HRESULT _7zCompress7zWithExtraFile(
    const wchar_t *archivePath,const wchar_t *folderPath,
    bool includeTopDirectory,const wchar_t *extraFilePath,
    const wchar_t *extraArchivePath,_7zProgressCb progressCb,
    void *progressUser){
    if(!archivePath || !folderPath || !extraFilePath || !extraArchivePath)
        return E_INVALIDARG;
#ifdef _WIN32
    NT_CHECK
#endif
    _7zScopedProgress sp(_7zOp::Compress,progressCb,progressUser);
    const FString arc=us2fs(UString(archivePath));
    const FString dir=us2fs(UString(folderPath));
    const FString extra=us2fs(UString(extraFilePath));
    const UString extraArc(extraArchivePath);
    HRESULT hr=CompressDirectory7zImpl(arc,dir,includeTopDirectory,&extra,
                                       &extraArc,true);
    if(SUCCEEDED(hr) && progressCb)
        progressCb(progressUser,_7zOp::Compress,100,nullptr);
    return hr;
}
