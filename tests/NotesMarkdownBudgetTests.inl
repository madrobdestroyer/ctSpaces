static void NativeMetadataBudget(const fs::path &artifacts) {
  ++groups;Fixture(L"Markdown budget Alpha");HWND owner=Open(L"Markdown budget Alpha");auto &state=State(owner);
  const std::wstring prefix=L"https://example.test/",urlA=prefix+std::wstring(2048-prefix.size(),L'a'),urlB=prefix+std::wstring(2048-prefix.size(),L'b');
  std::wstring label;for(unsigned i=0;i<1000;++i)label+=L"a **b** ";
  std::wstring input;for(unsigned i=0;i<3;++i)input+=L"["+label+L"]("+urlA+L")\n";
  std::string bytes;client_notes::EncodeUtf8(input,bytes);const fs::path source=artifacts/L"metadata-budget.md";Put(source,bytes);
  const auto count=state.pages.size();const auto beforeImport=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  Check(!NotesImportMarkdownFile(owner,state,source) && state.pages.size()==count &&
        client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==beforeImport && lastNotice.find(L"link and formatting")!=std::wstring::npos,
        "Excessive retained Markdown metadata is rejected before a preview, note creation or notebook write");
  notes_markdown::Document sourceDocument;notes_markdown::Block block;
  // This legitimate native notebook has two targets and 5,000 occurrences.
  // It is deliberately built independently of the bounded Markdown parser.
  for(unsigned i=0;i<5000;++i)block.spans.push_back({i%2?L"b":L"a",i%2?urlB:urlA,i%2?0u:notes_markdown::Bold});
  sourceDocument.blocks.push_back(std::move(block));
  const auto rendered=notes_markdown::Render(sourceDocument);client_notes::NotePage page;std::wstring reason;
  Check(NotesMarkdownPage(rendered,L"Repeated native links",page,reason) && page.links.size()==2 && page.linkRanges.size()==5000,
        "The native export-budget fixture fits actual notebook link target and occurrence limits");
  Check(NotesAddPage(owner,state,std::move(page)),"The bounded rich-export fixture saves through the real client notebook");
  const auto saved=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  notes_markdown::Document snapshot;
  Check(!NotesMarkdownSnapshot(Edit(owner),state.pages[state.activePage],snapshot,reason) && reason.find(L"link and formatting")!=std::wstring::npos,
        "Read-only rich snapshot bounds repeated URL copies before they amplify retained memory");
  const fs::path output=artifacts/L"budget-fallback.md";answers.push_back(IDOK);files.push_back(output.wstring());
  Check(NotesExportMarkdown(owner,state) && files.empty() && lastNotice.find(L"safe rich Markdown preview budget")!=std::wstring::npos,
        "A rich snapshot over budget offers the explicitly disclosed literal Markdown fallback");
  const auto exported=client_notes::ReadFileSnapshot(artifacts,L"budget-fallback.md",client_notes::kMaxBytes);std::wstring text;
  const bool decoded=exported.status==client_notes::ReadStatus::Ok && client_notes::DecodeUtf8(exported.bytes,text);
  const auto roundTrip=notes_markdown::Parse(text);std::vector<std::wstring> destinations;
  for(const auto &exportedBlock:roundTrip.document.blocks)for(const auto &span:exportedBlock.spans)if(!span.url.empty())destinations.push_back(span.url);
  std::wstring expected;for(unsigned i=0;i<2500;++i)expected+=L"ab";
  Check(decoded && roundTrip.ok && !roundTrip.document.blocks.empty() &&
        roundTrip.document.blocks[0].kind==notes_markdown::Kind::CodeBlock && roundTrip.document.blocks[0].spans.size()==1 &&
        roundTrip.document.blocks[0].spans[0].text==expected && destinations==std::vector<std::wstring>{urlA,urlB} &&
        client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==saved,
        "Budget fallback keeps readable text and both complete URLs without changing the authoritative notebook");
  DestroyWindow(owner);NotesBackgroundAudit("retained Markdown metadata budget, pre-mutation refusal and disclosed native export fallback");
}
