// Included from NotesWorkspaceUi.inl after NotesInsertSnippet, NotesPickFile and
// NotesWriteExport, before NotesWorkspaceCommand. It adds no permanent chrome.
#include "NotesMarkdown.h"
#include <wrl/client.h>
struct NotesMarkdownContext {
  HWND dialog=nullptr;
  ClientNotesDialogState *state=nullptr;
  std::wstring client;
  fs::path root;
  client_notes::DirectoryIdentity directory;
  uint64_t note=0;
  NotesMarkdownContext(HWND owner,ClientNotesDialogState &workspace)
      :dialog(owner),state(&workspace),client(workspace.clientName),root(workspace.clientRoot),
       directory(workspace.original.directory_identity),note(HasActiveNote(workspace)?workspace.pages[workspace.activePage].id:0){}
  bool Valid()const {
    if(!dialog || !IsWindow(dialog) || g_hNotesWindow!=dialog ||
       reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(dialog,DWLP_USER))!=state)return false;
    return state->clientName==client && state->clientRoot==root && state->original.directory_identity==directory &&
      (HasActiveNote(*state)?state->pages[state->activePage].id:0)==note;
  }
  bool Check()const {
    if(Valid())return true;
    MessageBoxW(IsWindow(dialog)?dialog:g_hGui,L"The Notes client or active note changed while this dialog was open. Nothing was imported or exported. Retry from the intended client and note.",L"Notes changed",MB_OK|MB_ICONINFORMATION);return false;
  }
};
static bool NotesMarkdownPick(HWND dialog,bool save,std::wstring &path) {
  wchar_t buffer[32768]{};if(save)wcscpy_s(buffer,L"Note.md");
  OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=dialog;picker.lpstrFile=buffer;
  picker.nMaxFile=static_cast<DWORD>(std::size(buffer));
  picker.lpstrFilter=L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0\0";
  picker.lpstrDefExt=L"md";picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
  if(!(save?GetSaveFileNameW(&picker):GetOpenFileNameW(&picker)))return false;
  path=buffer;return true;
}
static bool NotesMarkdownPage(const notes_markdown::Rendered &rendered,std::wstring name,client_notes::NotePage &page,std::wstring &error) {
  if(!rendered.ok || !client_notes::ValidRichText(rendered.rtf)){error=rendered.error.empty()?L"The Markdown could not be converted safely.":rendered.error;return false;}
  page={};page.name=std::move(name);page.rich=true;page.rtf=rendered.rtf;
  for(const auto &link:rendered.links) {
    if(link.label.empty() || link.label.size()>2048 || !ValidNotesUrl(link.url)){error=L"A Markdown link exceeds the note's supported link limits.";return false;}
    const auto target=std::pair{link.label,link.url};auto found=std::find(page.links.begin(),page.links.end(),target);
    if(found==page.links.end()){if(page.links.size()>=256){error=L"This Markdown contains more than 256 distinct web link targets.";return false;}page.links.push_back(target);found=page.links.end()-1;}
    page.linkRanges.push_back({link.first,link.last,static_cast<uint32_t>(found-page.links.begin())});
  }
  page.linkRangesKnown=!page.links.empty();
  if(!client_notes::ValidLinkRanges(page.linkRanges,page.linkRangesKnown,page.links.size())){error=L"The Markdown link ranges could not be preserved safely.";return false;}
  return true;
}
static bool NotesImportMarkdownFile(HWND dialog,ClientNotesDialogState &state,const fs::path &path) {
  const NotesMarkdownContext context(dialog,state);
  const auto snapshot=client_notes::ReadFileSnapshot(path.parent_path(),path.filename().c_str(),client_notes::kMaxBytes);
  if(snapshot.status!=client_notes::ReadStatus::Ok){MessageBoxW(dialog,L"The Markdown file could not be read safely.",L"Import Markdown",MB_OK|MB_ICONWARNING);return false;}
  std::string bytes=snapshot.bytes;if(bytes.starts_with("\xef\xbb\xbf"))bytes.erase(0,3);
  std::wstring text;
  if(!client_notes::DecodeUtf8(bytes,text)){MessageBoxW(dialog,L"Markdown imports must use UTF-8 encoding. The original file and notes have not been changed.",L"Import Markdown",MB_OK|MB_ICONWARNING);return false;}
  const auto parsed=notes_markdown::Parse(text);
  if(!parsed.ok){MessageBoxW(dialog,parsed.error.c_str(),L"Import Markdown",MB_OK|MB_ICONWARNING);return false;}
  const auto rendered=notes_markdown::Render(parsed.document);client_notes::NotePage page;std::wstring error;
  if(!NotesMarkdownPage(rendered,path.stem().wstring(),page,error)){MessageBoxW(dialog,error.c_str(),L"Import Markdown",MB_OK|MB_ICONWARNING);return false;}
  std::vector<NotesLinkPiece> links;
  const LONG length=NotesReplacementStoryLength(dialog,page,links);
  if(length<0 || static_cast<size_t>(length)!=rendered.plain.size()) {
    MessageBoxW(dialog,L"The formatted Markdown preview could not preserve its text and link positions. Import the file as plain text instead. Your current notes are kept.",L"Import Markdown",MB_OK|MB_ICONWARNING);return false;
  }
  std::wstring preview=L"Import as a new note for "+state.clientName+L"?\n\nHeadings, emphasis, lists, checklists, code and web links are converted. Existing notes and the source file remain unchanged.\n\n";
  for(const auto &warning:parsed.warnings)preview+=L"\x2022 "+warning+L"\n";
  if(std::any_of(parsed.document.blocks.begin(),parsed.document.blocks.end(),[](const auto &block){return block.kind==notes_markdown::Kind::Heading && block.level>=3;}))
    preview+=L"\x2022 Heading levels 3-6 appear as compact bold subheadings; their exact outline levels are not retained by the native editor.\n";
  if(std::any_of(parsed.document.blocks.begin(),parsed.document.blocks.end(),[](const auto &block){return block.kind==notes_markdown::Kind::CodeBlock;}))
    preview+=L"\x2022 Fenced code becomes labeled monospaced text. Use Insert labeled code block for the native boxed-code layout.\n";
  preview+=L"\nPreview:\n"+rendered.plain.substr(0,1000);
  if(rendered.plain.size()>1000)preview+=L"\n...";
  if(MessageBoxW(dialog,preview.c_str(),L"Import Markdown preview",MB_OKCANCEL|(parsed.warnings.empty()?MB_ICONINFORMATION:MB_ICONWARNING))!=IDOK)return false;
  if(!context.Check())return false;
  return NotesAddPage(dialog,state,std::move(page));
}
static bool NotesImportMarkdown(HWND dialog,ClientNotesDialogState &state) {
  const NotesMarkdownContext context(dialog,state);
  if(!SaveClientNotesDraft(dialog,state,true))return false;
  std::wstring path;if(!NotesMarkdownPick(dialog,false,path))return false;
  if(!context.Check())return false;
  return NotesImportMarkdownFile(dialog,state,path);
}
static bool NotesMarkdownSnapshot(HWND edit,const client_notes::NotePage &page,
    notes_markdown::Document &document,std::wstring &reason) {
  using Microsoft::WRL::ComPtr;
  document={};reason.clear();
  ComPtr<IUnknown> unknown;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));
  ComPtr<ITextDocument> native;ComPtr<ITextRange> range;
  if(!unknown || FAILED(unknown.As(&native)) || FAILED(native->Range(0,0,&range))) {reason=L"The native rich-text snapshot is unavailable.";return false;}
  LONG story=0;if(FAILED(range->GetStoryLength(&story)) || story<1 || static_cast<size_t>(story-1)>notes_markdown::kMaxCharacters){reason=L"The native note exceeds the Markdown text limit.";return false;}
  const LONG length=story-1;size_t linkIndex=0,runs=0,retainedUnits=0;bool previousNumbered=false;unsigned nextNumber=1;
  for(LONG first=0;first<length;) {
    if(document.blocks.size()>=notes_markdown::kMaxBlocks){reason=L"There are too many native paragraphs.";return false;}
    LONG moved=0,last=first;
    if(FAILED(range->SetRange(first,first)) || FAILED(range->MoveEnd(tomParagraph,1,&moved)) || FAILED(range->GetEnd(&last)) || last<=first){reason=L"A native paragraph could not be read safely.";return false;}
    last=(std::min)(last,length);range->SetRange(first,last);
    ComPtr<ITextPara> paragraph;ComPtr<ITextPara2> paragraph2;LONG effects=0,mask=0;
    if(FAILED(range->GetPara(&paragraph)) || FAILED(paragraph.As(&paragraph2)) || FAILED(paragraph2->GetEffects(&effects,&mask))) {reason=L"Native paragraph formatting is unavailable.";return false;}
    if(effects&(tomParaEffectTable|tomParaEffectTableRowDelimiter)){reason=L"Native tables and labeled code boxes need a text copy; their cell layout cannot be represented by this Markdown exporter.";return false;}
    BSTR raw=nullptr;if(FAILED(range->GetText(&raw))){reason=L"Native paragraph text is unavailable.";return false;}
    std::wstring text(raw?raw:L"",raw?SysStringLen(raw):0);SysFreeString(raw);
    bool newline=!text.empty() && text.back()==L'\r';if(newline)text.pop_back();
    if(text.find_first_of(L"\v\a\f")!=std::wstring::npos || text.size()!=static_cast<size_t>(last-first)-(newline?1:0)){reason=L"Native soft line breaks or structural text need an explicitly disclosed text copy.";return false;}
    LONG contentFirst=first,contentLast=last-(newline?1:0);notes_markdown::Block block;
    if(text.empty())block.kind=notes_markdown::Kind::Blank;
    LONG list=0,level=0,listStart=1;paragraph->GetListType(&list);paragraph->GetListLevelIndex(&level);paragraph->GetListStart(&listStart);
    if(level>1 || (list&0xffff)>tomListNumberAsArabic){reason=L"Nested or specialized list numbering needs a text copy.";return false;}
    if((list&0xffff)==tomListBullet)block.kind=notes_markdown::Kind::Bullet;
    else if((list&0xffff)==tomListNumberAsArabic) {
      block.kind=notes_markdown::Kind::Numbered;block.number=previousNumbered?nextNumber:static_cast<unsigned>((std::max)(1L,listStart));
      if(block.number>999999){reason=L"The list number exceeds the Markdown limit.";return false;}nextNumber=block.number+1;
    }
    if(text.size()>=2 && IsNotesChecklist(std::wstring_view(text).substr(0,2))) {
      block.kind=notes_markdown::Kind::Checklist;block.checked=text[0]==L'\x2611';contentFirst+=2;text.erase(0,2);
    } else if(text.size()>=2 && text[0]==L'\x2022' && (text[1]==L' '||text[1]==L'\t')) {
      block.kind=notes_markdown::Kind::Bullet;contentFirst+=2;text.erase(0,2);
    } else if(block.kind==notes_markdown::Kind::Paragraph) {
      size_t digits=0;unsigned number=0;
      while(digits<text.size() && digits<6 && text[digits]>=L'0' && text[digits]<=L'9'){number=number*10+text[digits]-L'0';++digits;}
      if(digits && number && digits+1<text.size() && text[digits]==L'.' && text[digits+1]==L' ') {
        block.kind=notes_markdown::Kind::Numbered;block.number=number;contentFirst+=static_cast<LONG>(digits+2);text.erase(0,digits+2);nextNumber=number+1;
      }
    }
    previousNumbered=block.kind==notes_markdown::Kind::Numbered;
    if(contentFirst>first && std::any_of(page.linkRanges.begin(),page.linkRanges.end(),[&](const auto &link){return (link.first < static_cast<uint32_t>(contentFirst)) && (link.last > static_cast<uint32_t>(first));})) {
      reason=L"A linked list marker cannot be represented safely; its web address is retained in the text copy's link appendix.";return false;
    }
    if(contentFirst<contentLast && block.kind==notes_markdown::Kind::Paragraph) {
      range->SetRange(contentFirst,contentLast);ComPtr<ITextFont> font;LONG bold=tomUndefined;float size=0;
      if(SUCCEEDED(range->GetFont(&font)) && SUCCEEDED(font->GetBold(&bold)) && SUCCEEDED(font->GetSize(&size)) && bold==tomTrue) {
        if(size>=20){block.kind=notes_markdown::Kind::Heading;block.level=1;}
        else if(size>=16){block.kind=notes_markdown::Kind::Heading;block.level=2;}
      }
    }
    for(LONG at=contentFirst;at<contentLast;) {
      if(++runs>notes_markdown::kMaxSpans){reason=L"There are too many native formatting runs.";return false;}
      range->SetRange(at,at);LONG end=at;
      if(FAILED(range->MoveEnd(tomCharFormat,1,&moved)) || FAILED(range->GetEnd(&end)) || end<=at){reason=L"A native formatting run could not be read.";return false;}
      end=(std::min)(end,contentLast);
      while(linkIndex<page.linkRanges.size() && page.linkRanges[linkIndex].last<=static_cast<uint32_t>(at))++linkIndex;
      std::wstring url;
      if(linkIndex<page.linkRanges.size()) {
        const auto &link=page.linkRanges[linkIndex];
        if(link.first<=static_cast<uint32_t>(at) && link.last>static_cast<uint32_t>(at)) {
          if(link.target>=page.links.size() || !ValidNotesUrl(page.links[link.target].second)){reason=L"A saved web link cannot be mapped safely.";return false;}
          url=page.links[link.target].second;end=(std::min)(end,static_cast<LONG>(link.last));
        } else end=(std::min)(end,static_cast<LONG>(link.first));
      }
      range->SetRange(at,end);ComPtr<ITextFont> font;ComPtr<ITextFont2> font2;LONG fontEffects=0,fontMask=0;
      if(FAILED(range->GetFont(&font)) || FAILED(font.As(&font2)) || FAILED(font2->GetEffects(&fontEffects,&fontMask))) {reason=L"A native font run could not be read.";return false;}
      if(fontEffects&(CFE_HIDDEN|CFE_SUPERSCRIPT|CFE_SUBSCRIPT|CFE_SMALLCAPS|CFE_ALLCAPS|CFE_OUTLINE|CFE_SHADOW|CFE_EMBOSS|CFE_IMPRINT)) {reason=L"Some character effects need an explicitly disclosed text copy.";return false;}
      unsigned style=0;if(fontEffects&CFE_BOLD)style|=notes_markdown::Bold;if(fontEffects&CFE_ITALIC)style|=notes_markdown::Italic;if(fontEffects&CFE_STRIKEOUT)style|=notes_markdown::Strike;
      if(block.kind==notes_markdown::Kind::Heading)style&=~notes_markdown::Bold;
      if(block.kind==notes_markdown::Kind::Checklist && block.checked)style&=~notes_markdown::Strike;
      BSTR face=nullptr;if(FAILED(font->GetName(&face))){reason=L"A native font name could not be read.";return false;}
      const std::wstring name(face?face:L"",face?SysStringLen(face):0);SysFreeString(face);
      if(!_wcsicmp(name.c_str(),L"Consolas") || !_wcsicmp(name.c_str(),L"Courier New") || !_wcsicmp(name.c_str(),L"Cascadia Mono"))style|=notes_markdown::Code;
      raw=nullptr;if(FAILED(range->GetText(&raw))){reason=L"A native text run could not be read.";return false;}
      std::wstring value(raw?raw:L"",raw?SysStringLen(raw):0);SysFreeString(raw);
      if(value.size()!=static_cast<size_t>(end-at) || !notes_markdown::ValidText(value)){reason=L"A native text run contains unsupported structural characters.";return false;}
      if((fontEffects&CFE_LINK) && url.empty()) {
        if(ValidNotesUrl(value))url=value;
        else {reason=L"A native hyperlink cannot be mapped to one complete web address; a text copy retains its visible content.";return false;}
      }
      const bool merge=!block.spans.empty() && block.spans.back().style==style && block.spans.back().url==url;
      const size_t retained=value.size()+(merge?0:url.size());
      if(retained>notes_markdown::kMaxRetainedUnits-retainedUnits) {reason=L"The repeated link and formatting data exceeds the safe rich Markdown preview budget. A literal text copy preserves the content and web addresses.";return false;}
      retainedUnits+=retained;
      if(merge)block.spans.back().text+=value;
      else block.spans.push_back({std::move(value),std::move(url),style});
      at=end;
    }
    document.blocks.push_back(std::move(block));document.trailingNewline=last==length && newline;first=last;
  }
  std::wstring checked;
  if(!notes_markdown::Write(document,checked,reason))return false;
  return true;
}
static bool NotesExportMarkdown(HWND dialog,ClientNotesDialogState &state) {
  const NotesMarkdownContext context(dialog,state);
  if(!HasActiveNote(state) || !CaptureNotesPage(dialog,state,true))return false;
  const auto &page=state.pages[state.activePage];auto plain=NotesWindowText(GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT));
  // RichEdit's readable text can expose soft line breaks and cell terminators.
  // Normalize them only in this explicitly disclosed text copy, never in the
  // live rich document or its saved notebook.
  for(auto &character:plain) {
    if(character==L'\v' || character==L'\f')character=L'\n';
    else if(character==L'\a')character=L'\t';
  }
  if(plain.size()>notes_markdown::kMaxCharacters || !notes_markdown::ValidText(plain)) {
    MessageBoxW(dialog,L"This note is too large or contains native table markers that cannot be converted as readable Markdown text. Export it as RTF or a ctSpaces notebook to preserve it.",L"Export Markdown",MB_OK|MB_ICONWARNING);return false;
  }
  notes_markdown::Document document;std::wstring markdown,error,richReason;
  const bool rich=NotesMarkdownSnapshot(GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT),page,document,richReason);
  if(!rich) {
    // A fenced literal copy also preserves indentation and punctuation in
    // code/table text; escaping an indented paragraph would change how a
    // Markdown reader interprets its backslashes.
    document={};notes_markdown::Block literal;literal.kind=notes_markdown::Kind::CodeBlock;
    std::wstring normalized;
    for(size_t i=0;i<plain.size();++i){if(plain[i]==L'\r'){normalized+=L'\n';if(i+1<plain.size() && plain[i+1]==L'\n')++i;}else normalized+=plain[i];}
    literal.spans.push_back({std::move(normalized),{},notes_markdown::Code});document.blocks.push_back(std::move(literal));
  }
  if(!notes_markdown::Write(document,markdown,error)){MessageBoxW(dialog,error.c_str(),L"Export Markdown",MB_OK|MB_ICONWARNING);return false;}
  // Rich-text link labels alone do not retain their destinations. Include an
  // explicit web-link appendix so this text interchange never drops addresses.
  if(!rich && !page.links.empty()) {
    markdown+=L"\n\n## Web links\n";
    for(const auto &[label,url]:page.links) {
      if(!ValidNotesUrl(url)){MessageBoxW(dialog,L"A saved link cannot be exported safely. Use RTF or notebook export to retain the complete note.",L"Export Markdown",MB_OK|MB_ICONWARNING);return false;}
      markdown+=L"- ["+notes_markdown::Escape(label)+L"]("+notes_markdown::Escape(url)+L")\n";
    }
  }
  if(markdown.size()>notes_markdown::kMaxCharacters){MessageBoxW(dialog,L"The Markdown export exceeds the supported text size. Use RTF or notebook export instead.",L"Export Markdown",MB_OK|MB_ICONWARNING);return false;}
  std::wstring preview=rich ? L"Export a Markdown copy?\n\nSupported headings, bold, italic, strikethrough, lists, checklists, inline code and web links are kept. Font choices/sizes, colors, highlights, underline and exact paragraph layout are not preserved. Use RTF for exact formatting or .ctn for metadata and history.\n\n" :
    L"Export a readable Markdown text copy?\n\n"+richReason+L"\n\nText and web addresses are kept. Rich formatting, heading styles, list formatting, colors, highlights, table/code-box layout and checkbox behavior are not preserved in this text export. Use RTF for formatting or .ctn for metadata and history.\n\n";
  preview+=L"Your client notebook stays unchanged.\n\nPreview:\n"+markdown.substr(0,1000);
  if(MessageBoxW(dialog,preview.c_str(),L"Markdown text export preview",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return false;
  if(!context.Check())return false;
  std::wstring path;if(!NotesMarkdownPick(dialog,true,path))return false;
  if(!context.Check())return false;
  if(_wcsicmp(fs::path(path).extension().c_str(),L".md") && _wcsicmp(fs::path(path).extension().c_str(),L".markdown")) {
    MessageBoxW(dialog,L"Choose a .md or .markdown filename for this text copy.",L"Export Markdown",MB_OK|MB_ICONWARNING);return false;
  }
  std::string bytes;
  if(!client_notes::EncodeUtf8(markdown,bytes) || !NotesWriteExport(path,bytes)) {
    MessageBoxW(dialog,client_notes::PersistenceFailure(L"The Markdown copy could not be exported. Your client notebook is kept.").c_str(),L"Export Markdown",MB_OK|MB_ICONWARNING);return false;
  }
  return true;
}
static bool NotesInsertSimpleTable(HWND dialog,ClientNotesDialogState &state,unsigned rows,unsigned columns) {
  if(!HasActiveNote(state))return false;
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  if(paragraph.wEffects&PFE_TABLE){MessageBoxW(dialog,L"Move the cursor outside the current table or code box before inserting a table.",L"Insert table",MB_OK|MB_ICONINFORMATION);return false;}
  const auto rtf=notes_markdown::TableRtf(rows,columns);
  if(rtf.empty() || !client_notes::ValidRichText(rtf))return false;
  client_notes::NotePage page;page.rich=true;page.rtf=rtf;
  // Reuse the native insertion transaction: prospective link/range limits,
  // one TOM Undo group, checked stream-in and rollback, same-client autosave.
  NotesTemplates insertion{dialog,&state};return NotesInsertSnippet(dialog,insertion,page);
}
static bool NotesCreateSimpleTable(HWND dialog,ClientNotesDialogState &state) {
  const NotesMarkdownContext context(dialog,state);
  NotesInput input{L"Insert table",L"Rows (1-12)",L"Columns (1-12)",L"3",L"3"};
  input.hint=L"Type inside its cells. Tab and Shift+Tab move between existing cells without adding rows. Rich text and notebook exports preserve the table; Markdown text export cannot preserve its layout.";
  if(!NotesAsk(dialog,input))return false;
  if(!context.Check())return false;
  const auto number=[](const std::wstring &value)->unsigned {
    if(value.empty() || value.size()>2 || !std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))return 0;
    unsigned result=0;for(wchar_t c:value)result=result*10+c-L'0';return result;
  };
  const auto rows=number(input.first),columns=number(input.second);
  if(!rows || rows>12 || !columns || columns>12){MessageBoxW(dialog,L"Choose between 1 and 12 rows and between 1 and 12 columns.",L"Insert table",MB_OK|MB_ICONWARNING);return false;}
  return NotesInsertSimpleTable(dialog,state,rows,columns);
}
static bool NotesNavigateTableCell(HWND edit,bool previous) {
  if(!edit || !IsWindow(edit))return false;
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  if(!(paragraph.wEffects&PFE_TABLE))return false;
  // RichEdit deliberately ignores translated Tab after the dialog manager's
  // WM_GETDLGCODE query. Use its native cell units without inserting text,
  // creating rows, changing Undo or acquiring focus.
  using Microsoft::WRL::ComPtr;
  CHARRANGE original{};NotesSelection(edit,original);
  ComPtr<IUnknown> unknown;ComPtr<ITextDocument> document;ComPtr<ITextRange> range;
  SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));
  if(!unknown || FAILED(unknown.As(&document)) || FAILED(document->Range(original.cpMin,original.cpMax,&range)))return true;
  // Move(-1) from inside a nonempty cell only reaches that cell's beginning.
  // Normalize the private range first so either direction means another cell,
  // including when part of the current cell's text is selected.
  LONG normalized=0,moved=0,target=-1;
  if(FAILED(range->StartOf(tomCell,tomMove,&normalized)) || FAILED(range->Move(tomCell,previous?-1:1,&moved)) ||
      moved!=(previous?-1:1) || FAILED(range->GetStart(&target)) || target<0)return true;
  ComPtr<ITextPara> targetParagraph;ComPtr<ITextPara2> targetParagraph2;LONG effects=0,mask=0;
  const auto readTarget=[&]() {
    targetParagraph2.Reset();targetParagraph.Reset();
    return SUCCEEDED(range->SetRange(target,target)) && SUCCEEDED(range->GetPara(&targetParagraph)) && SUCCEEDED(targetParagraph.As(&targetParagraph2)) &&
      SUCCEEDED(targetParagraph2->GetEffects(&effects,&mask));
  };
  if(!readTarget())return true;
  // Forward cell movement can stop on the row's structural delimiter. The
  // native row unit reaches the following row; validate it before selecting.
  if(!previous && (effects&tomParaEffectTableRowDelimiter)) {
    LONG rows=0;if(FAILED(range->Move(tomRow,1,&rows)) || rows!=1 || FAILED(range->GetStart(&target)) || target<0 || !readTarget())return true;
  }
  if(!(effects&tomParaEffectTable) || (effects&tomParaEffectTableRowDelimiter))return true;
  const CHARRANGE desired{target,target};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&desired));
  CHARRANGE actual{};NotesSelection(edit,actual);
  if(actual.cpMin!=target || actual.cpMax!=target)SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&original));
  else SendMessageW(edit,EM_SCROLLCARET,0,0);
  return true;
}
