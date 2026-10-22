// Included immediately after NotesCodeUi.inl in NotesWorkspaceUi.inl.
// Checking is read-only; one explicit Change produces one normal Undo step.
#include "NotesSpelling.h"
struct NotesSpellingState {
  HWND owner=nullptr,edit=nullptr;
  std::wstring client;fs::path root;uint64_t page=0;
  notes_spelling::Range scope;CHARRANGE originalSelection{};POINT originalScroll{};
  notes_spelling::WindowsDictionary dictionary;std::vector<std::wstring> languages;
  std::wstring snapshot,masked;size_t scan=0;
  std::optional<notes_spelling::Error> error;
  std::set<std::wstring> ignored;
  bool changed=false,updating=false;
};
static constexpr UINT_PTR kNotesSpellingTimer=0x5350;
static ClientNotesDialogState *NotesSpellingWorkspace(const NotesSpellingState &check){
  if(!IsWindow(check.owner)||!IsWindow(check.edit))return nullptr;
  auto *state=reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(check.owner,DWLP_USER));
  if(!state||!NotesEditorMatchesActive(*state)||state->attachedEditor!=check.edit||
      GetDlgItem(check.owner,IDC_CLIENT_NOTES_TEXT)!=check.edit||state->clientName!=check.client||state->clientRoot!=check.root||state->attachedPageId!=check.page)return nullptr;
  return state;
}
static bool NotesSpellingUnchanged(const NotesSpellingState &check){
  if(!NotesSpellingWorkspace(check))return false;
  std::wstring actual;return SUCCEEDED(notes_spelling::NativeText(check.edit,check.scope,actual))&&actual==check.snapshot;
}
static void NotesSpellingStop(HWND dialog,NotesSpellingState &check,const wchar_t *hint){
  KillTimer(dialog,kNotesSpellingTimer);check.error.reset();
  SetDlgItemTextW(dialog,IDC_NOTES_SPELL_HINT,hint);SetDlgItemTextW(dialog,IDC_NOTES_SPELL_WORD,L"");
  check.updating=true;SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,CB_RESETCONTENT,0,0);SetDlgItemTextW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,L"");check.updating=false;
  for(const int id:{IDC_NOTES_SPELL_CHANGE,IDC_NOTES_SPELL_IGNORE,IDC_NOTES_SPELL_IGNORE_ALL})EnableWindow(GetDlgItem(dialog,id),FALSE);
}
static void NotesSpellingQueue(HWND dialog,NotesSpellingState &check){
  NotesSpellingStop(dialog,check,L"Checking prose with the selected Windows dictionary...");
  if(!SetTimer(dialog,kNotesSpellingTimer,15,nullptr))NotesSpellingStop(dialog,check,L"The spelling check could not start. Close and try again.");
}
static void NotesSpellingStep(HWND dialog,NotesSpellingState &check){
  KillTimer(dialog,kNotesSpellingTimer);
  if(!NotesSpellingUnchanged(check)){NotesSpellingStop(dialog,check,L"The note changed. Close and reopen spelling to review its current text.");return;}
  if(check.scan>=check.masked.size()){NotesSpellingStop(dialog,check,L"Spelling review finished. Code, links, technical IDs and commands were skipped.");return;}
  const size_t start=check.scan;size_t last=(std::min)(check.masked.size(),start+notes_spelling::kChunk);
  while(last<check.masked.size()&&!iswspace(check.masked[last])&&last<start+notes_spelling::kChunk+64)++last;
  std::vector<notes_spelling::Error> errors;
  if(FAILED(check.dictionary.Check(std::wstring_view(check.masked).substr(start,last-start),errors))){NotesSpellingStop(dialog,check,L"The Windows dictionary could not check this text. No changes were made.");return;}
  if(!IsWindow(dialog)||GetWindowLongPtrW(dialog,DWLP_USER)!=reinterpret_cast<LONG_PTR>(&check))return;
  if(!NotesSpellingUnchanged(check)){NotesSpellingStop(dialog,check,L"The note changed. Close and reopen spelling to review its current text.");return;}
  check.scan=last;
  for(auto &error:errors){
    error.first+=start;const auto word=check.snapshot.substr(error.first,error.length);
    if(!notes_spelling::ProseWord(word)||check.ignored.contains(notes_spelling::Fold(word)))continue;
    check.error=error;check.scan=error.first+error.length;
    SetDlgItemTextW(dialog,IDC_NOTES_SPELL_WORD,word.c_str());
    std::vector<std::wstring> suggestions;check.dictionary.Suggestions(word,suggestions);
    if(!error.replacement.empty()&&std::find(suggestions.begin(),suggestions.end(),error.replacement)==suggestions.end())suggestions.insert(suggestions.begin(),error.replacement);
    check.updating=true;SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,CB_RESETCONTENT,0,0);
    for(const auto &suggestion:suggestions)SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(suggestion.c_str()));
    if(!suggestions.empty())SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,CB_SETCURSEL,0,0);else SetDlgItemTextW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,L"");check.updating=false;
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_SPELL_CHANGE),!suggestions.empty());EnableWindow(GetDlgItem(dialog,IDC_NOTES_SPELL_IGNORE),TRUE);EnableWindow(GetDlgItem(dialog,IDC_NOTES_SPELL_IGNORE_ALL),TRUE);
    SetDlgItemTextW(dialog,IDC_NOTES_SPELL_HINT,L"Choose or type a correction, then Change. Nothing changes automatically.");return;
  }
  if(!SetTimer(dialog,kNotesSpellingTimer,15,nullptr))NotesSpellingStop(dialog,check,L"The spelling check could not continue. Close and try again.");
}
static bool NotesSpellingChange(HWND dialog,NotesSpellingState &check){
  auto *state=NotesSpellingWorkspace(check);const auto replacement=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_SPELL_SUGGESTIONS));
  if(!check.error||!state||!NotesSpellingUnchanged(check)||!notes_spelling::ValidReplacement(replacement)||
      !RevalidateSafeClientContainerPath(check.client,check.root)||IsClientArchived(check.client))return false;
  const auto error=*check.error;const auto word=check.snapshot.substr(error.first,error.length);
  if(replacement==word){NotesSpellingQueue(dialog,check);return true;}
  bool changed=false;std::wstring updated,updatedMask;HRESULT result=E_FAIL;
  const CHARRANGE affected{check.scope.first+static_cast<LONG>(error.first),check.scope.first+static_cast<LONG>(error.first+error.length)};
  const auto transaction=NotesApplyNativeTransaction(check.owner,*state,affected,[&](HWND edit){
    // The app transaction owns the checked collection and event suppression.
    // Publish only a correction whose complete native readback was validated.
    result=notes_spelling::ReplaceWord(edit,check.scope,check.snapshot,error,replacement,changed,updated,updatedMask,false);
    return SUCCEEDED(result)&&changed;
  });
  if(transaction!=NotesAppTransactionResult::Committed){
    NotesAppTransactionNotice(dialog,transaction,L"This spelling correction could not be applied. Your previous text is kept.",L"Check spelling");return false;
  }
  check.changed=true;MarkNotesChanged(check.owner,*state);SaveClientNotesDraft(check.owner,*state,true);
  check.scope.last=check.scope.first+static_cast<LONG>(updated.size());check.snapshot=std::move(updated);check.masked=std::move(updatedMask);check.scan=error.first+replacement.size();NotesSpellingQueue(dialog,check);return true;
}
static INT_PTR CALLBACK NotesSpellingProc(HWND dialog,UINT message,WPARAM w,LPARAM l){
  auto *check=reinterpret_cast<NotesSpellingState *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if(message==WM_INITDIALOG){
    check=reinterpret_cast<NotesSpellingState *>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);ApplyCleanupDialogTheme(dialog);
    if(!check)return FALSE;
    for(const int id:{IDC_NOTES_SPELL_LANGUAGE,IDC_NOTES_SPELL_SUGGESTIONS}){const auto combo=GetDlgItem(dialog,id);SetWindowSubclass(combo,NotesComboSubclass,1,0);const int row=ScaleByDpi(19,GetDpiForWindow(dialog));SendMessageW(combo,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),row);SendMessageW(combo,CB_SETITEMHEIGHT,0,row);}
    check->updating=true;
    for(const auto &language:check->languages){wchar_t name[128]{};GetLocaleInfoEx(language.c_str(),LOCALE_SLOCALIZEDLANGUAGENAME,name,static_cast<int>(std::size(name)));const auto label=std::wstring(name[0]?name:language.c_str())+L" ("+language+L")";SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_LANGUAGE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(locale,LOCALE_NAME_MAX_LENGTH);
    auto language=std::find(check->languages.begin(),check->languages.end(),locale);if(language==check->languages.end())language=std::find(check->languages.begin(),check->languages.end(),L"en-US");if(language==check->languages.end())language=check->languages.begin();
    const auto index=std::distance(check->languages.begin(),language);SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_LANGUAGE,CB_SETCURSEL,index,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,CB_LIMITTEXT,notes_spelling::kMaxSuggestion,0);
    check->updating=false;
    if(FAILED(check->dictionary.Select(*language)))NotesSpellingStop(dialog,*check,L"This Windows dictionary is unavailable. Choose another installed language.");else NotesSpellingQueue(dialog,*check);
    return TRUE;
  }
  if(message==WM_DRAWITEM&&l&&DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(l)))return TRUE;
  if(const auto themed=HandleCleanupDialogTheme(dialog,message,w,l))return *themed;
  if(!check)return FALSE;
  if(message==WM_DESTROY){KillTimer(dialog,kNotesSpellingTimer);return TRUE;}
  if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(w)==IDCANCEL)){EndDialog(dialog,IDCANCEL);return TRUE;}
  if(message==WM_TIMER&&w==kNotesSpellingTimer){NotesSpellingStep(dialog,*check);return TRUE;}
  if(message!=WM_COMMAND||check->updating)return FALSE;
  const int id=LOWORD(w);
  if(id==IDC_NOTES_SPELL_LANGUAGE&&HIWORD(w)==CBN_SELCHANGE){
    const auto selected=SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_LANGUAGE,CB_GETCURSEL,0,0);
    if(selected<0||static_cast<size_t>(selected)>=check->languages.size())return TRUE;
    check->scan=0;check->ignored.clear();if(FAILED(check->dictionary.Select(check->languages[selected])))NotesSpellingStop(dialog,*check,L"This Windows dictionary is unavailable.");else NotesSpellingQueue(dialog,*check);return TRUE;
  }
  if(id==IDC_NOTES_SPELL_SUGGESTIONS&&(HIWORD(w)==CBN_EDITCHANGE||HIWORD(w)==CBN_SELCHANGE)){
    if(HIWORD(w)==CBN_SELCHANGE){const auto selected=SendDlgItemMessageW(dialog,id,CB_GETCURSEL,0,0);if(selected>=0){wchar_t text[notes_spelling::kMaxSuggestion+1]{};SendDlgItemMessageW(dialog,id,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(text));SetDlgItemTextW(dialog,id,text);}}
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_SPELL_CHANGE),check->error.has_value()&&notes_spelling::ValidReplacement(NotesWindowText(GetDlgItem(dialog,id))));return TRUE;
  }
  if(id==IDC_NOTES_SPELL_CHANGE){if(!NotesSpellingChange(dialog,*check))NotesSpellingStop(dialog,*check,L"The note changed or this correction could not be applied. Close and review the note; your text is kept.");return TRUE;}
  if((id==IDC_NOTES_SPELL_IGNORE||id==IDC_NOTES_SPELL_IGNORE_ALL)&&check->error){
    if(id==IDC_NOTES_SPELL_IGNORE_ALL)check->ignored.insert(notes_spelling::Fold(check->snapshot.substr(check->error->first,check->error->length)));
    NotesSpellingQueue(dialog,*check);return TRUE;
  }
  return FALSE;
}
static void NotesCheckSpelling(HWND dialog,ClientNotesDialogState &state){
  if(!NotesEditorMatchesActive(state))return;
  NotesSpellingState check;check.owner=dialog;check.edit=state.attachedEditor;check.client=state.clientName;check.root=state.clientRoot;check.page=state.attachedPageId;
  NotesSelection(check.edit,check.originalSelection);SendMessageW(check.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&check.originalScroll));
  check.scope={check.originalSelection.cpMin,check.originalSelection.cpMax};
  if(check.scope.first==check.scope.last){notes_spelling::ComPtr<ITextDocument> document;notes_spelling::ComPtr<ITextRange> range;LONG length=0;if(FAILED(notes_spelling::TextDocument(check.edit,document))||FAILED(document->Range(0,0,&range))||FAILED(range->GetStoryLength(&length)))return;check.scope={0,(std::max)(0L,length-1)};}
  if(static_cast<size_t>(check.scope.last-check.scope.first)>notes_spelling::kMaxScope){MessageBoxW(dialog,L"Select up to 256,000 characters to check part of this large note. Spelling only changes words you explicitly approve.",L"Check spelling",MB_OK|MB_ICONINFORMATION);return;}
  if(FAILED(notes_spelling::ReadScope(check.edit,check.scope,check.snapshot,check.masked))){MessageBoxW(dialog,L"This selection could not be checked safely. Select a shorter paragraph and try again. Your note has not changed.",L"Check spelling",MB_OK|MB_ICONINFORMATION);return;}
  if(FAILED(check.dictionary.Initialize())||FAILED(check.dictionary.Languages(check.languages))||check.languages.empty()){MessageBoxW(dialog,L"No Windows spelling dictionary is available. Install a language's basic typing features in Windows Settings, then try again. Your note has not changed.",L"Check spelling",MB_OK|MB_ICONINFORMATION);return;}
  auto *current=NotesSpellingWorkspace(check);if(!current||!NotesSpellingUnchanged(check))return;
  HideNotesFormat(dialog,*current);CloseNotesPanel(*current);
  DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_SPELLING),dialog,NotesSpellingProc,reinterpret_cast<LPARAM>(&check));
  if(!check.changed&&NotesSpellingUnchanged(check)){SendMessageW(check.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&check.originalSelection));SendMessageW(check.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&check.originalScroll));}
  if(auto *current=NotesSpellingWorkspace(check))UpdateNotesToolbar(dialog,*current);
}
