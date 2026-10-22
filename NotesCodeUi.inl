// Editable native RTF tables keep code separate from prose and survive notebook,
// history, snippet and RTF export round trips without a second rendering engine.
static constexpr const wchar_t *kNotesCodeLanguages[]={
  L"Plain text",L"PowerShell",L"Command prompt / Batch",L"Bash / Shell",L"Python",
  L"JavaScript",L"TypeScript",L"SQL",L"JSON",L"YAML",L"XML",L"HTML",L"CSS",
  L"C#",L"C / C++",L"Java",L"Go",L"Rust",L"INI / Configuration",L"Log output"
};
static constexpr size_t kNotesCodeLimit=128*1024;
struct NotesCodeInput {int language=0;std::wstring source;HFONT font=nullptr;};
static INT_PTR CALLBACK NotesCodeProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
  auto *input=reinterpret_cast<NotesCodeInput *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    input=reinterpret_cast<NotesCodeInput *>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);ApplyCleanupDialogTheme(dialog);
    for (const auto label:kNotesCodeLanguages) SendDlgItemMessageW(dialog,IDC_NOTES_CODE_LANGUAGE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    HWND combo=GetDlgItem(dialog,IDC_NOTES_CODE_LANGUAGE);SetWindowSubclass(combo,NotesComboSubclass,1,0);
    const auto dpi=GetDpiForWindow(dialog);const int row=ScaleByDpi(26,dpi);
    SendMessageW(combo,CB_SETITEMHEIGHT,-1,row);SendMessageW(combo,CB_SETITEMHEIGHT,0,row);
    SendMessageW(combo,CB_SETCURSEL,input->language,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_CODE_SOURCE,EM_SETLIMITTEXT,kNotesCodeLimit,0);
    SetDlgItemTextW(dialog,IDC_NOTES_CODE_SOURCE,input->source.c_str());
    input->font=CreateFontW(-MulDiv(11,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
    if (input->font) SendDlgItemMessageW(dialog,IDC_NOTES_CODE_SOURCE,WM_SETFONT,reinterpret_cast<WPARAM>(input->font),TRUE);
    EnableWindow(GetDlgItem(dialog,IDOK),!input->source.empty());return TRUE;
  }
  if (message==WM_DRAWITEM && l && DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(l))) return TRUE;
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,w,l)) return *themed;
  if (message==WM_DESTROY && input && input->font) {DeleteObject(input->font);input->font=nullptr;}
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(w)==IDCANCEL)) {EndDialog(dialog,IDCANCEL);return TRUE;}
  if (message==WM_COMMAND && input) {
    if (LOWORD(w)==IDC_NOTES_CODE_SOURCE && HIWORD(w)==EN_CHANGE) {EnableWindow(GetDlgItem(dialog,IDOK),GetWindowTextLengthW(GetDlgItem(dialog,IDC_NOTES_CODE_SOURCE))>0);return TRUE;}
    if (LOWORD(w)==IDOK) {
      const auto selection=SendDlgItemMessageW(dialog,IDC_NOTES_CODE_LANGUAGE,CB_GETCURSEL,0,0);
      const auto source=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_CODE_SOURCE));
      if (selection<0 || selection>=static_cast<LRESULT>(std::size(kNotesCodeLanguages)) || source.empty() || source.size()>kNotesCodeLimit) return TRUE;
      input->language=static_cast<int>(selection);input->source=source;EndDialog(dialog,IDOK);return TRUE;
    }
  }
  return FALSE;
}
static std::string NotesCodeRtfText(const std::wstring &text) {
  std::string rtf;
  for (size_t i=0;i<text.size();++i) {
    const wchar_t c=text[i];
    if (c==L'\r' || c==L'\n') {if(c==L'\r' && i+1<text.size() && text[i+1]==L'\n') ++i;rtf+="\\line ";}
    else if (c==L'\t') rtf+="\\tab ";
    else if (c==L'\\' || c==L'{' || c==L'}') {rtf+='\\';rtf+=static_cast<char>(c);}
    else if (c>=32 && c<127) rtf+=static_cast<char>(c);
    else rtf+="\\u"+std::to_string(static_cast<int16_t>(c))+"?";
  }
  return rtf;
}
static std::string NotesCodeRtf(const NotesCodeInput &input) {
  if (input.language<0 || input.language>=static_cast<int>(std::size(kNotesCodeLanguages)) || input.source.empty() || input.source.size()>kNotesCodeLimit) return {};
  // Automatic text/background follow the Notes theme. A neutral gray frame has
  // contrast in both light and dark themes; fixed cell width never mutates the
  // document during window resizing. Long source lines wrap within the box.
  const std::string row="\\trowd\\trgaph160\\trleft0\\clbrdrt\\brdrs\\brdrw12\\brdrcf1\\clbrdrl\\brdrs\\brdrw12\\brdrcf1\\clbrdrb\\brdrs\\brdrw12\\brdrcf1\\clbrdrr\\brdrs\\brdrw12\\brdrcf1\\cellx7200";
  return "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}{\\colortbl;\\red128\\green128\\blue128;}\\uc1\\pard\\plain\\f0\\fs28\\par "+row+
    "\\pard\\intbl\\plain\\f0\\fs22\\b\\cf0 "+NotesCodeRtfText(kNotesCodeLanguages[input.language])+" - Code\\b0\\cell\\row "+row+
    "\\pard\\intbl\\plain\\f1\\fs24\\cf0 "+NotesCodeRtfText(input.source)+"\\cell\\row\\pard\\plain\\f0\\fs28\\cf0\\par }";
}
static bool NotesInsertCode(HWND dialog,ClientNotesDialogState &state,const NotesCodeInput &input) {
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  if (paragraph.wEffects&PFE_TABLE) {
    MessageBoxW(dialog,L"Move the cursor outside the existing code box before inserting another. You can edit its code directly inside the box.",L"Code block",MB_OK|MB_ICONINFORMATION);return false;
  }
  const auto rtf=NotesCodeRtf(input);if (!client_notes::ValidRichText(rtf)) return false;
  CHARRANGE selected{};NotesSelection(edit,selected);
  const auto result=NotesApplyNativeTransaction(dialog,state,selected,[&](HWND target) {
    NotesStream stream{rtf};EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
    SendMessageW(target,EM_STREAMIN,SF_RTF|SFF_SELECTION,reinterpret_cast<LPARAM>(&data));
    const bool complete=!data.dwError && stream.offset==stream.bytes.size();
    if(complete) ApplyNotesTextColors(target);
    return complete;
  });
  if(result!=NotesAppTransactionResult::Committed) {
    NotesAppTransactionNotice(dialog,result,L"Code could not be inserted completely. Your previous draft is still open.",L"Code block");return false;
  }
  MarkNotesChanged(dialog,state);SaveClientNotesDraft(dialog,state,true);SetFocus(edit);return true;
}
static void NotesCreateCodeBlock(HWND dialog,ClientNotesDialogState &state) {
  if (!HasActiveNote(state)) return;
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);CHARRANGE selection{};NotesSelection(edit,selection);
  if (selection.cpMax-selection.cpMin>static_cast<LONG>(kNotesCodeLimit)) {MessageBoxW(dialog,L"Select at most 128,000 characters of code for one block.",L"Code block",MB_OK|MB_ICONINFORMATION);return;}
  NotesCodeInput input;
  if (selection.cpMax>selection.cpMin) {
    input.source.resize(static_cast<size_t>(selection.cpMax-selection.cpMin)+1);
    TEXTRANGEW range{selection,input.source.data()};const auto count=SendMessageW(edit,EM_GETTEXTRANGE,0,reinterpret_cast<LPARAM>(&range));
    input.source.resize(static_cast<size_t>((std::max)(LRESULT{0},count)));
  }
  if (DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CODE_BLOCK),dialog,NotesCodeProc,reinterpret_cast<LPARAM>(&input))==IDOK) NotesInsertCode(dialog,state,input);
  UpdateNotesToolbar(dialog,state);
}
