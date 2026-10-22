// Included in NotesWorkspaceUi.inl in place of its old NotesFind function.
#include "NotesFindModel.h"
#include "NotesUndo.h"
static constexpr UINT_PTR kNotesFindTimer=19;
static void NotesFindPaintOption(HWND control,HDC dc){
 RECT area{};GetClientRect(control,&area);FillRect(dc,&area,g_hbrThemeWindow);const auto dpi=GetDpiForWindow(control);const int size=ScaleByDpi(16,dpi),inset=ScaleByDpi(1,dpi);
 RECT box{inset,(area.bottom-size)/2,inset+size,(area.bottom+size)/2};const bool enabled=IsWindowEnabled(control)!=FALSE,checked=SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED;
 const auto fill=checked&&enabled?g_themeColors.crAccent:g_themeColors.crWindow;const auto border=enabled&&(checked||g_hHotButton==control)?g_themeColors.crAccent:g_themeColors.crControlBorder;
 notes_editor_ui::DrawSurface(dc,box,fill,fill,border,static_cast<float>(ScaleByDpi(2,dpi)));
 if(checked){const auto color=enabled?g_themeColors.crAccentText:g_themeColors.crControlBorder;const auto pen=CreatePen(PS_SOLID,(std::max)(1,ScaleByDpi(2,dpi)),color);const auto prior=SelectObject(dc,pen);POINT mark[]={{box.left+ScaleByDpi(3,dpi),box.top+ScaleByDpi(8,dpi)},{box.left+ScaleByDpi(7,dpi),box.top+ScaleByDpi(12,dpi)},{box.left+ScaleByDpi(13,dpi),box.top+ScaleByDpi(4,dpi)}};Polyline(dc,mark,3);SelectObject(dc,prior);DeleteObject(pen);}
 RECT label=area;label.left=box.right+ScaleByDpi(6,dpi);const auto prior=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,enabled?g_themeColors.crWindowText:g_themeColors.crControlBorder);const auto text=NotesWindowText(control);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
 if(GetFocus()==control){InflateRect(&label,-ScaleByDpi(1,dpi),-ScaleByDpi(3,dpi));DrawFocusRect(dc,&label);}SelectObject(dc,prior);
}
static LRESULT CALLBACK NotesFindOptionPaint(HWND control,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
 if(message==WM_ERASEBKGND)return TRUE;
 if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(control,&paint);NotesFindPaintOption(control,dc);EndPaint(control,&paint);return 0;}
 if((message==WM_PRINTCLIENT||message==WM_PRINT)&&w){NotesFindPaintOption(control,reinterpret_cast<HDC>(w));return 0;}
 const auto result=DefSubclassProc(control,message,w,l);
 if(message==BM_SETCHECK||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_ENABLE||message==WM_THEMECHANGED||message==WM_SYSCOLORCHANGE)InvalidateRect(control,nullptr,FALSE);
 if(message==WM_NCDESTROY)RemoveWindowSubclass(control,NotesFindOptionPaint,id);
 return result;
}
static notes_find::Options NotesFindOptions(const ClientNotesDialogState &state){return {state.findMatchCase,state.findWholeWord,state.findWrap};}
static void NotesFindRefresh(HWND dialog,ClientNotesDialogState &state){
 KillTimer(dialog,kNotesFindTimer);if(!state.findVisible)return;
 state.findText=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_FIND));
 std::wstring info;
 if(state.pendingTransactionRecovery)info=L"Editing and saving are paused";
 else if(!HasActiveNote(state))info=L"No open note";
 else if(state.findText.empty())info=L"Type to search this note";
 else{
  const auto edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);CHARRANGE selected{};NotesSelection(edit,selected);
  const auto count=notes_find::Count(edit,state.findText,NotesFindOptions(state),selected);
  if(!count.count)info=L"No matches";
  else if(count.current)info=std::to_wstring(count.current)+L" of "+std::to_wstring(count.count)+(count.limited?L"+":L"");
  else info=std::to_wstring(count.count)+(count.limited?L"+ matches":count.count==1?L" match":L" matches");
 }
 if(!state.findNotice.empty())info=state.findNotice+(info.empty()?L"":L"  \u00b7  "+info);
 SetDlgItemTextW(dialog,IDC_NOTES_FIND_INFO,info.c_str());
}
// Query edits never move the editor selection. The count is bounded to 10,000
// plus one probe and debounced so ordinary typing does not rescan every key.
static void NotesFindChanged(HWND dialog,ClientNotesDialogState &state){
 if(!state.findVisible||state.loading)return;state.findNotice.clear();
 SetDlgItemTextW(dialog,IDC_NOTES_FIND_INFO,L"Searching...");SetTimer(dialog,kNotesFindTimer,200,nullptr);
}
static void NotesFindInitialize(HWND dialog,const ClientNotesDialogState &state){
 for(const auto [id,enabled]:{std::pair{IDC_NOTES_FIND_MATCH_CASE,state.findMatchCase},{IDC_NOTES_FIND_WHOLE_WORD,state.findWholeWord},{IDC_NOTES_FIND_WRAP,state.findWrap}}){
  const auto control=GetDlgItem(dialog,id);if(control){SetWindowSubclass(control,NotesFindOptionPaint,0x4e464f50,0);SetWindowSubclass(control,ButtonHotSubclassProc,1,0);SendMessageW(control,BM_SETCHECK,enabled?BST_CHECKED:BST_UNCHECKED,0);}
 }
 SendDlgItemMessageW(dialog,IDC_NOTES_REPLACE_TEXT,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Replace with (blank removes matches)"));
}
static void NotesFind(HWND dialog,ClientNotesDialogState &state,bool previous){
 if(!HasActiveNote(state))return;
 state.findText=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_FIND));
 if(state.findText.empty()){
  state.findVisible=true;LayoutClientNotes(dialog,GetDpiForWindow(dialog));NotesFindRefresh(dialog,state);SetFocus(GetDlgItem(dialog,IDC_NOTES_FIND));return;
 }
 const auto edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);CHARRANGE selected{};NotesSelection(edit,selected);
 const auto match=notes_find::Navigate(edit,state.findText,NotesFindOptions(state),selected,previous);
 state.findNotice.clear();
 if(match.found){SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&match.range));SendMessageW(edit,EM_HIDESELECTION,FALSE,FALSE);SendMessageW(edit,EM_SCROLLCARET,0,0);if(match.wrapped)state.findNotice=previous?L"Wrapped to end":L"Wrapped to start";SetFocus(edit);}
 else if(!state.findWrap)state.findNotice=previous?L"Reached the beginning":L"Reached the end";
 // F3 must make an inline no-match/end-of-document message visible even when
 // the search row had previously been closed, without creating a modal box.
 if(!state.findVisible){state.findVisible=true;LayoutClientNotes(dialog,GetDpiForWindow(dialog));}
 NotesFindRefresh(dialog,state);
}
static notes_undo::Recovery NotesFindUndoRecovery(HWND edit,ClientNotesDialogState &state){
 const auto id=state.attachedPageId;const auto name=state.clientName;const auto root=state.clientRoot;
 const auto found=state.editorLinks.find(id);const bool hadRegistry=found!=state.editorLinks.end();
 const auto saved=std::make_shared<const ClientNotesDialogState::LinkRegistry>(hadRegistry?found->second:ClientNotesDialogState::LinkRegistry{});
 const auto matches=[&state,edit,id,name,root]{return state.clientName==name && state.clientRoot==root && state.attachedEditor==edit && state.attachedPageId==id && NotesEditorMatchesActive(state);};
 notes_undo::Recovery recovery;
 recovery.repairLinks=[&state,edit,id,hadRegistry,saved,matches]{
  if(!matches())throw std::runtime_error("The replacement editor no longer belongs to its original client and note");
  // Before-replacement touches change only this note's cookie-use metadata.
  // Restore its immutable preimage on every recovery invocation; other pages'
  // registries and the actual notebook are not involved in native rollback.
  if(hadRegistry)state.editorLinks[id]=*saved;else state.editorLinks.erase(id);
  NotesRestoreCookieLinks(edit,state);
  const auto current=state.editorLinks.find(id);
  if(hadRegistry && (current==state.editorLinks.end() || current->second.nextCookie!=saved->nextCookie || current->second.useCounter!=saved->useCounter || current->second.urls!=saved->urls || current->second.lastUse!=saved->lastUse))throw std::runtime_error("The replacement's original link bindings could not be restored");
  if(!hadRegistry && current!=state.editorLinks.end())throw std::runtime_error("A replacement recovery created unexpected link bindings");
 };
 recovery.isRegisteredCookie=[&state,id,hadRegistry,saved,matches](LONG cookie){
  if(!hadRegistry || cookie<=0 || !matches())return false;
  const auto original=saved->urls.find(cookie);const auto current=state.editorLinks.find(id);
  if(original==saved->urls.end() || original->second.empty() || !ValidNotesUrl(original->second) || current==state.editorLinks.end())return false;
  const auto bound=current->second.urls.find(cookie);
  return bound!=current->second.urls.end() && bound->second==original->second;
 };
 return recovery;
}
struct NotesFindProspectiveCopy {
 HWND edit=nullptr;
 ITextDocument *source=nullptr,*document=nullptr;
 ITextRange *from=nullptr,*to=nullptr;
#ifdef CTSPACES_NOTES_TEST_HOOKS
 const char *stage="source document";HRESULT status=S_OK;LONG sourceStory=0,copiedStory=0;
#endif
 ~NotesFindProspectiveCopy(){if(to)to->Release();if(from)from->Release();if(document)document->Release();if(source)source->Release();if(edit)DestroyWindow(edit);}
};
static bool NotesFindCloneForReplacement(HWND edit,NotesFindProspectiveCopy &copy){
 // A draft may already exceed a save budget. Copy its native formatted story,
 // rather than serializing it first, so a replacement can shorten that draft.
 IUnknown *ole=nullptr;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
 if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&copy.source));ole->Release();}
 if(!copy.source || FAILED(copy.source->Range(0,0,&copy.from)))return false;
 LONG story=0;if(FAILED(copy.from->GetStoryLength(&story)) || story<1 || FAILED(copy.from->SetRange(0,story-1)))return false;
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.sourceStory=story;copy.stage="create copy";
#endif
 copy.edit=CreateWindowExW(WS_EX_NOACTIVATE,L"RICHEDIT50W",L"",WS_POPUP|ES_MULTILINE,0,0,1,1,nullptr,nullptr,g_hInst,nullptr);
 if(!copy.edit)return false;
 SendMessageW(copy.edit,EM_EXLIMITTEXT,0,LONG_MAX);
 CHARFORMAT2W defaults{sizeof(defaults)};SendMessageW(edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&defaults));SendMessageW(copy.edit,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&defaults));
 SendMessageW(copy.edit,EM_AUTOURLDETECT,SendMessageW(edit,EM_GETAUTOURLDETECT,0,0),0);
 ole=nullptr;
 SendMessageW(copy.edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
 if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&copy.document));ole->Release();}
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.stage="copy document/range";
#endif
 if(!copy.document || FAILED(copy.document->Range(0,0,&copy.to)))return false;
 const HRESULT copied=copy.to->SetFormattedText(copy.from);
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.status=copied;copy.stage="SetFormattedText";
#endif
 if(FAILED(copied))return false;
 LONG copiedStory=0;
 const HRESULT measured=copy.to->GetStoryLength(&copiedStory);
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.status=measured;copy.copiedStory=copiedStory;copy.stage="copied story length";
#endif
 if(FAILED(measured) || copiedStory!=story)return false;
 // Every RichEdit story already owns its final paragraph mark. Including the
 // source mark in SetFormattedText inserts an extra paragraph; copy that final
 // mark's formatting separately instead of adding it as text.
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.stage="final paragraph formatting";
#endif
 if(FAILED(copy.from->SetRange(story-1,story)) || FAILED(copy.to->SetRange(story-1,story)))return false;
 ITextFont *lastFont=nullptr;ITextPara *lastParagraph=nullptr;
 const bool finalFormat=SUCCEEDED(copy.from->GetFont(&lastFont)) && SUCCEEDED(copy.from->GetPara(&lastParagraph)) && SUCCEEDED(copy.to->SetFont(lastFont)) && SUCCEEDED(copy.to->SetPara(lastParagraph));
 if(lastParagraph)lastParagraph->Release();if(lastFont)lastFont->Release();
 if(!finalFormat)return false;
 // Native cross-document SetFormattedText/SetPara can omit paragraph spacing.
 // Transfer those values explicitly on private homogeneous paragraph runs;
 // no source selection or native Undo state is changed while reading them.
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.stage="paragraph spacing";
#endif
 for(LONG first=0;first<story;){
  LONG moved=0,last=first;
  if(FAILED(copy.from->SetRange(first,first)) || FAILED(copy.from->MoveEnd(tomParaFormat,1,&moved)) || FAILED(copy.from->GetEnd(&last)) || last<=first)return false;
  last=(std::min)(last,story);
  ITextPara *from=nullptr,*to=nullptr;LONG rule=tomUndefined;float spacing=tomUndefined,before=tomUndefined,after=tomUndefined;
  bool ok=SUCCEEDED(copy.from->SetRange(first,last)) && SUCCEEDED(copy.to->SetRange(first,last)) && SUCCEEDED(copy.from->GetPara(&from)) && SUCCEEDED(copy.to->GetPara(&to)) && SUCCEEDED(from->GetLineSpacingRule(&rule)) && SUCCEEDED(from->GetLineSpacing(&spacing)) && SUCCEEDED(from->GetSpaceBefore(&before)) && SUCCEEDED(from->GetSpaceAfter(&after));
  if(ok)ok=rule!=tomUndefined && spacing!=static_cast<float>(tomUndefined) && before!=static_cast<float>(tomUndefined) && after!=static_cast<float>(tomUndefined) && SUCCEEDED(to->SetLineSpacing(rule,spacing)) && SUCCEEDED(to->SetSpaceBefore(before)) && SUCCEEDED(to->SetSpaceAfter(after));
  if(to)to->Release();if(from)from->Release();
  if(!ok)return false;
  first=last;
 }
 // RTF does not retain application cookies. Copy each original native format
 // run explicitly, without reading captions or pruning the live URL registry.
 for(LONG first=0;first<story;){
#ifdef CTSPACES_NOTES_TEST_HOOKS
  copy.stage="source format run";
#endif
  LONG moved=0,last=first;
  if(FAILED(copy.from->SetRange(first,first)) || FAILED(copy.from->MoveEnd(tomCharFormat,1,&moved)) || FAILED(copy.from->GetEnd(&last)) || last<=first)return false;
  last=(std::min)(last,story);
  ITextFont *font=nullptr;ITextFont2 *extended=nullptr;LONG effects=0,mask=0,cookie=0;
  bool ok=SUCCEEDED(copy.from->SetRange(first,last)) && SUCCEEDED(copy.from->GetFont(&font)) && SUCCEEDED(font->QueryInterface(__uuidof(ITextFont2),reinterpret_cast<void **>(&extended))) && SUCCEEDED(extended->GetEffects(&effects,&mask)) && SUCCEEDED(extended->GetCookie(&cookie));
  if(extended)extended->Release();if(font)font->Release();
  if(!ok)return false;
#ifdef CTSPACES_NOTES_TEST_HOOKS
  copy.stage="copy cookie/effects";
#endif
  effects&=CFM_LINK|CFM_LINKPROTECTED;
  NotesRangeLinkEffects(copy.edit,{first,last},&effects,&cookie,true,&ok);
  if(!ok)return false;
  LONG actual=0;bool read=false;NotesRangeLinkEffects(copy.edit,{first,last},nullptr,&actual,false,&read);
  if(!read || actual!=cookie)return false;
  first=last;
 }
 SendMessageW(copy.edit,EM_EXLIMITTEXT,0,SendMessageW(edit,EM_GETLIMITTEXT,0,0));
 CHARRANGE selected{};NotesSelection(edit,selected);SendMessageW(copy.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
 CHARRANGE actual{};NotesSelection(copy.edit,actual);
 SendMessageW(copy.edit,EM_EMPTYUNDOBUFFER,0,0);
#ifdef CTSPACES_NOTES_TEST_HOOKS
 copy.stage="selection/limit";
#endif
 return actual.cpMin==selected.cpMin && actual.cpMax==selected.cpMax;
}
static notes_find::Replacement NotesFindProspectiveReplacement(HWND edit,const ClientNotesDialogState &state,const std::wstring &query,const std::wstring &replacement,bool all){
 using Status=notes_find::ReplaceStatus;
 if(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY)return {Status::ReadOnly};
 try{
  NotesFindProspectiveCopy copy;
  if(!NotesFindCloneForReplacement(edit,copy))return {Status::Unavailable};
  ClientNotesDialogState prospective;
  prospective.clientName=state.clientName;prospective.clientRoot=state.clientRoot;
  prospective.pages=state.pages;prospective.activePage=state.activePage;
  prospective.attachedEditor=copy.edit;prospective.attachedPageId=state.attachedPageId;prospective.loading=true;
  const auto bindings=state.editorLinks.find(state.attachedPageId);
  if(bindings!=state.editorLinks.end())prospective.editorLinks.emplace(bindings->first,bindings->second);
  const auto result=notes_find::Replace(copy.edit,query,replacement,NotesFindOptions(state),all,{},NotesFindUndoRecovery(copy.edit,prospective));
  // A failed private-copy edit has never touched the user's document, even
  // when native Undo could not restore that disposable copy.
  if(result.status==Status::Failed)return {Status::Unavailable};
  if(result.status!=Status::Changed)return result;
  NotesRestoreCookieLinks(copy.edit,prospective);
  if(!NotesPlainTextWithinLimit(copy.edit))return {Status::TooLarge};
  std::string rtf;if(!StreamClientNotesOut(copy.edit,rtf))return {Status::TooLarge};
  auto &page=prospective.pages[prospective.activePage];
  const auto previousLinks=page.links;const auto previousRanges=page.linkRanges;
  const bool previousRangesKnown=page.linkRangesKnown;const auto previous=page.rtf;
  const CHARRANGE wholeStory{0,-1};
  if(!NotesCaptureLinkMappings(copy.edit,prospective,page,&wholeStory))return {Status::TooLarge};
  page.rich=true;page.rtf=std::move(rtf);page.text.clear();page.pristine=false;
  if(page.rtf!=previous || page.links!=previousLinks || page.linkRanges!=previousRanges || page.linkRangesKnown!=previousRangesKnown){
   client_notes::RememberRevision(page,previous,false,&previousLinks,&previousRanges,previousRangesKnown);
   page.modified=client_notes::Now();if(!page.created)page.created=page.modified;
  }
  std::string notebook;
  if(!client_notes::SerializeNotebook(prospective.pages,notebook))return {Status::TooLarge};
  return result;
 }catch(const std::bad_alloc &){return {Status::TooLarge};}
}
static void NotesFindReplace(HWND dialog,ClientNotesDialogState &state,bool all){
 if(!HasActiveNote(state)||!NotesEditorMatchesActive(state))return;
 state.findText=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_FIND));
 if(state.findText.empty()){SetFocus(GetDlgItem(dialog,IDC_NOTES_FIND));NotesFindRefresh(dialog,state);return;}
 const auto edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);const auto replacement=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_REPLACE_TEXT));
 if(edit!=state.attachedEditor)return;
 auto result=NotesFindProspectiveReplacement(edit,state,state.findText,replacement,all);
 notes_undo::Recovery recovery;
 std::shared_ptr<NotesAppTransactionRecovery> appBefore;
 if(result.status==notes_find::ReplaceStatus::Changed){
  appBefore=NotesCaptureAppTransactionRecovery(dialog,state);
  if(!appBefore)result={notes_find::ReplaceStatus::Unavailable};
  else try{
   recovery=NotesFindUndoRecovery(edit,state);
   recovery.retainOriginal=[&state,appBefore](const std::shared_ptr<const notes_undo::Document> &document,const notes_undo::View &view){
    appBefore->modelDocument=document;appBefore->view=view;NotesAppBlockTransaction(appBefore,state);
   };
  }catch(const std::bad_alloc &){result={notes_find::ReplaceStatus::Unavailable};}
 }
 const bool apply=result.status==notes_find::ReplaceStatus::Changed;
 const bool loading=state.loading;state.loading=true;
 const auto events=SendMessageW(edit,EM_GETEVENTMASK,0,0);SendMessageW(edit,EM_SETEVENTMASK,0,events&~(ENM_CHANGE|ENM_SELCHANGE));
 if(apply)result=notes_find::Replace(edit,state.findText,replacement,NotesFindOptions(state),all,[&](const CHARRANGE &range){NotesTouchRangeCookies(edit,state,range);},recovery);
 SendMessageW(edit,EM_SETEVENTMASK,0,events);
 state.loading=loading;
 if(apply && result.status==notes_find::ReplaceStatus::Failed && !result.rolledBack && appBefore && !state.pendingTransactionRecovery){
  if(result.recoveryDocument){appBefore->modelDocument=result.recoveryDocument;appBefore->view=result.recoveryView;}
  NotesAppBlockTransaction(appBefore,state);
 }
 if(state.pendingTransactionRecovery){
  state.findNotice=L"Replacement could not be recovered safely; the original draft is retained";
  SetNotesSaveFailure(dialog,state,true);NotesFindRefresh(dialog,state);return;
 }
 if(apply && (result.status==notes_find::ReplaceStatus::Changed || (result.status==notes_find::ReplaceStatus::Failed&&(result.rolledBack||result.count))))NotesRestoreCookieLinks(edit,state);
 using Status=notes_find::ReplaceStatus;
 switch(result.status){
 case Status::Changed:state.findNotice=L"Replaced "+std::to_wstring(result.count)+(result.count==1?L" match":L" matches");if(result.wrapped)state.findNotice+=L" \u00b7 wrapped to start";MarkNotesChanged(dialog,state);UpdateNotesToolbar(dialog,state);break;
 case Status::NoMatch:state.findNotice=L"No matching text to replace";break;
 case Status::NoChange:state.findNotice=L"Matches already contain the replacement text";break;
 case Status::ReadOnly:state.findNotice=L"This editor is read-only";break;
 case Status::TooLarge:state.findNotice=L"Replacement is too large for this note or its links; no text changed";break;
 case Status::Unavailable:state.findNotice=L"Could not prepare replacement; no text changed";break;
 case Status::Failed:state.findNotice=result.rolledBack?L"Replacement could not finish; changes undone":L"Replacement could not finish; check the note and use Undo if needed";if(result.count&&!result.rolledBack)MarkNotesChanged(dialog,state);break;
 }
 NotesFindRefresh(dialog,state);SetFocus(edit);
}
static bool NotesFindCommand(HWND dialog,ClientNotesDialogState &state,UINT command){
 const auto edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);
 if(command==IDC_NOTES_FIND_TOGGLE||command==IDC_NOTES_FIND_CLOSE||command==IDC_NOTES_REPLACE){
  state.findVisible=command!=IDC_NOTES_FIND_CLOSE;state.replaceVisible=command==IDC_NOTES_REPLACE;state.findNotice.clear();
  if(!state.findVisible)KillTimer(dialog,kNotesFindTimer);
  LayoutClientNotes(dialog,GetDpiForWindow(dialog));NotesFindInitialize(dialog,state);NotesFindRefresh(dialog,state);
  SetFocus(!state.findVisible?edit:GetDlgItem(dialog,state.replaceVisible&&!NotesWindowText(GetDlgItem(dialog,IDC_NOTES_FIND)).empty()?IDC_NOTES_REPLACE_TEXT:IDC_NOTES_FIND));return true;
 }
 if(command==IDC_NOTES_FIND_NEXT||command==IDC_NOTES_FIND_PREV){NotesFind(dialog,state,command==IDC_NOTES_FIND_PREV);return true;}
 if(command==IDC_NOTES_REPLACE_NEXT||command==IDC_NOTES_REPLACE_ALL){NotesFindReplace(dialog,state,command==IDC_NOTES_REPLACE_ALL);return true;}
 if(command==IDC_NOTES_FIND_MATCH_CASE||command==IDC_NOTES_FIND_WHOLE_WORD||command==IDC_NOTES_FIND_WRAP){
  if(command==IDC_NOTES_FIND_MATCH_CASE)state.findMatchCase=!state.findMatchCase;
  else if(command==IDC_NOTES_FIND_WHOLE_WORD)state.findWholeWord=!state.findWholeWord;else state.findWrap=!state.findWrap;
  NotesFindInitialize(dialog,state);NotesFindChanged(dialog,state);return true;
 }
 return false;
}
static int NotesFindHeight(const ClientNotesDialogState &state){return state.findVisible?(state.replaceVisible?108:72):0;}
