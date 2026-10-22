#pragma once
#include <windows.h>
#include <commdlg.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>
#include "NotesUndo.h"

namespace notes_find {
struct Options {bool matchCase=false,wholeWord=false,wrap=true;};
struct Match {bool found=false,wrapped=false;CHARRANGE range{};};
struct Summary {size_t count=0,current=0;bool limited=false;};
enum class ReplaceStatus {Changed,NoMatch,NoChange,ReadOnly,TooLarge,Unavailable,Failed};
struct Replacement {ReplaceStatus status=ReplaceStatus::NoMatch;size_t count=0;bool wrapped=false,rolledBack=false;std::shared_ptr<const notes_undo::Document> recoveryDocument;notes_undo::View recoveryView;};
inline LONG Length(HWND edit){GETTEXTLENGTHEX measure{GTL_PRECISE|GTL_NUMCHARS,1200};return static_cast<LONG>(SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&measure),0));}
inline DWORD Flags(const Options &options,bool forward=true){return (forward?FR_DOWN:0)|(options.matchCase?FR_MATCHCASE:0)|(options.wholeWord?FR_WHOLEWORD:0);}
inline bool Search(HWND edit,const std::wstring &query,const Options &options,LONG first,LONG last,bool forward,CHARRANGE &range){
 if(query.empty()||first<0||last<0||(forward?first>last:first<last))return false;
 FINDTEXTEXW native{{first,last},query.c_str(),{}};
 if(SendMessageW(edit,EM_FINDTEXTEXW,Flags(options,forward),reinterpret_cast<LPARAM>(&native))<0)return false;
 if(native.chrgText.cpMin<0||native.chrgText.cpMax<=native.chrgText.cpMin)return false;
 range=native.chrgText;return true;
}
inline Match Navigate(HWND edit,const std::wstring &query,const Options &options,CHARRANGE selected,bool previous){
 Match result;const LONG length=Length(edit);const LONG first=(std::clamp)(previous?selected.cpMin:selected.cpMax,0L,length);
 result.found=Search(edit,query,options,first,previous?0:length,!previous,result.range);
 if(!result.found&&options.wrap&&first!=(previous?length:0)){
  result.found=Search(edit,query,options,previous?length:0,first,!previous,result.range);result.wrapped=result.found;
 }
 return result;
}
inline Summary Count(HWND edit,const std::wstring &query,const Options &options,CHARRANGE selected,size_t maximum=10000){
 Summary result;if(query.empty()||!maximum)return result;const LONG length=Length(edit);LONG start=0;CHARRANGE next{};
 while(result.count<maximum&&Search(edit,query,options,start,length,true,next)){
  ++result.count;if(next.cpMin==selected.cpMin&&next.cpMax==selected.cpMax)result.current=result.count;
  start=next.cpMax;
 }
 if(result.count==maximum)result.limited=Search(edit,query,options,start,length,true,next);
 return result;
}
inline std::wstring RangeText(HWND edit,CHARRANGE range){
 if(range.cpMin<0||range.cpMax<range.cpMin)return {};
 std::wstring text(static_cast<size_t>(range.cpMax-range.cpMin)+1,L'\0');TEXTRANGEW native{range,text.data()};
 const auto count=SendMessageW(edit,EM_GETTEXTRANGE,0,reinterpret_cast<LPARAM>(&native));
 if(count<0||static_cast<size_t>(count)>=text.size())return {};text.resize(static_cast<size_t>(count));return text;
}
inline std::wstring NativeLines(std::wstring text){
 std::wstring native;native.reserve(text.size());for(size_t i=0;i<text.size();++i){if(text[i]==L'\r'){native+=L'\r';if(i+1<text.size()&&text[i+1]==L'\n')++i;}else native+=text[i]==L'\n'?L'\r':text[i];}return native;
}
// Resolve every original match before mutating. Applying backwards means that
// replacement text containing the query is never searched again, and positions
// remain valid even when replacement lengths differ. RichEdit owns native Undo.
inline Replacement Replace(HWND edit,const std::wstring &query,const std::wstring &input,const Options &options,bool all,const std::function<void(const CHARRANGE &)> &beforeReplace={},const notes_undo::Recovery &recovery={}){
 Replacement result;if(query.empty())return result;
 if(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY){result.status=ReplaceStatus::ReadOnly;return result;}
 std::wstring replacement;try{replacement=NativeLines(input);}catch(const std::bad_alloc &){result.status=ReplaceStatus::TooLarge;return result;}
 const LONG originalLength=Length(edit);CHARRANGE selected{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
 std::vector<CHARRANGE> matches;
 try{
  CHARRANGE next{};
  if(all){LONG start=0;while(Search(edit,query,options,start,originalLength,true,next)){matches.push_back(next);start=next.cpMax;}}
  else if(Search(edit,query,options,selected.cpMin,selected.cpMax,true,next)&&next.cpMin==selected.cpMin&&next.cpMax==selected.cpMax)matches.push_back(next);
  else{const auto found=Navigate(edit,query,options,selected,false);result.wrapped=found.wrapped;if(found.found)matches.push_back(found.range);}
  if(matches.empty())return result;
  matches.erase(std::remove_if(matches.begin(),matches.end(),[&](const auto &range){return RangeText(edit,range)==replacement;}),matches.end());
 }catch(const std::bad_alloc &){result.status=ReplaceStatus::TooLarge;return result;}
 if(matches.empty()){result.status=ReplaceStatus::NoChange;return result;}
 int64_t finalLength=originalLength;for(const auto &range:matches)finalLength+=static_cast<int64_t>(replacement.size())-(range.cpMax-range.cpMin);
 const LRESULT nativeLimit=SendMessageW(edit,EM_GETLIMITTEXT,0,0);
 if(finalLength<0||finalLength>(std::numeric_limits<LONG>::max)()||(nativeLimit>0&&finalLength>nativeLimit)){result.status=ReplaceStatus::TooLarge;return result;}
 notes_undo::View originalView;std::shared_ptr<notes_undo::Document> originalDocument;
 try{originalDocument=std::make_shared<notes_undo::Document>();}catch(...){result.status=ReplaceStatus::Unavailable;return result;}
 if(!notes_undo::CaptureView(edit,originalView,recovery.isRegisteredCookie)||!notes_undo::CaptureDocument(edit,*originalDocument)){result.status=ReplaceStatus::Unavailable;return result;}
 bool recoveryIdentity=true;
 const auto repairApp=[&]{try{if(recovery.repairLinks)recovery.repairLinks();return true;}catch(...){recoveryIdentity=false;return false;}};
 const auto repairAndVerify=[&]{const bool app=repairApp();notes_undo::Diagnose("model.app-repair",edit,app);if(!app)return false;const bool links=notes_undo::RestoreDocumentLinks(edit,*originalDocument);notes_undo::Diagnose("model.native-links",edit,links);const bool verified=links&&notes_undo::VerifyOriginal(edit,*originalDocument);notes_undo::Diagnose("model.original",edit,verified);return verified;};
 const auto retainUnsafe=[&]{result.recoveryDocument=originalDocument;result.recoveryView=originalView;try{if(recovery.retainOriginal)recovery.retainOriginal(result.recoveryDocument,originalView);}catch(...){}};
 IUnknown *ole=nullptr;ITextDocument *document=nullptr;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
 if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document));ole->Release();}
 if(!document){result.status=ReplaceStatus::Unavailable;return result;}
 SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
 LONG freezeCount=0;const bool frozen=SUCCEEDED(document->Freeze(&freezeCount));
 if(!frozen){document->Release();result.status=ReplaceStatus::Unavailable;return result;}
 if(FAILED(document->BeginEditCollection())){const bool unfrozen=SUCCEEDED(document->Unfreeze(&freezeCount));document->Release();const bool finalized=unfrozen&&repairApp()&&notes_undo::FinalizeView(edit,originalView);result.status=finalized?ReplaceStatus::Unavailable:ReplaceStatus::Failed;if(!finalized)retainUnsafe();return result;}
 bool failed=false,mutated=false;
 try{
  for(auto item=matches.rbegin();item!=matches.rend();++item){
   if(beforeReplace)beforeReplace(*item);
   const LONG before=Length(edit);const auto original=RangeText(edit,*item);SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&*item));
   CHARRANGE actual{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&actual));if(actual.cpMin!=item->cpMin||actual.cpMax!=item->cpMax){failed=true;break;}
   SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(replacement.c_str()));
   const LONG after=Length(edit);const CHARRANGE inserted{item->cpMin,item->cpMin+static_cast<LONG>(replacement.size())};
   if(after!=before-static_cast<LONG>(item->cpMax-item->cpMin)+static_cast<LONG>(replacement.size())||RangeText(edit,inserted)!=replacement){failed=true;mutated|=after!=before||RangeText(edit,*item)!=original;break;}
   mutated=true;++result.count;
  }
 }catch(...){failed=true;}
 const bool ended=SUCCEEDED(document->EndEditCollection());SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
 if(failed||!ended){
  result.status=ReplaceStatus::Failed;bool restored=false;notes_undo::Document current;
  const bool currentOwner=repairApp(),readable=currentOwner&&notes_undo::CaptureDocument(edit,current),unchanged=readable&&current.length==originalDocument->length&&current.rtf==originalDocument->rtf&&current.links==originalDocument->links;
  notes_undo::Diagnose("model.failure-readable",edit,readable);notes_undo::Diagnose("model.failure-unchanged",edit,unchanged);
  // An empty/rejected group must never consume an older user's Undo action.
  // A throwing callback with an unowned mutation is retained for app recovery.
  if(unchanged)restored=repairAndVerify();
  else if(readable&&mutated)restored=SendMessageW(edit,EM_UNDO,0,0)!=FALSE&&repairAndVerify()&&notes_undo::DiscardAbortedRedo(edit,originalView,*originalDocument,repairApp);
  const bool unfrozen=SUCCEEDED(document->Unfreeze(&freezeCount));document->Release();
  const bool finalized=restored&&unfrozen&&recoveryIdentity&&notes_undo::FinalizeView(edit,originalView);result.rolledBack=restored&&unfrozen&&finalized&&notes_undo::VerifyOriginal(edit,*originalDocument);
  if(result.rolledBack)result.count=0;else{if(!unchanged&&!result.count)result.count=1;retainUnsafe();}return result;
 }
 const bool unfrozen=SUCCEEDED(document->Unfreeze(&freezeCount));document->Release();if(!unfrozen){result.status=ReplaceStatus::Failed;retainUnsafe();return result;}
 result.status=ReplaceStatus::Changed;return result;
}
}
