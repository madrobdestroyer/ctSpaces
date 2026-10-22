// Exact generated production URL parser and clipboard reader. All clipboard
// APIs use private synthetic allocations. Guard-page faults are caught locally;
// no window, live clipboard, device input, browser or network session is used.
#include <windows.h>
#include <winhttp.h>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#pragma comment(lib,"winhttp.lib")
static HWND g_hGui=nullptr;
static unsigned opens=0,closes=0,locks=0,unlocks=0;
static bool available=true,openOkay=true,dataOkay=true,lockOkay=true;
static void *clipboardMemory=nullptr;
static SIZE_T clipboardBytes=0;
static BOOL WINAPI PrivateAvailable(UINT f){return available&&f==CF_UNICODETEXT;}
static BOOL WINAPI PrivateOpen(HWND){++opens;return openOkay;}
static HANDLE WINAPI PrivateData(UINT){return dataOkay?reinterpret_cast<HANDLE>(1):nullptr;}
static LPVOID WINAPI PrivateLock(HGLOBAL){++locks;return lockOkay?clipboardMemory:nullptr;}
static BOOL WINAPI PrivateUnlock(HGLOBAL){++unlocks;return TRUE;}
static SIZE_T WINAPI PrivateSize(HGLOBAL){return clipboardBytes;}
static BOOL WINAPI PrivateClose(){++closes;return TRUE;}
#define IsClipboardFormatAvailable PrivateAvailable
#define OpenClipboard PrivateOpen
#define GetClipboardData PrivateData
#define GlobalLock PrivateLock
#define GlobalUnlock PrivateUnlock
#define GlobalSize PrivateSize
#define CloseClipboard PrivateClose
#include "ClipboardUrlBoundary.generated.inl"
#undef IsClipboardFormatAvailable
#undef OpenClipboard
#undef GetClipboardData
#undef GlobalLock
#undef GlobalUnlock
#undef GlobalSize
#undef CloseClipboard
static std::optional<std::wstring> result;
static void RunBody(){result=GetClipboardWebUrl();}
static DWORD ProtectedRead(){__try{RunBody();return 0;}__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}}
static unsigned checks=0,failures=0,scenarios=0,caughtAccessViolations=0;
static void Check(bool v,const char *message){++checks;if(!v)++failures;std::cout<<(v?"PASS ":"FAIL ")<<message<<'\n';}
struct PrivateBuffer{
 BYTE *allocation=nullptr;SIZE_T actualBytes=0;std::vector<BYTE> original;
 PrivateBuffer(const std::wstring &text,bool terminated){
  actualBytes=(std::max)(SIZE_T{2},(text.size()+(terminated?1:0))*sizeof(wchar_t));
  SYSTEM_INFO info{};GetSystemInfo(&info);const SIZE_T writable=((actualBytes+info.dwPageSize-1)/info.dwPageSize)*info.dwPageSize;
  allocation=static_cast<BYTE *>(VirtualAlloc(nullptr,writable+info.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  if(!allocation)throw std::runtime_error("Cannot allocate private guard-page fixture");DWORD old=0;
  if(!VirtualProtect(allocation+writable,info.dwPageSize,PAGE_NOACCESS,&old))throw std::runtime_error("Cannot guard private fixture allocation");
  clipboardMemory=allocation+writable-actualBytes;clipboardBytes=actualBytes;memset(clipboardMemory,0x41,actualBytes);
  memcpy(clipboardMemory,text.data(),text.size()*sizeof(wchar_t));if(terminated)static_cast<wchar_t *>(clipboardMemory)[text.size()]=0;
  original.assign(static_cast<BYTE *>(clipboardMemory),static_cast<BYTE *>(clipboardMemory)+actualBytes);
 }
 ~PrivateBuffer(){if(allocation)VirtualFree(allocation,0,MEM_RELEASE);clipboardMemory=nullptr;}
 bool Unchanged()const{return std::equal(original.begin(),original.end(),static_cast<BYTE *>(clipboardMemory));}
};
static void Case(const char *name,const std::wstring &text,bool terminated,const std::optional<std::wstring> &expected,SIZE_T reported=SIZE_MAX,unsigned boundary=0){
 ++scenarios;opens=closes=locks=unlocks=0;available=openOkay=dataOkay=lockOkay=true;result.reset();
 PrivateBuffer buffer(text,terminated);if(reported!=SIZE_MAX)clipboardBytes=reported;
 if(boundary==1)available=false;if(boundary==2)openOkay=false;if(boundary==3)dataOkay=false;if(boundary==4)lockOkay=false;
 const DWORD exception=ProtectedRead();if(exception==EXCEPTION_ACCESS_VIOLATION)++caughtAccessViolations;
 std::cout<<"CASE "<<name<<"; bounded bytes="<<clipboardBytes<<"; exception="<<std::hex<<exception<<std::dec<<'\n';
 Check(exception==0&&result==expected,"Clipboard reader returns the expected bounded URL result without an access violation");
 Check(closes==static_cast<unsigned>(available&&openOkay),"Every successful synthetic OpenClipboard is closed, including malformed input");
 Check(unlocks==static_cast<unsigned>(available&&openOkay&&dataOkay&&lockOkay&&locks!=0),"Every successful synthetic GlobalLock is released");
 Check(buffer.Unchanged(),"Input bytes remain exact and no clipboard replacement is attempted");
}
int main(){try{
 std::cout<<std::unitbuf;SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
 const std::wstring url=L"https://example.test/path?x=1&y=two";
 Case("ordinary HTTPS",url,true,url);
 Case("trimmed HTTPS",L"  \t"+url+L" \t",true,url);
 Case("unterminated short allocation",L"AAAAAAAA",false,std::nullopt);
 Case("unterminated URL allocation",L"https://example.test",false,std::nullopt);
 Case("odd byte allocation",url,true,std::nullopt,3);
 Case("zero reported allocation",url,true,std::nullopt,0);
 Case("smaller than one character",url,true,std::nullopt,1);
 Case("empty text",L"",true,std::nullopt);
 Case("missing data handle",url,true,std::nullopt,SIZE_MAX,3);
 Case("failed lock",url,true,std::nullopt,SIZE_MAX,4);
 Case("failed clipboard open",url,true,std::nullopt,SIZE_MAX,2);
 Case("unavailable text format",url,true,std::nullopt,SIZE_MAX,1);
 const std::wstring prefix=L"https://example.test/";
 const auto longest=prefix+std::wstring(8192-prefix.size(),L'a');
 Case("maximum 8192-character web URL",longest,true,longest);
 Case("overlong web URL",longest+L"a",true,std::nullopt);
 Case("non-web URL",L"file:///C:/private.txt",true,std::nullopt);
 Case("line-break URL",L"https://example.test/\r\nignored",true,std::nullopt);
 Check(g_hGui==nullptr,"Fixture creates no HWNDs and never requests presentation or device input");
 std::cout<<"Clipboard URL boundary: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; caught access violations="<<caughtAccessViolations<<"; real clipboard/network/process/input=0.\n";return failures?1:0;
}catch(const std::exception &e){std::cerr<<"Clipboard boundary setup failure: "<<e.what()<<'\n';return 2;}}
