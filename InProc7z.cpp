#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "InProc7z.h"
#include "RestoreIoPath.h"

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
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "ClientNameComparison.h"

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
static thread_local _7zDetailedProgressCb g__7zDetailedCb = nullptr;
static thread_local void *g__7zDetailedUser = nullptr;
static thread_local _7zProgressPhase g__7zDetailedPhase = _7zProgressPhase::Verifying;
#ifdef CTSPACES_BACKUP_TEST_HOOKS
static thread_local unsigned g_cleanupDenied=0,g_cleanupObserved=0;
static thread_local bool g_cleanupTargeted=false;
static thread_local bool g_failOutputOwnershipAllocation=false,g_outputOwnershipAllocationFailureHit=false;
void _7zDenyCleanupParentAttemptsForTest(unsigned attempts){
    g_cleanupDenied=attempts;g_cleanupObserved=0;g_cleanupTargeted=false;
}
unsigned _7zCleanupParentAttemptsForTest(){return g_cleanupObserved;}
void _7zFailNextOutputOwnershipAllocationForTest(){
    g_failOutputOwnershipAllocation=true;g_outputOwnershipAllocationFailureHit=false;
}
bool _7zOutputOwnershipAllocationFailureHitForTest(){return g_outputOwnershipAllocationFailureHit;}
#endif

struct _7zScopedProgress {
    _7zProgressCb prevCb{};
    void* prevUser{};
    _7zOp prevOp{_7zOp::Extract};
    _7zDetailedProgressCb prevDetailed{};
    void *prevDetailedUser{};
    _7zProgressPhase prevPhase{};
    _7zScopedProgress(_7zOp op, _7zProgressCb cb, void* user,
        _7zDetailedProgressCb detailed = nullptr, void *detailedUser = nullptr){
        prevCb = g__7zProgressCb;
        prevUser = g__7zProgressUser;
        prevOp = g__7zProgressOp;
        prevDetailed = g__7zDetailedCb;
        prevDetailedUser = g__7zDetailedUser;
        prevPhase = g__7zDetailedPhase;
        g__7zProgressCb = cb;
        g__7zProgressUser = user;
        g__7zProgressOp = op;
        g__7zDetailedCb = detailed;
        g__7zDetailedUser = detailedUser;
        g__7zDetailedPhase = op == _7zOp::Compress ? _7zProgressPhase::Compressing : _7zProgressPhase::Verifying;
    }
    ~_7zScopedProgress(){
        g__7zProgressCb = prevCb;
        g__7zProgressUser = prevUser;
        g__7zProgressOp = prevOp;
        g__7zDetailedCb = prevDetailed;
        g__7zDetailedUser = prevDetailedUser;
        g__7zDetailedPhase = prevPhase;
    }
};

static HRESULT ReportArchiveDetail(_7zProgressPhase phase, UInt64 completed,
    UInt64 total, UInt64 items, UInt64 totalItems, bool known,
    const wchar_t *current = nullptr) {
    if(!g__7zDetailedCb)return S_OK;
    const _7zProgressInfo info{phase,completed,total,items,totalItems,known,current};
    try{g__7zDetailedCb(g__7zDetailedUser,info);return S_OK;}
    catch(...){return E_ABORT;}
}

// Capture operation-local callbacks on the caller thread. 7-Zip can later
// invoke the callback object on codec worker threads whose TLS is unrelated.
struct COperationProgress {
    struct State {std::recursive_mutex dispatch;std::atomic<bool> aborted{false};};
    std::shared_ptr<State> Shared=std::make_shared<State>();
    _7zProgressCb Callback=g__7zProgressCb;
    void *User=g__7zProgressUser;
    _7zOp Operation=g__7zProgressOp;
    _7zDetailedProgressCb Detailed=g__7zDetailedCb;
    void *DetailedUser=g__7zDetailedUser;
    _7zProgressPhase Phase=g__7zDetailedPhase;
    HRESULT Report(unsigned percent,const _7zProgressInfo &info) const {
        std::lock_guard<std::recursive_mutex> dispatch(Shared->dispatch);
        if(Shared->aborted.load())return E_ABORT;
        try{
            if(Callback)Callback(User,Operation,percent,info.currentItem);
            if(Detailed)Detailed(DetailedUser,info);
            return S_OK;
        }catch(...){Shared->aborted.store(true);return E_ABORT;}
    }
    bool Aborted()const{return Shared->aborted.load();}
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

static HRESULT LastExtractIoError() noexcept {
    const DWORD error=::GetLastError();
    return HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE);
}

// Called only inside a catch handler. This translation does not allocate or
// touch callback state, so SDK progress worker threads can use it safely.
static HRESULT CurrentExtractExceptionCode() noexcept {
    try{throw;}
    catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    catch(const std::filesystem::filesystem_error& error){
        const auto& code=error.code();
        if(code==std::errc::filename_too_long)return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        if(code==std::errc::invalid_argument)return E_INVALIDARG;
        if(code.category()==std::system_category() && code.value()>0)
            return HRESULT_FROM_WIN32(static_cast<DWORD>(code.value()));
        return E_FAIL;
    }
    catch(...){return E_FAIL;}
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
    // Keep the exact file object read-only for the entire archive transaction.
    // Reopening a pathname later can otherwise follow a substituted ancestor.
    CMyComPtr<IInStream> SourceStream;
    UInt64 FileId=0;
    UInt32 VolumeId=0;
};

struct CSourceDirectoryLease {
    HANDLE Handle=INVALID_HANDLE_VALUE;
    ~CSourceDirectoryLease(){if(Handle!=INVALID_HANDLE_VALUE)::CloseHandle(Handle);}
};

using CSourceDirectoryLeases=std::vector<std::unique_ptr<CSourceDirectoryLease>>;

