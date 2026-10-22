// On-demand editing tools. The normal editor has no additional permanent row.
static std::optional<std::wstring> NotesRawEditorText(HWND edit) {
  GETTEXTLENGTHEX length{GTL_PRECISE|GTL_NUMCHARS,1200};
  const LRESULT count=SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
  if(count<0||static_cast<size_t>(count)>client_notes::kMaxRichBytes)return std::nullopt;
  std::wstring text(static_cast<size_t>(count)+2,L'\0');
  GETTEXTEX request{static_cast<DWORD>(text.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
  const LRESULT read=SendMessageW(edit,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&request),reinterpret_cast<LPARAM>(text.data()));
  if(read<0||static_cast<size_t>(read)>=text.size())return std::nullopt;
  text.resize(static_cast<size_t>(read));return text;
}
static std::wstring NotesStatisticsText(HWND edit) {
  const auto text=NotesRawEditorText(edit);if(!text)return L"Document information is unavailable.";
  const auto stats=notes_text_tools::Count(*text);CHARRANGE range{};NotesSelection(edit,range);
  const size_t first=static_cast<size_t>((std::clamp)(range.cpMin,0L,static_cast<LONG>(text->size())));
  const size_t last=static_cast<size_t>((std::clamp)(range.cpMax,static_cast<LONG>(first),static_cast<LONG>(text->size())));
  const auto selected=notes_text_tools::Count(std::wstring_view(*text).substr(first,last-first));
  return std::format(L"{} words  |  {} characters  |  {} lines\r\n{} selected characters",stats.words,stats.characters,stats.lines,selected.characters);
}
static void NotesApplyWordWrap(HWND edit,bool enabled) {
  if(!edit||!IsWindow(edit))return;
  CHARRANGE selected{};NotesSelection(edit,selected);POINT scroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  // Runtime RichEdit options change presentation without rewriting any text.
  SendMessageW(edit,EM_SETOPTIONS,enabled?ECOOP_AND:ECOOP_OR,enabled?~static_cast<LPARAM>(ECO_AUTOHSCROLL):ECO_AUTOHSCROLL);
  SendMessageW(edit,EM_SETTARGETDEVICE,0,enabled?0:1);
  ShowScrollBar(edit,SB_HORZ,enabled?FALSE:TRUE);
  SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
  SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
}
static void NotesGoToLine(HWND dialog,ClientNotesDialogState &state) {
  if(!NotesEditorMatchesActive(state))return;
  const HWND edit=state.attachedEditor;const auto page=state.attachedPageId;const auto client=state.clientRoot;
  const auto text=NotesRawEditorText(edit);if(!text)return;
  CHARRANGE selected{};NotesSelection(edit,selected);
  const size_t before=static_cast<size_t>((std::clamp)(selected.cpMin,0L,static_cast<LONG>(text->size())));
  const size_t line=notes_text_tools::Count(std::wstring_view(*text).substr(0,before)).lines;
  NotesInput input{L"Go to line",L"Line number",L"",std::to_wstring(line),L""};
  input.hint=L"Logical lines are counted from 1, independent of word wrapping. This note has "+std::to_wstring(notes_text_tools::Count(*text).lines)+L" lines.";
  if(!NotesAsk(dialog,input))return;
  auto *current=IsWindow(dialog)?reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(dialog,DWLP_USER)):nullptr;
  if(!current||current->clientRoot!=client||current->attachedPageId!=page||current->attachedEditor!=edit||!IsWindow(edit))return;
  const auto requested=notes_text_tools::ParseLine(input.first);const auto now=NotesRawEditorText(edit);
  const auto position=requested&&now?notes_text_tools::LineStart(*now,*requested):std::nullopt;
  if(!position){MessageBoxW(dialog,L"Enter an existing line number, starting from 1.",L"Go to line",MB_OK|MB_ICONINFORMATION);return;}
  CHARRANGE caret{static_cast<LONG>(*position),static_cast<LONG>(*position)};
  SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&caret));SendMessageW(edit,EM_SCROLLCARET,0,0);SetFocus(edit);
}
static bool NotesPrintRange(HWND clone,HDC dc,CHARRANGE range,const std::wstring &title) {
  const int dpiX=GetDeviceCaps(dc,LOGPIXELSX),dpiY=GetDeviceCaps(dc,LOGPIXELSY);
  const int width=GetDeviceCaps(dc,HORZRES),height=GetDeviceCaps(dc,VERTRES);
  if(dpiX<=0||dpiY<=0||width<=0||height<=0)return false;
  const int pageWidth=MulDiv(width,1440,dpiX),pageHeight=MulDiv(height,1440,dpiY);
  const int inset=360;
  FORMATRANGE format{};format.hdc=format.hdcTarget=dc;format.rcPage={0,0,pageWidth,pageHeight};
  format.rc={inset,inset,(std::max)(inset+1,pageWidth-inset),(std::max)(inset+1,pageHeight-inset)};format.chrg=range;
  DOCINFOW document{sizeof(document)};document.lpszDocName=title.c_str();
  if(StartDocW(dc,&document)<=0)return false;
  bool succeeded=true;unsigned pages=0;
  while(format.chrg.cpMin<format.chrg.cpMax) {
    if(++pages>10000||StartPage(dc)<=0){succeeded=false;break;}
    const LONG next=static_cast<LONG>(SendMessageW(clone,EM_FORMATRANGE,TRUE,reinterpret_cast<LPARAM>(&format)));
    if(next<=format.chrg.cpMin||EndPage(dc)<=0){succeeded=false;break;}
    format.chrg.cpMin=next;
  }
  SendMessageW(clone,EM_FORMATRANGE,FALSE,0);
  if(succeeded)succeeded=EndDoc(dc)>0;
  if(!succeeded)AbortDoc(dc);
  return succeeded;
}
static void NotesPrint(HWND dialog,ClientNotesDialogState &state) {
  if(!NotesEditorMatchesActive(state))return;
  std::string rtf;if(!StreamClientNotesOut(state.attachedEditor,rtf))return;
  CHARRANGE selection{};NotesSelection(state.attachedEditor,selection);
  const auto title=state.pages[state.activePage].name;
  struct PrintResources {
    PRINTDLGW choices{sizeof(PRINTDLGW)};HWND clone=nullptr;HMODULE richEdit=nullptr;
    ~PrintResources(){if(clone)DestroyWindow(clone);if(richEdit)FreeLibrary(richEdit);if(choices.hDC)DeleteDC(choices.hDC);if(choices.hDevMode)GlobalFree(choices.hDevMode);if(choices.hDevNames)GlobalFree(choices.hDevNames);}
  } print;
  print.richEdit=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!print.richEdit)return;
  print.clone=CreateWindowExW(WS_EX_NOACTIVATE,L"RICHEDIT50W",L"",WS_POPUP|ES_MULTILINE,0,0,1,1,nullptr,nullptr,g_hInst,nullptr);
  if(!print.clone)return;
  SendMessageW(print.clone,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
  NotesStream stream{rtf};EDITSTREAM source{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(print.clone,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&source));
  if(source.dwError||stream.offset!=stream.bytes.size())return;
  // The print copy uses readable black ink; headings, lists, boxes and
  // highlighting remain. The original editor and its Undo queue are untouched.
  CHARFORMAT2W ink{sizeof(ink)};ink.dwMask=CFM_COLOR;ink.crTextColor=RGB(0,0,0);
  SendMessageW(print.clone,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&ink));
  SendMessageW(print.clone,EM_SETBKGNDCOLOR,0,RGB(255,255,255));
  auto &choices=print.choices;choices.hwndOwner=dialog;
  choices.Flags=PD_RETURNDC|PD_USEDEVMODECOPIESANDCOLLATE|PD_NOPAGENUMS|PD_HIDEPRINTTOFILE|PD_NOWARNING;
  if(selection.cpMin==selection.cpMax)choices.Flags|=PD_NOSELECTION;
  if(!PrintDlgW(&choices)) {
    if(CommDlgExtendedError())MessageBoxW(dialog,L"The printer dialog could not be opened. Check that a printer or Microsoft Print to PDF is installed.",L"Print note",MB_OK|MB_ICONWARNING);
    return;
  }
  if(!choices.hDC)return;
  GETTEXTLENGTHEX measure{GTL_PRECISE|GTL_NUMCHARS,1200};const LONG end=static_cast<LONG>(SendMessageW(print.clone,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&measure),0));
  CHARRANGE range=(choices.Flags&PD_SELECTION)?selection:CHARRANGE{0,end};
  if(range.cpMin<range.cpMax&&!NotesPrintRange(print.clone,choices.hDC,range,title))MessageBoxW(IsWindow(dialog)?dialog:nullptr,L"Printing did not finish. Your note is unchanged.",L"Print note",MB_OK|MB_ICONWARNING);
}
