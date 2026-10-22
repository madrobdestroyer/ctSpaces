static void Core() {
  ++groups;
  const std::vector<std::wstring> examples={
    L"# Office rollout\n\nKeep **important** and *useful* text.\n- [x] Verified\n- [ ] Pending\n3. Third\n",
    L"## Commands\n```powershell\nGet-Printer | Select-Object Name\nWrite-Host '**literal**'\n```\n",
    L"A [portal](https://example.test/help?a=1&b=2) and [same label](https://example.test/other).",
    L"Unicode \x6771\x4eac \x00e9 \U0001f680 and escaped \\*literal\\*.",
    L"A `literal **code**` run and ~~finished~~ work.",
    L"Escaped \\<script\\> is literal text.\n\n",L"",L"\n",L"Line\r\nNext\rTail"
  };
  for(const auto &source:examples) {
    const auto parsed=notes_markdown::Parse(source);Check(parsed.ok,"Representable Markdown parses without partial mutation");
    std::wstring written,error;Check(notes_markdown::Write(parsed.document,written,error),"Representable Markdown has a checked canonical export");
    const auto again=notes_markdown::Parse(written);
    Check(again.ok && notes_markdown::Canonical(again.document)==notes_markdown::Canonical(parsed.document),"Canonical Markdown round-trip keeps block kinds, formatting, checklist state, code language and link targets");
    const auto rendered=notes_markdown::Render(parsed.document);
    Check(rendered.ok && client_notes::ValidRichText(rendered.rtf) && rendered.rtf.find("\\field")==std::string::npos,"Rendered Markdown is bounded safe RTF without external fields or objects");
  }
  const std::vector<std::wstring> unsafe={L"[**do not run**](javascript:alert(1))",L"[file](file:///C:/secret)",L"Use ![image](https://example.test/x) here",L"Use <b>HTML</b> literally",L"> **quoted** text",L"```\n**unclosed source**\n"};
  for(const auto &source:unsafe) {
    const auto parsed=notes_markdown::Parse(source);const auto rendered=notes_markdown::Render(parsed.document);
    std::wstring normalized;for(wchar_t c:source)normalized+=c==L'\n'?L'\r':c;
    Check(parsed.ok && !parsed.warnings.empty() && rendered.ok && rendered.links.empty() && rendered.plain==normalized,
          "Unsupported or unsafe Markdown is preserved literally with a preview warning and no clickable unsafe target");
  }
  Check(!notes_markdown::Parse(std::wstring(notes_markdown::kMaxCharacters+1,L'x')).ok,"Oversize Markdown is refused before rendering");
  Check(!notes_markdown::Parse(std::wstring(notes_markdown::kMaxLine+1,L'x')).ok,"An excessive single line is refused");
  Check(!notes_markdown::Parse(std::wstring(L"a\0b",3)).ok,"Embedded NUL cannot truncate an imported note");
  Check(!notes_markdown::Parse(std::wstring(1,static_cast<wchar_t>(0xd800))).ok,"Unpaired UTF-16 surrogate is refused");
  const auto hostile=notes_markdown::Parse(std::wstring(50000,L'['));
  Check(!hostile.ok || hostile.document.blocks.size()==1,"Malformed bracket input completes under a fixed parser operation budget");
  notes_markdown::Document ambiguous;notes_markdown::Block block;block.spans.push_back({L" leading space",{},notes_markdown::Bold});ambiguous.blocks.push_back(block);
  std::wstring written,error;Check(!notes_markdown::Write(ambiguous,written,error) && written.empty() && !error.empty(),"Unrepresentable Markdown formatting boundaries refuse instead of silently changing style");
  const auto labeled=notes_markdown::Render(notes_markdown::Parse(L"```python\nprint('test')\n```").document);
  Check(labeled.ok && labeled.plain.starts_with(L"python - Code\r"),"A fenced code language is visible in its imported note");
  const auto identifiers=notes_markdown::Render(notes_markdown::Parse(L"client_name_field foo__bar__baz").document);
  Check(identifiers.ok && identifiers.plain==L"client_name_field foo__bar__baz","Technical identifiers retain intraword underscores instead of becoming accidental emphasis");
  Check(notes_markdown::SafeUrl(L"HTTPS://Example.test/Path") && notes_markdown::SafeUrl(L"HtTp://example.test/path") &&
        !notes_markdown::SafeUrl(L"https://") && !notes_markdown::SafeUrl(L"https://example.test/a b") && !notes_markdown::SafeUrl(L"file:///C:/secret"),
        "HTTP scheme matching is ASCII case-insensitive and still refuses empty hosts, whitespace and non-web schemes");
  const std::wstring urlPrefix=L"https://example.test/",maximumUrl=urlPrefix+std::wstring(2048-urlPrefix.size(),L'a');
  Check(notes_markdown::SafeUrl(maximumUrl) && !notes_markdown::SafeUrl(maximumUrl+L"a"),"Markdown web destinations share the native note's exact 2048-character boundary");
  std::wstring repeatedLabel;for(unsigned i=0;i<1000;++i)repeatedLabel+=L"a **b** ";
  std::wstring repeatedLinks;for(unsigned i=0;i<3;++i)repeatedLinks+=L"["+repeatedLabel+L"]("+maximumUrl+L")\n";
  const auto amplified=notes_markdown::Parse(repeatedLinks);
  Check(!amplified.ok && amplified.error.find(L"link and formatting")!=std::wstring::npos,"A small Markdown input cannot amplify repeated URL copies beyond the retained formatting budget");
  notes_markdown::Document literalCopy;notes_markdown::Block literalCode;literalCode.kind=notes_markdown::Kind::CodeBlock;
  literalCode.spans.push_back({L"    Select-Object **literal**\n\t{ value: `tick` }\n",{},notes_markdown::Code});literalCopy.blocks.push_back(literalCode);
  Check(notes_markdown::Write(literalCopy,written,error) && notes_markdown::Canonical(notes_markdown::Parse(written).document)==notes_markdown::Canonical(literalCopy),"A fenced text copy preserves indented code punctuation and trailing line breaks exactly");
  Check(notes_markdown::TableRtf(0,3).empty() && notes_markdown::TableRtf(3,0).empty() && notes_markdown::TableRtf(13,3).empty() && notes_markdown::TableRtf(3,13).empty(),"Native table bounds refuse zero and dimensions above twelve");
  for(const auto dimensions:{std::pair{1u,1u},std::pair{3u,3u},std::pair{12u,12u}})
    Check(client_notes::ValidRichText(notes_markdown::TableRtf(dimensions.first,dimensions.second)),"Bounded simple tables contain safe native RTF only");
}
