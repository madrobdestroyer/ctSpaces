#pragma once
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// Native transaction recovery. Callers suppress their own change notifications
// and do not surround these functions with tomSuspend. FinalizeView runs only
// after the caller has released its outermost native Freeze.
namespace notes_undo {
struct View;struct Document;
struct Recovery {
 std::function<void()> repairLinks;
 std::function<bool(LONG)> isRegisteredCookie;
 std::function<void(const std::shared_ptr<const Document> &,const View &)> retainOriginal;
};
struct View {
 CHARRANGE selection{};POINT scroll{};CHARFORMAT2W typing{sizeof(typing)};
 LONG length=0;LRESULT limit=0,autoUrlFlags=0;BOOL modified=FALSE;bool inheritLink=false;
};
struct LinkRun {
 LONG first=0,last=0,cookie=0,effects=0;
 bool operator==(const LinkRun &)const=default;
};
struct Document {LONG length=0;std::string rtf;std::vector<LinkRun> links;};
#ifdef CTSPACES_NOTES_TEST_HOOKS
inline std::function<void(const char *,HWND,bool)> diagnosticSink;
#endif
inline void Diagnose(const char *stage,HWND edit,bool result){
#ifdef CTSPACES_NOTES_TEST_HOOKS
 try{if(diagnosticSink)diagnosticSink(stage,edit,result);}catch(...){}
#else
 (void)stage;(void)edit;(void)result;
#endif
}
inline constexpr size_t kMaximumSnapshotRtf=256u*1024u*1024u;
inline constexpr size_t kMaximumSnapshotRuns=1024u*1024u;
inline constexpr LONG kLinkEffects=CFM_LINK|CFM_LINKPROTECTED;
struct NativeDocument {
 ITextDocument *value=nullptr;
 explicit NativeDocument(HWND edit){IUnknown *ole=nullptr;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&value));ole->Release();}}
 ~NativeDocument(){if(value)value->Release();}
 NativeDocument(const NativeDocument &)=delete;NativeDocument &operator=(const NativeDocument &)=delete;
};
struct NativeRange {ITextRange *value=nullptr;~NativeRange(){if(value)value->Release();}};
inline LONG Length(HWND edit){GETTEXTLENGTHEX measure{GTL_PRECISE|GTL_NUMCHARS,1200};return static_cast<LONG>(SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&measure),0));}
inline bool RangeFont(ITextDocument *document,LONG first,LONG last,LONG &effects,LONG &cookie){
 ITextRange *range=nullptr;ITextFont *font=nullptr;ITextFont2 *extended=nullptr;LONG mask=0;bool ok=false;
 if(document&&SUCCEEDED(document->Range(first,last,&range))&&SUCCEEDED(range->GetFont(&font))&&SUCCEEDED(font->QueryInterface(__uuidof(ITextFont2),reinterpret_cast<void **>(&extended))))ok=SUCCEEDED(extended->GetEffects(&effects,&mask))&&SUCCEEDED(extended->GetCookie(&cookie));
 if(extended)extended->Release();if(font)font->Release();if(range)range->Release();return ok;
}
inline bool WriteRange(ITextDocument *document,const LinkRun &target){
 ITextRange *range=nullptr;ITextFont *font=nullptr;ITextFont2 *extended=nullptr;LONG effects=0,mask=0,cookie=0;bool ok=false;
 if(document&&SUCCEEDED(document->Range(target.first,target.last,&range))&&SUCCEEDED(range->GetFont(&font))&&SUCCEEDED(font->QueryInterface(__uuidof(ITextFont2),reinterpret_cast<void **>(&extended)))){
  ok=SUCCEEDED(extended->GetEffects(&effects,&mask))&&SUCCEEDED(extended->GetCookie(&cookie));
  if(ok&&cookie!=target.cookie)ok=SUCCEEDED(extended->SetCookie(target.cookie));
  if(ok&&(effects&kLinkEffects)!=target.effects)ok=SUCCEEDED(extended->SetEffects(target.effects,kLinkEffects));
 }
 if(extended)extended->Release();if(font)font->Release();if(range)range->Release();return ok;
}
inline bool CaptureView(HWND edit,View &view,const std::function<bool(LONG)> &registered={}){
 try{
  View candidate;candidate.length=Length(edit);if(candidate.length<0)return false;
  SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&candidate.selection));SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&candidate.scroll));SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&candidate.typing));
  candidate.limit=SendMessageW(edit,EM_GETLIMITTEXT,0,0);candidate.autoUrlFlags=SendMessageW(edit,EM_GETAUTOURLDETECT,0,0);candidate.modified=static_cast<BOOL>(SendMessageW(edit,EM_GETMODIFY,0,0));
  if(registered&&candidate.selection.cpMin==candidate.selection.cpMax&&candidate.selection.cpMin>0&&candidate.selection.cpMin<candidate.length){
   NativeDocument document(edit);LONG leftEffects=0,rightEffects=0,leftCookie=0,rightCookie=0;
   if(!document.value||!RangeFont(document.value,candidate.selection.cpMin-1,candidate.selection.cpMin,leftEffects,leftCookie)||!RangeFont(document.value,candidate.selection.cpMin,candidate.selection.cpMin+1,rightEffects,rightCookie))return false;
   candidate.inheritLink=leftCookie>0&&leftCookie==rightCookie&&(leftEffects&CFE_LINK)&&(rightEffects&CFE_LINK)&&registered(leftCookie);
  }
  view=candidate;return true;
 }catch(...){return false;}
}
struct RtfStream {std::string bytes;bool failed=false;};
inline DWORD CALLBACK ReadRtf(DWORD_PTR context,LPBYTE data,LONG requested,LONG *actual){
 auto &stream=*reinterpret_cast<RtfStream *>(context);*actual=0;
 if(requested<0||stream.bytes.size()>kMaximumSnapshotRtf||static_cast<size_t>(requested)>kMaximumSnapshotRtf-stream.bytes.size()){stream.failed=true;return ERROR_FILE_TOO_LARGE;}
 try{stream.bytes.append(reinterpret_cast<const char *>(data),static_cast<size_t>(requested));*actual=requested;return 0;}catch(...){stream.failed=true;return ERROR_NOT_ENOUGH_MEMORY;}
}
inline bool CaptureDocument(HWND edit,Document &snapshot){
 try{
  Document candidate;candidate.length=Length(edit);if(candidate.length<0)return false;
  RtfStream buffer;EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&buffer),0,ReadRtf};SendMessageW(edit,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&stream));
  if(stream.dwError||buffer.failed||buffer.bytes.empty())return false;while(!buffer.bytes.empty()&&buffer.bytes.back()=='\0')buffer.bytes.pop_back();if(buffer.bytes.empty())return false;candidate.rtf=std::move(buffer.bytes);
  NativeDocument document(edit);NativeRange holder;auto &range=holder.value;if(!document.value||FAILED(document.value->Range(0,0,&range)))return false;
  LONG story=0;const bool measured=SUCCEEDED(range->GetStoryLength(&story));if(!measured||story<1||story-1!=candidate.length)return false;
  LONG first=0;size_t visited=0;bool ok=true;
  while(first<candidate.length){
   LONG moved=0,last=first,effects=0,cookie=0;
   if(++visited>kMaximumSnapshotRuns||FAILED(range->SetRange(first,first))||FAILED(range->MoveEnd(tomCharFormat,1,&moved))||FAILED(range->GetEnd(&last))||last<=first){ok=false;break;}
   last=(std::min)(last,candidate.length);if(!RangeFont(document.value,first,last,effects,cookie)){ok=false;break;}effects&=kLinkEffects;
   if(!candidate.links.empty()&&candidate.links.back().last==first&&candidate.links.back().cookie==cookie&&candidate.links.back().effects==effects)candidate.links.back().last=last;
   else{if(candidate.links.size()>=kMaximumSnapshotRuns){ok=false;break;}candidate.links.push_back({first,last,cookie,effects});}
   first=last;
  }
  if(!ok)return false;snapshot=std::move(candidate);return true;
 }catch(...){return false;}
}
inline bool RestoreDocumentLinks(HWND edit,const Document &snapshot){
 if(Length(edit)!=snapshot.length)return false;NativeDocument document(edit);if(!document.value||FAILED(document.value->Undo(tomSuspend,nullptr)))return false;
 bool restored=true;for(const auto &range:snapshot.links)if(!WriteRange(document.value,range)){restored=false;break;}
 const bool resumed=SUCCEEDED(document.value->Undo(tomResume,nullptr));return restored&&resumed;
}
inline bool VerifyOriginal(HWND edit,const Document &snapshot){Document actual;return CaptureDocument(edit,actual)&&actual.length==snapshot.length&&actual.rtf==snapshot.rtf&&actual.links==snapshot.links;}
inline bool Invoke(const std::function<bool()> &callback){try{return callback&&callback();}catch(...){return false;}}
inline bool Selected(HWND edit,LONG first,LONG last){CHARRANGE actual{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&actual));return actual.cpMin==first&&actual.cpMax==last;}
inline bool MarkerAt(HWND edit,LONG position){wchar_t text[2]{};TEXTRANGEW range{{position,position+1},text};return SendMessageW(edit,EM_GETTEXTRANGE,0,reinterpret_cast<LPARAM>(&range))==1&&text[0]==L'\u2060';}
inline bool DiscardAbortedRedo(HWND edit,const View &view,const Document &original,const std::function<bool()> &repairAppAndCheckOwnership){
 const auto repairNative=[&]{return Invoke(repairAppAndCheckOwnership)&&RestoreDocumentLinks(edit,original);};
 const auto repairAndVerifyOriginal=[&]{return repairNative()&&VerifyOriginal(edit,original);};
 const bool entry=repairAndVerifyOriginal();Diagnose("discard.entry-original",edit,entry);if(!entry||Length(edit)!=view.length)return false;
 if(!SendMessageW(edit,EM_CANREDO,0,0))return true;
 if(view.length==(std::numeric_limits<LONG>::max)())return false;
 NativeDocument document(edit);if(!document.value)return false;LONG freezes=0;if(FAILED(document.value->Freeze(&freezes)))return false;
 const auto events=SendMessageW(edit,EM_GETEVENTMASK,0,0);SendMessageW(edit,EM_SETEVENTMASK,0,0);
 bool neutral=false,inserted=false,removed=false,undone=false,verified=false;const auto finish=[&]{SendMessageW(edit,EM_EXLIMITTEXT,0,view.limit);SendMessageW(edit,EM_AUTOURLDETECT,view.autoUrlFlags,0);const bool flags=SendMessageW(edit,EM_GETAUTOURLDETECT,0,0)==view.autoUrlFlags;Diagnose("discard.auto-restored",edit,flags);const bool restoredOriginal=repairAndVerifyOriginal();Diagnose("discard.final-original",edit,restoredOriginal);const bool unfrozen=SUCCEEDED(document.value->Unfreeze(&freezes));Diagnose("discard.unfrozen",edit,unfrozen);SendMessageW(edit,EM_SETEVENTMASK,0,events);return flags&&restoredOriginal&&unfrozen;};
 SendMessageW(edit,EM_AUTOURLDETECT,0,0);const bool disabled=SendMessageW(edit,EM_GETAUTOURLDETECT,0,0)==0;Diagnose("discard.auto-disabled",edit,disabled);
 // RichEdit's raw SF_RTF serialization changes while auto-detection is off:
 // synthetic link fields can disappear even though native binding is intact.
 // Keep a second raw preimage for this temporary mode; never normalize either
 // snapshot, and still require the exact original raw stream after flags return.
 Document temporaryOriginal;
 const bool temporaryReady=disabled&&repairNative()&&CaptureDocument(edit,temporaryOriginal)&&temporaryOriginal.length==original.length&&temporaryOriginal.links==original.links;
 Diagnose("discard.mode-original",edit,temporaryReady);
 if(temporaryReady){
  CHARRANGE end{view.length,view.length};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));
  if(Selected(edit,end.cpMin,end.cpMax)&&SUCCEEDED(document.value->Undo(tomSuspend,nullptr))){CHARFORMAT2W plain{sizeof(plain)};plain.dwMask=CFM_LINK|CFM_LINKPROTECTED|CFM_UNDERLINE;SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&plain));neutral=WriteRange(document.value,{view.length,view.length,0,0});neutral=SUCCEEDED(document.value->Undo(tomResume,nullptr))&&neutral;}Diagnose("discard.neutral",edit,neutral);
  if(view.limit>0&&view.limit<=view.length)SendMessageW(edit,EM_EXLIMITTEXT,0,static_cast<LPARAM>(view.length)+1);
  if(neutral&&SUCCEEDED(document.value->BeginEditCollection())){
   SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\u2060"));inserted=Length(edit)==view.length+1&&MarkerAt(edit,view.length);Diagnose("discard.marker-inserted",edit,inserted);
   if(inserted){CHARRANGE marker{view.length,view.length+1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&marker));if(Selected(edit,marker.cpMin,marker.cpMax)){SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L""));removed=Length(edit)==view.length;}}Diagnose("discard.marker-deleted",edit,removed);
   const bool ended=SUCCEEDED(document.value->EndEditCollection());Diagnose("discard.marker-ended",edit,ended);if(inserted&&ended)undone=SendMessageW(edit,EM_UNDO,0,0)!=FALSE;Diagnose("discard.marker-undone",edit,undone);
  }
  if(inserted&&removed&&undone)verified=repairNative()&&VerifyOriginal(edit,temporaryOriginal);Diagnose("discard.marker-original",edit,verified);
 }
 const bool finished=finish();return disabled&&neutral&&inserted&&removed&&undone&&verified&&finished&&!SendMessageW(edit,EM_CANREDO,0,0)&&SendMessageW(edit,EM_GETLIMITTEXT,0,0)==view.limit;
}
inline bool FinalizeView(HWND edit,const View &view){
 NativeDocument document(edit);if(!document.value)return false;const auto events=SendMessageW(edit,EM_GETEVENTMASK,0,0);SendMessageW(edit,EM_SETEVENTMASK,0,0);
 SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&view.selection));SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&view.scroll));bool restored=Selected(edit,view.selection.cpMin,view.selection.cpMax);
 if(restored&&view.selection.cpMin==view.selection.cpMax){restored=SUCCEEDED(document.value->Undo(tomSuspend,nullptr));if(restored){auto format=view.typing;if(view.inheritLink)format.dwMask&=~(CFM_LINK|CFM_LINKPROTECTED|CFM_COOKIE);restored=SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format))!=FALSE;restored=SUCCEEDED(document.value->Undo(tomResume,nullptr))&&restored;}}
 SendMessageW(edit,EM_SETMODIFY,view.modified,0);SendMessageW(edit,EM_SETEVENTMASK,0,events);return restored;
}
}
