// Real production search commands and native RichEdit, under the existing
// process-private background guard. The historical foreground entry never runs.
#define wmain HistoricalFindNotesEntryNotRun
#include "NotesWorkspaceTests.cpp"
#undef wmain
static unsigned findScenarios=0;
static HWND FindFixture(const wchar_t *name,const wchar_t *text){
 ++findScenarios;Fixture(name);const auto owner=Open(name);Command(owner,IDC_NOTES_ADD_TAB);Type(owner,text);Save(owner);return owner;
}
static CHARRANGE FindSelection(HWND owner){CHARRANGE selected{};NotesSelection(Edit(owner),selected);return selected;}
static void FindQuery(HWND owner,const wchar_t *query){Command(owner,IDC_NOTES_FIND_TOGGLE);SetDlgItemTextW(owner,IDC_NOTES_FIND,query);SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);}
static std::wstring FindInfo(HWND owner){return NotesWindowText(GetDlgItem(owner,IDC_NOTES_FIND_INFO));}
static void FindOptionsAndNavigation(){
 const auto owner=FindFixture(L"Find options",L"Alpha alpha alphabet xalpha ALPHA");auto &state=State(owner);const auto root=state.clientRoot;
 const auto durable=client_notes::ReadNotebook(root).notebook_snapshot.bytes;Select(Edit(owner),4,4);const auto before=FindSelection(owner);const auto undo=SendMessageW(Edit(owner),EM_CANUNDO,0,0);
 FindQuery(owner,L"alpha");auto selected=FindSelection(owner);
 Check(selected.cpMin==before.cpMin&&selected.cpMax==before.cpMax,"Editing the search query never moves the document selection");
 Check(FindInfo(owner)==L"5 matches","Inline count finds case-insensitive text including word fragments");
 Check(SendMessageW(Edit(owner),EM_CANUNDO,0,0)==undo&&client_notes::ReadNotebook(root).notebook_snapshot.bytes==durable,"Counting does not create Undo entries or mutate the saved notebook");
 Command(owner,IDC_NOTES_FIND_MATCH_CASE);SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);
 Check(state.findMatchCase&&FindInfo(owner)==L"3 matches","Native Match case toggle restricts results using the original character casing");
 Command(owner,IDC_NOTES_FIND_MATCH_CASE);Command(owner,IDC_NOTES_FIND_WHOLE_WORD);SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);
 Check(state.findWholeWord&&FindInfo(owner)==L"3 matches","Native Whole word toggle excludes longer prefix and suffix words");
 Command(owner,IDC_NOTES_FIND_WHOLE_WORD);Command(owner,IDC_NOTES_FIND_WRAP);Select(Edit(owner),notes_find::Length(Edit(owner)),notes_find::Length(Edit(owner)));
 const auto atEnd=FindSelection(owner);Command(owner,IDC_NOTES_FIND_NEXT);selected=FindSelection(owner);
 Check(!state.findWrap&&selected.cpMin==atEnd.cpMin&&selected.cpMax==atEnd.cpMax&&FindInfo(owner).find(L"Reached the end")!=std::wstring::npos,"Find without Wrap stops at the end and explains it inline without changing selection");
 Command(owner,IDC_NOTES_FIND_WRAP);Command(owner,IDC_NOTES_FIND_NEXT);selected=FindSelection(owner);
 Check(selected.cpMin==0&&selected.cpMax==5&&FindInfo(owner).find(L"Wrapped to start")!=std::wstring::npos&&FindInfo(owner).find(L"1 of 5")!=std::wstring::npos,"Find next wraps to the first native match and reports its current ordinal");
 Command(owner,IDC_NOTES_FIND_PREV);selected=FindSelection(owner);
 Check(selected.cpMin==28&&selected.cpMax==33&&FindInfo(owner).find(L"Wrapped to end")!=std::wstring::npos,"Find previous wraps to the last native match");
 lastNotice=L"unchanged notice sentinel";SetDlgItemTextW(owner,IDC_NOTES_FIND,L"does not exist");SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);Command(owner,IDC_NOTES_FIND_NEXT);
 Check(FindInfo(owner)==L"No matches"&&lastNotice==L"unchanged notice sentinel","No-match feedback stays inline and never opens a modal prompt");
 Command(owner,IDC_NOTES_FIND_CLOSE);Check(!state.findVisible&&!state.replaceVisible,"Close search collapses the entire temporary editing surface");DestroyWindow(owner);NotesBackgroundAudit("find options and navigation");
}
static void ReplaceOptionsAndHistory(){
 const auto owner=FindFixture(L"Replace options",L"Alpha alpha alphabet xalpha ALPHA");auto &state=State(owner);
 FindQuery(owner,L"alpha");Command(owner,IDC_NOTES_FIND_WHOLE_WORD);Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"DONE");
 Check(state.findVisible&&state.replaceVisible,"Replace opens its inline edit and actions in the existing temporary search surface");
 const auto history=state.pages[state.activePage].history.size();Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(NotesWindowText(Edit(owner))==L"DONE DONE alphabet xalpha DONE"&&FindInfo(owner).find(L"Replaced 3 matches")!=std::wstring::npos,"Replace all honors Whole word while preserving untouched fragments");
 Save(owner);Check(state.pages[state.activePage].history.size()>=history,"Replacing text retains existing version history");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==L"Alpha alpha alphabet xalpha ALPHA","All native replacements undo together as one user action");
 Command(owner,IDC_NOTES_REDO);Check(NotesWindowText(Edit(owner))==L"DONE DONE alphabet xalpha DONE","The grouped replacement supports native Redo");DestroyWindow(owner);NotesBackgroundAudit("replace options and history");
}
static void UnicodeFindAndReplace(){
 const auto owner=FindFixture(L"Unicode literal search",L"\u00c5ngstr\u00f6m \u00e5ngstr\u00f6m \u00e5ngstr\u00f6ms \U0001f600 printer \U0001f600");
 FindQuery(owner,L"\u00e5ngstr\u00f6m");Check(FindInfo(owner)==L"3 matches","Case-insensitive Unicode search retains non-ASCII letters and diacritics");
 Command(owner,IDC_NOTES_FIND_WHOLE_WORD);SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);Check(FindInfo(owner)==L"2 matches","Unicode Whole word search excludes a longer suffixed word");
 Command(owner,IDC_NOTES_FIND_WHOLE_WORD);SetDlgItemTextW(owner,IDC_NOTES_FIND,L"\U0001f600");SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);Check(FindInfo(owner)==L"2 matches","A supplementary Unicode character is counted as a complete literal search term");
 Select(Edit(owner),0,0);Command(owner,IDC_NOTES_FIND_NEXT);const auto selected=FindSelection(owner);Check(selected.cpMax-selected.cpMin==2&&notes_find::RangeText(Edit(owner),selected)==L"\U0001f600","Native selection uses both UTF-16 units of the matched supplementary character");
 Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"\u732b");Command(owner,IDC_NOTES_REPLACE_ALL);Check(NotesWindowText(Edit(owner)).find(L"\U0001f600")==std::wstring::npos&&FindInfo(owner).find(L"Replaced 2 matches")!=std::wstring::npos,"Unicode replacement safely changes two surrogate-pair matches to a different script");
 Command(owner,IDC_NOTES_UNDO);Check(notes_find::Count(Edit(owner),L"\U0001f600",{},{}).count==2,"One Undo restores both complete supplementary characters");DestroyWindow(owner);NotesBackgroundAudit("Unicode find and replace");
}
static void ReplaceFiniteAndEmpty(){
 const auto owner=FindFixture(L"Replacement contents",L"a a");FindQuery(owner,L"a");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"aa");Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(NotesWindowText(Edit(owner))==L"aa aa"&&FindInfo(owner).find(L"Replaced 2 matches")!=std::wstring::npos,"Replace all visits original matches once when replacement text contains the query");
 Command(owner,IDC_NOTES_UNDO);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"");Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(NotesWindowText(Edit(owner))==L" ","An empty replacement removes only matched text");Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==L"a a","Deleting every match remains one native Undo action");
 auto &state=State(owner);state.findWrap=false;Select(Edit(owner),3,3);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"b");Command(owner,IDC_NOTES_REPLACE_NEXT);Check(NotesWindowText(Edit(owner))==L"a a","Replace next respects a disabled Wrap option at the end of the note");
 state.findWrap=true;Select(Edit(owner),0,1);Command(owner,IDC_NOTES_REPLACE_NEXT);Check(NotesWindowText(Edit(owner))==L"b a","Replace next updates an already selected matching range before advancing");DestroyWindow(owner);NotesBackgroundAudit("finite and empty replacements");
}
static void BulkReplaceUndo(){
 std::wstring original;for(unsigned i=0;i<10000;++i)original+=L"alpha ";const auto owner=FindFixture(L"Ten thousand replacements",original.c_str());
 SendMessageW(Edit(owner),EM_EMPTYUNDOBUFFER,0,0);Select(Edit(owner),0,0);Type(owner,L"Prior user edit ");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);const auto before=NotesWindowText(Edit(owner));
 FindQuery(owner,L"alpha");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"beta");Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(FindInfo(owner).find(L"Replaced 10000 matches")!=std::wstring::npos&&NotesWindowText(Edit(owner)).find(L"alpha")==std::wstring::npos,"Large replacement completes all ten thousand original matches");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==before,"One Undo restores all ten thousand replacements under the unchanged native Undo limit");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==original,"The user edit preceding a large replacement remains available in Undo history");
 SetDlgItemTextW(owner,IDC_NOTES_FIND,L"a");SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);Check(FindInfo(owner).find(L"10000+")!=std::wstring::npos,"A very large search reports its bounded ten-thousand-plus count");DestroyWindow(owner);NotesBackgroundAudit("large replacement and bounded count");
}
static void ReplaceLinksAndFormatting(){
 const auto owner=FindFixture(L"Search link isolation",L"alpha Before Support after alpha");auto &state=State(owner);Select(Edit(owner),13,20);inputs.push_back({L"Support",L"https://example.test/support?client=private"});Command(owner,IDC_NOTES_LINK);
 Select(Edit(owner),0,5);Command(owner,IDC_NOTES_BOLD);Save(owner);const auto before=NotesWindowText(Edit(owner));FindQuery(owner,L"alpha");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"long replacement");Command(owner,IDC_NOTES_REPLACE_ALL);
 FINDTEXTEXW support{{0,-1},L"Support",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&support))>=0,"The existing labeled link remains present after replacement shifts its position");
 Check(NotesNativeLinkAddress(Edit(owner),state,support.chrgText)==L"https://example.test/support?client=private","An untouched labeled link retains its exact destination and native cookie after shifted replacements");
 Select(Edit(owner),0,16);CHARFORMAT2W format{sizeof(format)};SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));Check((format.dwEffects&CFE_BOLD)!=0,"Replacement inherits the matched text's character formatting");
 Save(owner);Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==before&&NotesNativeLinkAddress(Edit(owner),state,{13,20})==L"https://example.test/support?client=private","Undo restores original text and exact labeled-link destination together");Command(owner,IDC_NOTES_REDO);Save(owner);
 const auto saved=client_notes::ReadNotebook(state.clientRoot);Check(std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return std::any_of(page.links.begin(),page.links.end(),[](const auto &link){return link.second==L"https://example.test/support?client=private";});}),"The unchanged link destination survives autosave and notebook reread");
 FindQuery(owner,L"Support");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"Helpdesk");support={{0,-1},L"Support",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&support))>=0,"The existing link label can be selected as a literal replacement match");Select(Edit(owner),support.chrgText.cpMin,support.chrgText.cpMax);Command(owner,IDC_NOTES_REPLACE_NEXT);
 FINDTEXTEXW helpdesk{{0,-1},L"Helpdesk",{}};const bool relabeled=SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&helpdesk))>=0;const bool immediateTarget=relabeled&&NotesNativeLinkAddress(Edit(owner),state,helpdesk.chrgText)==L"https://example.test/support?client=private";Save(owner);
 const auto relabeledSaved=client_notes::ReadNotebook(state.clientRoot);const bool durableTarget=std::any_of(relabeledSaved.pages.begin(),relabeledSaved.pages.end(),[](const auto &page){return std::find(page.links.begin(),page.links.end(),std::pair{std::wstring(L"Helpdesk"),std::wstring(L"https://example.test/support?client=private")})!=page.links.end();});std::cout<<"Inside-link replacement: native destination="<<immediateTarget<<"; saved exact label/destination="<<durableTarget<<std::endl;
 Check(immediateTarget&&durableTarget,"Replacing a labeled link's own text preserves its exact destination immediately and after saving");Command(owner,IDC_NOTES_UNDO);support={{0,-1},L"Support",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&support))>=0&&NotesNativeLinkAddress(Edit(owner),state,support.chrgText)==L"https://example.test/support?client=private","Undo of a link-label replacement restores the original label and exact destination");Command(owner,IDC_NOTES_REDO);helpdesk={{0,-1},L"Helpdesk",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&helpdesk))>=0&&NotesNativeLinkAddress(Edit(owner),state,helpdesk.chrgText)==L"https://example.test/support?client=private","Redo of a whole-link label replacement retains its exact destination");
 FindQuery(owner,L"desk");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"portal");FINDTEXTEXW part{{0,-1},L"desk",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&part))>=0,"A partial link-label match is selected independently of its whole label");Select(Edit(owner),part.chrgText.cpMin,part.chrgText.cpMax);Command(owner,IDC_NOTES_REPLACE_NEXT);FINDTEXTEXW portal{{0,-1},L"Helpportal",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&portal))>=0&&NotesNativeLinkAddress(Edit(owner),state,portal.chrgText)==L"https://example.test/support?client=private","Replacing part of a link label preserves the complete link's original destination");Save(owner);
 const auto partialSaved=client_notes::ReadNotebook(state.clientRoot);Check(std::any_of(partialSaved.pages.begin(),partialSaved.pages.end(),[](const auto &page){return std::find(page.links.begin(),page.links.end(),std::pair{std::wstring(L"Helpportal"),std::wstring(L"https://example.test/support?client=private")})!=page.links.end();}),"The partial-label edit persists its complete label and exact address");
 Command(owner,IDC_NOTES_UNDO);helpdesk={{0,-1},L"Helpdesk",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&helpdesk))>=0&&NotesNativeLinkAddress(Edit(owner),state,helpdesk.chrgText)==L"https://example.test/support?client=private","Undo of a partial-label replacement restores the preceding exact label and address");Command(owner,IDC_NOTES_REDO);portal={{0,-1},L"Helpportal",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&portal))>=0&&NotesNativeLinkAddress(Edit(owner),state,portal.chrgText)==L"https://example.test/support?client=private","Redo of a partial-label replacement retains the original address");Command(owner,IDC_NOTES_UNDO);Command(owner,IDC_NOTES_UNDO);Save(owner);
 Select(Edit(owner),LONG_MAX,LONG_MAX);Type(owner,L"\r\n");NotesCodeInput code;code.language=1;code.source=L"alpha = 1";Check(NotesInsertCode(owner,state,code),"A native code table is inserted after the existing formatted note and link");
 Select(Edit(owner),LONG_MAX,LONG_MAX);Type(owner,L"Trailing alpha");Save(owner);FindQuery(owner,L"alpha");
 Check(FindInfo(owner)==L"2 matches","Search finds literal text inside a native code-table cell and in prose after the table");
 Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"omega");Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(FindInfo(owner).find(L"Replaced 2 matches")!=std::wstring::npos&&notes_find::Count(Edit(owner),L"omega",{},{}).count==2,"Replace all changes both the code-table match and the later prose match");
 std::string table;StreamClientNotesOut(Edit(owner),table);Check(table.find("\\trowd")!=std::string::npos&&table.find("\\intbl")!=std::string::npos,"Replacing code text retains the native table structure in exported RTF");
 support={{0,-1},L"Support",{}};Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&support))>=0&&NotesNativeLinkAddress(Edit(owner),state,support.chrgText)==L"https://example.test/support?client=private","Replacing text in and after a code table preserves the independent labeled link's exact address");
 Command(owner,IDC_NOTES_UNDO);Check(notes_find::Count(Edit(owner),L"alpha",{},{}).count==2,"One Undo restores both table and post-table replacements");DestroyWindow(owner);NotesBackgroundAudit("replacement formatting, code tables and link isolation");
}
static WNDPROC findRejectPrevious=nullptr;
static LRESULT CALLBACK FindRejectSelection(HWND edit,UINT message,WPARAM wParam,LPARAM lParam){if(message==EM_EXSETSEL)return 0;return CallWindowProcW(findRejectPrevious,edit,message,wParam,lParam);}
static void FindRejectedSelection(HWND owner){
 auto &state=State(owner);(void)state;SetWindowTextW(Edit(owner),L"alpha delta");SendMessageW(Edit(owner),EM_EMPTYUNDOBUFFER,0,0);Select(Edit(owner),0,0);Type(owner,L"Prior ");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Select(Edit(owner),6,11);Command(owner,IDC_NOTES_BOLD);SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Select(Edit(owner),12,17);const auto before=NotesWindowText(Edit(owner));std::string rtfBefore;Check(StreamClientNotesOut(Edit(owner),rtfBefore),"The rejected-selection fixture retains its original native formatted document");
 findRejectPrevious=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(Edit(owner),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(FindRejectSelection)));const auto failed=notes_find::Replace(Edit(owner),L"alpha",L"omega",{},true);SetWindowLongPtrW(Edit(owner),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(findRejectPrevious));findRejectPrevious=nullptr;std::string rtfAfter;StreamClientNotesOut(Edit(owner),rtfAfter);
 Check(failed.status==notes_find::ReplaceStatus::Failed&&failed.count==0&&NotesWindowText(Edit(owner))==before&&rtfAfter==rtfBefore,"A rejected native target selection cannot replace a different same-length selection or change the original RTF");
 Command(owner,IDC_NOTES_UNDO);Select(Edit(owner),6,11);CHARFORMAT2W format{sizeof(format)};SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));Check(!(format.dwEffects&CFE_BOLD),"A rejected target selection preserves the user's previous formatting Undo action");Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==L"alpha delta","A rejected target selection preserves the user's older text Undo action");
}
static void FindFailedRollbackRedo(HWND owner){
 auto &state=State(owner);const std::wstring original=L"alpha alpha alpha";SetWindowTextW(Edit(owner),original.c_str());SendMessageW(Edit(owner),EM_EMPTYUNDOBUFFER,0,0);
 Select(Edit(owner),0,0);Type(owner,L"Prior ");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Save(owner);const auto before=NotesWindowText(Edit(owner));std::string rtfBefore;Check(StreamClientNotesOut(Edit(owner),rtfBefore),"The aborted replacement has an exact native RTF baseline and an older text Undo action");
 const auto sourceRoot=state.clientRoot;const auto sourceBytes=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;
 const std::wstring otherName=L"Find aborted other client";Fixture(otherName);const auto otherRoot=g_sDataDir/L"Sites"/otherName;const auto otherNotebook=client_notes::ReadNotebook(otherRoot);
 client_notes::NotePage other{L"Unrelated private note",true,"{\\rtf1\\ansi Unrelated client private}",{}};other.id=1;other.open=true;other.active=true;Check(client_notes::WriteNotebook(otherRoot,{other},otherNotebook),"The aborted replacement probe has another client's immutable durable notebook");const auto otherBytes=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes;
 unsigned visited=0;const auto result=notes_find::Replace(Edit(owner),L"alpha",L"beta",{},true,[&](const CHARRANGE &){if(++visited==2)throw std::runtime_error("Private abort before the second replacement");});
 std::string rollbackRtf;StreamClientNotesOut(Edit(owner),rollbackRtf);Check(result.status==notes_find::ReplaceStatus::Failed&&result.rolledBack&&NotesWindowText(Edit(owner))==before&&rollbackRtf==rtfBefore,"The failed transaction restores its complete text and native formatted baseline before Redo");
 const bool canRedo=SendMessageW(Edit(owner),EM_CANREDO,0,0)!=0;Command(owner,IDC_NOTES_REDO);const auto afterRedo=NotesWindowText(Edit(owner));std::string redoRtf;StreamClientNotesOut(Edit(owner),redoRtf);const bool unchanged=afterRedo==before&&redoRtf==rtfBefore;
 SendMessageW(owner,WM_TIMER,1,0);const bool saveOk=!state.saveFailed;const bool durableUnchanged=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes==sourceBytes;
 std::wcout<<L"Aborted replacement Redo: baseline=["<<before<<L"]; canRedo="<<canRedo<<L"; afterRedo=["<<afterRedo<<L"]; full native unchanged="<<unchanged<<L"; timer save="<<saveOk<<L"; durable bytes unchanged="<<durableUnchanged<<std::endl;
 Check(client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes,"Redo after an aborted replacement never writes the other client's notebook");
 if(!unchanged){Command(owner,IDC_NOTES_UNDO);std::string recovered;StreamClientNotesOut(Edit(owner),recovered);Check(NotesWindowText(Edit(owner))==before&&recovered==rtfBefore,"Undo after resurrecting an aborted partial replacement can recover the exact baseline");}
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==original,"The user edit before an aborted replacement remains independently recoverable after the Redo probe");
 Check(unchanged&&saveOk&&durableUnchanged,"Pure Redo cannot resurrect or autosave any part of an aborted replacement transaction");
}
static void FindRollbackCleanupProbe(){
 const char *const methods[]={"control","empty-nonundo","empty-undo","suspend-empty","group-empty-undo","group-text-barrier-undo","same-character-undo","format-barrier-undo","suspend-text-barrier"};
 const std::wstring original=L"alpha alpha alpha Support",url=L"https://example.test/cleanup-link?client=private";
 const std::wstring otherName=L"Cleanup probe other client";Fixture(otherName);const auto otherRoot=g_sDataDir/L"Sites"/otherName;const auto otherOriginal=client_notes::ReadNotebook(otherRoot);client_notes::NotePage other{L"Other private",true,"{\\rtf1\\ansi Other client private}",{}};other.id=1;
 Check(client_notes::WriteNotebook(otherRoot,{other},otherOriginal),"Cleanup probes retain an independent other-client notebook");const auto otherBytes=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes;
 for(unsigned method=0;method<std::size(methods);++method)for(const bool redoFirst:{false,true}){
  const auto name=L"Cleanup "+std::to_wstring(method)+(redoFirst?L" redo first":L" older first");const auto owner=FindFixture(name.c_str(),original.c_str());auto &state=State(owner);
  Select(Edit(owner),18,25);inputs.push_back({L"Support",url});Command(owner,IDC_NOTES_LINK);Select(Edit(owner),0,5);Command(owner,IDC_NOTES_BOLD);Save(owner);SendMessageW(Edit(owner),EM_EMPTYUNDOBUFFER,0,0);
  std::string originalRtf;Check(StreamClientNotesOut(Edit(owner),originalRtf),"Cleanup probe captures the older formatted document and exact link before the user edit");Select(Edit(owner),0,0);Type(owner,L"Prior ");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Save(owner);const auto before=NotesWindowText(Edit(owner));std::string beforeRtf;StreamClientNotesOut(Edit(owner),beforeRtf);const auto sourceBytes=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  const auto exact=[&](const std::wstring &text,const std::string &rtf){std::string actual;FINDTEXTEXW link{{0,-1},L"Support",{}};return NotesWindowText(Edit(owner))==text&&StreamClientNotesOut(Edit(owner),actual)&&actual==rtf&&SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&link))>=0&&NotesNativeLinkAddress(Edit(owner),state,link.chrgText)==url;};
  unsigned visited=0;const auto aborted=notes_find::Replace(Edit(owner),L"alpha",L"beta",{},true,[&](const CHARRANGE &){if(++visited==2)throw std::runtime_error("Private rollback cleanup probe interruption");});
  Check(aborted.status==notes_find::ReplaceStatus::Failed&&aborted.rolledBack&&exact(before,beforeRtf),"Every cleanup technique starts from a fully restored aborted transaction");const bool beforeUndo=SendMessageW(Edit(owner),EM_CANUNDO,0,0)!=0,beforeRedo=SendMessageW(Edit(owner),EM_CANREDO,0,0)!=0;
  IUnknown *ole=nullptr;ITextDocument *document=nullptr;SendMessageW(Edit(owner),EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document));ole->Release();}Check(document!=nullptr,"Cleanup probe retains its native TOM document only while the editor exists");
  CHARRANGE selection{};NotesSelection(Edit(owner),selection);Select(Edit(owner),0,0);const auto empty=[&](bool undo){SendMessageW(Edit(owner),EM_REPLACESEL,undo?TRUE:FALSE,reinterpret_cast<LPARAM>(L""));};
  switch(method){
   case 1:empty(false);break;
   case 2:empty(true);break;
   case 3:document->Undo(tomSuspend,nullptr);empty(true);document->Undo(tomResume,nullptr);break;
   case 4:document->BeginEditCollection();empty(true);document->EndEditCollection();SendMessageW(Edit(owner),EM_UNDO,0,0);break;
   case 5:document->BeginEditCollection();SendMessageW(Edit(owner),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\u2060"));Select(Edit(owner),0,1);empty(true);document->EndEditCollection();SendMessageW(Edit(owner),EM_UNDO,0,0);break;
   case 6:Select(Edit(owner),0,1);document->BeginEditCollection();SendMessageW(Edit(owner),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"P"));document->EndEditCollection();SendMessageW(Edit(owner),EM_UNDO,0,0);break;
   case 7:{Select(Edit(owner),0,1);CHARFORMAT2W initial{sizeof(initial)};SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&initial));CHARFORMAT2W toggle{sizeof(toggle)};toggle.dwMask=CFM_ITALIC;toggle.dwEffects=(initial.dwEffects&CFE_ITALIC)?0:CFE_ITALIC;document->BeginEditCollection();SendMessageW(Edit(owner),EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&toggle));toggle.dwEffects=initial.dwEffects&CFE_ITALIC;SendMessageW(Edit(owner),EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&toggle));document->EndEditCollection();SendMessageW(Edit(owner),EM_UNDO,0,0);break;}
   case 8:document->Undo(tomSuspend,nullptr);SendMessageW(Edit(owner),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\u2060"));Select(Edit(owner),0,1);empty(true);document->Undo(tomResume,nullptr);break;
  }
  document->Release();SendMessageW(Edit(owner),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));const bool cleanupExact=exact(before,beforeRtf),afterUndo=SendMessageW(Edit(owner),EM_CANUNDO,0,0)!=0,afterRedo=SendMessageW(Edit(owner),EM_CANREDO,0,0)!=0;
  bool firstRedoExact=true;if(redoFirst){Command(owner,IDC_NOTES_REDO);firstRedoExact=exact(before,beforeRtf);}Command(owner,IDC_NOTES_UNDO);const bool olderUndoExact=exact(original,originalRtf);Command(owner,IDC_NOTES_REDO);const bool olderRedoExact=exact(before,beforeRtf);Command(owner,IDC_NOTES_REDO);const bool extraRedoExact=exact(before,beforeRtf);
  const bool sourceUnchanged=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==sourceBytes,otherUnchanged=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes;
  std::cout<<"Rollback cleanup probe: method="<<methods[method]<<"; redoFirst="<<redoFirst<<"; beforeCanUndo="<<beforeUndo<<"; beforeCanRedo="<<beforeRedo<<"; cleanupExact="<<cleanupExact<<"; afterCanUndo="<<afterUndo<<"; afterCanRedo="<<afterRedo<<"; firstRedoExact="<<firstRedoExact<<"; olderUndoExact="<<olderUndoExact<<"; olderRedoExact="<<olderRedoExact<<"; extraRedoExact="<<extraRedoExact<<"; sourceBytesUnchanged="<<sourceUnchanged<<"; otherBytesUnchanged="<<otherUnchanged<<"; supported="<<(cleanupExact&&firstRedoExact&&olderUndoExact&&olderRedoExact&&extraRedoExact&&sourceUnchanged&&otherUnchanged)<<std::endl;
  DestroyWindow(owner);NotesBackgroundAudit("native rollback cleanup technique");
 }
}
static void FindLinkedLabelBudget(HWND owner){
 auto &state=State(owner);const std::wstring label(2048,L'a'),address=L"https://example.test/find-budget?client=source";
 SetWindowTextW(Edit(owner),L"");Select(Edit(owner),0,0);Type(owner,(label+L" end").c_str());Select(Edit(owner),0,2048);
 inputs.push_back({label,address});Command(owner,IDC_NOTES_LINK);Save(owner);
 const auto linked=client_notes::ReadNotebook(state.clientRoot);const auto &savedPage=linked.pages[state.activePage];
 Check(savedPage.links==std::vector<std::pair<std::wstring,std::wstring>>{{label,address}}&&savedPage.linkRanges.size()==1&&savedPage.linkRanges[0].first==0&&savedPage.linkRanges[0].last==2048&&NotesNativeLinkAddress(Edit(owner),state,{0,2048})==address,"A maximum-length saved label has one exact native range and its original destination");
 SendMessageW(Edit(owner),EM_EMPTYUNDOBUFFER,0,0);Select(Edit(owner),LONG_MAX,LONG_MAX);Type(owner,L" keep");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Save(owner);
 const auto before=NotesWindowText(Edit(owner));const auto sourceRoot=state.clientRoot;const auto sourceName=state.clientName;const auto sourcePageId=state.pages[state.activePage].id;std::string rtfBefore;Check(StreamClientNotesOut(Edit(owner),rtfBefore),"The linked-label budget fixture has an exact native formatted baseline and an older user Undo action");
 const std::wstring otherName=L"Find budget other client";Fixture(otherName);const auto otherRoot=g_sDataDir/L"Sites"/otherName;auto otherNotebook=client_notes::ReadNotebook(otherRoot);
 client_notes::NotePage other{L"Private other note",true,"{\\rtf1\\ansi Other client private}",{}};other.id=1;other.open=true;other.active=true;
 Check(client_notes::WriteNotebook(otherRoot,{other},otherNotebook),"An independent destination client has a durable private notebook before the replacement");const auto otherBytes=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes;
 FindQuery(owner,L"a");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"aa");Save(owner);const auto sourceBytes=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;Command(owner,IDC_NOTES_REPLACE_ALL);
 const auto after=NotesWindowText(Edit(owner));std::string rtfAfter;StreamClientNotesOut(Edit(owner),rtfAfter);const bool refused=after==before&&rtfAfter==rtfBefore;
 const auto targetAfter=NotesNativeLinkAddress(Edit(owner),state,{0,static_cast<LONG>(after.size()-9)});
 SendMessageW(owner,WM_TIMER,1,0);const bool autosaveOk=!state.saveFailed;const bool explicitSaveOk=SaveClientNotesDraft(owner,state,false);
 bool switchAttempted=false,switched=false;if(!refused){switchAttempted=true;switched=SwitchClientNotes(owner,state,otherName,otherRoot);}
 const auto committed=client_notes::ReadNotebook(sourceRoot);const auto saved=std::find_if(committed.pages.begin(),committed.pages.end(),[&](const auto &page){return page.id==sourcePageId;});
 const bool durableMapping=saved!=committed.pages.end()&&saved->links==savedPage.links&&saved->linkRanges==savedPage.linkRanges&&saved->linkRangesKnown==savedPage.linkRangesKnown;
 const auto reopened=Open(sourceName);const auto &reopenedState=State(reopened);const auto reopenedText=NotesWindowText(Edit(reopened));const bool reopenedTarget=reopenedText.size()>=9&&NotesNativeLinkAddress(Edit(reopened),reopenedState,{0,static_cast<LONG>(reopenedText.size()-9)})==address;DestroyWindow(reopened);g_hNotesWindow=owner;
 std::cout<<"Find linked-label budget: baseline label=2048; result linked length="<<(after.size()-9)<<"; original destination="<<(targetAfter==address)<<"; preflight refused="<<refused<<"; timer save="<<autosaveOk<<"; explicit save="<<explicitSaveOk<<"; switch attempted="<<switchAttempted<<"; switched="<<switched<<"; saved targets="<<(saved==committed.pages.end()?0:saved->links.size())<<"; saved ranges="<<(saved==committed.pages.end()?0:saved->linkRanges.size())<<"; exact durable mapping="<<durableMapping<<"; fresh reopen destination="<<reopenedTarget<<"; durable bytes unchanged="<<(committed.notebook_snapshot.bytes==sourceBytes)<<std::endl;
 Check(client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes,"The linked-label replacement and refused source transition leave the other client's notebook byte-for-byte unchanged");
 Check(durableMapping&&reopenedTarget&&reopenedText==before,"An over-budget replacement preserves the exact saved link target and occurrence through a fresh client reopen");
 Check(committed.notebook_snapshot.bytes==sourceBytes,"An over-budget linked-label replacement cannot overwrite the source client's durable baseline");
 Check(!switched&&state.clientRoot==sourceRoot,"A source save failure cannot attach the unsaved editor to a different client");
 if(!refused){Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==before&&NotesNativeLinkAddress(Edit(owner),state,{0,2048})==address,"One Undo recovers the exact original label and address after the unsafe expansion");}
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==label+L" end"&&NotesNativeLinkAddress(Edit(owner),state,{0,2048})==address,"The user edit preceding a refused or recovered label replacement remains available in Undo history");
 Check(refused&&autosaveOk&&explicitSaveOk,"Replace all refuses a linked label above the persistent 2048-character limit before mutation and leaves autosave usable");
 Command(owner,IDC_NOTES_REDO);Check(NotesWindowText(Edit(owner))==before,"Redo after a refused replacement reapplies only the user's preceding edit");Save(owner);
 Check(SwitchClientNotes(owner,state,otherName,otherRoot)&&NotesWindowText(Edit(owner))==L"Other client private","After budget refusal and Undo/Redo the same Notes window can switch to the other client's exact private document");
 Check(client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes,"Opening the independent client's note after a refused replacement preserves its durable bytes");
}
#include "NotesFindProspectiveTests.inl"
static void FindProductionRecoveryCases();
static void ReplaceRefusalAndRollback(){
 const auto owner=FindFixture(L"Replacement safety",L"alpha alpha alpha");FindQuery(owner,L"alpha");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"expanded replacement");const auto before=NotesWindowText(Edit(owner));
 SendMessageW(Edit(owner),EM_EXLIMITTEXT,0,static_cast<LPARAM>(before.size()+1));Command(owner,IDC_NOTES_REPLACE_ALL);Check(NotesWindowText(Edit(owner))==before&&FindInfo(owner).find(L"too large")!=std::wstring::npos,"An oversized replacement is refused before any document change");SendMessageW(Edit(owner),EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
 SendMessageW(Edit(owner),EM_SETREADONLY,TRUE,0);Command(owner,IDC_NOTES_REPLACE_ALL);Check(NotesWindowText(Edit(owner))==before&&FindInfo(owner).find(L"read-only")!=std::wstring::npos,"A read-only editor refuses replacement without changing its text");SendMessageW(Edit(owner),EM_SETREADONLY,FALSE,0);
 SendMessageW(Edit(owner),EM_EMPTYUNDOBUFFER,0,0);Select(Edit(owner),0,0);Type(owner,L"Prior ");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);const auto transactionBefore=NotesWindowText(Edit(owner));unsigned visited=0;
 const auto result=notes_find::Replace(Edit(owner),L"alpha",L"beta",{},true,[&](const CHARRANGE &){if(++visited==2)throw std::runtime_error("Private transaction interruption");});
 Check(result.status==notes_find::ReplaceStatus::Failed&&result.rolledBack&&NotesWindowText(Edit(owner))==transactionBefore,"An interrupted grouped replacement rolls back the earlier applied match");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==before,"Rolling back a failed replacement does not consume the user's earlier Undo action");FindRejectedSelection(owner);FindFailedRollbackRedo(owner);FindLinkedLabelBudget(owner);FindProspectiveBudgetCases(owner);FindProductionRecoveryCases();DestroyWindow(owner);NotesBackgroundAudit("replacement refusal and transaction rollback");
}
#include "NotesFindCapture.inl"
#include "NotesFindRollbackEdges.inl"
#include "NotesFindRecoveryTests.inl"
int wmain(int argc,wchar_t **argv){
 const bool captures=argc==3&&!wcscmp(argv[1],L"--capture-search-background");
 const bool redoProof=argc==2&&!wcscmp(argv[1],L"--rollback-redo-background"),budgetProof=argc==2&&!wcscmp(argv[1],L"--linked-label-budget-background"),cleanupProbe=argc==2&&!wcscmp(argv[1],L"--rollback-cleanup-probe-background"),edgeProbe=argc==2&&!wcscmp(argv[1],L"--rollback-cleanup-edges-background"),propertyProbe=argc==2&&!wcscmp(argv[1],L"--rollback-tom2-property-edges-background"),endProbe=argc==2&&!wcscmp(argv[1],L"--rollback-end-neutral-edges-background"),prospectiveProof=argc==2&&!wcscmp(argv[1],L"--prospective-budget-background"),repairedEndProbe=argc==2&&!wcscmp(argv[1],L"--rollback-repaired-end-edges-background"),autoOffProbe=argc==2&&!wcscmp(argv[1],L"--rollback-auto-off-end-edges-background"),typingProbe=argc==2&&!wcscmp(argv[1],L"--rollback-explicit-typing-end-edges-background"),typingControlProbe=argc==2&&!wcscmp(argv[1],L"--rollback-typing-control-end-edges-background"),charFormatProbe=argc==2&&!wcscmp(argv[1],L"--rollback-charformat-only-end-edges-background"),finalFormatProbe=argc==2&&!wcscmp(argv[1],L"--rollback-final-format-end-edges-background"),resumedFormatProbe=argc==2&&!wcscmp(argv[1],L"--rollback-final-format-resumed-end-edges-background"),selectionFontProbe=argc==2&&!wcscmp(argv[1],L"--rollback-selection-font-end-edges-background"),inheritedFontProbe=argc==2&&!wcscmp(argv[1],L"--rollback-inherited-font-end-edges-background"),contextProbe=argc==2&&!wcscmp(argv[1],L"--rollback-context-end-edges-background"),productionRecoveryProbe=argc==2&&!wcscmp(argv[1],L"--production-recovery-background");
 if(!captures&&!redoProof&&!budgetProof&&!cleanupProbe&&!edgeProbe&&!propertyProbe&&!endProbe&&!prospectiveProof&&!repairedEndProbe&&!autoOffProbe&&!typingProbe&&!typingControlProbe&&!charFormatProbe&&!finalFormatProbe&&!resumedFormatProbe&&!selectionFontProbe&&!inheritedFontProbe&&!contextProbe&&!productionRecoveryProbe&&(argc!=2||wcscmp(argv[1],L"--headless-background"))){std::cerr<<"Hidden background selector required\n";return 2;}
 try{std::cout<<std::unitbuf;backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
 g_sDataDir=fs::current_path()/L"build"/(L"notes-find-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"config.ini";WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());g_hGui=CreateWindowExW(0,L"STATIC",L"Private search parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
 const auto decoder=LoadLibraryW(L"Msftedit.dll");Check(decoder!=nullptr,"Fixture independently retains the native rich-text decoder");
 if(captures){
  const auto output=fs::weakly_canonical(fs::absolute(argv[2]));const auto allowed=fs::weakly_canonical(fs::current_path()/L"build").wstring()+L"\\";
  Check(output.wstring().size()>allowed.size()&&!_wcsnicmp(output.c_str(),allowed.c_str(),allowed.size()),"Native captures remain inside the project's build evidence directory");CaptureFindVisuals(output);
 }else if(productionRecoveryProbe)FindProductionRecoveryCases();
 else if(contextProbe)FindRollbackCleanupEdges(false,true,true,true,true,true,true,true,false,false,false,true);
 else if(selectionFontProbe||inheritedFontProbe)FindRollbackCleanupEdges(false,true,true,true,true,true,true,true,false,selectionFontProbe,inheritedFontProbe);
 else if(edgeProbe||propertyProbe||endProbe||repairedEndProbe||autoOffProbe||typingProbe||typingControlProbe||charFormatProbe||finalFormatProbe||resumedFormatProbe)FindRollbackCleanupEdges(propertyProbe,endProbe||repairedEndProbe||autoOffProbe||typingProbe||typingControlProbe||charFormatProbe||finalFormatProbe||resumedFormatProbe,repairedEndProbe||autoOffProbe||typingProbe||typingControlProbe||charFormatProbe||finalFormatProbe||resumedFormatProbe,autoOffProbe||typingProbe||typingControlProbe||charFormatProbe||finalFormatProbe||resumedFormatProbe,typingProbe||typingControlProbe||charFormatProbe||finalFormatProbe||resumedFormatProbe,typingControlProbe||charFormatProbe||finalFormatProbe||resumedFormatProbe,charFormatProbe||finalFormatProbe||resumedFormatProbe,finalFormatProbe||resumedFormatProbe,resumedFormatProbe);
 else if(prospectiveProof){const auto owner=FindFixture(L"Prospective replacement native proof",L"Private native proof");FindProspectiveBudgetCases(owner);DestroyWindow(owner);NotesBackgroundAudit("independent prospective replacement proof");}
 else if(cleanupProbe)FindRollbackCleanupProbe();
 else if(redoProof||budgetProof){const auto owner=FindFixture(redoProof?L"Aborted replacement Redo proof":L"Linked label budget proof",L"Private native proof");if(redoProof)FindFailedRollbackRedo(owner);else FindLinkedLabelBudget(owner);DestroyWindow(owner);NotesBackgroundAudit("independent adversarial Find proof");}
 else{FindOptionsAndNavigation();ReplaceOptionsAndHistory();UnicodeFindAndReplace();ReplaceFiniteAndEmpty();BulkReplaceUndo();ReplaceLinksAndFormatting();ReplaceRefusalAndRollback();FindEditChromeRegression();}
 DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("final Find and Replace audit");NotesBackgroundBanner();FreeLibrary(decoder);
 if(captures)std::cout<<"Notes Find visual capture: "<<checks<<" checks; 6 native surfaces; 0 failures\n";
 else std::cout<<"Notes Find and Replace: "<<checks<<" checks; "<<findScenarios<<" scenarios; 0 failures\n";return 0;
 }catch(const std::exception &error){std::cerr<<"FAIL Find and Replace: "<<error.what()<<std::endl;NotesBackgroundBanner();return 1;}}
