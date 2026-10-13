// The template library owns its editor draft independently of client notes.
// Explicit Save/Use and guarded navigation keep failed or conflicting saves
// from silently replacing a reusable starter.
static void NotesTemplateEditorState(HWND dialog,NotesTemplates &library) {
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_SAVE_STATE,
      library.snippets ? (library.dirty ? L"Unsaved snippet changes" : library.draft.id ? L"Saved snippet - inserted copies stay unchanged" : L"New snippet - write here, then save or insert it") :
      library.dirty ? L"Unsaved template changes" : library.draft.id ? L"Saved template - edits do not change existing notes" : L"New template - write here, then save or use it");
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_RESTORE),library.editorValid && (library.dirty || !library.draft.id));
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_USE),library.editorValid && (!library.snippets || HasActiveNote(*library.workspace)));
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_DELETE),library.draft.id!=0);
}
static std::wstring NotesNewTemplateName(const NotesTemplates &library,const std::wstring &base) {
  std::wstring name=base;
  for (size_t suffix=2;std::any_of(library.pages.begin(),library.pages.end(),[&](const auto &p){return client_notes::SamePageName(p.name,name);});++suffix)
    name=base.substr(0,54)+L" "+std::to_wstring(suffix);
  return name;
}
static void NotesTemplateEditorFill(HWND dialog,NotesTemplates &library) {
  HWND list=GetDlgItem(dialog,IDC_NOTES_RESULTS);
  const bool visible=(GetWindowLongPtrW(list,GWL_STYLE)&WS_VISIBLE)!=0;
  if (visible) SendMessageW(list,WM_SETREDRAW,FALSE,0);
  SendMessageW(list,LB_RESETCONTENT,0,0);
  library.selected=-1;
  for (size_t i=0;i<library.pages.size();++i) {
    SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(library.pages[i].name.c_str()));
    if (library.pages[i].id==library.draft.id) library.selected=static_cast<int>(i);
  }
  SendMessageW(list,LB_SETCURSEL,library.selected,0);
  if (visible) SendMessageW(list,WM_SETREDRAW,TRUE,0);
  InvalidateRect(list,nullptr,FALSE);
}
static bool NotesTemplateEditorLoad(HWND dialog,NotesTemplates &library,client_notes::NotePage page) {
  HWND edit=GetDlgItem(dialog,IDC_NOTES_PREVIEW);
  library.loading=true;
  SendMessageW(edit,WM_SETREDRAW,FALSE,0);
  const auto prior=library.draft;
  library.draft=std::move(page);
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,library.draft.name.c_str());
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,library.draft.ticket.c_str());
  SetWindowTextW(edit,L"");
  CHARFORMAT2W normal{sizeof(normal)};
  normal.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE|CFM_STRIKEOUT|CFM_LINK|CFM_COLOR|CFM_BACKCOLOR;
  normal.dwEffects=CFE_AUTOBACKCOLOR;normal.crTextColor=g_themeColors.crControlText;normal.yHeight=240;wcscpy_s(normal.szFaceName,L"Segoe UI");
  SendMessageW(edit,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&normal));
  PARAFORMAT2 paragraph{sizeof(paragraph)};paragraph.dwMask=PFM_NUMBERING|PFM_STARTINDENT|PFM_OFFSET|PFM_ALIGNMENT|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;
  paragraph.wAlignment=PFA_LEFT;paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  NotesStream stream{library.draft.rich ? library.draft.rtf : NotesPlainRtf(library.draft.text)};
  EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&data));
  ApplyNotesTextColors(edit);
  SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);
  SendMessageW(edit,EM_SETMODIFY,FALSE,0);
  SendMessageW(edit,WM_SETREDRAW,TRUE,0);
  InvalidateRect(edit,nullptr,FALSE);
  library.loading=false; library.dirty=false;library.editorValid=!data.dwError;
  SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETCURSEL,0,0);
  NotesTemplateEditorFill(dialog,library); NotesTemplateEditorState(dialog,library);
  if (data.dwError) {
    library.draft=prior;
    MessageBoxW(dialog,L"This template could not be loaded completely. Close this window and reopen the library before saving.",L"Templates",MB_OK|MB_ICONWARNING);
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_RESTORE),FALSE); EnableWindow(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_USE),FALSE);
    return false;
  }
  return true;
}
static bool NotesTemplateEditorSave(HWND dialog,NotesTemplates &library) {
  if (!library.editorValid) return false;
  library.dirty=library.dirty || SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETMODIFY,0,0)!=0;
  if (!library.dirty && library.draft.id) return true;
  auto page=library.draft;
  page.name=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_NAME));
  page.ticket=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_HINT));
  if (!client_notes::ValidPageName(page.name) || page.ticket.size()>128 ||
      std::any_of(library.pages.begin(),library.pages.end(),[&](const auto &p){return p.id!=page.id && client_notes::SamePageName(p.name,page.name);})) {
    MessageBoxW(dialog,L"Use a unique name of 1-64 characters and a hint of up to 128 characters.",library.snippets ? L"Snippet details" : L"Template details",MB_OK|MB_ICONINFORMATION);
    SetFocus(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_NAME)); return false;
  }
  HWND edit=GetDlgItem(dialog,IDC_NOTES_PREVIEW);
  bool exported=false;
  if (NotesPlainTextWithinLimit(edit)) {
    if (library.snippets) {
      // A whole-document RTF export includes a terminal paragraph that is not
      // part of the visible text. Export the real range so inserting a snippet
      // does not add an unexpected line break after an inline block.
      CHARRANGE prior{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&prior));
      GETTEXTLENGTHEX length{GTL_NUMCHARS|GTL_PRECISE,1200};
      CHARRANGE all{0,static_cast<LONG>(SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0))};
      SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
      NotesStream stream;EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamOut};
      SendMessageW(edit,EM_STREAMOUT,SF_RTF|SFF_SELECTION,reinterpret_cast<LPARAM>(&data));
      SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&prior));
      exported=!data.dwError && !stream.overflow && client_notes::NormalizeEditorExport(stream.bytes);
      if (exported) page.rtf=std::move(stream.bytes);
    } else exported=StreamClientNotesOut(edit,page.rtf);
  }
  if (!exported) {
    MessageBoxW(dialog,L"This reusable entry is too large to save safely. Shorten its content and try again. Your draft stays open.",library.snippets ? L"Save snippet" : L"Save template",MB_OK|MB_ICONWARNING); return false;
  }
  page.rich=true;page.text.clear();page.modified=client_notes::Now();
  auto pages=library.pages;
  auto existing=std::find_if(pages.begin(),pages.end(),[&](const auto &p){return p.id==page.id;});
  if (!page.id) {
    if (pages.size()>=client_notes::kMaxLibraryPages) {
      MessageBoxW(dialog,L"This shared library has 256 entries. Remove an unused entry first. Your draft stays open.",library.snippets ? L"Snippets limit" : L"Templates limit",MB_OK|MB_ICONINFORMATION);return false;
    }
    page.id=client_notes::NextId(pages);page.created=page.modified;pages.push_back(page);
  } else if (existing!=pages.end()) *existing=page;
  else return false;
  if (!NotesSaveTemplates(dialog,library,pages)) return false;
  library.draft=page;library.dirty=false;
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETMODIFY,FALSE,0);
  NotesTemplateEditorFill(dialog,library);NotesTemplateEditorState(dialog,library);return true;
}
static bool NotesTemplateEditorLeave(HWND dialog,NotesTemplates &library) {
  library.dirty=library.dirty || SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETMODIFY,0,0)!=0;
  if (!library.dirty) return true;
  const int answer=MessageBoxW(dialog,L"Save your changes before continuing?\n\nYes saves this reusable entry. No discards these edits. Cancel keeps the editor open.",library.snippets ? L"Unsaved snippet changes" : L"Unsaved template changes",MB_YESNOCANCEL|MB_ICONQUESTION);
  return answer==IDNO || (answer==IDYES && NotesTemplateEditorSave(dialog,library));
}
static void LayoutNotesTemplateEditor(HWND dialog) {
  RECT area{};GetClientRect(dialog,&area);const UINT dpi=GetDpiForWindow(dialog);
  const int m=ScaleByDpi(20,dpi),left=ScaleByDpi(258,dpi),bottom=area.bottom-ScaleByDpi(52,dpi);
  HDWP positions=BeginDeferWindowPos(20);
  auto place=[&](int id,int x,int y,int w,int h) {
    HWND control=GetDlgItem(dialog,id);RECT old{};GetWindowRect(control,&old);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&old),2);
    if (old.left==x && old.top==y && old.right-old.left==w && old.bottom-old.top==h) return;
    if (positions) positions=DeferWindowPos(positions,control,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    else SetWindowPos(control,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
  };
  place(IDC_NOTES_LIBRARY_HEADING,m,ScaleByDpi(12,dpi),area.right-2*m,ScaleByDpi(25,dpi));
  place(IDC_NOTES_LABEL1,m,ScaleByDpi(48,dpi),left-m-ScaleByDpi(20,dpi),ScaleByDpi(22,dpi));
  place(IDC_NOTES_RESULTS,m,ScaleByDpi(78,dpi),left-m-ScaleByDpi(20,dpi),bottom-ScaleByDpi(108,dpi));
  const int width=area.right-left-m;
  place(IDC_NOTES_LABEL2,left,ScaleByDpi(48,dpi),width,ScaleByDpi(22,dpi));
  place(IDC_NOTES_TEMPLATE_NAME,left,ScaleByDpi(74,dpi),width,ScaleByDpi(32,dpi));
  place(IDC_NOTES_INFO,left,ScaleByDpi(116,dpi),width,ScaleByDpi(22,dpi));
  place(IDC_NOTES_TEMPLATE_HINT,left,ScaleByDpi(142,dpi),width,ScaleByDpi(32,dpi));
  int x=left;
  for (const auto [id,w] : {std::pair{IDC_NOTES_STYLE,140},{IDC_NOTES_BOLD,66},{IDC_NOTES_ITALIC,66},{IDC_NOTES_UNDERLINE,90},{IDC_NOTES_BULLETS,72}}) {
    place(id,x,ScaleByDpi(188,dpi),ScaleByDpi(w,dpi),ScaleByDpi(id==IDC_NOTES_STYLE ? 180 : 32,dpi));x+=ScaleByDpi(w+8,dpi);
  }
  place(IDC_NOTES_PREVIEW,left,ScaleByDpi(230,dpi),width,bottom-ScaleByDpi(264,dpi));
  place(IDC_NOTES_TEMPLATE_SAVE_STATE,left,bottom-ScaleByDpi(27,dpi),width,ScaleByDpi(22,dpi));
  x=m;
  for (const auto [id,w] : {std::pair{IDC_NOTES_ARCHIVE,120},{IDC_NOTES_SAVE_TEMPLATE,140},{IDC_NOTES_DELETE,132}}) {
    place(id,x,bottom,ScaleByDpi(w,dpi),ScaleByDpi(32,dpi));x+=ScaleByDpi(w+8,dpi);
  }
  x=area.right-m-ScaleByDpi(372,dpi);
  for (const auto [id,w] : {std::pair{IDC_NOTES_RESTORE,132},{IDC_NOTES_TEMPLATE_USE,132},{IDCANCEL,92}}) {
    place(id,x,bottom,ScaleByDpi(w,dpi),ScaleByDpi(32,dpi));x+=ScaleByDpi(w+8,dpi);
  }
  if (positions) EndDeferWindowPos(positions);
  RECT text{};GetClientRect(GetDlgItem(dialog,IDC_NOTES_PREVIEW),&text);InflateRect(&text,-ScaleByDpi(16,dpi),-ScaleByDpi(12,dpi));
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&text));
  InvalidateRect(dialog,nullptr,FALSE);
}
static void RefreshNotesTemplateEditorTheme(HWND dialog,NotesTemplates &library) {
  HWND edit=GetDlgItem(dialog,IDC_NOTES_PREVIEW);
  SendMessageW(edit,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
  const bool loading=library.loading;library.loading=true;
  const auto modified=SendMessageW(edit,EM_GETMODIFY,0,0);
  ApplyNotesTextColors(edit);library.loading=loading;
  SendMessageW(edit,EM_SETMODIFY,modified,0);
  // Do not reload the saved template over a user's unsaved draft.
}
static LRESULT CALLBACK NotesTemplateTextSubclass(HWND edit,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR) {
  HWND dialog=GetParent(edit);
  if (message==WM_SETFONT) return 0;
  if (message==WM_PASTE) {NotesPastePlainText(edit);return 0;}
  if (message==WM_KEYDOWN && wParam==VK_INSERT && (GetKeyState(VK_SHIFT)&0x8000)) {NotesPastePlainText(edit);return 0;}
  if (message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)) {
    if (wParam=='V') {NotesPastePlainText(edit);return 0;}
    UINT command=0;
    switch (wParam) {
    case 'B': command=IDC_NOTES_BOLD;break;
    case 'I': command=IDC_NOTES_ITALIC;break;
    case 'U': command=IDC_NOTES_UNDERLINE;break;
    case 'S': command=IDC_NOTES_RESTORE;break;
    }
    if (command) {SendMessageW(dialog,WM_COMMAND,command,0);return 0;}
  }
  if (message==WM_NCDESTROY) RemoveWindowSubclass(edit,NotesTemplateTextSubclass,id);
  return DefSubclassProc(edit,message,wParam,lParam);
}
static INT_PTR CALLBACK NotesTemplateEditorProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *library=reinterpret_cast<NotesTemplates *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    library=reinterpret_cast<NotesTemplates *>(lParam);SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    SetWindowLongPtrW(dialog,GWL_STYLE,GetWindowLongPtrW(dialog,GWL_STYLE)|WS_CLIPCHILDREN);
    ApplyCleanupDialogTheme(dialog);
    if (library->snippets) {
      SetWindowTextW(dialog,L"Snippets - create, edit or insert");
      SetDlgItemTextW(dialog,IDC_NOTES_LIBRARY_HEADING,L"Reusable text blocks - shared across clients");
      SetDlgItemTextW(dialog,IDC_NOTES_LABEL1,L"Saved snippets");
      SetDlgItemTextW(dialog,IDC_NOTES_LABEL2,L"Snippet name");
      SetDlgItemTextW(dialog,IDC_NOTES_ARCHIVE,L"New snippet");
      SetDlgItemTextW(dialog,IDC_NOTES_SAVE_TEMPLATE,L"From selection");
      SetDlgItemTextW(dialog,IDC_NOTES_DELETE,L"Delete snippet");
      SetDlgItemTextW(dialog,IDC_NOTES_RESTORE,L"Save snippet");
      SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_USE,L"Insert in note");
      SetDlgItemTextW(dialog,IDC_NOTES_INFO,L"Hint (optional) - when to use this block");
    }
    // Match the Notes workspace's readable UI type rather than the smaller
    // resource-dialog default. The owner outlives this modal editor.
    if (library->workspace->uiFont) EnumChildWindows(dialog,[](HWND control,LPARAM font)->BOOL {
      SendMessageW(control,WM_SETFONT,font,FALSE);return TRUE;
    },reinterpret_cast<LPARAM>(library->workspace->uiFont));
    SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETITEMHEIGHT,0,ScaleByDpi(26,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETITEMHEIGHT,-1,ScaleByDpi(26,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETITEMHEIGHT,0,ScaleByDpi(26,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_TEMPLATE_NAME,EM_LIMITTEXT,64,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_TEMPLATE_HINT,EM_LIMITTEXT,128,0);
    HWND edit=GetDlgItem(dialog,IDC_NOTES_PREVIEW);
    SetWindowSubclass(edit,NotesTemplateTextSubclass,1,0);
    SendMessageW(edit,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
    SendMessageW(edit,EM_SETOLECALLBACK,0,reinterpret_cast<LPARAM>(&g_notesOleCallback));
    SendMessageW(edit,EM_SETEVENTMASK,0,ENM_CHANGE);
    SendMessageW(edit,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
    for (const auto text : {L"Normal text",L"Heading",L"Subheading"}) SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_STYLE),NotesComboSubclass,1,0);
    SetWindowTheme(GetDlgItem(dialog,IDC_NOTES_STYLE),L"",L"");
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE),HasActiveNote(*library->workspace));
    SetWindowPos(dialog,nullptr,0,0,ScaleByDpi(1000,GetDpiForWindow(dialog)),ScaleByDpi(720,GetDpiForWindow(dialog)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutNotesTemplateEditor(dialog);
    client_notes::NotePage page=library->pages.empty() ? client_notes::NotePage{library->snippets ? L"Untitled snippet" : L"Untitled template",true,NotesPlainRtf(L""),{}} : library->pages.front();
    if (library->startFromNote && HasActiveNote(*library->workspace)) {
      page=library->snippets ? NotesSnippetFromSelection(library->owner,library->workspace->pages[library->workspace->activePage]) : NotesTemplateFromPage(library->workspace->pages[library->workspace->activePage]);
      page.id=0;page.name=NotesNewTemplateName(*library,page.name);
    }
    NotesTemplateEditorLoad(dialog,*library,std::move(page));
    if (library->startFromNote) {library->dirty=true;NotesTemplateEditorState(dialog,*library);}
    SetFocus(GetDlgItem(dialog,IDC_NOTES_RESULTS));return FALSE;
  }
  if (message==WM_ERASEBKGND) return TRUE;
  if (message==WM_PAINT) {
    PAINTSTRUCT paint{};HDC target=BeginPaint(dialog,&paint);RECT area{};GetClientRect(dialog,&area);
    {
      NotesPaintBuffer buffer(target,area);FillRect(buffer.dc(),&area,g_hbrThemeWindow);
      for (int id : {IDC_NOTES_RESULTS,IDC_NOTES_TEMPLATE_NAME,IDC_NOTES_TEMPLATE_HINT,IDC_NOTES_PREVIEW}) {
        RECT bounds{};GetWindowRect(GetDlgItem(dialog,id),&bounds);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&bounds),2);
        // Keep a pixel of space outside single-line Edit's repaint bounds.
        const int inset=(id==IDC_NOTES_TEMPLATE_NAME || id==IDC_NOTES_TEMPLATE_HINT) ? 2 : 1;
        InflateRect(&bounds,inset,inset);FrameRect(buffer.dc(),&bounds,g_hbrThemeBorder);
      }
    }
    EndPaint(dialog,&paint);return TRUE;
  }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (!library) return FALSE;
  if (message==WM_SIZE) {LayoutNotesTemplateEditor(dialog);return TRUE;}
  if (message==WM_GETMINMAXINFO) {auto *limits=reinterpret_cast<MINMAXINFO *>(lParam);limits->ptMinTrackSize={ScaleByDpi(900,GetDpiForWindow(dialog)),ScaleByDpi(560,GetDpiForWindow(dialog))};return TRUE;}
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) {
    if (NotesTemplateEditorLeave(dialog,*library)) EndDialog(dialog,IDCANCEL);return TRUE;
  }
  if (message!=WM_COMMAND) return FALSE;
  const UINT id=LOWORD(wParam),notice=HIWORD(wParam);HWND edit=GetDlgItem(dialog,IDC_NOTES_PREVIEW);
  if ((id==IDC_NOTES_PREVIEW || id==IDC_NOTES_TEMPLATE_NAME || id==IDC_NOTES_TEMPLATE_HINT) && notice==EN_CHANGE) {
    if (!library->loading) {library->dirty=true;NotesTemplateEditorState(dialog,*library);}return TRUE;
  }
  if (id==IDC_NOTES_RESTORE) {NotesTemplateEditorSave(dialog,*library);return TRUE;}
  if (id==IDC_NOTES_RESULTS && notice==LBN_SELCHANGE) {
    const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
    if (row>=0 && static_cast<size_t>(row)<library->pages.size()) {
      if (NotesTemplateEditorLeave(dialog,*library)) NotesTemplateEditorLoad(dialog,*library,library->pages[row]);
      else SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,library->selected,0);
    }
    return TRUE;
  }
  if (id==IDC_NOTES_ARCHIVE || id==IDC_NOTES_SAVE_TEMPLATE) {
    if (id==IDC_NOTES_SAVE_TEMPLATE && !HasActiveNote(*library->workspace)) return TRUE;
    if (!NotesTemplateEditorLeave(dialog,*library)) return TRUE;
    auto page=id==IDC_NOTES_SAVE_TEMPLATE ? (library->snippets ? NotesSnippetFromSelection(library->owner,library->workspace->pages[library->workspace->activePage]) : NotesTemplateFromPage(library->workspace->pages[library->workspace->activePage])) : client_notes::NotePage{library->snippets ? L"Untitled snippet" : L"Untitled template",true,NotesPlainRtf(L""),{}};
    if (page.rich && page.rtf.empty()) {MessageBoxW(dialog,L"The selected text could not be captured safely. Select a smaller block and try again.",L"Reusable content",MB_OK|MB_ICONWARNING);return TRUE;}
    page.id=0;page.name=NotesNewTemplateName(*library,page.name);
    NotesTemplateEditorLoad(dialog,*library,std::move(page));library->dirty=true;NotesTemplateEditorState(dialog,*library);
    SetFocus(id==IDC_NOTES_SAVE_TEMPLATE ? GetDlgItem(dialog,IDC_NOTES_TEMPLATE_NAME) : edit);return TRUE;
  }
  if (id==IDC_NOTES_TEMPLATE_USE || id==IDOK || (id==IDC_NOTES_RESULTS && notice==LBN_DBLCLK)) {
    if (library->snippets && !HasActiveNote(*library->workspace)) return TRUE;
    if (!NotesTemplateEditorSave(dialog,*library)) return TRUE;
    auto page=NotesTemplateFromPage(library->draft);
    if (library->snippets) {
      if (NotesInsertSnippet(dialog,*library,page)) EndDialog(dialog,IDOK);return TRUE;
    }
    if (NotesAddPage(library->owner,*library->workspace,std::move(page))) EndDialog(dialog,IDOK);return TRUE;
  }
  if (id==IDC_NOTES_DELETE && library->draft.id) {
    if (MessageBoxW(dialog,library->snippets ? L"Delete this saved snippet and discard edits to it? Previously inserted copies stay unchanged." : L"Delete this saved template and discard edits to it? Notes already created from it will stay unchanged.",library->snippets ? L"Delete snippet" : L"Delete template",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return TRUE;
    auto pages=library->pages;std::erase_if(pages,[&](const auto &p){return p.id==library->draft.id;});
    if (NotesSaveTemplates(dialog,*library,pages)) {
      auto page=pages.empty() ? client_notes::NotePage{library->snippets ? L"Untitled snippet" : L"Untitled template",true,NotesPlainRtf(L""),{}} : pages[(std::min)(static_cast<size_t>((std::max)(0,library->selected)),pages.size()-1)];
      NotesTemplateEditorLoad(dialog,*library,std::move(page));
    }
    return TRUE;
  }
  if (id==IDC_NOTES_STYLE && (notice==CBN_SELCHANGE ||
      (notice==CBN_SELENDOK && SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_GETCURSEL,0,0)==0))) {
    const auto choice=SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_GETCURSEL,0,0);
    CHARFORMAT2W format{sizeof(format)};format.dwMask=CFM_SIZE|CFM_FACE|CFM_BOLD;
    format.yHeight=choice==1 ? 480 : choice==2 ? 360 : 240;format.dwEffects=choice ? CFE_BOLD : 0;wcscpy_s(format.szFaceName,L"Segoe UI");
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    if (choice==0) {
      PARAFORMAT2 paragraph{sizeof(paragraph)};paragraph.dwMask=PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;
      paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
    }
  } else if (id==IDC_NOTES_BOLD || id==IDC_NOTES_ITALIC || id==IDC_NOTES_UNDERLINE) {
    CHARFORMAT2W format{sizeof(format)};SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    const DWORD mask=id==IDC_NOTES_BOLD ? CFM_BOLD : id==IDC_NOTES_ITALIC ? CFM_ITALIC : CFM_UNDERLINE;
    const DWORD effect=id==IDC_NOTES_BOLD ? CFE_BOLD : id==IDC_NOTES_ITALIC ? CFE_ITALIC : CFE_UNDERLINE;
    format.dwMask=mask;format.dwEffects=(format.dwEffects&effect) ? 0 : effect;
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  } else if (id==IDC_NOTES_BULLETS) {
    PARAFORMAT2 p{sizeof(p)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));
    p.dwMask=PFM_NUMBERING|PFM_STARTINDENT|PFM_OFFSET;p.wNumbering=p.wNumbering ? 0 : PFN_BULLET;p.dxStartIndent=p.wNumbering ? 360 : 0;p.dxOffset=p.wNumbering ? 240 : 0;
    SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));
  } else return FALSE;
  library->dirty=true;NotesTemplateEditorState(dialog,*library);SetFocus(edit);return TRUE;
}
