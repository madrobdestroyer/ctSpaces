static unsigned checks=0,failures=0,shown=0,focused=0,foregrounded=0;
static void Check(bool value,const char*label){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';}
static void CALLBACK Event(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW&&IsWindowVisible(GetAncestor(window,GA_ROOT)))++shown;if(event==EVENT_OBJECT_FOCUS)++focused;if(event==EVENT_SYSTEM_FOREGROUND)++foregrounded;}
static DWORD CALLBACK Out(DWORD_PTR cookie,LPBYTE bytes,LONG count,LONG*written){reinterpret_cast<std::string*>(cookie)->append(reinterpret_cast<char*>(bytes),count);*written=count;return 0;}
static std::string Rtf(HWND edit){std::string output;EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&output),0,Out};SendMessageW(edit,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&stream));if(stream.dwError)throw std::runtime_error("RTF read failed");return output;}
struct Input{std::string bytes;size_t at=0;};
static DWORD CALLBACK In(DWORD_PTR cookie,LPBYTE bytes,LONG count,LONG*read){auto&input=*reinterpret_cast<Input*>(cookie);const auto take=(std::min)(static_cast<size_t>(count),input.bytes.size()-input.at);memcpy(bytes,input.bytes.data()+input.at,take);input.at+=take;*read=static_cast<LONG>(take);return 0;}
static void FilterCases(){
  for(const auto*word:{L"speling",L"Hello",L"can't",L"caf\x00e9",L"\x00c9l\x00e8ve",L"mistake"})Check(notes_spelling::ProseWord(word),"Ordinary and accented prose words remain eligible");
  for(const auto*word:{L"I",L"HTTP",L"SSO",L"PRINT01",L"PowerShell",L"camelCase",L"T20260921.0622",L"example.test",L"rob@example.test",L"https://example.test",L"C:\\Temp",L"--flag",L"$variable",L"Get-Printer",L"{code}",L"`inline`",L"A_B",L"\U0001f680"})Check(!notes_spelling::ProseWord(word),"Technical tokens, identifiers, commands and emoji are excluded");
  for(const auto*line:{L"Get-Printer -ComputerName PRINT01",L"ipconfig /all",L"git status",L"PS> Get-Service",L"$x = \"text\"",L"C:\\Temp\\cmd.exe args"})Check(notes_spelling::CommandLine(line),"Command lines are excluded before calling a spell provider");
  for(const auto*line:{L"Notes: Speling is checked",L"Follow-up details remain prose",L"The printer needs maintenance"})Check(!notes_spelling::CommandLine(line),"Ordinary labeled prose remains eligible for spelling review");
  const std::wstring text=L"Speling: hello, T20260921.0622 https://example.test PowerShell world.\r\nGet-Printer brokenword\r\n```\r\nfencedword\r\n```\r\nNotes: speling ";
  const auto masked=notes_spelling::MaskTechnicalText(text);
  Check(masked.size()==text.size(),"Masking retains exact UTF-16 offsets and CRLF length");
  Check(masked.find(L"Speling")!=masked.npos&&masked.find(L"world")!=masked.npos&&masked.find(L"Notes")!=masked.npos,"Ordinary prose survives masking");
  Check(masked.find(L"T2026")==masked.npos&&masked.find(L"example")==masked.npos&&masked.find(L"PowerShell")==masked.npos&&masked.find(L"brokenword")==masked.npos&&masked.find(L"fencedword")==masked.npos,"Technical lines, domains, identifiers and fenced code are masked");
  const auto excluded=notes_spelling::MaskTechnicalText(L"first second",{{6,12}});Check(excluded==L"first       ","Native protected ranges are excluded at exact UTF-16 offsets");
  for(const auto*value:{L"spelling",L"a lot",L"well-known",L"caf\x00e9"})Check(notes_spelling::ValidReplacement(value),"Explicit prose corrections are accepted");
  for(const auto*value:{L"",L" ",L"two\r\nparagraphs",L"https://x.test",L"$x",L" leading",L"trailing "})Check(!notes_spelling::ValidReplacement(value),"Suggestion inputs cannot insert control characters or technical commands");
}
static void NativeReadOnlyCases(){
  const auto module=LoadLibraryW(L"Msftedit.dll");if(!module)throw std::runtime_error("Native RichEdit unavailable");
  const auto edit=CreateWindowExW(WS_EX_NOACTIVATE,L"RICHEDIT50W",L"",WS_POPUP|ES_MULTILINE,0,0,600,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!edit)throw std::runtime_error("Hidden RichEdit failed");
  SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"hello speling \U0001f680\rConsolascode\rlinkedword\rordinary prose"));
  notes_spelling::ComPtr<ITextDocument> document;Check(SUCCEEDED(notes_spelling::TextDocument(edit,document)),"Read-only TOM document is available");
  notes_spelling::ComPtr<ITextRange> range;document->Range(17,29,&range);notes_spelling::ComPtr<ITextFont> font;range->GetFont(&font);BSTR face=SysAllocString(L"Consolas");font->SetName(face);SysFreeString(face);
  document->Range(30,40,&range);notes_spelling::ComPtr<ITextFont2> font2;range->GetFont(&font);font.As(&font2);font2->SetEffects(CFE_LINK,CFM_LINK);
  CHARRANGE selected{2,5};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));POINT scroll{0,0};SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  const auto before=Rtf(edit);const auto undo=SendMessageW(edit,EM_CANUNDO,0,0);LONG length=0;document->Range(0,0,&range);range->GetStoryLength(&length);
  std::wstring text,masked;Check(SUCCEEDED(notes_spelling::ReadScope(edit,{0,length-1},text,masked)),"Native RichEdit snapshot and protected-format mask read successfully");
  Check(text.size()==masked.size()&&masked.find(L"speling")!=masked.npos,"Native UTF-16 spelling text retains a misspelled prose word");
  Check(masked.find(L"Consolascode")==masked.npos&&masked.find(L"linkedword")==masked.npos,"Read-only TOM formatting inspection excludes code font and link text");
  CHARRANGE actual{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&actual));Check(actual.cpMin==selected.cpMin&&actual.cpMax==selected.cpMax,"Read-only spelling preparation preserves the native selection");
  Check(Rtf(edit)==before&&SendMessageW(edit,EM_CANUNDO,0,0)==undo,"Spelling preparation preserves exact RTF and existing Undo availability");
  SetWindowTextW(edit,L"Read https://speling.example.test now.\rGet-Printer spelingggg\rordinary ending");
  document->Range(0,0,&range);range->GetStoryLength(&length);std::wstring contextual;notes_spelling::NativeText(edit,{0,length-1},contextual);
  const auto url=contextual.find(L"speling"),command=contextual.find(L"spelingggg");
  Check(url!=contextual.npos&&SUCCEEDED(notes_spelling::ReadScope(edit,{static_cast<LONG>(url),static_cast<LONG>(url+7)},text,masked))&&masked==std::wstring(7,L' '),"Selecting only a URL fragment does not bypass technical-token exclusion");
  Check(command!=contextual.npos&&SUCCEEDED(notes_spelling::ReadScope(edit,{static_cast<LONG>(command),static_cast<LONG>(command+10)},text,masked))&&masked==std::wstring(10,L' '),"Selecting only a command argument does not bypass command-line exclusion");
  document.Reset();range.Reset();font.Reset();font2.Reset();
  Input input{"{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0 proseword\\par \\trowd\\cellx7200\\pard\\intbl tablebadword\\cell\\row\\pard ordinaryword\\par}"};
  EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&input),0,In};SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&stream));Check(!stream.dwError,"A native editable code-table fixture loads");
  notes_spelling::TextDocument(edit,document);document->Range(0,0,&range);range->GetStoryLength(&length);
  Check(SUCCEEDED(notes_spelling::ReadScope(edit,{0,length-1},text,masked)),"Native table content keeps accurate TOM UTF-16 spelling positions");
  Check(masked.find(L"tablebadword")==masked.npos&&masked.find(L"proseword")!=masked.npos&&masked.find(L"ordinaryword")!=masked.npos,"Code tables are excluded while surrounding prose remains checked");
  document.Reset();range.Reset();DestroyWindow(edit);FreeLibrary(module);
}
static void WindowsProviderCases(){
  notes_spelling::WindowsDictionary dictionary;Check(SUCCEEDED(dictionary.Initialize()),"Installed Windows spell checker factory initializes");
  std::vector<std::wstring> languages;Check(SUCCEEDED(dictionary.Languages(languages))&&!languages.empty(),"Available Windows dictionaries enumerate without changing settings");
  const auto language=std::find(languages.begin(),languages.end(),L"en-US")!=languages.end()?L"en-US":languages.front();
  Check(SUCCEEDED(dictionary.Select(language)),"An installed dictionary opens without downloading or installing assets");
  std::vector<notes_spelling::Error> errors;Check(SUCCEEDED(dictionary.Check(L"spelingggg",errors))&&!errors.empty(),"Actual Windows dictionary reports a deliberately misspelled word");
  Check(SUCCEEDED(dictionary.Check(L"hello \U0001f680 spelingggg",errors))&&std::any_of(errors.begin(),errors.end(),[](const auto&error){return error.first==9&&error.length==10;}),"Actual Windows spelling offsets remain native UTF-16 positions after a surrogate-pair emoji");
  std::vector<std::wstring> suggestions;Check(SUCCEEDED(dictionary.Suggestions(L"speling",suggestions)),"Actual Windows dictionary returns a bounded suggestion list");
  Check(std::all_of(suggestions.begin(),suggestions.end(),[](const auto&value){return notes_spelling::ValidReplacement(value);}),"Returned suggestions satisfy explicit replacement safety rules");
  std::cout<<"Windows spell language count="<<languages.size()<<"; suggestions="<<suggestions.size()<<'\n';
}
