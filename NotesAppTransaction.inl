// App ownership around checked native edit collections. A rejected native edit
// must not remain on Redo, and no failed recovery may be captured as a draft.
#include "NotesUndo.h"
struct NotesAppTransactionRecovery {
  HWND owner=nullptr,edit=nullptr;
  std::wstring clientName;
  fs::path clientRoot;
  uint64_t pageId=0;
  size_t pageIndex=0;
  notes_undo::Document document;
  std::shared_ptr<const notes_undo::Document> modelDocument;
  notes_undo::View view;
  bool hadRegistry=false;
  ClientNotesDialogState::LinkRegistry registry;
  bool formattingChanged=false,metadataChanged=false,pristine=false;
  bool saveFailed=false,editorLoadFailed=false;
  std::wstring saveProblemDetail;
};

enum class NotesAppTransactionResult { NotStarted, Committed, Recovered, HistoryReset, Blocked };
#ifdef CTSPACES_NOTES_TEST_HOOKS
// Bounded fault observation on a private attached editor only. The before call
// occurs after EndEditCollection; the after call records the REAL TOM outcome.
static void (*g_notesAppUndoObserver)(HWND,bool,HRESULT,LONG,void *)=nullptr;
static void *g_notesAppUndoContext=nullptr;
#endif

static bool NotesAppTransactionMatches(const NotesAppTransactionRecovery &before,
                                      const ClientNotesDialogState &state) {
  return IsWindow(before.owner) && IsWindow(before.edit) &&
      reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(before.owner,DWLP_USER))==&state &&
      state.clientName==before.clientName && state.clientRoot==before.clientRoot &&
      state.activePage==before.pageIndex && NotesEditorMatchesActive(state) &&
      state.attachedPageId==before.pageId && state.attachedEditor==before.edit &&
      GetDlgItem(before.owner,IDC_CLIENT_NOTES_TEXT)==before.edit;
}

static bool NotesAppRestoreRegistry(const NotesAppTransactionRecovery &before,
                                    ClientNotesDialogState &state) {
  try {
    if(before.hadRegistry) state.editorLinks[before.pageId]=before.registry;
    else state.editorLinks.erase(before.pageId);
    return true;
  } catch(...) {return false;}
}

static bool NotesAppRestoreFlags(const NotesAppTransactionRecovery &before,
                                 ClientNotesDialogState &state) {
  try {
  state.formattingChanged=before.formattingChanged;
  state.metadataChanged=before.metadataChanged;
  state.pages[before.pageIndex].pristine=before.pristine;
  state.saveFailed=before.saveFailed;state.saveProblemDetail=before.saveProblemDetail;
  state.editorLoadFailed=before.editorLoadFailed;
  SendMessageW(before.edit,EM_SETMODIFY,before.view.modified,0);
  return true;
  } catch(...) {return false;}
}

static std::shared_ptr<NotesAppTransactionRecovery> NotesCaptureAppTransactionRecovery(
    HWND dialog,ClientNotesDialogState &state) {
  if(state.loading || !NotesEditorMatchesActive(state)) return {};
  try {
    auto before=std::make_shared<NotesAppTransactionRecovery>();
    before->owner=dialog;before->edit=state.attachedEditor;before->clientName=state.clientName;
    before->clientRoot=state.clientRoot;before->pageId=state.attachedPageId;before->pageIndex=state.activePage;
    const auto registry=state.editorLinks.find(before->pageId);
    before->hadRegistry=registry!=state.editorLinks.end();
    if(before->hadRegistry) before->registry=registry->second;
    const auto registered=[&](LONG cookie) {
      if(!before->hadRegistry) return false;
      const auto target=before->registry.urls.find(cookie);
      return target!=before->registry.urls.end() && !target->second.empty() && ValidNotesUrl(target->second);
    };
    if(!notes_undo::CaptureView(before->edit,before->view,registered) ||
        !notes_undo::CaptureDocument(before->edit,before->document)) return {};
    before->formattingChanged=state.formattingChanged;before->metadataChanged=state.metadataChanged;
    before->pristine=state.pages[state.activePage].pristine;before->saveFailed=state.saveFailed;
    before->saveProblemDetail=state.saveProblemDetail;before->editorLoadFailed=state.editorLoadFailed;
    return before;
  } catch(...) {return {};}
}

