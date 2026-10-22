static LONG NativeTableCellStart(ITextDocument *document,unsigned index) {
  Microsoft::WRL::ComPtr<ITextRange> cell;
  if(!document || FAILED(document->Range(0,0,&cell)))return -1;
  LONG story=0;if(FAILED(cell->GetStoryLength(&story)) || story<1 || FAILED(cell->SetRange(0,story-1)))return -1;
  BSTR raw=nullptr;if(FAILED(cell->GetText(&raw)))return -1;
  const std::wstring text(raw?raw:L"",raw?SysStringLen(raw):0);SysFreeString(raw);
  // Locate actual cells from the native story's cell-end markers, rather
  // than repeating the helper's movement or assuming bulk unit counts.
  for(size_t at=0;at<text.size();++at)if(text[at]==L'\a') {
    if(index--!=0)continue;
    LONG moved=0,target=-1;
    if(FAILED(cell->SetRange(static_cast<LONG>(at),static_cast<LONG>(at))) || FAILED(cell->StartOf(tomCell,tomMove,&moved)))return -1;
    return SUCCEEDED(cell->GetStart(&target))?target:-1;
  }
  return -1;
}
#include "NotesMarkdownAutosaveTests.inl"
static void FilledTableNavigation() {
  ++groups;Fixture(L"Filled table Alpha");HWND owner=Open(L"Filled table Alpha");auto &state=State(owner);
  if(!HasActiveNote(state))Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Before filled table\r\n");Save(owner);HWND edit=Edit(owner);Select(edit,-1,-1);
  Check(NotesInsertSimpleTable(owner,state,3,3),"The filled-cell regression uses a real inserted three-by-three native table");
  LONG first=-1;const LONG length=GetWindowTextLengthW(edit);
  for(LONG at=0;at<=length;++at) {
    Select(edit,at,at);CHARRANGE accepted{};NotesSelection(edit,accepted);PARAFORMAT2 p{sizeof(p)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));
    if((p.wEffects&PFE_TABLE) && !(p.wEffects&PFE_TABLEROWDELIMITER) && accepted.cpMin==accepted.cpMax){first=accepted.cpMin;break;}
  }
  Microsoft::WRL::ComPtr<IUnknown> unknown;Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> cell;
  SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));
  const bool native=first>=0 && unknown && SUCCEEDED(unknown.As(&document)) && SUCCEEDED(document->Range(first,first,&cell));
  const auto position=[&](unsigned index)->LONG {
    return native?NativeTableCellStart(document.Get(),index):-1;
  };
  const auto fill=[&](unsigned index,const wchar_t *text)->bool {
    const auto at=position(index);if(at<0){std::cout<<"Native filled cell setup: cell="<<index+1<<" invalid TOM cell position\n";return false;}
    Select(edit,at,at);CHARRANGE accepted{};NotesSelection(edit,accepted);
    if(accepted.cpMin!=at || accepted.cpMax!=at){std::cout<<"Native filled cell setup: cell="<<index+1<<" requested="<<at<<" accepted="<<accepted.cpMin<<","<<accepted.cpMax<<"\n";return false;}
    SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Type(owner,text);SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
    Microsoft::WRL::ComPtr<ITextRange> typed;BSTR raw=nullptr;std::wstring actual;
    if(SUCCEEDED(document->Range(at,at+static_cast<LONG>(wcslen(text)),&typed)) && SUCCEEDED(typed->GetText(&raw))) {
      actual.assign(raw?raw:L"",raw?SysStringLen(raw):0);SysFreeString(raw);
    }
    CHARRANGE after{};NotesSelection(edit,after);
    std::cout<<"Native filled cell setup: cell="<<index+1<<" at="<<at<<" accepted="<<accepted.cpMin<<" after="<<after.cpMin<<" tomMatch="<<(actual==text)
      <<" legacyRangeMatch="<<(NotesRangeText(edit,at,at+static_cast<LONG>(wcslen(text)))==text)<<" actualUnits="<<actual.size()<<"\n";
    return actual==text;
  };
  const bool firstThree=fill(0,L"First") && fill(1,L"Second") && fill(2,L"Third");
  std::string beforeLast;StreamClientNotesOut(edit,beforeLast);
  Check(firstThree && fill(8,L"Last"),"Real editor replacements fill four cells independently of the navigation helper");
  Save(owner);std::string before;StreamClientNotesOut(edit,before);
  const auto saved=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  const bool formattingChanged=state.formattingChanged;const auto modified=SendMessageW(edit,EM_GETMODIFY,0,0);
  const auto navigation=[&](unsigned index,LONG offsetFirst,LONG offsetLast,bool previous,const char *kind) {
    const auto start=position(index);const CHARRANGE source{start+offsetFirst,start+offsetLast};
    const LONG expected=(previous?index==0:index==8)?source.cpMin:position(previous?index-1:index+1);
    const LONG expectedEnd=(previous?index==0:index==8)?source.cpMax:expected;
    Select(edit,source.cpMin,source.cpMax);const bool handled=NotesNavigateTableCell(edit,previous);CHARRANGE actual{};NotesSelection(edit,actual);
    std::string after;StreamClientNotesOut(edit,after);
    const std::string name=std::string("Filled table ")+kind+" cell="+std::to_string(index+1)+" offsets="+std::to_string(offsetFirst)+","+std::to_string(offsetLast)+(previous?" previous":" next");
    Check(start>=0 && expected>=0 && handled && actual.cpMin==expected && actual.cpMax==expectedEnd && after==before &&
          client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==saved && state.formattingChanged==formattingChanged && SendMessageW(edit,EM_GETMODIFY,0,0)==modified,
          name.c_str());
  };
  for(const auto [index,characters]:{std::pair{0u,5L},std::pair{1u,6L},std::pair{2u,5L},std::pair{8u,4L}})
    for(const LONG offset:{0L,characters/2,characters})for(const bool previous:{false,true})navigation(index,offset,offset,previous,"caret");
  for(const auto [begin,end]:{std::pair{1L,4L},std::pair{0L,6L}})
    for(const bool previous:{false,true})navigation(1,begin,end,previous,"selection");
  navigation(3,0,0,true,"row-boundary");
  SendMessageW(edit,EM_UNDO,0,0);std::string undone;StreamClientNotesOut(edit,undone);
  Check(undone==beforeLast,"Filled-cell navigation adds no Undo entry before undoing the final cell edit");
  SendMessageW(edit,EM_REDO,0,0);std::string redone;StreamClientNotesOut(edit,redone);
  Check(redone==before && client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==saved,
        "Redo restores the filled cell exactly while navigation and Undo leave saved notebook bytes unchanged");
  cell.Reset();document.Reset();unknown.Reset();DestroyWindow(owner);
  NotesBackgroundAudit("nonempty table caret/selection navigation, row crossing, boundaries and native Undo/Redo");
  MarkdownAutosaveUndoIsolation();
}
