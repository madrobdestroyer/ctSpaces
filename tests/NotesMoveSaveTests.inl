namespace notes_move_save {
static unsigned failures = 0;

static void Expect(bool condition, const char *description) {
  ++checks;
  std::cout << (condition ? "PASS " : "FAIL_NOTES_MOVE_SAVE ") << description << '\n';
  if (!condition) ++failures;
}

enum class Outcome { Moved, SourceLocked, DestinationLocked };

static void TestMove(Outcome outcome, bool secondTab) {
  const auto suffix = std::to_wstring(static_cast<int>(outcome)) + (secondTab ? L" neighbor" : L" only");
  const auto sourceName = L"Move source " + suffix;
  const auto destinationName = L"Move destination " + suffix;
  Fixture(sourceName);
  Fixture(destinationName);
  const auto owner = Open(sourceName);
  auto &state = State(owner);
  if (secondTab) {
    Command(owner, IDC_NOTES_ADD_TAB);
    Type(owner, L"Separate tab");
    Save(owner);
  }
  Command(owner, IDC_NOTES_ADD_TAB);
  Type(owner, L"Move this note");
  Save(owner);
  const auto sourceIndex = state.activePage;
  const auto sourceId = state.attachedPageId;
  const auto sourceEditor = Edit(owner);
  const auto sourceRoot = state.clientRoot;
  const auto targetRoot = g_sDataDir / L"Sites" / destinationName;
  client_notes::NotePage existing{L"Other client's note", true, NotesPlainRtf(L"Destination stays intact"), {}};
  existing.id = 1;
  Check(client_notes::WriteNotebook(targetRoot, {existing}, client_notes::ReadNotebook(targetRoot)),
        "Seed an independent destination notebook");
  const auto destinationBefore = client_notes::ReadNotebook(targetRoot);
  const auto sourceBefore = client_notes::ReadNotebook(sourceRoot);
  const auto sourceTabsBefore = state.visiblePages;
  const auto undoBefore = SendMessageW(sourceEditor, EM_CANUNDO, 0, 0);
  Check(sourceBefore.status == client_notes::ReadStatus::Ok && sourceId && undoBefore,
        "The source note is saved with its native Undo available");

  HANDLE lock = INVALID_HANDLE_VALUE;
  notesDialogObserver = [&](LPCWSTR resource, DLGPROC procedure, LPARAM data) -> INT_PTR {
    Check(resource == MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER) && procedure == NotesClientPickerProc,
          "The guarded move uses the client picker");
    auto &picker = *reinterpret_cast<NotesClientPicker *>(data);
    Check(picker.moving && picker.source == sourceName, "The picker receives the source client's identity");
    picker.selected = destinationName;
    if (outcome != Outcome::Moved) {
      const auto &root = outcome == Outcome::SourceLocked ? sourceRoot : targetRoot;
      lock = CreateFileW((root / client_notes::kNotebookFileName).c_str(), GENERIC_READ, FILE_SHARE_READ,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      Check(lock != INVALID_HANDLE_VALUE, "A notebook becomes locked after the source preflight save");
    }
    return IDOK;
  };
  lastNotice.clear();
  answers.push_back(IDOK);
  Command(owner, IDC_NOTES_MOVE);
  notesDialogObserver = nullptr;
  if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);

  const auto source = client_notes::ReadNotebook(sourceRoot);
  const auto destination = client_notes::ReadNotebook(targetRoot);
  const auto moved = std::ranges::find(source.pages, sourceId, &client_notes::NotePage::id);
  Check(moved != source.pages.end() && NotesPlain(*moved) == L"Move this note",
        "The source notebook still contains the complete note");
  if (outcome == Outcome::DestinationLocked) {
    Check(client_notes::SameSnapshot(destination.notebook_snapshot, destinationBefore.notebook_snapshot) &&
          lastNotice.find(L"destination could not be saved") != std::wstring::npos,
          "A locked destination preserves its original bytes and reports the failed copy");
  } else {
    Check(destination.pages.size() == destinationBefore.pages.size() + 1 &&
          NotesPlain(destination.pages.back()) == L"Move this note" && !destination.pages.back().open,
          "Exactly one complete closed copy is saved in the destination notebook");
  }
  if (outcome == Outcome::Moved) {
    Check(moved->deleted && !moved->open && state.pages[sourceIndex].deleted &&
          state.attachedPageId != sourceId && !state.saveFailed,
          "A successful move sends the source to Trash in memory and on disk");
  } else {
    Check(client_notes::SameSnapshot(source.notebook_snapshot, sourceBefore.notebook_snapshot) &&
          !moved->deleted && moved->open, "A failed move leaves the source notebook byte-exact on disk");
    Expect(state.activePage == sourceIndex && state.attachedPageId == sourceId &&
           Edit(owner) == sourceEditor && state.visiblePages == sourceTabsBefore &&
           !state.pages[sourceIndex].deleted && state.pages[sourceIndex].open &&
           SendMessageW(sourceEditor, EM_CANUNDO, 0, 0) == undoBefore,
           "A failed move keeps the source tab, original editor, and Undo available");
    if (outcome == Outcome::SourceLocked) {
      Expect(!state.saveFailed && !fs::exists(sourceRoot / state.draftName) &&
             lastNotice.find(L"The destination copy was saved") != std::wstring::npos &&
             lastNotice.find(L"the source note is still open") != std::wstring::npos,
             "The partial move names the saved copy and keeps no pending Trash operation");
      Save(owner);
      Expect(client_notes::SameSnapshot(sourceBefore.notebook_snapshot,
                                        client_notes::ReadNotebook(sourceRoot).notebook_snapshot),
             "A later ordinary save never completes the failed Trash operation");
    }
  }
  DestroyWindow(owner);
  NotesBackgroundAudit("cross-client Notes move");
}

static bool Run() {
  TestMove(Outcome::SourceLocked, false);
  TestMove(Outcome::SourceLocked, true);
  TestMove(Outcome::DestinationLocked, true);
  TestMove(Outcome::Moved, false);
  TestMove(Outcome::Moved, true);
  return failures == 0;
}
}