static HRESULT PinSourceDirectory(const std::filesystem::path& path,
                                  CSourceDirectoryLeases& leases){
    auto lease=std::make_unique<CSourceDirectoryLease>();
    lease->Handle=::CreateFileW(path.c_str(),GENERIC_READ,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(lease->Handle==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(::GetLastError());
    BY_HANDLE_FILE_INFORMATION info{};
    if(!::GetFileInformationByHandle(lease->Handle,&info) ||
       !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
       (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
        return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);
    leases.push_back(std::move(lease));
    return S_OK;
}

static HRESULT PinSourceAncestors(const std::filesystem::path& source,
                                 CSourceDirectoryLeases& leases){
    const auto absolute=std::filesystem::absolute(source).lexically_normal();
    std::filesystem::path current=absolute.root_path();
    RINOK(PinSourceDirectory(current,leases))
    for(const auto& component:absolute.relative_path()){
        current/=component;
        RINOK(PinSourceDirectory(current,leases))
    }
    return S_OK;
}

static HRESULT BindSourceFile(CDirItem& item){
    HANDLE first=::CreateFileW(item.FullPath.Ptr(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(first==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(::GetLastError());
    BY_HANDLE_FILE_INFORMATION identity{};
    const bool regular=::GetFileInformationByHandle(first,&identity) &&
        !(identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    if(!regular){::CloseHandle(first);return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);}
    CInFileStream* stream=new CInFileStream;
    CMyComPtr<IInStream> owned(stream);
    const bool opened=stream->OpenShared(item.FullPath,false);
    CStreamFileProps props{};
    const HRESULT properties=opened?stream->GetProps2(&props):HRESULT_FROM_WIN32(::GetLastError());
    const UInt64 fileId=(static_cast<UInt64>(identity.nFileIndexHigh)<<32)|identity.nFileIndexLow;
    const bool same=SUCCEEDED(properties) && props.VolID==identity.dwVolumeSerialNumber &&
        props.FileID_Low==fileId && props.Size==item.Size &&
        !(props.Attrib&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    ::CloseHandle(first);
    if(!same)return FAILED(properties)?properties:HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    item.FileId=fileId;
    item.VolumeId=identity.dwVolumeSerialNumber;
    item.SourceStream=owned;
    return S_OK;
}

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
    CObjectVector<CDirItem>& outItems,
    CSourceDirectoryLeases& leases,
    const std::vector<std::wstring> *selectedChildren=nullptr){
#ifdef _WIN32
    std::vector<bool> seen;
    if(selectedChildren){
        if(selectedChildren->empty())return E_INVALIDARG;
        seen.resize(selectedChildren->size());
        for(size_t i=0;i<selectedChildren->size();++i){
            const auto &name=(*selectedChildren)[i];
            std::wstring key;
            if(name.find_first_of(L"\\/")!=std::wstring::npos || !GetArchivePathKey(UString(name.c_str()),key))return E_INVALIDARG;
            for(size_t j=0;j<i;++j)if(client_name_comparison::EqualIgnoringCase(name,(*selectedChildren)[j]))return E_INVALIDARG;
        }
    }
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
        if(selectedChildren){
            size_t selected=selectedChildren->size();
            for(size_t i=0;i<selectedChildren->size();++i)
                if(client_name_comparison::EqualIgnoringCase(name,(*selectedChildren)[i])){selected=i;break;}
            if(selected==selectedChildren->size())continue;
            if(seen[selected]){CloseFind();return HRESULT_FROM_WIN32(ERROR_DUP_NAME);}
            if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){CloseFind();return HRESULT_FROM_WIN32(ERROR_DIRECTORY);}
            seen[selected]=true;
        }

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
        hr=isDir?PinSourceDirectory(std::filesystem::path(childFull.Ptr()),leases):BindSourceFile(item);
        if(FAILED(hr)){CloseFind();return hr;}
        outItems.Add(item);
        hr=ReportArchiveDetail(g__7zDetailedPhase,budget.SourceBytes,0,budget.ItemCount,0,false,arcPath.Ptr());
        if(FAILED(hr)){CloseFind();return hr;}

        if(isDir){
            hr=EnumDirRecursive(rootDirFull,childFull,rootNameInArchive,
                                childDepth,budget,outItems,leases);
            if(FAILED(hr)){ CloseFind(); return hr; }
        }

    } while(::FindNextFileW(h,&fd));

    const DWORD last=::GetLastError();
    CloseFind();
    if(selectedChildren && std::find(seen.begin(),seen.end(),false)!=seen.end())return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
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

struct CExtractedObject {
    std::filesystem::path Path;
    HANDLE Handle=INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION Identity{};
    bool Directory=false;
    ~CExtractedObject(){if(Handle!=INVALID_HANDLE_VALUE)::CloseHandle(Handle);}
};

struct CExtractionOwnership {
    std::filesystem::path Root;
    CSourceDirectoryLeases Ancestors;
    std::vector<std::unique_ptr<CExtractedObject>> Created;
    std::set<std::wstring,CArchivePathLess> Directories;
    bool OwnRoot=false;
    HRESULT VerifyInventory(const std::vector<_7zRestoreObjectIdentity>& sealed) const {
        if(sealed.size()!=Created.size())return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        const auto key=[](const std::filesystem::path& path){
            auto value=path.lexically_normal().native();
            for(auto& character:value)if(character==L'/')character=L'\\';
            if(value==L".")value.clear();
            return value;
        };
        std::map<std::wstring,const _7zRestoreObjectIdentity*,CArchivePathLess> observed;
        for(const auto& entry:sealed)
            if(!observed.emplace(key(std::filesystem::path(entry.relativePath)),&entry).second)
                return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        for(const auto& owned:Created){
            const auto found=observed.find(key(owned->Path.lexically_relative(Root)));
            if(found==observed.end())return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            const auto& entry=*found->second;
            if(entry.directory!=owned->Directory || entry.volume!=owned->Identity.dwVolumeSerialNumber ||
               entry.indexHigh!=owned->Identity.nFileIndexHigh || entry.indexLow!=owned->Identity.nFileIndexLow)
                return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            observed.erase(found);
        }
        return observed.empty()?S_OK:HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    }
    HRESULT Bind(const std::filesystem::path& path,bool directory,
                 const BY_HANDLE_FILE_INFORMATION* expected=nullptr){
        auto object=std::make_unique<CExtractedObject>();object->Path=path;object->Directory=directory;
        const auto ioPath=CtRestoreIo::Path(path);
        object->Handle=::CreateFileW(ioPath.c_str(),(directory?DELETE|GENERIC_READ:GENERIC_READ),
            FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
        if(object->Handle==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(::GetLastError());
        if(!::GetFileInformationByHandle(object->Handle,&object->Identity))
            return LastExtractIoError();
        if((object->Identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
           (((object->Identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory) ||
           (!directory && object->Identity.nNumberOfLinks!=1) ||
           (expected && (expected->dwVolumeSerialNumber!=object->Identity.dwVolumeSerialNumber ||
              expected->nFileIndexHigh!=object->Identity.nFileIndexHigh || expected->nFileIndexLow!=object->Identity.nFileIndexLow)))
            return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        if(!directory)for(auto& recorded:Created) {
            if(!recorded->Directory && recorded->Path==path && recorded->Handle==INVALID_HANDLE_VALUE &&
               recorded->Identity.dwVolumeSerialNumber==object->Identity.dwVolumeSerialNumber &&
               recorded->Identity.nFileIndexHigh==object->Identity.nFileIndexHigh && recorded->Identity.nFileIndexLow==object->Identity.nFileIndexLow) {
                recorded->Handle=object->Handle;object->Handle=INVALID_HANDLE_VALUE;return S_OK;
            }
        }
        if(directory)Directories.insert(path.native());
        Created.push_back(std::move(object));return S_OK;
    }
    HRESULT Init(const std::filesystem::path& root,bool ownRoot,HANDLE protectedRoot){
        Root=std::filesystem::absolute(root).lexically_normal();OwnRoot=ownRoot;
        RINOK(PinSourceAncestors(Root.parent_path(),Ancestors))
        if(protectedRoot==INVALID_HANDLE_VALUE){RINOK(Bind(Root,true))}
        else {
            auto object=std::make_unique<CExtractedObject>();object->Path=Root;object->Directory=true;
            if(!::DuplicateHandle(::GetCurrentProcess(),protectedRoot,::GetCurrentProcess(),&object->Handle,0,FALSE,DUPLICATE_SAME_ACCESS) ||
               !::GetFileInformationByHandle(object->Handle,&object->Identity) ||
               !(object->Identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
               (object->Identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            const auto ioRoot=CtRestoreIo::Path(Root);
            HANDLE named=::CreateFileW(ioRoot.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            BY_HANDLE_FILE_INFORMATION identity{};
            const bool same=named!=INVALID_HANDLE_VALUE && ::GetFileInformationByHandle(named,&identity) &&
                identity.dwVolumeSerialNumber==object->Identity.dwVolumeSerialNumber && identity.nFileIndexHigh==object->Identity.nFileIndexHigh &&
                identity.nFileIndexLow==object->Identity.nFileIndexLow;
            if(named!=INVALID_HANDLE_VALUE)::CloseHandle(named);
            if(!same)return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            Directories.insert(Root.native());
            Created.push_back(std::move(object));
        }
        return S_OK;
    }
    HRESULT EnsureDirectories(const std::filesystem::path& directory){
        const auto relative=directory.lexically_relative(Root);
        std::filesystem::path current=Root;
        for(const auto& part:relative){
            if(part.empty() || part==L".")continue;
            if(part==L"..")return E_INVALIDARG;
            current/=part;
            if(Directories.contains(current.native()))continue;
            // Existing unexpected objects belong to somebody else, even when
            // their names happen to match an archive directory.
            const auto ioCurrent=CtRestoreIo::Path(current);
            if(!::CreateDirectoryW(ioCurrent.c_str(),nullptr))return HRESULT_FROM_WIN32(::GetLastError());
            RINOK(Bind(current,true))
        }
        return S_OK;
    }
    bool Cleanup(){
        bool complete=true;
        for(size_t index=Created.size();index>0;--index){
            auto& object=*Created[index-1];
            if(index==1 && !OwnRoot)continue;
            if(object.Handle==INVALID_HANDLE_VALUE && object.Directory){complete=false;continue;}
            std::filesystem::path ioPath;
            try{ioPath=CtRestoreIo::Path(object.Path);}
            catch(...){complete=false;continue;} // Cleanup must never throw from a failure-path destructor.
            if(!object.Directory){
                if(object.Handle!=INVALID_HANDLE_VALUE)::CloseHandle(object.Handle);
                object.Handle=INVALID_HANDLE_VALUE;
                object.Handle=::CreateFileW(ioPath.c_str(),DELETE|FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                BY_HANDLE_FILE_INFORMATION current{};
                if(object.Handle==INVALID_HANDLE_VALUE || !::GetFileInformationByHandle(object.Handle,&current) ||
                   current.dwVolumeSerialNumber!=object.Identity.dwVolumeSerialNumber ||
                   current.nFileIndexHigh!=object.Identity.nFileIndexHigh || current.nFileIndexLow!=object.Identity.nFileIndexLow ||
                   (current.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || current.nNumberOfLinks!=1){
                    complete=false;continue;
                }
            }
            // Set disposition on the bound original object; never recurse into
            // a pathname whose contents may now include foreign data.
            FILE_BASIC_INFO basic{};
            if(::GetFileInformationByHandleEx(object.Handle,FileBasicInfo,&basic,sizeof(basic)) &&
               (basic.FileAttributes&FILE_ATTRIBUTE_READONLY)){
                HANDLE attributes=::CreateFileW(ioPath.c_str(),FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT|(object.Directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
                if(attributes!=INVALID_HANDLE_VALUE){
                    BY_HANDLE_FILE_INFORMATION identity{};
                    if(::GetFileInformationByHandle(attributes,&identity) &&
                       identity.dwVolumeSerialNumber==object.Identity.dwVolumeSerialNumber &&
                       identity.nFileIndexHigh==object.Identity.nFileIndexHigh && identity.nFileIndexLow==object.Identity.nFileIndexLow){
                        basic.FileAttributes&=~FILE_ATTRIBUTE_READONLY;
                        if(!basic.FileAttributes)basic.FileAttributes=FILE_ATTRIBUTE_NORMAL;
                        ::SetFileInformationByHandle(attributes,FileBasicInfo,&basic,sizeof(basic));
                    }
                    ::CloseHandle(attributes);
                }
            }
            FILE_DISPOSITION_INFO disposition{TRUE};
            bool disposed=false;
#ifdef CTSPACES_BACKUP_TEST_HOOKS
            const bool targeted=object.Directory && !g_cleanupTargeted && g_cleanupDenied>0;
            if(targeted)g_cleanupTargeted=true;
#endif
            for(unsigned attempt=0;attempt<8;++attempt){
                BOOL removed=FALSE;
#ifdef CTSPACES_BACKUP_TEST_HOOKS
                if(targeted)++g_cleanupObserved;
                if(targeted && g_cleanupDenied>0){--g_cleanupDenied;::SetLastError(ERROR_DIR_NOT_EMPTY);}
                else
#endif
                removed=::SetFileInformationByHandle(object.Handle,FileDispositionInfo,&disposition,sizeof(disposition));
                if(removed){disposed=true;break;}
                const DWORD error=::GetLastError();
                // NTFS can briefly report a parent nonempty while recently
                // disposed children finish unlinking. Retry only disposition
                // of this still-bound original object; foreign contents are
                // never traversed or deleted.
                if(!object.Directory || (error!=ERROR_DIR_NOT_EMPTY && error!=ERROR_ACCESS_DENIED) || attempt==7)break;
                BY_HANDLE_FILE_INFORMATION identity{};
                if(!::GetFileInformationByHandle(object.Handle,&identity) ||
                   identity.dwVolumeSerialNumber!=object.Identity.dwVolumeSerialNumber ||
                   identity.nFileIndexHigh!=object.Identity.nFileIndexHigh || identity.nFileIndexLow!=object.Identity.nFileIndexLow ||
                   !(identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))break;
                ::Sleep(1u<<attempt);
            }
            if(!disposed)complete=false;
            ::CloseHandle(object.Handle);object.Handle=INVALID_HANDLE_VALUE;
        }
        return complete;
    }
};

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
    CExtractionOwnership* _ownership=nullptr;
    BY_HANDLE_FILE_INFORMATION _outputIdentity{};
    HRESULT _firstFailure=S_OK;
    std::wstring _firstFailureDetails;

public:
    void Init(IInArchive* archiveHandler,const FString& outDir,CExtractionOwnership* ownership=nullptr){
        Progress=COperationProgress{};
        NumErrors=0;
        _firstFailure=S_OK;
        _firstFailureDetails.clear();
        TotalBytes=0;
        CompletedBytes=0;
        LastPercent=(UInt32)(Int32)-1;
        LastTick=0;

        _archiveHandler=archiveHandler;
        TotalItems=0;
        UInt32 count=0;
        if(archiveHandler && SUCCEEDED(archiveHandler->GetNumberOfItems(&count)))TotalItems=count;
        CompletedItems=0;
        _outDir=outDir;
        _seenPaths.clear();
        _ownership=ownership;
        NName::NormalizeDirPathPrefix(_outDir);
        if(!_ownership && !_outDir.IsEmpty())
            CreateComplexDir(_outDir);
    }
    UInt64 NumErrors=0;

    bool ProgressAborted()const{return Progress.Aborted();}

    HRESULT FirstFailure()const{return _firstFailure;}
    const std::wstring& FirstFailureDetails()const{return _firstFailureDetails;}
    HRESULT RecordFailure(HRESULT code,const wchar_t* message,Int32 operationResult=-1,
                          const wchar_t* reason=nullptr) noexcept {
        ++NumErrors;
        if(SUCCEEDED(code))code=E_FAIL;
        if(SUCCEEDED(_firstFailure)){
            _firstFailure=code;
            try{
                _firstFailureDetails=message?message:L"Archive extraction failed.";
                if(reason){_firstFailureDetails+=L"\n";_firstFailureDetails+=reason;}
                if(operationResult>=0){
                    _firstFailureDetails+=L"\n7-Zip operation result: ";
                    _firstFailureDetails+=std::to_wstring(operationResult);
                }
                if(!_filePath.IsEmpty()){
                    _firstFailureDetails+=L"\nArchive item: ";
                    constexpr unsigned detailPathLimit=4096;
                    const unsigned count=_filePath.Len()>detailPathLimit?detailPathLimit:_filePath.Len();
                    _firstFailureDetails.append(_filePath.Ptr(),count);
                    if(count<_filePath.Len())_firstFailureDetails+=L"... [truncated]";
                }
            }catch(...){/* Preserve the native cause even if details cannot allocate. */}
        }
        return code;
    }

    HRESULT ExceptionFailure(const wchar_t* context) noexcept {
        const HRESULT code=CurrentExtractExceptionCode();
        const wchar_t* reason=L"The extraction callback failed unexpectedly.";
        if(code==E_OUTOFMEMORY)reason=L"There was not enough memory to continue archive extraction.";
        else if(code==HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE))
            reason=L"The restore staging path exceeds the supported Windows path length.";
        else if(code==E_INVALIDARG)reason=L"The restore staging path is invalid or uses an unsupported namespace.";
        else if(HRESULT_FACILITY(code)==FACILITY_WIN32)reason=L"A Windows operation failed during archive extraction.";
        return RecordFailure(code,context,-1,reason);
    }

    HRESULT ClosePendingOutput(){
        const HRESULT result=_outFileStreamSpec?_outFileStreamSpec->Close():S_OK;
        _outFileStream.Release();_outFileStreamSpec=nullptr;return result;
    }

    void EndProgressLine();

private:
    std::mutex ProgressMutex;
    COperationProgress Progress;
    UInt64 CompletedItems=0,TotalItems=0;
    UInt64 TotalBytes=0;
    UInt64 CompletedBytes=0;
    UInt32 LastPercent=(UInt32)(Int32)-1;
    ULONGLONG LastTick=0;

    HRESULT PrintProgress(bool force);
    UString CurrentItem;
    unsigned LastLineLen=0;


};

Z7_COM7F_IMF(CArchiveExtractCallback::SetTotal(UInt64 size)){
    try{
    {
    std::lock_guard<std::mutex> lock(ProgressMutex);
    TotalBytes=size;
    CompletedBytes=0;
    LastPercent=(UInt32)(Int32)-1;
    LastTick=0;
    }
    return PrintProgress(true);
    }catch(...){return CurrentExtractExceptionCode();}
}

Z7_COM7F_IMF(CArchiveExtractCallback::SetCompleted(const UInt64* completeValue)){
    try{
    {
    std::lock_guard<std::mutex> lock(ProgressMutex);
    if(completeValue)
        CompletedBytes=*completeValue;
    }
    return PrintProgress(false);
    }catch(...){return CurrentExtractExceptionCode();}
}

HRESULT CArchiveExtractCallback::PrintProgress(bool force){
    _7zProgressInfo info;
    std::wstring item;
    unsigned percent=0;
    {
        std::lock_guard<std::mutex> lock(ProgressMutex);
        if(!Progress.Callback && !Progress.Detailed)return S_OK;
        const ULONGLONG tick=::GetTickCount64();
        percent=TotalBytes?(CompletedBytes>=TotalBytes?100:static_cast<unsigned>((CompletedBytes*100)/TotalBytes)):0;
        if(!force && percent==LastPercent && tick-LastTick<150)return S_OK;
        LastPercent=percent;LastTick=tick;
        item=CurrentItem.Ptr();
        info={Progress.Phase,CompletedBytes,TotalBytes,CompletedItems,TotalItems,TotalBytes!=0,nullptr};
    }
    // User callbacks run without the state mutex. The snapshot owns its path;
    // an SDK worker can update the next item without racing this report.
    info.currentItem=item.empty()?nullptr:item.c_str();
    return Progress.Report(percent,info);
}


void CArchiveExtractCallback::EndProgressLine(){
    std::lock_guard<std::mutex> lock(ProgressMutex);
    TotalBytes=0;
    LastLineLen=0;
}


Z7_COM7F_IMF(CArchiveExtractCallback::GetStream(UInt32 index,ISequentialOutStream** outStream,Int32 askExtractMode)){
    try{
    *outStream=NULL;
    _outFileStream.Release();
    _outFileStreamSpec=nullptr;
    _processed={};
    _filePath.Empty();

    // Path
    {
        NCOM::CPropVariant prop;
        const HRESULT property=_archiveHandler->GetProperty(index,kpidPath,&prop);
        if(property!=S_OK)return RecordFailure(property,L"The archive entry path could not be read.");
            if(prop.vt==VT_EMPTY)
                _filePath=kEmptyFileAlias;
            else{
                if(prop.vt!=VT_BSTR){
                    PrintError("WARNING: bad kpidPath type (skipping)");
                    return RecordFailure(E_INVALIDARG,L"The archive entry has an invalid path type.");
                }
                _filePath=prop.bstrVal;
            }
    }
    {
        std::lock_guard<std::mutex> lock(ProgressMutex);
        CurrentItem=_filePath;
    }

    std::wstring pathKey;
    if(!GetArchivePathKey(_filePath,pathKey)){
        PrintError("Unsafe archive path",ArcPathToRelFsPath(_filePath));
        return RecordFailure(E_INVALIDARG,L"The archive contains an unsafe item path.");
    }
    if(!_seenPaths.insert(std::move(pathKey)).second){
        PrintError("Duplicate archive path",ArcPathToRelFsPath(_filePath));
        return RecordFailure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),L"The archive contains a duplicate item path.");
    }

    if(askExtractMode!=NArchive::NExtract::NAskMode::kExtract)
        return S_OK;

    // Attrib
    {
        NCOM::CPropVariant prop;
        const HRESULT property=_archiveHandler->GetProperty(index,kpidAttrib,&prop);
        if(property!=S_OK)return RecordFailure(property,L"The archive entry attributes could not be read.");

            if(prop.vt==VT_EMPTY){
                _processed.Attrib=0;
                _processed.Attrib_Defined=false;
            } else if(prop.vt==VT_UI4){
                if(IsUnsafeArchiveAttrib(prop.ulVal)){
                    PrintError("Unsafe archive attributes",
                               ArcPathToRelFsPath(_filePath));
                    return RecordFailure(E_INVALIDARG,L"The archive contains unsafe item attributes.");
                }
                _processed.Attrib=prop.ulVal;
                _processed.Attrib_Defined=true;
            } else{
                // was: return E_FAIL;
                _processed.Attrib=0;
                _processed.Attrib_Defined=false;
                PrintError("WARNING: unexpected kpidAttrib type (continuing)");
                RecordFailure(E_INVALIDARG,L"The archive entry has an invalid attribute type.");
            }
    }


    // IsDir
    {
        NCOM::CPropVariant prop;
        const HRESULT property=_archiveHandler->GetProperty(index,kpidIsDir,&prop);
        if(property!=S_OK)return RecordFailure(property,L"The archive entry type could not be read.");

            if(prop.vt==VT_BOOL){
                _processed.isDir=VARIANT_BOOLToBool(prop.boolVal);
            } else if(prop.vt==VT_EMPTY){
                _processed.isDir=false;
            } else{
                // was: return E_FAIL;
                _processed.isDir=false;
                PrintError("WARNING: unexpected kpidIsDir type (continuing)");
                RecordFailure(E_INVALIDARG,L"The archive entry has an invalid directory type.");
            }
    }


    // Ensure parent dirs exist
    {
        int slashPos=_filePath.ReverseFind_PathSepar();
        if(slashPos>=0){
            const FString parent=_outDir+ArcPathToRelFsPath(_filePath.Left(slashPos));
            const HRESULT created=_ownership?_ownership->EnsureDirectories(std::filesystem::path(parent.Ptr())):
                (CreateComplexDir(parent)?S_OK:LastExtractIoError());
            if(FAILED(created)){
                PrintError("WARNING: can't create directory (skipping)",parent);
                return RecordFailure(created,L"A restore staging parent directory could not be created or protected.");
            }
        }
    }

    _diskFilePath=_outDir+ArcPathToRelFsPath(_filePath);

    if(_processed.isDir){
        const HRESULT created=_ownership?_ownership->EnsureDirectories(std::filesystem::path(_diskFilePath.Ptr())):
            (CreateComplexDir(_diskFilePath)?S_OK:LastExtractIoError());
        if(FAILED(created)){
            PrintError("WARNING: can't create directory",_diskFilePath);
            return RecordFailure(created,L"A restore staging directory could not be created or protected.");
        }
        return S_OK;
    }

    // Replace if exists
    if(!_ownership){
        NFind::CFileInfo fi;
        if(fi.Find(_diskFilePath)){
            if(!DeleteFileAlways(_diskFilePath)){
                const HRESULT error=LastExtractIoError();
                PrintError("WARNING: cannot delete output file (skipping)",_diskFilePath);
                return RecordFailure(error,L"An existing extraction output file could not be replaced.");
            }
        }
    }

    const FString ioOutput=_ownership?us2fs(UString(CtRestoreIo::Path(std::filesystem::path(_diskFilePath.Ptr())).c_str())):_diskFilePath;
    _outFileStreamSpec=new COutFileStream;
    CMyComPtr<ISequentialOutStream> outLoc(_outFileStreamSpec);
    // SDK exception handlers can return here after owned-record allocation
    // fails. Keep the raw pending-close pointer backed by a member reference.
    _outFileStream=outLoc;
    if(_ownership?!_outFileStreamSpec->Create_NEW(ioOutput):!_outFileStreamSpec->Create_ALWAYS(ioOutput)){
        const HRESULT error=LastExtractIoError();
        PrintError("WARNING: cannot open output file (skipping)",_diskFilePath);
        _outFileStreamSpec=nullptr;
        return RecordFailure(error,L"A restore staging output file could not be created.");
    }
    if(_ownership && !::GetFileInformationByHandle(_outFileStreamSpec->File.GetHandle(),&_outputIdentity)){
        const HRESULT error=LastExtractIoError();_outFileStreamSpec=nullptr;
        return RecordFailure(error,L"The created restore staging file identity could not be read.");
    }
    if(_ownership){
#ifdef CTSPACES_BACKUP_TEST_HOOKS
        if(g_failOutputOwnershipAllocation){
            g_failOutputOwnershipAllocation=false;g_outputOwnershipAllocationFailureHit=true;
            throw std::bad_alloc();
        }
#endif
        auto recorded=std::make_unique<CExtractedObject>();recorded->Path=std::filesystem::path(_diskFilePath.Ptr());
        recorded->Identity=_outputIdentity;
        _ownership->Created.push_back(std::move(recorded));
    }

    *outStream=outLoc.Detach();
    return S_OK;
    }catch(...){return ExceptionFailure(L"A restore output stream could not be prepared.");}
}

Z7_COM7F_IMF(CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)){
    _extractMode=(askExtractMode==NArchive::NExtract::NAskMode::kExtract);
    return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::SetOperationResult(Int32 operationResult)){
    try{
    if(operationResult!=NArchive::NExtract::NOperationResult::kOK){
        PrintError("Extract error (operationResult != OK)");
        using namespace NArchive::NExtract::NOperationResult;
        HRESULT error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const wchar_t* detail=L"The archive decoder reported an extraction error.";
        switch(operationResult){
        case kUnsupportedMethod:error=E_NOTIMPL;detail=L"The backup uses an unsupported compression method.";break;
        case kDataError:detail=L"The archive decoder found invalid compressed data.";break;
        case kCRCError:error=HRESULT_FROM_WIN32(ERROR_CRC);detail=L"The backup failed its CRC integrity check.";break;
        case kUnavailable:detail=L"Required archive data is unavailable.";break;
        case kUnexpectedEnd:error=HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);detail=L"The compressed archive data ended unexpectedly.";break;
        case kDataAfterEnd:detail=L"Unexpected compressed data follows the archive stream.";break;
        case kIsNotArc:error=HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);detail=L"The selected file is not a supported archive.";break;
        case kHeadersError:error=HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);detail=L"The archive decoder found invalid archive headers.";break;
        case kWrongPassword:error=HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD);detail=L"The archive requires a valid password; encrypted backup extraction is not supported.";break;
        default:break;
        }
        RecordFailure(error,detail,operationResult);
    }

    if(_outFileStream&&_outFileStreamSpec){
        const HRESULT hrClose=_outFileStreamSpec->Close();
        if(hrClose!=S_OK){
            PrintError("WARNING: cannot close output file",_diskFilePath);
            RecordFailure(hrClose,L"A restore staging output file could not be closed.");
        }
    }
    _outFileStream.Release();
    _outFileStreamSpec=nullptr;

    if(_extractMode&&_processed.Attrib_Defined){
        const FString ioOutput=_ownership?us2fs(UString(CtRestoreIo::Path(std::filesystem::path(_diskFilePath.Ptr())).c_str())):_diskFilePath;
        SetFileAttrib_PosixHighDetect(ioOutput,_processed.Attrib);
    }
    if(_ownership && _extractMode && !_processed.isDir){
        const HRESULT bound=_ownership->Bind(std::filesystem::path(_diskFilePath.Ptr()),false,&_outputIdentity);
        if(FAILED(bound)){
            return RecordFailure(bound,L"The extracted restore file could not be protected or no longer matches its created identity.");
        }
    }
    {
        std::lock_guard<std::mutex> lock(ProgressMutex);
        ++CompletedItems;
    }
    return PrintProgress(false);
    }catch(...){return ExceptionFailure(L"An extracted restore item could not be finalized or protected.");}
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
        Progress=COperationProgress{};
        Items=items;
        FailedFiles.Clear();
        FailedCodes.Clear();
        NumErrors=0;
        TotalBytes=0;
        CompletedBytes=0;
        LastPercent=(UInt32)(Int32)-1;
        LastTick=0;
        CompletedItems=0;
        TotalItems=items?items->Size():0;
    }
    FStringVector FailedFiles;
    CRecordVector<HRESULT> FailedCodes;

    UInt64 TotalBytes=0;
    UInt64 CompletedBytes=0;
    UInt32 LastPercent=(UInt32)(Int32)-1;
    ULONGLONG LastTick=0;

    void EndProgressLine();

    UInt64 NumErrors=0;
    bool ProgressAborted()const{return Progress.Aborted();}
private:
    std::mutex ProgressMutex;
    COperationProgress Progress;
    UInt64 CompletedItems=0,TotalItems=0;
    HRESULT PrintProgress(bool force);
    UString CurrentItem;
    unsigned LastLineLen=0;







    // volumes unused
};

Z7_COM7F_IMF(CArchiveUpdateCallback::SetTotal(UInt64 size)){
    {
    std::lock_guard<std::mutex> lock(ProgressMutex);
    TotalBytes=size;
    CompletedBytes=0;
    LastPercent=(UInt32)(Int32)-1;
    LastTick=0;
    }
    return PrintProgress(true);
}

Z7_COM7F_IMF(CArchiveUpdateCallback::SetCompleted(const UInt64* completeValue)){
    {
    std::lock_guard<std::mutex> lock(ProgressMutex);
    if(completeValue)
        CompletedBytes=*completeValue;
    }
    return PrintProgress(false);
}

HRESULT CArchiveUpdateCallback::PrintProgress(bool force){
    _7zProgressInfo info;
    std::wstring item;
    unsigned percent=0;
    {
        std::lock_guard<std::mutex> lock(ProgressMutex);
        if(!Progress.Callback && !Progress.Detailed)return S_OK;
        const ULONGLONG tick=::GetTickCount64();
        percent=TotalBytes?(CompletedBytes>=TotalBytes?100:static_cast<unsigned>((CompletedBytes*100)/TotalBytes)):0;
        if(!force && percent==LastPercent && tick-LastTick<150)return S_OK;
        LastPercent=percent;LastTick=tick;
        item=CurrentItem.Ptr();
        info={Progress.Phase,CompletedBytes,TotalBytes,CompletedItems,TotalItems,TotalBytes!=0,nullptr};
    }
    info.currentItem=item.empty()?nullptr:item.c_str();
    return Progress.Report(percent,info);
}


void CArchiveUpdateCallback::EndProgressLine(){
    std::lock_guard<std::mutex> lock(ProgressMutex);
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
    {
        std::lock_guard<std::mutex> lock(ProgressMutex);
        CurrentItem=it.Path_For_Handler;
    }

    if(it.IsDir)
        return S_OK;

    if(!it.SourceStream || FAILED(it.SourceStream->Seek(0,STREAM_SEEK_SET,nullptr))){
        const HRESULT hr=HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        FailedFiles.Add(it.FullPath);
        FailedCodes.Add(hr);
        PrintError("WARNING: source snapshot is unavailable",it.FullPath);
        return S_FALSE;
    }
    *inStream=it.SourceStream;
    (*inStream)->AddRef();
    return S_OK;

}

Z7_COM7F_IMF(CArchiveUpdateCallback::SetOperationResult(Int32 operationResult)){
    if(operationResult!=0) // 0 == OK in 7-Zip update results
        NumErrors++;
    {
        std::lock_guard<std::mutex> lock(ProgressMutex);
        ++CompletedItems;
    }
    return PrintProgress(false);
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
    RINOK(BindSourceFile(item))
    items.Add(item);
    return S_OK;
}

Z7_CLASS_IMP_COM_1(CBoundBackupOutput,IOutStream)
    Z7_IFACE_COM7_IMP(ISequentialOutStream)
public:
    HANDLE File=INVALID_HANDLE_VALUE;
    ~CBoundBackupOutput(){if(File!=INVALID_HANDLE_VALUE)::CloseHandle(File);}
};
Z7_COM7F_IMF(CBoundBackupOutput::Write(const void* data,UInt32 size,UInt32* processedSize)){
    if(processedSize)*processedSize=0;
    DWORD written=0;
    if(!::WriteFile(File,data,size,&written,nullptr))return HRESULT_FROM_WIN32(::GetLastError());
    if(processedSize)*processedSize=written;
    return size && !written?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):S_OK;
}
Z7_COM7F_IMF(CBoundBackupOutput::Seek(Int64 offset,UInt32 origin,UInt64* newPosition)){
    if(origin>FILE_END)return E_INVALIDARG;
    LARGE_INTEGER distance{},position{};distance.QuadPart=offset;
    if(!::SetFilePointerEx(File,distance,&position,origin))return HRESULT_FROM_WIN32(::GetLastError());
    if(newPosition)*newPosition=static_cast<UInt64>(position.QuadPart);
    return S_OK;
}
Z7_COM7F_IMF(CBoundBackupOutput::SetSize(UInt64 size)){
    if(size>static_cast<UInt64>(INT64_MAX))return E_INVALIDARG;
    FILE_END_OF_FILE_INFO end{};end.EndOfFile.QuadPart=static_cast<LONGLONG>(size);
    return ::SetFileInformationByHandle(File,FileEndOfFileInfo,&end,sizeof(end))?S_OK:HRESULT_FROM_WIN32(::GetLastError());
}

static HRESULT CompressDirectory7zImpl(
    const FString& archivePath,const FString& folderPath,bool includeTopDirectory,
    const FString *extraFilePath=nullptr,const UString *extraArchivePath=nullptr,
    bool useBackupProps=false,bool *outputCreated=nullptr,
    BY_HANDLE_FILE_INFORMATION *outputIdentity=nullptr,HANDLE *outputLease=nullptr,
    const std::vector<std::wstring> *selectedRootChildren=nullptr){
    std::optional<std::vector<std::wstring>> capturedSelection;
    if(selectedRootChildren){capturedSelection=*selectedRootChildren;selectedRootChildren=&*capturedSelection;}
    // Normalize folderPath: must exist and be directory
    NFind::CFileInfo fi;
    if(!fi.Find(folderPath))
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    if(!fi.IsDir())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    if((fi.Attrib&FILE_ATTRIBUTE_REPARSE_POINT)!=0)
        return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);

    CObjectVector<CDirItem> items;
    CSourceDirectoryLeases leases;
    RINOK(PinSourceAncestors(std::filesystem::path(folderPath.Ptr()),leases))
    if(extraFilePath)
        RINOK(PinSourceAncestors(std::filesystem::path(extraFilePath->Ptr()).parent_path(),leases))
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
    g__7zDetailedPhase=_7zProgressPhase::Scanning;
    RINOK(ReportArchiveDetail(g__7zDetailedPhase,0,0,0,0,false))
    hr=EnumDirRecursive(folderPath,folderPath,rootName,initialDepth,budget,
                        items,leases,selectedRootChildren);
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
    CMyComPtr<IOutStream> outStream;
    HANDLE createdOutput=INVALID_HANDLE_VALUE;
    if(outputLease){
        CBoundBackupOutput* owned=new CBoundBackupOutput;outStream=owned;
        owned->File=::CreateFileW(archivePath.Ptr(),GENERIC_READ|GENERIC_WRITE|DELETE,
            FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(owned->File==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(::GetLastError());
        createdOutput=owned->File;
    } else {
        COutFileStream* ordinary=new COutFileStream;outStream=ordinary;
        if(!ordinary->Create_NEW(archivePath))return HRESULT_FROM_WIN32(::GetLastError());
        createdOutput=ordinary->File.GetHandle();
    }
    if(outputCreated)
        *outputCreated=true;
    if(outputIdentity && !::GetFileInformationByHandle(createdOutput,outputIdentity))
        return HRESULT_FROM_WIN32(::GetLastError());
    if(outputLease && !::DuplicateHandle(::GetCurrentProcess(),createdOutput,::GetCurrentProcess(),
        outputLease,0,FALSE,DUPLICATE_SAME_ACCESS))
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
    g__7zDetailedPhase=_7zProgressPhase::Compressing;
    RINOK(ReportArchiveDetail(g__7zDetailedPhase,0,budget.SourceBytes,0,items.Size(),budget.SourceBytes!=0))
    CArchiveUpdateCallback* cbSpec=new CArchiveUpdateCallback;
    CMyComPtr<IArchiveUpdateCallback2> cb(cbSpec);
    cbSpec->Init(&items);

    hr=outArchive->UpdateItems(outStream,items.Size(),cb);
    if(cbSpec->ProgressAborted())hr=E_ABORT;
    cbSpec->EndProgressLine();
    if(hr==S_FALSE || cbSpec->NumErrors!=0 || cbSpec->FailedFiles.Size()!=0)
        return E_FAIL;
    if(SUCCEEDED(hr) && g__7zProgressCb)
        g__7zProgressCb(g__7zProgressUser,_7zOp::Compress,100,nullptr);
    if(FAILED(hr))return hr;
    RINOK(ReportArchiveDetail(_7zProgressPhase::Compressing,budget.SourceBytes,budget.SourceBytes,items.Size(),items.Size(),budget.SourceBytes!=0))
    // Directory leases prevent namespace substitutions; file streams deny
    // writes/deletes throughout compression. Detect additions made meanwhile.
    CObjectVector<CDirItem> after;
    CEnumBudget afterBudget;
    CSourceDirectoryLeases afterLeases;
    g__7zDetailedPhase=_7zProgressPhase::CheckingSource;
    RINOK(ReportArchiveDetail(g__7zDetailedPhase,0,0,0,0,false))
    if(includeTopDirectory)after.Add(items[0]);
    RINOK(EnumDirRecursive(folderPath,folderPath,rootName,initialDepth,afterBudget,after,afterLeases,selectedRootChildren))
    if(extraFilePath && extraArchivePath)
        RINOK(AppendFileItem(*extraFilePath,*extraArchivePath,afterBudget,after))
    if(after.Size()!=items.Size())return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    using SourceKey=std::tuple<std::wstring,bool,UInt32,UInt64>;
    std::multiset<SourceKey> originals,current;
    for(unsigned i=0;i<items.Size();++i)
        originals.emplace(items[i].Path_For_Handler.Ptr(),items[i].IsDir,items[i].VolumeId,items[i].FileId);
    for(unsigned i=0;i<after.Size();++i)
        current.emplace(after[i].Path_For_Handler.Ptr(),after[i].IsDir,after[i].VolumeId,after[i].FileId);
    if(current!=originals)return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
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
                                             bool testMode,CExtractionOwnership* ownership=nullptr,
                                             std::wstring* errorDetails=nullptr){
    if(errorDetails)errorDetails->clear();
    if(!inArchive)
        return E_INVALIDARG;
    // Ensure output directory exists
    if(!ownership && !outDir.IsEmpty())
        CreateComplexDir(outDir);

    // Extract callback
    CArchiveExtractCallback* cbSpec=new CArchiveExtractCallback;
    CMyComPtr<IArchiveExtractCallback> cb(cbSpec);
    cbSpec->Init(inArchive,outDir,ownership);

    HRESULT hr=inArchive->Extract(NULL,(UInt32)(Int32)(-1),testMode,cb);
    const HRESULT closed=cbSpec->ClosePendingOutput();
    if(FAILED(closed))cbSpec->RecordFailure(closed,L"The pending restore output file could not be closed.");
    cbSpec->EndProgressLine();
    if(hr==E_ABORT || cbSpec->ProgressAborted())return E_ABORT;
    if(FAILED(cbSpec->FirstFailure())){
        if(errorDetails)*errorDetails=cbSpec->FirstFailureDetails();
        return cbSpec->FirstFailure();
    }
    if(hr==S_FALSE)hr=E_FAIL;
    if(FAILED(hr) && errorDetails){
        if(hr==E_OUTOFMEMORY)*errorDetails=L"There was not enough memory to extract the archive.";
        else if(hr==E_NOTIMPL)*errorDetails=L"The backup uses an unsupported compression method.";
        else if(HRESULT_FACILITY(hr)==FACILITY_WIN32)
            *errorDetails=L"A Windows operation failed while reading or writing the archive extraction data.";
        else *errorDetails=L"The archive decoder could not complete extraction.";
    }
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
        const auto registered=seenPaths.insert(std::move(pathKey));
        if(!registered.second)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        const auto& normalizedPath=*registered.first;

        bool isDir=false;
        NCOM::CPropVariant dirProp;
        RINOK(inArchive->GetProperty(index,kpidIsDir,&dirProp))
        if(dirProp.vt==VT_BOOL)
            isDir=VARIANT_BOOLToBool(dirProp.boolVal);
        else if(dirProp.vt!=VT_EMPTY)
            return E_INVALIDARG;

        const bool directSitesChild=normalizedPath.size()>6 && normalizedPath[5]==L'\\' &&
            ::CompareStringOrdinal(normalizedPath.data(),5,L"Sites",5,TRUE)==CSTR_EQUAL &&
            normalizedPath.find(L'\\',6)==std::wstring::npos;
        if(isDir){
            ++info.directoryCount;
            if(directSitesChild){
                ++info.sitesProfileCount;
                info.sitesClientDirectoryNames.push_back(normalizedPath.substr(6));
            }
        } else {
            ++info.fileCount;
            if(directSitesChild)++info.sitesRootFileCount;
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
    std::unique_ptr<CExtractionOwnership> Output;
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
    // Progress callbacks may throw while reporting completion. Retain the
    // session owner before invoking one so archive/output/ancestor leases are
    // released on every exit, including exception unwinding.
    std::unique_ptr<_7zRestoreSession,decltype(&_7zCloseRestoreSession)>
        owner(session,&_7zCloseRestoreSession);
    if(SUCCEEDED(hr))
        hr=_7zExtractRestoreSession(session,outDir,progressCb,progressUser);
    return hr;
}

HRESULT _7zInspect7z(const wchar_t *archivePath,_7zArchiveInfo *info){
    if(!archivePath || !info) return E_INVALIDARG;
    _7zRestoreSession *session=nullptr;
    const HRESULT hr=_7zOpenRestoreSession(archivePath,info,&session);
    std::unique_ptr<_7zRestoreSession,decltype(&_7zCloseRestoreSession)>
        owner(session,&_7zCloseRestoreSession);
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
    std::unique_ptr<_7zRestoreSession,decltype(&_7zCloseRestoreSession)>
        candidate(new(std::nothrow) _7zRestoreSession,&_7zCloseRestoreSession);
    if(!candidate)
        return E_OUTOFMEMORY;
    HRESULT hr=OpenArchive7z(arc,candidate->InStream,candidate->InArchive);
    if(SUCCEEDED(hr))
        hr=InspectOpenedArchive7z(candidate->InArchive,*info);
    if(FAILED(hr))return hr;
    *session=candidate.release();
    return S_OK;
}

HRESULT _7zExtractRestoreSession(_7zRestoreSession *session,
                                 const wchar_t *outDir,
                                 _7zProgressCb progressCb,
                                 void *progressUser,bool ownsEmptyOutputRoot,HANDLE protectedRoot,
                                 std::wstring* errorDetails){
    if(errorDetails)errorDetails->clear();
    if(!session || !session->InArchive || !outDir) return E_INVALIDARG;
    _7zScopedProgress sp(_7zOp::Extract,progressCb,progressUser);
    const FString out=us2fs(UString(std::filesystem::absolute(std::filesystem::path(outDir)).lexically_normal().c_str()));
    if(session->Output)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    session->Output=std::make_unique<CExtractionOwnership>();
    if(!CreateComplexDir(out)){
        const HRESULT error=LastExtractIoError();
        if(errorDetails)*errorDetails=L"The restore staging root could not be created.";
        return error;
    }
    HRESULT hr=session->Output->Init(std::filesystem::path(outDir),ownsEmptyOutputRoot,protectedRoot);
    if(FAILED(hr) && errorDetails)*errorDetails=L"The restore staging root or its original ancestors could not be protected.";
    if(SUCCEEDED(hr))hr=ExtractOrTestOpenedArchive7z(session->InArchive,out,false,session->Output.get(),errorDetails);
    if(SUCCEEDED(hr) && progressCb)
        progressCb(progressUser,_7zOp::Extract,100,nullptr);
    return hr;
}

bool _7zCleanupRestoreOutput(_7zRestoreSession *session){
    if(!session)return true;
    // A failed codec may retain its last output stream/callback until Close.
    // Release those writer handles before dispositioning owned files/parents.
    if(session->InArchive){session->InArchive->Close();session->InArchive.Release();}
    session->InStream.Release();
    return !session->Output || session->Output->Cleanup();
}

HRESULT _7zVerifyRestoreOutputInventory(_7zRestoreSession *session,
    const std::vector<_7zRestoreObjectIdentity>& sealedObjects){
    if(!session || !session->Output)return E_INVALIDARG;
    try{return session->Output->VerifyInventory(sealedObjects);}
    catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    catch(...){return E_FAIL;}
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
    std::unique_ptr<_7zRestoreSession,decltype(&_7zCloseRestoreSession)>
        owner(session,&_7zCloseRestoreSession);
    _7zScopedProgress sp(_7zOp::Extract,progressCb,progressUser);
    if(SUCCEEDED(hr))
        hr=ExtractOrTestOpenedArchive7z(session->InArchive,FString(),true);
    if(SUCCEEDED(hr) && progressCb)
        progressCb(progressUser,_7zOp::Extract,100,nullptr);
    return hr;
}

Z7_CLASS_IMP_COM_1(CBoundBackupInput,IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
public:
    HANDLE File=INVALID_HANDLE_VALUE;
    ~CBoundBackupInput(){if(File!=INVALID_HANDLE_VALUE)::CloseHandle(File);}
};
Z7_COM7F_IMF(CBoundBackupInput::Read(void *data,UInt32 size,UInt32 *processedSize)){
    DWORD read=0;if(processedSize)*processedSize=0;
    if(!::ReadFile(File,data,size,&read,nullptr))return HRESULT_FROM_WIN32(::GetLastError());
    if(processedSize)*processedSize=read;return S_OK;
}
Z7_COM7F_IMF(CBoundBackupInput::Seek(Int64 offset,UInt32 origin,UInt64 *position)){
    if(origin>FILE_END)return E_INVALIDARG;
    LARGE_INTEGER distance{},actual{};distance.QuadPart=offset;
    if(!::SetFilePointerEx(File,distance,&actual,origin))return HRESULT_FROM_WIN32(::GetLastError());
    if(position)*position=static_cast<UInt64>(actual.QuadPart);return S_OK;
}
HRESULT _7zInspectAndTest7zHandle(HANDLE archive,_7zArchiveInfo *info,
                                  _7zProgressCb progressCb,void *progressUser,
                                  _7zDetailedProgressCb detailedCb,void *detailedUser){
    if(archive==INVALID_HANDLE_VALUE || !archive || !info)return E_INVALIDARG;
    BY_HANDLE_FILE_INFORMATION identity{};
    if(!::GetFileInformationByHandle(archive,&identity) || ::GetFileType(archive)!=FILE_TYPE_DISK ||
       (identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || identity.nNumberOfLinks!=1)
        return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    auto* input=new(std::nothrow) CBoundBackupInput;
    if(!input)return E_OUTOFMEMORY;
    CMyComPtr<IInStream> stream=input;
    if(!::DuplicateHandle(::GetCurrentProcess(),archive,::GetCurrentProcess(),&input->File,0,FALSE,DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(::GetLastError());
    RINOK(stream->Seek(0,STREAM_SEEK_SET,nullptr))
    CMyComPtr<IInArchive> handler;
    HRESULT hr=CreateArchiver(&CLSID_Format,&IID_IInArchive,(void**)&handler);
    if(FAILED(hr)||!handler)return FAILED(hr)?hr:E_FAIL;
    _7zScopedProgress progress(_7zOp::Extract,progressCb,progressUser,detailedCb,detailedUser);
    RINOK(ReportArchiveDetail(_7zProgressPhase::Verifying,0,0,0,0,false))
    const UInt64 scanSize=(UInt64)1<<23;
    hr=handler->Open(stream,&scanSize,nullptr);
    if(SUCCEEDED(hr))hr=InspectOpenedArchive7z(handler,*info);
    if(SUCCEEDED(hr))hr=ExtractOrTestOpenedArchive7z(handler,FString(),true);
    if(SUCCEEDED(hr) && progressCb)progressCb(progressUser,_7zOp::Extract,100,nullptr);
    if(SUCCEEDED(hr))hr=ReportArchiveDetail(_7zProgressPhase::Verifying,info->totalUncompressedBytes,info->totalUncompressedBytes,info->itemCount,info->itemCount,info->totalUncompressedBytes!=0);
    handler->Close();return hr;
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
    return hr;
}


HRESULT _7zCompress7zWithExtraFile(
    const wchar_t *archivePath,const wchar_t *folderPath,
    bool includeTopDirectory,const wchar_t *extraFilePath,
    const wchar_t *extraArchivePath,_7zProgressCb progressCb,
    void *progressUser,bool *outputCreated,BY_HANDLE_FILE_INFORMATION *outputIdentity,HANDLE *outputLease,
    _7zDetailedProgressCb detailedCb,void *detailedUser,const std::vector<std::wstring> *selectedRootChildren){
    if(outputCreated)
        *outputCreated=false;
    if(outputIdentity)*outputIdentity={};
    if(outputLease)*outputLease=INVALID_HANDLE_VALUE;
    if(!archivePath || !folderPath || !extraFilePath || !extraArchivePath)
        return E_INVALIDARG;
#ifdef _WIN32
    NT_CHECK
#endif
    _7zScopedProgress sp(_7zOp::Compress,progressCb,progressUser,detailedCb,detailedUser);
    const FString arc=us2fs(UString(archivePath));
    const FString dir=us2fs(UString(folderPath));
    const FString extra=us2fs(UString(extraFilePath));
    const UString extraArc(extraArchivePath);
    HRESULT hr=CompressDirectory7zImpl(arc,dir,includeTopDirectory,&extra,
                                        &extraArc,true,outputCreated,outputIdentity,outputLease,selectedRootChildren);
    return hr;
}
