// Focused productivity dialogs and portable handover output.
static constexpr const wchar_t *kNotesStatuses[]={L"No status",L"Investigating",L"Waiting on client",L"Follow-up needed",L"Resolved"};
static std::wstring NotesTagsText(const client_notes::NotePage &page) {
  std::wstring text; for (const auto &tag:page.tags) { if (!text.empty()) text+=L", "; text+=tag; } return text;
}
static bool NotesParseTags(std::wstring text,std::vector<std::wstring> &tags) {
  tags.clear(); size_t pos=0;
  while (pos<text.size()) {
    const auto end=text.find(L',',pos); auto tag=text.substr(pos,end==std::wstring::npos ? end : end-pos);
    const auto first=tag.find_first_not_of(L" \t"),last=tag.find_last_not_of(L" \t");
    if (first!=std::wstring::npos) {
      tag=tag.substr(first,last-first+1);
      if (tag.size()>32 || !client_notes::ValidPageName(tag)) return false;
      if (std::none_of(tags.begin(),tags.end(),[&](const auto &prior){return client_notes::SamePageName(prior,tag);})) tags.push_back(tag);
      if (tags.size()>16) return false;
    }
    if (end==std::wstring::npos) break; pos=end+1;
  }
  return true;
}
static INT_PTR CALLBACK NotesMetadataProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
  auto *page=reinterpret_cast<client_notes::NotePage *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    page=reinterpret_cast<client_notes::NotePage *>(l); SetWindowLongPtrW(dialog,DWLP_USER,l); ApplyCleanupDialogTheme(dialog);
    for (const auto label:kNotesStatuses) SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_STATUS_PICK),NotesComboSubclass,1,0);
    const int itemHeight=ScaleByDpi(18,GetDpiForWindow(dialog));
    SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_SETITEMHEIGHT,-1,itemHeight);SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_SETITEMHEIGHT,0,itemHeight);
    SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,page->status,0); SetDlgItemTextW(dialog,IDC_NOTES_TAGS_EDIT,NotesTagsText(*page).c_str());
    SendDlgItemMessageW(dialog,IDC_NOTES_TAGS_EDIT,EM_SETLIMITTEXT,1024,0); return TRUE;
  }
  if (message==WM_DRAWITEM && l && DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(l))) return TRUE;
  if (const auto theme=HandleCleanupDialogTheme(dialog,message,w,l)) return *theme;
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(w)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (message==WM_COMMAND && LOWORD(w)==IDOK && page) {
    std::vector<std::wstring> tags; const auto status=SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_GETCURSEL,0,0);
    if (status<0 || status>4 || !NotesParseTags(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TAGS_EDIT)),tags)) {
      MessageBoxW(dialog,L"Use up to 16 comma-separated tags, each 1-32 characters. Avoid line breaks or control characters.",L"Tags",MB_OK|MB_ICONINFORMATION); return TRUE;
    }
    page->status=static_cast<uint32_t>(status); page->tags=std::move(tags); EndDialog(dialog,IDOK); return TRUE;
  }
  return FALSE;
}
static std::string NotesPlainRtf(const std::wstring &text) {
  std::string rtf="{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs24\\pard\\sb0\\sa0\\uc1 ";
  for (wchar_t c:text) {
    if (c==L'\r') continue;
    if (c==L'\n') rtf+="\\par "; else if (c==L'\t') rtf+="\\tab ";
    else if (c==L'\\' || c==L'{' || c==L'}') { rtf+='\\'; rtf+=static_cast<char>(c); }
    else if (c>=32 && c<127) rtf+=static_cast<char>(c);
    else rtf+="\\u"+std::to_string(static_cast<int16_t>(c))+"?";
  }
  return rtf+"}";
}
static std::wstring NotesHtmlEscape(std::wstring text) {
  std::wstring escaped;
  for (wchar_t ch:text) { switch(ch) {case L'&':escaped+=L"&amp;";break;case L'<':escaped+=L"&lt;";break;case L'>':escaped+=L"&gt;";break;case L'\"':escaped+=L"&quot;";break;case L'\'':escaped+=L"&#39;";break;default:escaped+=ch;} }
  return escaped;
}
static std::vector<std::pair<std::wstring,std::wstring>> NotesDocumentLinks(const client_notes::NotePage &page) {
  auto links=page.links;if (links.empty()) return links;
  if (!page.rich) return {};
  HWND edit=CreateWindowExW(0,L"RICHEDIT50W",L"",ES_MULTILINE,0,0,0,0,nullptr,nullptr,g_hInst,nullptr);
  if (!edit) return {};
  SendMessageW(edit,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
  NotesStream stream{page.rtf};EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&data));
  if (data.dwError || stream.offset!=stream.bytes.size()) links.clear();
  else if(page.linkRangesKnown) {
    if(!client_notes::ValidLinkRanges(page.linkRanges,true,page.links.size())) links.clear();
    else {
      GETTEXTLENGTHEX measure{GTL_PRECISE|GTL_NUMCHARS,1200};const auto length=SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&measure),0);
      for(const auto &range:page.linkRanges) if(range.last>static_cast<uint32_t>(length) ||
          NotesRangeText(edit,static_cast<LONG>(range.first),static_cast<LONG>(range.last))!=page.links[range.target].first) {links.clear();break;}
    }
  } else std::erase_if(links,[&](const auto &link){return !NotesLinkLabelExists(edit,link.first,true);});
  DestroyWindow(edit);return links;
}
static std::wstring NotesHandoverHtml(const ClientNotesDialogState &state,const std::vector<size_t> &indices) {
  std::wstring html=L"<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>Handover - "+NotesHtmlEscape(state.clientName)+L"</title><style>body{font:16px/1.55 Segoe UI,Arial,sans-serif;max-width:900px;margin:40px auto;padding:0 24px;color:#202124}h1{font-size:32px}h2{font-size:24px}article{border-top:1px solid #ccc;padding-top:20px;margin-top:28px;break-inside:auto}.details{color:#555;font-size:14px}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}@media print{body{margin:0;max-width:none}a{color:inherit}}</style><h1>Handover: "+NotesHtmlEscape(state.clientName)+L"</h1><p class=\"details\">Exported "+NotesHtmlEscape(NotesDate(client_notes::Now()))+L". "+std::to_wstring(indices.size())+L" selected notes.</p>";
  for (const auto index:indices) {
    if (index>=state.pages.size()) continue; const auto &p=state.pages[index];
    html+=L"<article><h2>"+NotesHtmlEscape(p.name)+L"</h2><p class=\"details\">"+NotesHtmlEscape(kNotesStatuses[p.status])+L" | "+(p.deleted ? L"Trash" : p.archived ? L"Archived" : L"Current note")+L"<br>Created: "+NotesHtmlEscape(NotesDate(p.created))+L" | Edited: "+NotesHtmlEscape(NotesDate(p.modified));
    if (!p.ticket.empty()) html+=L"<br>Ticket: "+NotesHtmlEscape(p.ticket);
    if (ValidNotesUrl(p.ticketUrl)) html+=L" <a href=\""+NotesHtmlEscape(p.ticketUrl)+L"\">Open ticket</a>";
    if (!p.tags.empty()) html+=L"<br>Tags: "+NotesHtmlEscape(NotesTagsText(p));
    html+=L"</p><pre>"+NotesHtmlEscape(NotesPlain(p))+L"</pre>";
    for (const auto &[label,url]:NotesDocumentLinks(p)) if (ValidNotesUrl(url)) html+=L"<p><a href=\""+NotesHtmlEscape(url)+L"\">"+NotesHtmlEscape(label)+L"</a></p>";
    html+=L"</article>";
  }
  return html+L"</html>";
}
static bool NotesExportHandover(HWND dialog,const ClientNotesDialogState &state,const std::vector<size_t> &indices) {
  if (indices.empty()) return false;
  wchar_t path[32768]=L"Handover.html"; OPENFILENAMEW file{sizeof(file)}; file.hwndOwner=dialog; file.lpstrFile=path; file.nMaxFile=static_cast<DWORD>(std::size(path));
  file.lpstrFilter=L"Readable handover (*.html)\0*.html\0\0"; file.lpstrDefExt=L"html"; file.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
  if (!GetSaveFileNameW(&file)) return false;
  std::string bytes;
  if (!client_notes::EncodeUtf8(NotesHandoverHtml(state,indices),bytes) || !client_notes::WriteAuxiliary(fs::path(path).parent_path(),fs::path(path).filename().wstring(),bytes)) {
    MessageBoxW(dialog,client_notes::PersistenceFailure(L"The handover could not be exported. Your notes were kept.").c_str(),L"Export handover",MB_OK|MB_ICONWARNING); return false;
  }
  MessageBoxW(dialog,L"Handover saved as an HTML document. Open it in a browser to read, print or save as PDF. It contains the selected notes, status, tags and ticket links; revision histories are excluded.",L"Handover exported",MB_OK|MB_ICONINFORMATION); return true;
}
struct NotesAction {UINT id;const wchar_t *name,*hint;bool needsNote=true;};
static constexpr NotesAction kNotesActions[]={
 {IDC_NOTES_TEMPLATE,L"New blank note",L"Start writing immediately; name the tab later.",false},
 {IDC_NOTES_LIBRARY,L"All notes / search and bulk actions",L"Find, sort, filter, select several, archive, restore and export notes.",false},
 {IDC_NOTES_TRASH,L"Trash",L"Restore deleted notes or permanently remove them. No automatic expiry.",false},
 {IDC_NOTES_METADATA,L"Status and tags",L"Organize this note by work status and comma-separated tags."},
 {IDC_NOTES_WORKLOG,L"Add work log update",L"Append a dated section to the end of this note and start writing."},
 {IDC_NOTES_SNIPPETS,L"Snippets - create, edit or insert",L"Write a new reusable block, edit saved blocks, or insert one at the cursor.",false},
 {IDC_NOTES_SAVE_SNIPPET,L"Save selection as snippet",L"Save selected words (or the whole note) as a shared reusable block."},
 {IDC_NOTES_TEMPLATES,L"Templates",L"Preview starters, create notes, or customize templates.",false},
 {IDC_NOTES_SAVE_TEMPLATE,L"Save note as template",L"Create a reusable starter shared across clients."},
 {IDC_NOTES_TICKET,L"Ticket details",L"Add an optional ticket number and browser URL."},
 {IDC_NOTES_OPEN_TICKET,L"Open ticket link",L"Open the linked ticket in your chosen regular PC browser, outside client profiles."},
 {IDC_NOTES_LINK_BROWSER,L"Link browser",L"Choose System default, Edge, Chrome, Brave or Firefox for all note and ticket links. Uses regular PC sign-in, outside client profiles.",false},
 {IDC_NOTES_CODE,L"Insert code block",L"Choose a language and insert a labeled, bordered box for code or log output."},
 {IDC_NOTES_HANDOVER,L"Export handover",L"Save this note as a readable, printable HTML document."},
 {IDC_NOTES_TRANSFER,L"Copy to another client",L"Choose a client from the dropdown; keep the original."},
 {IDC_NOTES_MOVE,L"Move to another client",L"Save in the destination and keep the source in trash."},
 {IDC_NOTES_FIND_TOGGLE,L"Find text",L"Search within this note (Ctrl+F)."},
 {IDC_NOTES_REPLACE,L"Find and replace",L"Replace text in this note (Ctrl+H)."},
 {IDC_NOTES_WORD_WRAP,L"Word wrap",L"Toggle wrapping for wide logs. Remembers your choice without changing the note."},
 {IDC_NOTES_GO_TO_LINE,L"Go to line",L"Jump to a logical line number (Ctrl+G), independent of wrapping."},
 {IDC_NOTES_STATISTICS,L"Document information",L"View word, character, line and selection counts."},
 {IDC_NOTES_PRINT,L"Print or save as PDF",L"Print this note or its selected text in readable black ink (Ctrl+P). Choose Microsoft Print to PDF to save a PDF."},
 {IDC_NOTES_SPELLING,L"Check spelling",L"Review spelling and choose corrections with installed Windows dictionaries (F7). Code and technical tokens are skipped."},
 {IDC_NOTES_MARKDOWN_IMPORT,L"Import Markdown",L"Preview a Markdown file and add it as a new note for this client.",false},
 {IDC_NOTES_MARKDOWN_EXPORT,L"Export Markdown",L"Save supported headings, emphasis, lists and links as a portable copy. Preview explains formatting limits; RTF and notebook exports preserve more."},
 {IDC_NOTES_TABLE,L"Insert table",L"Insert a simple editable table with a chosen number of rows and columns."},
 {IDC_NOTES_HISTORY,L"Version history",L"Preview and restore earlier document text."},
 {IDC_NOTES_DUPLICATE,L"Duplicate note",L"Create an independent copy of this note."},
 {IDC_NOTES_TAB_LEFT,L"Move tab left",L"Reorder this tab within its pinned or unpinned group. Ctrl+Shift+PgUp."},
 {IDC_NOTES_TAB_RIGHT,L"Move tab right",L"Reorder this tab within its pinned or unpinned group. Ctrl+Shift+PgDn."},
 {IDC_NOTES_IMPORT,L"Import notes",L"Preview and import text, RTF or a notebook.",false},
 {IDC_NOTES_EXPORT,L"Export this note",L"Save the document as TXT or RTF."},
 {IDC_NOTES_EXPORT_ALL,L"Export client notebook",L"Back up notes, metadata and history in a .ctn file.",false},
 {IDC_NOTES_RETRY,L"Retry saving",L"Retry after a reported save failure.",false},
 {IDC_NOTES_CONFLICT,L"Resolve save conflict",L"Compare saved notes or preserve drafts as recovered copies.",false},
 {IDC_NOTES_COPY_ALL,L"Copy entire note",L"Copy the note's text to the clipboard."},
 {IDC_NOTES_MORE_MENU,L"All formatting and window commands",L"Open the full menu for links, styles, zoom and window options.",false},
 {IDC_NOTES_HELP,L"Help",L"Open the quick Notes overview.",false},
 {IDC_NOTES_WALKTHROUGH,L"Notes walkthrough",L"Step-by-step guide with real screenshots.",false}
};
struct NotesActionsState {bool active=false;std::vector<size_t> results;};
static void NotesActionsFill(HWND dialog,NotesActionsState &state) {
  const auto query=NotesLower(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_QUERY))); state.results.clear(); SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_RESETCONTENT,0,0);
  for (size_t i=0;i<std::size(kNotesActions);++i) if ((!kNotesActions[i].needsNote || state.active) && NotesLower(std::wstring(kNotesActions[i].name)+L" "+kNotesActions[i].hint).find(query)!=std::wstring::npos) {
    const std::wstring label=std::wstring(kNotesActions[i].name)+(kNotesActions[i].id==IDC_NOTES_LINK_BROWSER ? L": "+NotesLinkBrowserLabel() : L"");
    state.results.push_back(i); SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
  }
  if (!state.results.empty()) SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,0,0);
  EnableWindow(GetDlgItem(dialog,IDOK),!state.results.empty()); SetDlgItemTextW(dialog,IDC_NOTES_INFO,state.results.empty() ? L"No matches. Try a feature name such as tags, snippet or export." : kNotesActions[state.results.front()].hint);
}
static INT_PTR CALLBACK NotesActionsProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
  auto *state=reinterpret_cast<NotesActionsState *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {state=reinterpret_cast<NotesActionsState *>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);ApplyCleanupDialogTheme(dialog);NotesActionsFill(dialog,*state);SetFocus(GetDlgItem(dialog,IDC_NOTES_QUERY));return FALSE;}
  if (const auto theme=HandleCleanupDialogTheme(dialog,message,w,l)) return *theme;
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(w)==IDCANCEL)) {EndDialog(dialog,0);return TRUE;}
  if (!state || message!=WM_COMMAND) return FALSE;
  if (LOWORD(w)==IDC_NOTES_QUERY && HIWORD(w)==EN_CHANGE) {NotesActionsFill(dialog,*state);return TRUE;}
  const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  if (row<0 || static_cast<size_t>(row)>=state->results.size()) return TRUE;
  const auto &action=kNotesActions[state->results[row]];
  if (LOWORD(w)==IDC_NOTES_RESULTS && HIWORD(w)==LBN_SELCHANGE) SetDlgItemTextW(dialog,IDC_NOTES_INFO,action.hint);
  if (LOWORD(w)==IDOK || (LOWORD(w)==IDC_NOTES_RESULTS && HIWORD(w)==LBN_DBLCLK)) EndDialog(dialog,action.id);
  return TRUE;
}
struct NotesQuickState {std::wstring client;fs::path root;client_notes::NotebookResult baseline;std::vector<client_notes::NotePage> pages;uint64_t id=0;};
static bool NotesQuickSave(HWND dialog,NotesQuickState &state) {
  const auto text=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_QUICK_TEXT)); auto title=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_QUICK_TITLE));
  if (!state.id && text.find_first_not_of(L" \t\r\n")==std::wstring::npos) return true;
  if (!title.empty() && !client_notes::ValidPageName(title)) { SetDlgItemTextW(dialog,IDC_NOTES_QUICK_HINT,L"Not saved: title must be 1-64 characters, without outer spaces."); return false; }
  auto pages=state.pages;
  if (!state.id && state.baseline.status==client_notes::ReadStatus::Missing) pages.clear();
  auto found=std::find_if(pages.begin(),pages.end(),[&](const auto &p){return p.id==state.id;});
  if (found==pages.end()) {
    if (pages.size()>=client_notes::kMaxPages) {SetDlgItemTextW(dialog,IDC_NOTES_QUICK_HINT,L"Notebook full. Keep this window open and copy your text.");return false;}
    client_notes::NotePage page; page.id=client_notes::NextId(pages); page.created=client_notes::Now(); page.open=false;
    pages.push_back(page); found=pages.end()-1;
  }
  if (title.empty()) title=L"Quick note";
  const auto base=title; unsigned suffix=2;
  while (std::any_of(pages.begin(),pages.end(),[&](const auto &p){return p.id!=found->id && client_notes::SamePageName(p.name,title);})) title=NotesNamePrefix(base,52)+L" "+std::to_wstring(suffix++);
  const auto rtf=NotesPlainRtf(text);
  if (found->name==title && found->rtf==rtf) return true;
  found->name=title; found->rich=true; found->rtf=rtf; found->modified=client_notes::Now();
  for (auto &p:pages) if (!p.rich) {p.rtf=NotesPlainRtf(p.text);p.text.clear();p.rich=true;}
  const auto id=found->id;
  if (!client_notes::WriteNotebook(state.root,pages,state.baseline,[&]{return RevalidateSafeClientContainerPath(state.client,state.root) && !IsClientArchived(state.client);})) {
    SetWindowTextW(dialog,(L"Quick note - "+state.client+L" - SAVE FAILED").c_str()); SetDlgItemTextW(dialog,IDC_NOTES_QUICK_HINT,client_notes::PersistenceFailure(L"Not saved. Keep this window open; retry closing or copy your text.").c_str());return false;
  }
