#pragma once
// Read-only Windows spell checking. No autocorrect registration or dictionary
// writes are used. UTF-16 offsets remain native RichEdit character positions.
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <spellcheck.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace notes_spelling {
using Microsoft::WRL::ComPtr;
inline constexpr size_t kMaxScope=256*1024,kChunk=2048,kMaxSuggestion=128;
struct Range {LONG first=0,last=0;};
struct Error {size_t first=0,length=0;std::wstring replacement;};
inline WORD CharacterType(wchar_t value){WORD type=0;GetStringTypeW(CT_CTYPE1,&value,1,&type);return type;}
inline std::wstring Fold(std::wstring_view word){
  if(word.empty())return {};
  std::wstring lower(word.size(),L'\0');
  if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,word.data(),static_cast<int>(word.size()),lower.data(),static_cast<int>(lower.size()),nullptr,nullptr,0))return lower;
  return std::wstring(word);
}
inline bool ValidReplacement(std::wstring_view word){
  if(word.empty()||word.size()>kMaxSuggestion||word.front()==L' '||word.back()==L' ')return false;
  bool letter=false;
  for(wchar_t ch:word){if(CharacterType(ch)&C1_ALPHA){letter=true;continue;}if(ch!=L' '&&ch!=L'\''&&ch!=L'\x2019'&&ch!=L'-')return false;}
  return letter;
}
inline bool ProseWord(std::wstring_view word){
  if(word.size()<2||word.size()>64)return false;
  bool lower=false,upper=false;
  for(size_t i=0;i<word.size();++i){
    const wchar_t ch=word[i];
    if(ch==L'\''||ch==L'\x2019'){if(!i||i+1==word.size())return false;continue;}
    const auto type=CharacterType(ch);if(!(type&C1_ALPHA))return false;
    if(type&C1_LOWER)lower=true;
    if(type&C1_UPPER){upper=true;if(i&&(CharacterType(word[i-1])&C1_LOWER))return false;}
  }
  return !upper||lower;
}
inline bool CommandLine(std::wstring_view line){
  const auto begin=line.find_first_not_of(L" \t");if(begin==line.npos)return false;line.remove_prefix(begin);
  if(line.starts_with(L"```")||line.starts_with(L"$ ")||line.starts_with(L"> ")||line.starts_with(L"PS>")||line.starts_with(L"PS "))return true;
  const auto end=line.find_first_of(L" \t");const auto first=Fold(line.substr(0,end));
  if(first.find_first_of(L"\\/=$`")!=first.npos)return true;
  if(const auto dash=first.find(L'-');dash!=first.npos){
    const auto verb=first.substr(0,dash);
    static constexpr std::wstring_view verbs[]={L"get",L"set",L"new",L"add",L"remove",L"start",L"stop",L"write",L"read",L"test",L"invoke",L"import",L"export",L"update",L"find",L"select",L"where",L"foreach",L"connect",L"disconnect",L"enable",L"disable",L"restart",L"resolve",L"convert",L"copy",L"move",L"clear",L"out",L"measure",L"sort",L"format",L"wait",L"join",L"split"};
    if(std::find(std::begin(verbs),std::end(verbs),verb)!=std::end(verbs))return true;
  }
  static constexpr std::wstring_view commands[]={L"powershell",L"pwsh",L"cmd",L"bash",L"sh",L"python",L"python3",L"node",L"npm",L"git",L"net",L"netsh",L"ipconfig",L"ping",L"tracert",L"nslookup",L"curl",L"wget",L"ssh",L"scp",L"robocopy",L"xcopy",L"mkdir",L"cd",L"dir",L"ls",L"cat",L"grep",L"sed",L"awk",L"sudo",L"docker",L"kubectl",L"winget",L"choco"};
  return std::find(std::begin(commands),std::end(commands),first)!=std::end(commands);
}
inline std::wstring MaskTechnicalText(std::wstring_view text,const std::vector<Range>&excluded={}){
  std::wstring masked(text.size(),L' ');bool fence=false;
  for(size_t line=0;line<text.size();){
    auto last=text.find_first_of(L"\r\n\v",line);if(last==text.npos)last=text.size();
    const auto content=text.substr(line,last-line);const auto start=content.find_first_not_of(L" \t");
    const bool delimiter=start!=content.npos&&content.substr(start).starts_with(L"```");
    if(delimiter)fence=!fence;
    if(!fence&&!delimiter&&!CommandLine(content)){
      for(size_t at=line;at<last;){
        if(iswspace(text[at])){++at;continue;}size_t end=at;while(end<last&&!iswspace(text[end]))++end;
        size_t first=at,limit=end;const std::wstring_view punctuation=L".,!?:;()\"\x201c\x201d";
        while(first<limit&&punctuation.find(text[first])!=punctuation.npos)++first;
        while(limit>first&&punctuation.find(text[limit-1])!=punctuation.npos)--limit;
        if(ProseWord(text.substr(first,limit-first)))for(size_t i=first;i<limit;++i)masked[i]=text[i];
        at=end;
      }
    }
    if(last<text.size())masked[last]=text[last];line=last+1;
  }
  for(const auto &range:excluded){
    const size_t first=static_cast<size_t>((std::max)(0L,range.first)),last=(std::min)(text.size(),static_cast<size_t>((std::max)(0L,range.last)));
    if(first<last)std::fill(masked.begin()+first,masked.begin()+last,L' ');
  }
  return masked;
}
inline HRESULT TextDocument(HWND edit,ComPtr<ITextDocument>&document){
  ComPtr<IRichEditOle> ole;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
  return ole?ole.As(&document):E_NOINTERFACE;
}
inline HRESULT NativeText(HWND edit,Range scope,std::wstring&text){
  text.clear();if(scope.first<0||scope.last<scope.first||static_cast<size_t>(scope.last-scope.first)>kMaxScope)return E_INVALIDARG;
  ComPtr<ITextDocument> document;HRESULT result=TextDocument(edit,document);if(FAILED(result))return result;
  ComPtr<ITextRange> range;result=document->Range(scope.first,scope.last,&range);if(FAILED(result))return result;
  BSTR value=nullptr;result=range->GetText(&value);if(SUCCEEDED(result)&&value)text.assign(value,SysStringLen(value));SysFreeString(value);
  if(SUCCEEDED(result)&&text.size()!=static_cast<size_t>(scope.last-scope.first))return E_UNEXPECTED;return result;
}
inline HRESULT ReadScope(HWND edit,Range scope,std::wstring&text,std::wstring&masked){
  masked.clear();HRESULT result=NativeText(edit,scope,text);if(FAILED(result))return result;
  ComPtr<ITextDocument> document;result=TextDocument(edit,document);if(FAILED(result))return result;
  std::vector<Range> excluded;
  unsigned runs=0;
  for(LONG at=scope.first;at<scope.last;){
    if(++runs>4096)return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    ComPtr<ITextRange> range;result=document->Range(at,at+1,&range);if(FAILED(result))return result;
    LONG moved=0,last=at+1;range->MoveEnd(tomCharFormat,1,&moved);range->GetEnd(&last);last=(std::min)(scope.last,(std::max)(at+1,last));
    ComPtr<ITextRange> paragraphRange;result=document->Range(at,at+1,&paragraphRange);if(FAILED(result))return result;
    LONG paragraphLast=at+1;paragraphRange->MoveEnd(tomParaFormat,1,&moved);paragraphRange->GetEnd(&paragraphLast);last=(std::min)(last,(std::max)(at+1,paragraphLast));
    range->SetRange(at,last);
    ComPtr<ITextFont> font;ComPtr<ITextFont2> font2;ComPtr<ITextPara> paragraph;ComPtr<ITextPara2> paragraph2;
    if(FAILED(range->GetFont(&font))||FAILED(font.As(&font2))||FAILED(range->GetPara(&paragraph))||FAILED(paragraph.As(&paragraph2)))return E_NOINTERFACE;
    LONG effects=0,mask=0,paraEffects=0,paraMask=0;
    if(FAILED(font2->GetEffects(&effects,&mask))||FAILED(paragraph2->GetEffects(&paraEffects,&paraMask)))return E_FAIL;
    bool skip=(effects&(CFE_LINK|CFE_LINKPROTECTED|CFE_PROTECTED|CFE_HIDDEN))!=0||
      (paraEffects&(tomParaEffectTable|tomParaEffectTableRowDelimiter))!=0;
    BSTR face=nullptr;if(FAILED(font->GetName(&face)))return E_FAIL;
    if(face){const auto name=Fold(std::wstring_view(face,SysStringLen(face)));skip=skip||name.find(L"consolas")!=name.npos||name.find(L"courier")!=name.npos||name.find(L"cascadia")!=name.npos||name.find(L"lucida console")!=name.npos;}SysFreeString(face);
    if(skip)excluded.push_back({at-scope.first,last-scope.first});at=last;
  }
  // Keep boundary context when the user selects only part of a URL, command or
  // identifier: a substring of a technical token is never offered as prose.
  ComPtr<ITextRange> context;result=document->Range(scope.first,scope.last,&context);if(FAILED(result))return result;
  LONG moved=0,first=scope.first,last=scope.last,story=0;
  if(FAILED(context->StartOf(tomParagraph,tomExtend,&moved))||FAILED(context->EndOf(tomParagraph,tomExtend,&moved))||FAILED(context->GetStart(&first))||FAILED(context->GetEnd(&last))||FAILED(context->GetStoryLength(&story)))return E_FAIL;
  last=(std::min)(last,(std::max)(0L,story-1));
  std::wstring full;result=NativeText(edit,{first,last},full);if(FAILED(result))return result;
  const auto fullMasked=MaskTechnicalText(full);const auto offset=static_cast<size_t>(scope.first-first);
  if(offset>fullMasked.size()||text.size()>fullMasked.size()-offset)return E_UNEXPECTED;
  masked=fullMasked.substr(offset,text.size());
  for(const auto &range:excluded)if(range.first>=0&&range.last>=range.first&&static_cast<size_t>(range.last)<=masked.size())std::fill(masked.begin()+range.first,masked.begin()+range.last,L' ');
  return S_OK;
}
inline HRESULT ReplaceWord(HWND edit,Range scope,const std::wstring &snapshot,const Error &error,
    const std::wstring &replacement,bool &changed,std::wstring &updated,std::wstring &masked,bool collect=true){
  changed=false;updated.clear();masked.clear();
  if(!ValidReplacement(replacement)||error.first>snapshot.size()||!error.length||error.length>snapshot.size()-error.first)return E_INVALIDARG;
  std::wstring actual;HRESULT result=NativeText(edit,scope,actual);if(FAILED(result)||actual!=snapshot)return E_UNEXPECTED;
  const auto word=snapshot.substr(error.first,error.length);if(!ProseWord(word)||word==replacement)return S_FALSE;
  const CHARRANGE affected{scope.first+static_cast<LONG>(error.first),scope.first+static_cast<LONG>(error.first+error.length)};
  std::wstring originalMask;if(FAILED(ReadScope(edit,{affected.cpMin,affected.cpMax},actual,originalMask))||actual!=word||originalMask!=word)return E_ACCESSDENIED;
  auto expected=snapshot;expected.replace(error.first,error.length,replacement);if(expected.size()>kMaxScope)return E_INVALIDARG;
  ComPtr<ITextDocument> document;
  if(collect){result=TextDocument(edit,document);if(FAILED(result))return result;}
  CHARRANGE before{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
  if(collect){SendMessageW(edit,EM_STOPGROUPTYPING,0,0);result=document->BeginEditCollection();if(FAILED(result))return result;}
  SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&affected));CHARRANGE selected{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
  if(selected.cpMin!=affected.cpMin||selected.cpMax!=affected.cpMax){if(collect)document->EndEditCollection();SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return E_FAIL;}
  SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(replacement.c_str()));if(collect)document->EndEditCollection();
  const Range newScope{scope.first,scope.first+static_cast<LONG>(expected.size())};result=ReadScope(edit,newScope,updated,masked);
  if(SUCCEEDED(result)&&updated==expected){changed=true;return S_OK;}
  std::wstring original;changed=FAILED(NativeText(edit,scope,original))||original!=snapshot;
  if(!changed)SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return E_FAIL;
}
class WindowsDictionary {
  ComPtr<ISpellCheckerFactory> factory_;ComPtr<ISpellChecker> checker_;
public:
  HRESULT Initialize(){return CoCreateInstance(__uuidof(SpellCheckerFactory),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()));}
  HRESULT Languages(std::vector<std::wstring>&languages){
    languages.clear();if(!factory_)return E_UNEXPECTED;ComPtr<IEnumString> values;HRESULT result=factory_->get_SupportedLanguages(&values);if(FAILED(result))return result;
    for(unsigned count=0;count<128;++count){LPOLESTR value=nullptr;ULONG fetched=0;result=values->Next(1,&value,&fetched);if(result==S_FALSE){CoTaskMemFree(value);return S_OK;}if(FAILED(result)||fetched!=1||!value){CoTaskMemFree(value);return E_FAIL;}if(wcslen(value)<LOCALE_NAME_MAX_LENGTH)languages.emplace_back(value);CoTaskMemFree(value);}return S_OK;
  }
  HRESULT Select(const std::wstring&language){checker_.Reset();return factory_?factory_->CreateSpellChecker(language.c_str(),&checker_):E_UNEXPECTED;}
  HRESULT Check(std::wstring_view text,std::vector<Error>&errors){
    errors.clear();if(!checker_)return E_UNEXPECTED;if(text.empty())return S_OK;if(text.size()>kChunk+128)return E_INVALIDARG;
    ComPtr<IEnumSpellingError> values;const std::wstring terminated(text);HRESULT result=checker_->Check(terminated.c_str(),&values);if(FAILED(result)||!values)return FAILED(result)?result:E_FAIL;
    for(unsigned count=0;count<512;++count){
      ComPtr<ISpellingError> error;result=values->Next(&error);if(result==S_FALSE)return S_OK;if(FAILED(result)||!error)return E_FAIL;
      ULONG first=0,length=0;CORRECTIVE_ACTION action=CORRECTIVE_ACTION_NONE;
      if(FAILED(error->get_StartIndex(&first))||FAILED(error->get_Length(&length))||FAILED(error->get_CorrectiveAction(&action)))return E_FAIL;
      if(action==CORRECTIVE_ACTION_NONE)continue;
      if(!length||length>64||first>text.size()||length>text.size()-first)return E_UNEXPECTED;
      Error item{first,length,{}};
      if(action==CORRECTIVE_ACTION_REPLACE){LPWSTR replacement=nullptr;result=error->get_Replacement(&replacement);if(SUCCEEDED(result)&&replacement&&ValidReplacement(replacement))item.replacement=replacement;CoTaskMemFree(replacement);}
      errors.push_back(std::move(item));
    }
    return E_UNEXPECTED;
  }
  HRESULT Suggestions(const std::wstring&word,std::vector<std::wstring>&suggestions){
    suggestions.clear();if(!checker_||!ProseWord(word))return E_INVALIDARG;ComPtr<IEnumString> values;HRESULT result=checker_->Suggest(word.c_str(),&values);if(FAILED(result)||!values)return FAILED(result)?result:E_FAIL;
    for(unsigned count=0;count<12;++count){LPOLESTR value=nullptr;ULONG fetched=0;result=values->Next(1,&value,&fetched);if(result==S_FALSE){CoTaskMemFree(value);return S_OK;}if(FAILED(result)||fetched!=1||!value){CoTaskMemFree(value);return E_FAIL;}if(ValidReplacement(value)&&std::find(suggestions.begin(),suggestions.end(),value)==suggestions.end())suggestions.emplace_back(value);CoTaskMemFree(value);}return S_OK;
  }
};
}
