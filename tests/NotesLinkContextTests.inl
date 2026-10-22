// Hidden native context menus. Clipboard and browser targets are process-private.
static POINT LinkContextPoint(HWND edit,LONG index,bool screen=true){
 POINTL position{};SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&position),index);
 POINT point{position.x+2,position.y+3};if(screen)ClientToScreen(edit,&point);return point;
}
static unsigned linkContextCases=0;
static bool linkContextOpen=false,linkContextCopy=false;
static unsigned linkContextDiagnosticMisses=0;
static void LinkContextExpected(bool condition,const char *message){
#ifdef CTSPACES_LINK_CONTEXT_DIAGNOSTIC
 if(!condition){++linkContextDiagnosticMisses;std::cout<<"DIAGNOSTIC unmet expectation: "<<message<<'\n';return;}
#endif
 Check(condition,message);
}
static void LinkContextDiagnosticUrl(HWND edit,LONG first,const std::wstring &expected){
 std::cout<<"DIAGNOSTIC expected UTF16=";for(wchar_t unit:expected)std::cout<<std::hex<<static_cast<unsigned>(unit)<<',';
 std::cout<<" captured UTF16=";for(wchar_t unit:notesCapturedClipboard)std::cout<<std::hex<<static_cast<unsigned>(unit)<<',';
 std::cout<<std::dec<<" open="<<linkContextOpen<<" copy="<<linkContextCopy<<" native=";
 for(LONG offset=0;offset<static_cast<LONG>(expected.size())+2;++offset){LONG cookie=0;const auto linked=NotesRangeLinkEffects(edit,{first+offset,first+offset+1},nullptr,&cookie);std::cout<<offset<<':'<<linked<<':'<<cookie<<',';}std::cout<<'\n';
}
static LONG LinkContextFind(HWND edit,const std::wstring &text){FINDTEXTEXW find{{0,-1},text.c_str(),{}};const auto result=SendMessageW(edit,EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&find));Check(result>=0,"Native editor finds the intended context target");return find.chrgText.cpMin;}
static std::function<void()> linkContextDuringPopup;
static void LinkContextMenuFor(HWND target,POINT point,UINT action=0,bool rightButton=false){
 linkContextOpen=linkContextCopy=false;
 notesPopupObserver=[](HMENU menu){
  const auto present=[&](UINT id,const wchar_t *text){MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_DATA|MIIM_STATE;if(!GetMenuItemInfoW(menu,id,FALSE,&item))return false;const auto *data=reinterpret_cast<MenuItemData *>(item.dwItemData);return data&&data->text==text&&!(item.fState&MFS_DISABLED);};
  linkContextOpen=present(7,L"Open link");linkContextCopy=present(8,L"Copy link address");
  if(linkContextDuringPopup){auto mutation=std::move(linkContextDuringPopup);linkContextDuringPopup={};mutation();}
 };
 popupAction=action;
 if(rightButton){SendMessageW(target,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(point.x,point.y));SendMessageW(target,WM_RBUTTONUP,0,MAKELPARAM(point.x,point.y));}
 else SendMessageW(target,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(target),MAKELPARAM(point.x,point.y));
 notesPopupObserver={};linkContextDuringPopup={};++linkContextCases;
}
static void LinkContextMenu(HWND owner,POINT point,UINT action=0,bool rightButton=false){LinkContextMenuFor(Edit(owner),point,action,rightButton);}
static bool SameSelection(HWND edit,const CHARRANGE &expected){CHARRANGE actual{};NotesSelection(edit,actual);return actual.cpMin==expected.cpMin&&actual.cpMax==expected.cpMax;}
static void NotesLinkContextCases(){
 const std::wstring client=L"Link context QA",destination=L"https://example.test/real-target?x=1&y=\u4E2D\u6587";
 Fixture(client);HWND owner=Open(client);Command(owner,IDC_NOTES_ADD_TAB);
 Type(owner,L"Keep selection\r\nSupport\r\nhttps://example.test/path?q=one&v=two.\r\nhttps://example.test/\u00E9?q=two&x=2\r\nPlain body");
 const LONG first=LinkContextFind(Edit(owner),L"Support");
 Select(Edit(owner),first,first+7);inputs.push_back({L"Support",destination});Command(owner,IDC_NOTES_LINK);Save(owner);
 Check(NotesNativeLinkAddress(Edit(owner),State(owner),{first,first+7})==destination,"Friendly-link fixture carries its actual TOM target");
 Select(Edit(owner),0,4);CHARRANGE original{};NotesSelection(Edit(owner),original);const auto originalText=NotesWindowText(Edit(owner));
 const auto originalWrites=notesCapturedClipboardWrites;LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+2),8);
 Check(linkContextOpen&&linkContextCopy,"Right-clicked friendly link offers Open link and Copy link address");
 Check(notesCapturedClipboardWrites==originalWrites+1&&notesCapturedClipboard==destination,"Copy link address captures immutable URL instead of its label or unrelated selection");
 Check(SameSelection(Edit(owner),original)&&NotesWindowText(Edit(owner))==originalText,"Link copy preserves existing selection and document");
 std::string richBefore;Check(StreamClientNotesOut(Edit(owner),richBefore),"Before-query rich text captured");
 const auto registry=State(owner).editorLinks.at(State(owner).attachedPageId);const auto history=SendMessageW(Edit(owner),EM_CANUNDO,0,0);
 LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+3,false),8,true);
 std::string richAfter;Check(StreamClientNotesOut(Edit(owner),richAfter),"After-query rich text captured");
 const auto &afterRegistry=State(owner).editorLinks.at(State(owner).attachedPageId);
 Check(SameSelection(Edit(owner),original)&&richBefore==richAfter&&history==SendMessageW(Edit(owner),EM_CANUNDO,0,0),"Actual right-button down/up preserves selection, formatting and Undo availability");
 Check(registry.urls==afterRegistry.urls&&registry.lastUse==afterRegistry.lastUse&&registry.nextCookie==afterRegistry.nextCookie&&registry.useCounter==afterRegistry.useCounter,"Target lookup never mutates immutable link cookies or registry history");
 Select(Edit(owner),first+2,first+2);LinkContextMenu(owner,{-1,-1},8);Check(linkContextCopy&&notesCapturedClipboard==destination,"Keyboard menu at link caret copies full destination");
 Select(Edit(owner),first+1,first+4);CHARRANGE partial{};NotesSelection(Edit(owner),partial);LinkContextMenu(owner,{-1,-1},8);Check(notesCapturedClipboard==destination&&SameSelection(Edit(owner),partial),"Keyboard menu accepts partial friendly-link selection without replacing it");
 Select(Edit(owner),first+7,first+7);LinkContextMenu(owner,{-1,-1},8);Check(linkContextCopy&&notesCapturedClipboard==destination,"Caret at a friendly-link end still resolves that link");
 // RichEdit auto detection accepts the accented path but ends before CJK
 // query text; exact CJK destinations are tested through explicit TOM links.
 for(const auto &url:{std::wstring(L"https://example.test/path?q=one&v=two"),std::wstring(L"https://example.test/\u00E9?q=two&x=2")}){
  const LONG position=LinkContextFind(Edit(owner),url);Check(NotesRangeLinkEffects(Edit(owner),{position+8,position+9}),"Native RichEdit auto-detects the test URL");
  Select(Edit(owner),0,4);LinkContextMenu(owner,LinkContextPoint(Edit(owner),position+10),8);if(!linkContextCopy||notesCapturedClipboard!=url)LinkContextDiagnosticUrl(Edit(owner),position,url);LinkContextExpected(linkContextCopy&&notesCapturedClipboard==url,"Auto URL context copies exact query/Unicode URL and excludes sentence punctuation");
  Select(Edit(owner),position+10,position+10);LinkContextMenu(owner,{-1,-1},8);LinkContextExpected(linkContextCopy&&notesCapturedClipboard==url,"Keyboard caret resolves entire auto-detected URL");
 }
 const auto beforeMisses=notesCapturedClipboardWrites;Select(Edit(owner),first,first+7);
 LinkContextMenu(owner,LinkContextPoint(Edit(owner),LinkContextFind(Edit(owner),L"Plain body")+2),8);
 Check(!linkContextOpen&&!linkContextCopy&&notesCapturedClipboardWrites==beforeMisses,"Right-clicking ordinary text does not reuse a separately selected link");
 RECT area{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&area));POINT outside{area.left-3,area.top+3};ClientToScreen(Edit(owner),&outside);LinkContextMenu(owner,outside,8);
 Check(!linkContextOpen&&!linkContextCopy&&notesCapturedClipboardWrites==beforeMisses,"Formatting margin is not mistaken for the nearest link");
 auto blank=LinkContextPoint(Edit(owner),first);ScreenToClient(Edit(owner),&blank);blank.x=area.right-2;ClientToScreen(Edit(owner),&blank);LinkContextMenu(owner,blank,8);
 Check(!linkContextOpen&&!linkContextCopy&&notesCapturedClipboardWrites==beforeMisses,"Whitespace beyond a short link line is not a link hit");
 Select(Edit(owner),first,LinkContextFind(Edit(owner),L"Plain body")+5);LinkContextMenu(owner,{-1,-1},8);
 Check(!linkContextOpen&&!linkContextCopy&&notesCapturedClipboardWrites==beforeMisses,"Keyboard selection spanning different links and ordinary text has no single target");
 const auto cancelWrites=notesCapturedClipboardWrites;LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+2));Check(notesCapturedClipboardWrites==cancelWrites,"Canceling the link menu leaves the clipboard untouched");
 Select(Edit(owner),-1,-1);SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);const auto undoBase=NotesWindowText(Edit(owner));Type(owner,L" Undo marker");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);
 LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+1),8);Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(Edit(owner))==undoBase&&NotesNativeLinkAddress(Edit(owner),State(owner),{first,first+7})==destination,"One Undo after copying removes only the previous text edit and retains original link binding");Command(owner,IDC_NOTES_REDO);Check(NotesWindowText(Edit(owner))==undoBase+L" Undo marker","Redo remains available after context copying");
 const auto oldEdge=g_sEdgePath;g_sEdgePath=g_sDataDir/L"msedge.exe";Put(g_sEdgePath,"Inert browser boundary; never executed");Check(NotesSaveLinkBrowser(2),"Private Notes preference chooses regular Edge");
 auto opened=shellOpens;answers.push_back(IDNO);LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+2),7);Check(shellOpens==opened,"Canceling existing browser confirmation opens no URL");answers.push_back(IDYES);LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+2),7);
 Check(shellOpens==opened+1&&shellTarget==g_sEdgePath&&shellParameters==QuoteCommandLineArgument(destination),"Context Open uses existing regular-PC browser route with only the exact URL");
 const auto pageWrites=notesCapturedClipboardWrites;linkContextDuringPopup=[&]{Command(owner,IDC_NOTES_ADD_TAB);};LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+2),8);Check(notesCapturedClipboardWrites==pageWrites,"Changing active note while menu is open cancels stale copy action");Command(owner,IDC_NOTES_CLOSE_TAB);
 auto &page=State(owner).pages[State(owner).activePage];page.ticket=L"T20261016.0123";page.ticketUrl=L"https://tickets.example.test/T20261016.0123?q=\u4E2D\u6587";State(owner).metadataChanged=true;UpdateNotesToolbar(owner,State(owner));LayoutClientNotes(owner,GetDpiForWindow(owner));Save(owner);
 const auto ticket=page.ticketUrl;const auto header=GetDlgItem(owner,IDC_NOTES_OPEN_TICKET);const auto headerMenu=[&](UINT action){LinkContextMenuFor(header,{-1,-1},action);};
 headerMenu(8);Check(linkContextOpen&&linkContextCopy&&notesCapturedClipboard==ticket,"Clickable ticket header copies its exact ticket URL");
 LinkContextMenuFor(header,{5,5},8,true);Check(linkContextCopy&&notesCapturedClipboard==ticket,"Ticket header right-button path exposes the same exact URL");
 opened=shellOpens;answers.push_back(IDYES);headerMenu(7);Check(shellOpens==opened+1&&shellTarget==g_sEdgePath&&shellParameters==QuoteCommandLineArgument(ticket),"Ticket header context Open retains regular browser routing");
 auto writes=notesCapturedClipboardWrites;headerMenu(0);Check(notesCapturedClipboardWrites==writes,"Canceling ticket header menu copies nothing");
 linkContextDuringPopup=[&]{State(owner).pages[State(owner).activePage].ticket+=L"X";};headerMenu(8);Check(notesCapturedClipboardWrites==writes,"Changed ticket number invalidates captured header action");
 linkContextDuringPopup=[&]{State(owner).pages[State(owner).activePage].ticketUrl=L"https://tickets.example.test/newer";};headerMenu(8);Check(notesCapturedClipboardWrites==writes,"Changed ticket URL invalidates captured header action");
 linkContextDuringPopup=[&]{Command(owner,IDC_NOTES_ADD_TAB);};headerMenu(8);Check(notesCapturedClipboardWrites==writes,"Changed active page invalidates captured header action");Command(owner,IDC_NOTES_CLOSE_TAB);
 State(owner).pages[State(owner).activePage].ticketUrl.clear();UpdateNotesToolbar(owner,State(owner));const auto menus=popupCalls;headerMenu(8);Check(!linkContextOpen&&!linkContextCopy&&popupCalls==menus&&notesCapturedClipboardWrites==writes,"Ticket number without a URL exposes no Open or Copy-address menu");
 Fixture(L"Other context QA");linkContextDuringPopup=[&]{Check(SwitchClientNotes(owner,State(owner),L"Other context QA",g_sDataDir/L"Sites/Other context QA"),"Popup mutation switches to independent client");};LinkContextMenu(owner,LinkContextPoint(Edit(owner),first+2),8);Check(notesCapturedClipboardWrites==writes,"Switching clients during body menu cancels stale action");
 g_sEdgePath=oldEdge;DestroyWindow(owner);NotesBackgroundAudit("link context cleanup");
}
static void NotesLinkContextMisses(){
 Fixture(L"Link context misses");HWND owner=Open(L"Link context misses");
 for(bool automatic:{false,true}){
  Command(owner,IDC_NOTES_ADD_TAB);
  const std::wstring label=automatic?L"https://example.test/last?q=\u4E2D\u6587":L"Support";
  Type(owner,((automatic?L"\xD83D\xDE80\r\n":L"")+label).c_str());
  const LONG first=LinkContextFind(Edit(owner),label);
  if(!automatic){Select(Edit(owner),first,first+7);inputs.push_back({label,L"https://example.test/last-friendly"});Command(owner,IDC_NOTES_LINK);}
  Save(owner);Check(NotesRangeLinkEffects(Edit(owner),{first+2,first+3}),"Last-line miss fixture contains an actual native link");
  RECT area{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&area));POINT point=LinkContextPoint(Edit(owner),first+2,false);Check(area.bottom>point.y+30,"Miss point lies far below final linked line inside editor formatting area");point.y=area.bottom-2;ClientToScreen(Edit(owner),&point);
  const auto writes=notesCapturedClipboardWrites;LinkContextMenu(owner,point,8);LinkContextExpected(!linkContextOpen&&!linkContextCopy&&notesCapturedClipboardWrites==writes,"Click far below the last linked line never copies its nearest URL");
 }
 Command(owner,IDC_NOTES_ADD_TAB);const std::wstring wrapped=L"https://example.test/"+std::wstring(200,L'W');Type(owner,(wrapped+L" ").c_str());
 Check(NotesRangeLinkEffects(Edit(owner),{10,11}),"Wrapped miss fixture is auto-linked natively");
 RECT original{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&original));POINT gap{};bool foundGap=false;
 for(int inset=0;inset<32&&!foundGap;++inset){
  RECT requested=original;requested.right-=inset;SendMessageW(Edit(owner),EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&requested));
  const LONG next=static_cast<LONG>(SendMessageW(Edit(owner),EM_LINEINDEX,1,0));if(next<2||NotesRangeText(Edit(owner),next-2,next)!=L"WW")continue;
  POINTL last{},previous{},following{};SendMessageW(Edit(owner),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&last),next-1);SendMessageW(Edit(owner),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&previous),next-2);SendMessageW(Edit(owner),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&following),next);
  RECT area{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&area));const LONG width=last.x-previous.x;
  if(width>0&&following.y>last.y&&area.right-1>last.x+width){gap={area.right-1,last.y+3};foundGap=true;}
 }
 Check(foundGap,"Native wrapped URL line has a measured blank gap beyond its final equal-width glyph");ClientToScreen(Edit(owner),&gap);
 const auto writes=notesCapturedClipboardWrites;LinkContextMenu(owner,gap,8);LinkContextExpected(!linkContextOpen&&!linkContextCopy&&notesCapturedClipboardWrites==writes,"Blank gap after wrapped linked line is not a nearest-character URL hit");
 DestroyWindow(owner);NotesBackgroundAudit("link context miss cleanup");
}
struct LinkGlyphSample {POINT point{};LONG character=0;std::wstring address;};
static void LinkContextTraceGlyph(HWND edit,const ClientNotesDialogState &state,const POINT &point){
 Microsoft::WRL::ComPtr<IUnknown> unknown;Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> range;
 SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));if(!unknown||FAILED(unknown.As(&document))||FAILED(document->Range(0,0,&range)))return;
 const LONG nearest=static_cast<LONG>(SendMessageW(edit,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&point)));LONG story=0,moved=0,start=-1;range->GetStoryLength(&story);
 const auto set=range->SetRange(nearest,nearest);const auto move=range->Move(tomCluster,-1,&moved);range->GetStart(&start);
 const LONG previous=SUCCEEDED(set)&&SUCCEEDED(move)&&moved==-1&&start>=0&&start<nearest?start:nearest-1;
 std::cout<<"Exact native glyph diagnostic point="<<point.x<<','<<point.y<<" nearest="<<nearest<<" story="<<story<<" set="<<std::hex<<set<<" move="<<move<<std::dec<<" moved="<<moved<<" start="<<start<<" chosen-previous="<<previous<<'\n';
 for(LONG candidate:{nearest,previous}){
  const LONG original=candidate;const auto pair=candidate>0?NotesRangeText(edit,candidate-1,candidate+1):std::wstring{};std::cout<<" candidate="<<original<<" legacy-adjusted-slice-size="<<pair.size()<<" units=";for(const auto unit:pair)std::cout<<std::hex<<static_cast<unsigned>(unit)<<',';std::cout<<std::dec;
  const auto setCollapsed=range->SetRange(candidate,candidate);range->GetStart(&candidate);LONG first=candidate,last=candidate+1;const auto expand=range->Expand(tomCluster,&moved);LONG a=first,b=last;range->GetStart(&a);range->GetEnd(&b);if(a>=0&&a<=candidate&&b>candidate&&b<=story-1){first=a;last=b;}
  const auto setExpanded=range->SetRange(first,last);range->GetStart(&first);range->GetEnd(&last);LONG left=0,right=0,top=0,bottom=0,endX=0,endY=0;const auto lt=range->GetPoint(tomStart|tomClientCoord|TA_TOP|TA_LEFT,&left,&top);const auto rb=range->GetPoint(tomStart|tomClientCoord|TA_BOTTOM|TA_RIGHT,&right,&bottom);const auto edge=range->GetPoint(tomEnd|tomClientCoord|TA_TOP|TA_LEFT,&endX,&endY);if(edge==S_OK&&endY==top&&endX>left)right=endX;
  std::cout<<" normalized="<<candidate<<" collapsed="<<std::hex<<setCollapsed<<" expand="<<expand<<" set-expanded="<<setExpanded<<std::dec<<" native-expanded="<<a<<','<<b<<" used="<<first<<','<<last<<" points-status="<<std::hex<<lt<<','<<rb<<','<<edge<<std::dec<<" rectangle="<<left<<','<<top<<','<<right<<','<<bottom<<" end="<<endX<<','<<endY<<" contains="<<(point.x>=left&&point.x<right&&point.y>=top&&point.y<bottom)<<" mapped-target-length="<<NotesNativeLinkAddress(edit,state,{first,first+1}).size()<<'\n';
 }
}
static void NotesLinkContextGlyphCoverage(){
 const std::wstring literal=L"https://example.com/ctspaces-review";
 for(unsigned variant=0;variant<8;++variant){
  const auto client=L"Link glyph QA "+std::to_wstring(variant);Fixture(client);const auto owner=Open(client);Command(owner,IDC_NOTES_ADD_TAB);const auto edit=Edit(owner);
  const std::wstring prefix=L"Keep selection\r\n";
  const std::wstring label=variant<2?literal:variant==2?L"WWiWQ":variant==3?L"WWiWQWWiWQ":variant==4?L"https://example.test/"+std::wstring(200,L'W'):variant==5?L"Wi\U0001f600W\u4e2di":variant==6?L"Wie\u0301W":L"Wi\U0001f468\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466Wi";
  Type(owner,(prefix+label+(variant==1?L"":L"\r\n")).c_str());const LONG first=LinkContextFind(edit,label);auto &state=State(owner);
  const auto primary=variant<2||variant==4?label:L"https://example.test/first-target?x=1&y=2";
  const std::wstring secondary=L"https://example.test/second-target?other=client";
  if(variant==2||variant==3||variant>=5){
   const LONG split=variant==3?5:static_cast<LONG>(label.size());Select(edit,first,first+split);inputs.push_back({label.substr(0,split),primary});Command(owner,IDC_NOTES_LINK);
   if(variant==3){Select(edit,first+split,first+static_cast<LONG>(label.size()));inputs.push_back({label.substr(split),secondary});Command(owner,IDC_NOTES_LINK);}
  }
  if(variant==2){Select(edit,first,first+static_cast<LONG>(label.size()));NotesCharacterFormat(owner,state,CFM_BOLD|CFM_SIZE,CFE_BOLD,0,320);SendMessageW(edit,EM_SETZOOM,150,100);}
  Save(owner);Select(edit,0,4);CHARRANGE selection{};NotesSelection(edit,selection);const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  std::string before;Check(StreamClientNotesOut(edit,before),"Full-width link hit testing captures the original native rich text");const auto undo=SendMessageW(edit,EM_CANUNDO,0,0);const auto registry=state.editorLinks.at(state.attachedPageId);
  Microsoft::WRL::ComPtr<IUnknown> unknown;Microsoft::WRL::ComPtr<ITextDocument> document;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));Check(unknown&&SUCCEEDED(unknown.As(&document)),"Measured link glyph coverage obtains the native text model without selecting its text");
  LONG maximumCluster=0;std::vector<CHARRANGE> clusters;
  for(LONG index=first;index<first+static_cast<LONG>(label.size());){Microsoft::WRL::ComPtr<ITextRange> cluster;LONG count=0,begin=0,end=0;
   Check(SUCCEEDED(document->Range(index,index,&cluster))&&cluster&&cluster->Expand(tomCluster,&count)==S_OK&&cluster->GetStart(&begin)==S_OK&&cluster->GetEnd(&end)==S_OK&&begin==index&&end>index&&end<=first+static_cast<LONG>(label.size()),"Native cluster expansion advances through valid text boundaries without changing the editor selection");maximumCluster=(std::max)(maximumCluster,end-begin);clusters.push_back({begin,end});index=end;
  }
  if(variant>=5)Check(maximumCluster>=2,"Unicode link coverage proves that the native model groups multiple UTF-16 units into actual text clusters");
  if(variant==7)Check(maximumCluster==11,"The native model groups the actual emoji ZWJ family as one eleven-unit cluster");
  RECT area{};SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&area));std::vector<LinkGlyphSample> samples;unsigned misses=0,advanced=0,measured=0,matchingTomWidths=0,wrapped=0;
  for(const auto &cluster:clusters){
   const LONG index=cluster.cpMin,offset=index-first;POINTL left{},next{};SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&left),index);SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next),cluster.cpMax);
   LONG right=next.x;
   if(next.y!=left.y){
    if(variant!=4||offset<1||label[offset]!=L'W'||label[offset-1]!=L'W')continue;
    POINTL previous{};SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&previous),index-1);if(previous.y!=left.y||left.x<=previous.x)continue;right=left.x+(left.x-previous.x);++wrapped;
   }
   if(right-left.x<2)continue;
   Microsoft::WRL::ComPtr<ITextRange> range;LONG tomLeft=0,tomRight=0,top=0,bottom=0,endX=0,endY=0;
   const bool bounds=SUCCEEDED(document->Range(index,cluster.cpMax,&range))&&range&&range->GetPoint(tomStart|tomClientCoord|TA_TOP|TA_LEFT,&tomLeft,&top)==S_OK&&range->GetPoint(tomStart|tomClientCoord|TA_BOTTOM|TA_RIGHT,&tomRight,&bottom)==S_OK&&bottom>top;
   if(bounds&&range->GetPoint(tomEnd|tomClientCoord|TA_TOP|TA_LEFT,&endX,&endY)==S_OK&&endY==top&&endX>tomLeft)tomRight=endX;
   Check(bounds,"A linked glyph has real native top, bottom, left and right bounds");++measured;if(tomLeft==left.x&&tomRight==right)++matchingTomWidths;
   if(offset<3||offset+2>=static_cast<LONG>(label.size())||next.y!=left.y)std::cout<<"Glyph geometry variant="<<variant<<" cp="<<index<<" EM="<<left.x<<','<<right<<" TOM="<<tomLeft<<','<<tomRight<<','<<top<<','<<bottom<<'\n';
   const auto expected=variant==3&&offset>=5?secondary:primary;
   for(LONG x=left.x;x<right;++x){
    POINT point{x,top+(bottom-top)/2};if(!PtInRect(&area,point))continue;
    const LONG nearest=static_cast<LONG>(SendMessageW(edit,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&point)));if(nearest>index)++advanced;
    samples.push_back({point,index,expected});const auto actual=NotesContextLinkAddress(edit,state,&point);
    if(actual!=expected){++misses;if(misses<=8)std::cout<<"Glyph hit miss variant="<<variant<<" cp="<<index<<" nearest="<<nearest<<" point="<<point.x<<','<<point.y<<" glyph="<<left.x<<','<<right<<" expected target length="<<expected.size()<<" actual="<<actual.size()<<'\n';if(misses==1)LinkContextTraceGlyph(edit,state,point);}
   }
  }
  std::cout<<"Full-width glyph proof variant="<<variant<<" samples="<<samples.size()<<" advancing-nearest="<<advanced<<" measured="<<measured<<" TOM-width-matches="<<matchingTomWidths<<" wrapped-last-glyphs="<<wrapped<<" maximum-native-cluster="<<maximumCluster<<" misses="<<misses<<'\n';
  Check(samples.size()>12&&advanced>0,"Measured glyph coverage includes actual right-half points whose nearest insertion index advances");
  Check(matchingTomWidths==measured,"Native TOM glyph bounds agree with independently measured insertion-position widths, including wrapped equal-width glyphs");
  Check(misses==0,"Every visible pixel across the measured link glyph widths resolves that glyph's exact URL, including right halves and last characters");
  if(variant==4)Check(wrapped>0,"Wrapped link coverage includes the final full-width glyph before a native visual line break");
  POINTL end{};SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),first+static_cast<LONG>(label.size()));POINT beyond{end.x+2,samples.back().point.y};if(PtInRect(&area,beyond)&&end.y<=beyond.y)Check(NotesContextLinkAddress(edit,state,&beyond).empty(),"A measured blank point beyond the final linked cluster does not borrow its target");
  for(const size_t sampleIndex:{size_t{0},samples.size()/2,samples.size()-1})for(const bool rightButton:{false,true}){
   const auto &sample=samples[sampleIndex];POINT point=sample.point;if(!rightButton)ClientToScreen(edit,&point);const auto writes=notesCapturedClipboardWrites;
   LinkContextMenu(owner,point,8,rightButton);Check(linkContextOpen&&linkContextCopy&&notesCapturedClipboardWrites==writes+1&&notesCapturedClipboard==sample.address,"Both native context-menu paths copy the exact measured first, middle and last glyph target");
  }
  std::string after;Check(StreamClientNotesOut(edit,after),"Full-width link hit testing reads the native rich text after context queries");const auto &afterRegistry=state.editorLinks.at(state.attachedPageId);
  Check(before==after&&SameSelection(edit,selection)&&undo==SendMessageW(edit,EM_CANUNDO,0,0)&&client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==durable,"Measured link lookup and context copying preserve exact rich text, selection, Undo and saved notebook bytes");
  Check(registry.urls==afterRegistry.urls&&registry.lastUse==afterRegistry.lastUse&&registry.nextCookie==afterRegistry.nextCookie&&registry.useCounter==afterRegistry.useCounter,"Full-width glyph queries preserve immutable cookie destinations and registry history");
  document.Reset();unknown.Reset();DestroyWindow(owner);NotesBackgroundAudit("measured full-width link glyph coverage");
 }
 std::cout<<"Link glyph coverage: 8 variants completed; native measured glyphs, right halves, final glyphs, wrapped boundaries, adjacent targets, Unicode clusters and immutable state passed.\n";
}