class NotesAppTransactionGate {
public:
  NotesAppTransactionGate(HWND edit,ClientNotesDialogState &state,ITextDocument *document)
      : edit_(edit),state_(state),document_(document),loading_(state.loading),
        events_(SendMessageW(edit,EM_GETEVENTMASK,0,0)) {
    state_.loading=true;SendMessageW(edit_,EM_SETEVENTMASK,0,0);
    LONG count=0;frozen_=document_->Freeze(&count)==S_OK;
  }
  bool Frozen() const {return frozen_;}
  bool Unfreeze() {
    if(!frozen_) return true;
    LONG count=0;const bool ok=document_->Unfreeze(&count)==S_OK;
    frozen_=false;return ok;
  }
  void ReleaseSuppression() {
    if(!suppressed_) return;
    SendMessageW(edit_,EM_SETEVENTMASK,0,events_);state_.loading=loading_;suppressed_=false;
  }
  ~NotesAppTransactionGate() {Unfreeze();ReleaseSuppression();}
private:
  HWND edit_;ClientNotesDialogState &state_;ITextDocument *document_;
  bool loading_,frozen_=false,suppressed_=true;LRESULT events_;
};

static NotesAppTransactionResult NotesAppBlockTransaction(
    const std::shared_ptr<NotesAppTransactionRecovery> &before,ClientNotesDialogState &state) {
  // Retain the actual pre-operation native image and URL registry before
  // detaching. Merely setting saveFailed would not stop CaptureNotesPage.
  const bool originalEditor=NotesAppTransactionMatches(*before,state);
  state.pendingTransactionRecovery=before;
  if(originalEditor) {
    SetWindowLongPtrW(before->edit,GWLP_ID,0);
    SendMessageW(before->edit,EM_SETREADONLY,TRUE,0);
    state.attachedEditor=nullptr;state.attachedPageId=0;
  }
  state.editorLoadFailed=true;state.saveFailed=true;
  try {state.saveProblemDetail=L"An edit could not be recovered safely. The original draft is retained in memory; saving is paused.";}
  catch(...) {state.saveProblemDetail.clear();}
  return NotesAppTransactionResult::Blocked;
}

template<class Mutation>
static NotesAppTransactionResult NotesApplyNativeTransaction(
    HWND dialog,ClientNotesDialogState &state,const CHARRANGE &affected,Mutation mutate) {
  auto before=NotesCaptureAppTransactionRecovery(dialog,state);
  if(!before) return NotesAppTransactionResult::NotStarted;
  notes_undo::Diagnose("app.original",before->edit,true);
  IUnknown *ole=nullptr;ITextDocument *raw=nullptr;
  SendMessageW(before->edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
  if(ole) {ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&raw));ole->Release();}
  struct ReleaseDocument {void operator()(ITextDocument *value)const {if(value)value->Release();}};
  std::unique_ptr<ITextDocument,ReleaseDocument> document(raw);
  if(!document || !NotesAppTransactionMatches(*before,state)) return NotesAppTransactionResult::NotStarted;
  SendMessageW(before->edit,EM_STOPGROUPTYPING,0,0);
  try {NotesTouchRangeCookies(before->edit,state,affected);}
  catch(...) {return NotesAppBlockTransaction(before,state);}
  NotesAppTransactionGate gate(before->edit,state,document.get());
  if(!gate.Frozen()) {
    if(!NotesAppRestoreRegistry(*before,state)) return NotesAppBlockTransaction(before,state);
    return NotesAppTransactionResult::NotStarted;
  }
  if(document->BeginEditCollection()!=S_OK) {
    if(!NotesAppRestoreRegistry(*before,state) || !NotesAppRestoreFlags(*before,state)) return NotesAppBlockTransaction(before,state);
    if(!gate.Unfreeze() || !notes_undo::FinalizeView(before->edit,before->view) ||
        !notes_undo::VerifyOriginal(before->edit,before->document)) return NotesAppBlockTransaction(before,state);
    return NotesAppTransactionResult::NotStarted;
  }
  bool changed=false;
  try {changed=mutate(before->edit);}catch(...) {changed=false;}
  const HRESULT ended=document->EndEditCollection();
  if(!NotesAppTransactionMatches(*before,state)) return NotesAppBlockTransaction(before,state);
  if(changed && ended==S_OK && NotesAppTransactionMatches(*before,state)) {
    if(!gate.Unfreeze()) return NotesAppBlockTransaction(before,state);
    return NotesAppTransactionResult::Committed;
  }
  const auto repairOriginal=[&] {
    if(!NotesAppTransactionMatches(*before,state)) return false;
    if(!NotesAppRestoreRegistry(*before,state)) return false;
    // Direct native runs repair the app links without label/budget reads. The
    // shared function owns its checked suspend/resume; do not nest another one.
    const bool repaired=notes_undo::RestoreDocumentLinks(before->edit,before->document) &&
        notes_undo::VerifyOriginal(before->edit,before->document);
    notes_undo::Diagnose("app.repaired",before->edit,repaired);return repaired;
  };
  // A failed pre-edit/no-op may leave the native collection empty. Undoing in
  // that case would consume an older valid edit, and a barrier would discard a
  // legitimate existing Redo. Distinguish an exact image from a failed read.
  notes_undo::Document afterFailure;
  if(ended!=S_OK || !NotesAppTransactionMatches(*before,state) ||
      !notes_undo::CaptureDocument(before->edit,afterFailure) || !NotesAppTransactionMatches(*before,state))
    return NotesAppBlockTransaction(before,state);
  if(afterFailure.length==before->document.length && afterFailure.rtf==before->document.rtf &&
      afterFailure.links==before->document.links) {
    if(!NotesAppRestoreRegistry(*before,state)) return NotesAppBlockTransaction(before,state);
    if(!gate.Unfreeze() || !notes_undo::FinalizeView(before->edit,before->view) ||
        !notes_undo::VerifyOriginal(before->edit,before->document)) return NotesAppBlockTransaction(before,state);
    if(!NotesAppRestoreFlags(*before,state)) return NotesAppBlockTransaction(before,state);
    return NotesAppTransactionResult::Recovered;
  }
  LONG undone=0;
  if(!NotesAppTransactionMatches(*before,state)) return NotesAppBlockTransaction(before,state);
