namespace notes_details_tab_switch {
static unsigned failures = 0;
static constexpr uint64_t sourceId = 1, targetId = 2;

static void Expect(bool condition, const char *description) {
  ++checks;
  std::cout << (condition ? "PASS " : "FAIL_NOTES_DETAILS_SWITCH ") << description << '\n';
  if (!condition) ++failures;
}

static HWND OpenTabs(const std::wstring &client) {
  Fixture(client);
  std::vector<client_notes::NotePage> pages{
    {L"From", true, NotesPlainRtf(L"Source body"), {}},
    {L"To", true, NotesPlainRtf(L"Target body"), {}}
  };
  pages[0].id = sourceId;
  pages[0].active = true;
  pages[1].id = targetId;
  for (auto &page : pages) page.open = true;
  std::string bytes;
  Check(client_notes::SerializeNotebook(pages, bytes), "Prepare two saved open tabs");
  Put(g_sDataDir / L"Sites" / client / client_notes::kNotebookFileName, bytes);
  const auto owner = Open(client);
  Check(State(owner).attachedPageId == sourceId, "The source tab starts active");
  return owner;
}

static void ClickSecondTab(HWND tabs, bool contextMenu) {
  RECT item{};
  Check(TabCtrl_GetItemRect(tabs, 1, &item), "Read the destination tab bounds");
  POINT point{item.left + ScaleByDpi(8, GetDpiForWindow(tabs)), (item.top + item.bottom) / 2};
  TCHITTESTINFO hit{point, 0};
  Check(TabCtrl_HitTest(tabs, &hit) == 1, "The destination gesture hits its native tab");
  if (contextMenu) {
    Check(ClientToScreen(tabs, &point) != FALSE, "Map the destination tab to its menu anchor");
    const auto initialPopups = popupCalls;
    SendMessageW(tabs, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(tabs), MAKELPARAM(point.x, point.y));
    Check(popupCalls == initialPopups + 1, "The guarded right-click opens its tab menu");
  } else {
    const auto position = MAKELPARAM(point.x, point.y);
    SendMessageW(tabs, WM_LBUTTONDOWN, MK_LBUTTON, position);
    SendMessageW(tabs, WM_LBUTTONUP, 0, position);
  }
  Check(GetCapture() != tabs, "The tab gesture leaves no capture on the tab strip");
}

static void ScrollToSecondTab(HWND tabs) {
  SendMessageW(tabs, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
}

static void TestTabSwitch(unsigned scenario) {
  const auto owner = OpenTabs(L"Pending Details switch " + std::to_wstring(scenario));
  auto &state = State(owner);
  const auto tabs = GetDlgItem(owner, IDC_NOTES_TABS);
  const auto sourceEditor = Edit(owner);
  Command(owner, IDC_NOTES_DETAILS);
  const auto panel = state.panel;
  Check(panel && IsWindow(panel), "The source Details panel is open");
  Select(sourceEditor, -1, -1);
  Type(owner, L" pending body");
  const bool invalid = scenario == 1;
  if (invalid) SetDlgItemTextW(panel, IDC_NOTES_INPUT2, L"unfinished URL");
  else SetDlgItemTextW(panel, IDC_NOTES_INPUT1, L"QA-531");
  Check(state.pages[state.activePage].ticket.empty() && state.detailsDrafts.empty(),
        "The new Details are still waiting for the panel timer or dismissal");

  SendMessageW(panel, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(owner));
  Check(IsWindow(panel), "The outside-click dismissal is queued before tab navigation");
  HWND lastFocus = nullptr;
  notesPolishFocusObserver = [&](HWND window) { lastFocus = window; };
  if (scenario == 1) ScrollToSecondTab(tabs);
  else ClickSecondTab(tabs, scenario == 2);
  notesPolishFocusObserver = nullptr;
  NotesBackgroundAudit("tab navigation with pending Details");

  Check(state.attachedPageId == targetId && TabCtrl_GetCurSel(tabs) == 1 && !state.panel &&
        lastFocus == Edit(owner) && NotesWindowText(Edit(owner)) == L"Target body",
        "Navigation selects the destination, requests focus on its editor and keeps its body intact");
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  const auto source = std::ranges::find(saved.pages, sourceId, &client_notes::NotePage::id);
  const auto target = std::ranges::find(saved.pages, targetId, &client_notes::NotePage::id);
  Check(source != saved.pages.end() && target != saved.pages.end() &&
        NotesPlain(*source) == L"Source body pending body" && target->ticket.empty() &&
        target->ticketUrl.empty(), "The source body saves and the destination Details stay untouched");
  Expect(invalid ? source->ticketUrl.empty() && state.detailsDrafts.contains(sourceId) &&
                    state.detailsDrafts.at(sourceId)[3] == L"unfinished URL"
                 : source->ticket == L"QA-531",
         invalid ? "Wheel selection keeps the unfinished URL with its source note"
                 : "Tab selection saves the pending ticket on the source note");
  Check(NotesOpenSavedPage(owner, state, 0) && Edit(owner) == sourceEditor &&
        SendMessageW(sourceEditor, EM_CANUNDO, 0, 0),
        "Returning to the source reuses its editor and preserves Undo");
  if (invalid && state.detailsDrafts.contains(sourceId)) {
    Command(owner, IDC_NOTES_DETAILS);
    Check(state.panel && NotesWindowText(GetDlgItem(state.panel, IDC_NOTES_INPUT2)) == L"unfinished URL",
          "Reopening the source Details restores its unfinished field for editing");
    SetDlgItemTextW(state.panel, IDC_NOTES_INPUT2, L"https://example.test/ticket");
  }
  CloseClientNotesDialog(owner, state);
  Check(!IsWindow(owner), "The private notebook closes after the navigation check");
}

static void TestLockedNotebook() {
  const auto owner = OpenTabs(L"Locked Details switch");
  auto &state = State(owner);
  const auto tabs = GetDlgItem(owner, IDC_NOTES_TABS);
  const auto sourceEditor = Edit(owner);
  Command(owner, IDC_NOTES_DETAILS);
  const auto panel = state.panel;
  Check(panel && IsWindow(panel), "The source Details panel opens before the save failure");
  Select(sourceEditor, -1, -1);
  Type(owner, L" retained body");
  SetDlgItemTextW(panel, IDC_NOTES_INPUT1, L"QA-LOCK");
  const auto path = state.clientRoot / client_notes::kNotebookFileName;
  const auto initial = client_notes::ReadNotebook(state.clientRoot);
  const auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(lock != INVALID_HANDLE_VALUE, "The private notebook file is locked against replacement");
  SendMessageW(panel, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(owner));
  ScrollToSecondTab(tabs);
  CloseHandle(lock);
  Check(state.attachedPageId == sourceId && TabCtrl_GetCurSel(tabs) == 0 && Edit(owner) == sourceEditor &&
        state.saveFailed && NotesWindowText(sourceEditor) == L"Source body retained body" &&
        SendMessageW(sourceEditor, EM_CANUNDO, 0, 0) &&
        client_notes::SameSnapshot(initial.notebook_snapshot, client_notes::ReadNotebook(state.clientRoot).notebook_snapshot),
        "A failed save vetoes tab selection and preserves the editor, draft, Undo and saved bytes");
  ScrollToSecondTab(tabs);
  NotesBackgroundAudit("locked notebook tab navigation retry");
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  const auto source = std::ranges::find(saved.pages, sourceId, &client_notes::NotePage::id);
  Check(state.attachedPageId == targetId && !state.saveFailed && source != saved.pages.end() &&
        NotesPlain(*source) == L"Source body retained body", "The unlocked retry saves the source body and selects the destination");
  Expect(source->ticket == L"QA-LOCK", "The unlocked retry also saves the source ticket that was pending at the first switch");
  CloseClientNotesDialog(owner, state);
}

static bool Run() {
  for (unsigned scenario = 0; scenario < 3; ++scenario) TestTabSwitch(scenario);
  TestLockedNotebook();
  return failures == 0;
}
}
