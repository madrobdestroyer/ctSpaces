// Exercise real native publication after the final application comparison.
// Hooks replace only exact disposable test paths and leave the native call real.
static std::function<void(const fs::path &)> publicationReplaceHook;
static std::function<void(const fs::path &)> publicationRenameHook;
static BOOL WINAPI PublicationReplaceFile(LPCWSTR target,LPCWSTR stage,LPCWSTR backup,DWORD flags,LPVOID exclude,LPVOID reserved) {
  if(publicationReplaceHook) {auto hook=std::move(publicationReplaceHook);publicationReplaceHook={};hook(fs::path(target));}
  return ::ReplaceFileW(target,stage,backup,flags,exclude,reserved);
}
static BOOL WINAPI PublicationSetFileInformation(HANDLE file,FILE_INFO_BY_HANDLE_CLASS type,LPVOID data,DWORD size) {
  if(type==FileRenameInfo && publicationRenameHook) {
    const auto *rename=static_cast<const FILE_RENAME_INFO *>(data);
    auto hook=std::move(publicationRenameHook);publicationRenameHook={};
    hook(fs::path(std::wstring(rename->FileName,rename->FileNameLength/sizeof(wchar_t))));
  }
  return ::SetFileInformationByHandle(file,type,data,size);
}
static void TestPublicationBoundaries(const fs::path &base) {
  using namespace client_notes;
  unsigned assertions=0;
  const auto verify=[&](bool condition,const char *message){Check(condition,message);++assertions;};
  for(const bool auxiliary:{false,true}) {
    const auto root=base/(auxiliary ? L"Native template publication" : L"Native notebook publication");fs::create_directory(root);
    const auto name=auxiliary ? L"templates.ctn" : kNotebookFileName;const auto target=root/name;
    NotePage page{L"Original",true,"{\\rtf1 Original document}",{}};page.id=51;
    std::string original;verify(SerializeNotebook({page},original),"Original native publication fixture serializes");
    verify(auxiliary ? WriteAuxiliary(root,name,original) : WriteNotebook(root,{page},ReadNotebook(root)),"Original native publication fixture commits");
    const auto expected=ReadNotebook(root);auto pending=page;pending.rtf="{\\rtf1 Precious editor draft}";
    auto external=page;external.ticket=L"NEWER EXTERNAL VERSION";std::string externalBytes,pendingBytes;verify(SerializeNotebook({external},externalBytes) && SerializeNotebook({pending},pendingBytes),"Independent external and pending documents serialize");
    const auto prepared=root/L"external-prepared",externalOriginal=root/L"external-original";PutBytes(prepared,externalBytes);bool injected=false;
    publicationReplaceHook=[&](const fs::path &actual) {
      verify(actual==target,"Late writer intercepts the exact application native target");
      injected=::ReplaceFileW(target.c_str(),prepared.c_str(),externalOriginal.c_str(),REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
      verify(injected,"External native replacement really commits after the application's last comparison");
    };
    const bool saved=auxiliary ? WriteAuxiliary(root,name,pendingBytes) : WriteNotebook(root,{pending},expected);
    const auto detail=PersistenceError();verify(!publicationReplaceHook && injected && !saved,"A newer displaced version is not accepted as a successful stale save");
    verify(ReadFileSnapshot(root,name,kMaxNotebookBytes).bytes==externalBytes,"Native rollback preserves the exact newer external notebook bytes");
    verify(detail.find(L".ctSpaces-save-")!=std::wstring::npos,"Native publication failure exposes the retained recovery directory");
    verify(WriteAuxiliary(root,L"snippets.ctn",original),"A retained template/notebook recovery does not block creating a different snippets library");
    verify(WriteAuxiliary(root,L"ctSpaces-notes-draft-independent.ctn",pendingBytes),"A retained save recovery does not block a separate crash-draft file in the same directory");
    if(auxiliary) {
      verify(MoveFileExW(target.c_str(),(root/L"complete-template-aside").c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"Simulate a missing template target while its durable recovery record remains");
      verify(ReadFileSnapshot(root,L"TEMPLATES.CTN",kMaxNotebookBytes).status==ReadStatus::Error,"Target-specific recovery still blocks a missing owned document with case-insensitive filename matching");
      verify(WriteAuxiliary(root,L"unrelated-export.ctn",original),"A missing recovery target does not block unrelated new export names");
    }
    if(!auxiliary) verify(ReadNotebook(root).status==ReadStatus::Ok,"A restored current notebook remains readable beside retained versions");
  }
  for(const bool rich:{false,true}) for(const bool replace:{false,true}) {
    const auto root=base/(std::wstring(L"Native legacy migration ")+(rich?L"RTF ":L"TXT ")+(replace?L"replace":L"rewrite"));fs::create_directory(root);
    const auto source=root/(rich ? kRichFileName : kFileName),target=root/kNotebookFileName;
    const std::string original=rich ? "{\\rtf1 Original legacy source}" : "Original legacy source";PutBytes(source,original);
    const auto expected=ReadNotebook(root);auto pending=expected.pages.front();pending.rich=true;pending.text.clear();pending.rtf="{\\rtf1 Authorized migration draft}";
    const auto prepared=root/L"external-prepared",backup=root/L"externally-displaced";PutBytes(prepared,rich ? "{\\rtf1 Newer legacy source}" : "Newer legacy source");bool hit=false,blocked=false;
    publicationRenameHook=[&](const fs::path &actual) {
      verify(actual==target,"First migration intercepts the exact no-replace CTN boundary");hit=true;
      if(replace) blocked=::ReplaceFileW(source.c_str(),prepared.c_str(),backup.c_str(),REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)==FALSE;
      else {HANDLE writer=CreateFileW(source.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,TRUNCATE_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);blocked=writer==INVALID_HANDLE_VALUE;if(!blocked) CloseHandle(writer);}
    };
    verify(WriteNotebook(root,{pending},expected) && hit && blocked,"Existing TXT/RTF source cannot be rewritten or replaced through first publication");
    verify(ReadFileSnapshot(root,source.filename().c_str(),kMaxRichBytes).bytes==original,"The pinned original legacy source remains byte-exact");
  }
  const auto root=base/L"Native missing legacy source";fs::create_directory(root);const auto expected=ReadNotebook(root);NotePage pending{L"Editor",true,"{\\rtf1 Pending empty-notebook draft}",{}};pending.id=72;bool hit=false;
  publicationRenameHook=[&](const fs::path &actual) {verify(actual==root/kNotebookFileName,"Missing-source creation intercepts actual no-replace publication");hit=true;PutBytes(root/kFileName,"NEWER SOURCE CREATED AT PUBLICATION");};
  verify(!WriteNotebook(root,{pending},expected) && hit,"A missing legacy source created at publication rejects stale migration");
  const auto reopened=ReadNotebook(root);verify(reopened.status==ReadStatus::Ok && reopened.notebook_snapshot.status==ReadStatus::Missing && reopened.pages.front().text==L"NEWER SOURCE CREATED AT PUBLICATION","Bound rollback removes only the new CTN so the latest source stays authoritative");
  std::cout<<"Native notebook publication: "<<assertions<<" boundary checks passed.\n";
}