#ifdef CTSPACES_NOTES_TEST_HOOKS
  if(g_notesAppUndoObserver)g_notesAppUndoObserver(before->edit,true,S_OK,0,g_notesAppUndoContext);
#endif
  if(!NotesAppTransactionMatches(*before,state)) return NotesAppBlockTransaction(before,state);
  const HRESULT undoResult=document->Undo(1,&undone);
  notes_undo::Diagnose("app.undo",before->edit,undoResult==S_OK && undone==1);
#ifdef CTSPACES_NOTES_TEST_HOOKS
  if(g_notesAppUndoObserver)g_notesAppUndoObserver(before->edit,false,undoResult,undone,g_notesAppUndoContext);
#endif
  const bool restored=undoResult==S_OK && undone==1 && repairOriginal();
  const auto repairOwnership=[&] {
    const bool owned=NotesAppTransactionMatches(*before,state) && NotesAppRestoreRegistry(*before,state);
    notes_undo::Diagnose("app.ownership",before->edit,owned);return owned;
  };
  const bool discarded=restored && notes_undo::DiscardAbortedRedo(before->edit,before->view,before->document,repairOwnership);
  notes_undo::Diagnose("app.discarded",before->edit,discarded);
  bool reset=false;
  if(!discarded && restored && repairOriginal()) {
    // Fault-only fallback: clear history only AFTER exact original recovery.
    SendMessageW(before->edit,EM_EMPTYUNDOBUFFER,0,0);
    reset=!SendMessageW(before->edit,EM_CANUNDO,0,0) && !SendMessageW(before->edit,EM_CANREDO,0,0) && repairOriginal();
    notes_undo::Diagnose("app.history-reset",before->edit,reset);
  }
  if(!discarded && !reset) return NotesAppBlockTransaction(before,state);
  if(!gate.Unfreeze() || !notes_undo::FinalizeView(before->edit,before->view) ||
      !notes_undo::VerifyOriginal(before->edit,before->document)) return NotesAppBlockTransaction(before,state);
  if(!NotesAppRestoreRegistry(*before,state) || !NotesAppRestoreFlags(*before,state)) return NotesAppBlockTransaction(before,state);
  return discarded ? NotesAppTransactionResult::Recovered : NotesAppTransactionResult::HistoryReset;
}

static void NotesAppTransactionNotice(HWND dialog,NotesAppTransactionResult result,
                                      const wchar_t *ordinary,const wchar_t *title) {
  std::wstring message;
  if(result==NotesAppTransactionResult::Blocked)
    message=L"The edit failed and its original content could not be verified. Saving is paused. Your pre-operation draft is retained in memory for recovery. Keep this Notes window open.";
  else if(result==NotesAppTransactionResult::NotStarted)
    message=L"The edit could not be started safely. Your current text and links are unchanged.";
  else {
    message=ordinary;
    if(result==NotesAppTransactionResult::HistoryReset)
      message+=L"\n\nYour Undo and Redo history could not be retained and was cleared.";
  }
  MessageBoxW(dialog,message.c_str(),title,MB_OK|MB_ICONWARNING);
}
