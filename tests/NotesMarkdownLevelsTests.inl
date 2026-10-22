static void ImportedHeadingAndListDisclosure(const fs::path &artifacts) {
  ++groups;Fixture(L"Markdown levels Alpha");HWND owner=Open(L"Markdown levels Alpha");auto &state=State(owner);
  const fs::path source=artifacts/L"levels.md";
  Put(source,"### Third level\n#### Fourth level\n##### Fifth level\n###### Sixth level\n\n3. Third item\n4. Fourth item\n");
  answers.push_back(IDOK);
  Check(NotesImportMarkdownFile(owner,state,source),"Higher headings and visible numbered lists import as an independent note");
  Check(lastNotice.find(L"Heading levels 3-6")!=std::wstring::npos && lastNotice.find(L"not retained")!=std::wstring::npos,
        "The import preview explicitly discloses the native editor's higher-heading-level conversion");
  notes_markdown::Document snapshot;std::wstring reason,markdown;
  Check(NotesMarkdownSnapshot(Edit(owner),state.pages[state.activePage],snapshot,reason) && notes_markdown::Write(snapshot,markdown,reason),
        "The imported native note remains exportable with its disclosed heading normalization");
  Check(markdown.find(L"3. Third item")!=std::wstring::npos && markdown.find(L"4. Fourth item")!=std::wstring::npos,
        "Visible imported numbering retains its explicit numeric starts when exported as Markdown lists");
  DestroyWindow(owner);NotesBackgroundAudit("imported heading-level disclosure and numbered-list semantics");
}