#ifdef CTSPACES_NOTES_TEST_HOOKS
  CtSpacesNotesTestAfterCommit(state.root.c_str());
#endif
  state.id=id;state.pages=std::move(pages);state.baseline.status=client_notes::ReadStatus::Ok;
  state.baseline.pages=state.pages;state.baseline.notebook_snapshot.status=client_notes::ReadStatus::Ok;
  client_notes::SerializeNotebook(state.pages,state.baseline.notebook_snapshot.bytes);
  SetWindowTextW(dialog,(L"Quick note - "+state.client).c_str());SetDlgItemTextW(dialog,IDC_NOTES_QUICK_HINT,L"");return true;
}
static INT_PTR CALLBACK NotesQuickProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
  auto *state=reinterpret_cast<NotesQuickState *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {state=reinterpret_cast<NotesQuickState *>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);ApplyCleanupDialogTheme(dialog);SetWindowTextW(dialog,(L"Quick note - "+state->client).c_str());SendDlgItemMessageW(dialog,IDC_NOTES_QUICK_TEXT,EM_SETLIMITTEXT,32768,0);SendDlgItemMessageW(dialog,IDC_NOTES_QUICK_TITLE,EM_SETLIMITTEXT,64,0);SetFocus(GetDlgItem(dialog,IDC_NOTES_QUICK_TEXT));return FALSE;}
  if (const auto theme=HandleCleanupDialogTheme(dialog,message,w,l)) return *theme;
  if (!state) return FALSE;
  if (message==WM_COMMAND && HIWORD(w)==EN_CHANGE) {KillTimer(dialog,1);SetTimer(dialog,1,600,nullptr);return TRUE;}
  if (message==WM_TIMER && w==1) {KillTimer(dialog,1);NotesQuickSave(dialog,*state);return TRUE;}
  if (message==WM_CLOSE || (message==WM_COMMAND && (LOWORD(w)==IDCANCEL || LOWORD(w)==IDOK))) {KillTimer(dialog,1);if (NotesQuickSave(dialog,*state)) EndDialog(dialog,IDOK);else MessageBoxW(dialog,L"This quick note has not been saved. The draft remains open so you can copy it or try again.",L"Quick note",MB_OK|MB_ICONWARNING);return TRUE;}
  return FALSE;
}
static void ShowQuickCapture(const std::wstring &client) {
  fs::path root;
  if (!g_bUiEnabled || g_bClientNotesBusy.load() || !IsExistingClientProfile(client) || IsClientArchived(client) || !TryGetSafeClientProfilePath(client,root) || !RevalidateSafeClientContainerPath(client,root) || !PrepareNotesForClientChange(client)) return;
  NotesQuickState state{client,root,client_notes::ReadNotebook(root)};
  if (state.baseline.status==client_notes::ReadStatus::Error) {MessageBoxW(g_hGui,client_notes::PersistenceFailure(L"This client's notebook could not be read safely.").c_str(),L"Quick note",MB_OK|MB_ICONWARNING);return;}
  state.pages=state.baseline.pages;
  DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),g_hGui,NotesQuickProc,reinterpret_cast<LPARAM>(&state));
}
