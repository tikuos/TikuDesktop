# Architecture notes — File operations: copy/move/trash/undo/clipboard

Read from the real source; structural facts the rewrite must
reproduce for this area.

THREADING / OWNERSHIP
- Every user-visible FS operation runs on its own spawned thread. FSMoveToFolder/FSDuplicate/FSMoveToTrash use LaunchInNewThread("MoveTask"/"DupTask", B_NORMAL_PRIORITY, MoveTask, ...); FSDeleteRefList->_DeleteTask, FSRestoreRefList->_RestoreTask (both can also run sync when async==false); FSEmptyTrash spawns a *named* thread "_tracker_empty_trash_" and refuses to start a second one if find_thread() already finds it (single global Empty Trash job).
- The async entry points TAKE OWNERSHIP of their arguments: MoveTask deletes srcList, destEntry, and each BPoint in pointList at the end; _DeleteTask/_RestoreTask delete their list. Callers that bail (FSMoveToFolder on empty list, FSClipboardPaste on refusal) must delete them instead. UndoItemCopy/UndoItemMove Undo()/Redo() therefore always pass freshly-allocated copies (new BObjectList<entry_ref,true>(fSourceList), new BEntry(&fTargetRef)).
- The worker thread does the UI too: every alert (BAlert/OverrideAlert) is raised synchronously *from the worker thread*, so the copy loop blocks on user answers. Progress/pause/cancel go the other way: the worker polls, the window sets flags.

PROGRESS / CANCEL PLUMBING (CopyLoopControl indirection)
- CopyLoopControl is the policy object; TrackerCopyLoopControl is the Tracker subclass that bridges to the global BStatusWindow* gStatusWindow. Its identity key is fThread = find_thread(NULL) captured in the constructor, so status items are per-thread; ~TrackerCopyLoopControl calls gStatusWindow->RemoveStatusItem(fThread). A rewrite must keep one status item per job keyed by job identity, created in Init(jobKind) and removed on destruction.
- CheckUserCanceled() is polled at every loop boundary (per source item in MoveTask, per read chunk in LowLevelCopy, per entry in CopyFolder/FSDeleteFolder/FSRecursiveCalcSize/CalcItemsAndSize). Pause is implemented by BStatusWindow::CheckCanceledOrPaused() calling suspend_thread(view->Thread()) on the *calling* worker thread; resume_thread() comes from BStatusView::MessageReceived (kPauseButton/kStopButton). That self-suspend has no equivalent in a cooperative scheduler.
- Cancellation is cooperative and lossy: it aborts between items; already-copied items stay, the in-flight file is unlinked (LowLevelCopy), and a partially-copied *directory tree* is left behind.

ERROR PROPAGATION
- The copy engine uses C++ exceptions: ThrowOnError/ThrowOnInitCheckError throw status_t; MoveError and FailWithAlert are thrown for move/link failures. MoveItem is the single catch site (catch status_t / MoveError / FailWithAlert) and converts to a status_t return; the alert for MoveError/FailWithAlert is raised there, while plain status_t is assumed "already alerted".
- kUserCanceled/kCopyCanceled/kTrashCanceled are sentinel status_t values above B_ERRORS_END; they must be distinguishable from real errors because the top-level tasks suppress the "Error ..." alert for them.

MODE ENCODING
- moveMode is a 32-bit command constant (kCopySelectionTo 'Tcsl', kMoveSelectionTo 'Tmsl', kCreateLink 'Tlnk', kCreateRelativeLink 'Trln', kDuplicateSelection 'Tdsl'). The top byte is a namespace: FSUndoMoveMode() stamps 'U'<<24 and FSMoveMode() stamps 'T'<<24; MoveTask calls FSIsUndoMoveMode() first and, when set, calls undo.Remove() so undo/redo replays are not themselves pushed onto the undo stack. Any rewrite needs an equivalent "this op is a replay" flag.

UNDO MODEL
- FSUndoRedo.cpp owns two global BObjectList<UndoItem> (sUndoList, sRedoList) guarded by one BLocker("undo"), capped at kUndoRedoListMaxCount = 20 each. AddUndoItem() drops the oldest and CLEARS the redo list on any new operation. The stack is process-global (not per-window), RAM-only, and dies with Tracker.
- Undo objects are created eagerly and pushed by Undo::~Undo() at scope exit, i.e. AFTER the operation ran; Undo::Remove() cancels the push. The undo record must be mutated during the operation: Undo::UpdateEntry(entry, newName) is called whenever FSMakeOriginalName renamed a destination (CopyFile, CopyFolder, MoveItem link path, MoveEntryToTrash) so UndoItemCopy::UpdateEntry can fix the recorded target name.
- UndoItemMove/UndoItemCopy store the *parent* entry_ref of the FIRST source item only; ChangeListSource() rewrites every ref's (device, directory) to that one parent. So multi-source-folder operations undo to a single folder.

CLIPBOARD MODEL
- The clipboard is the *system* BClipboard, not a Tracker-private list, so cut/copy is visible to other apps and survives Tracker windows. Encoding: for each node, a B_REF_TYPE field named "r<dev>_<ino>" (MakeRefName) holding the entry_ref, plus an int32 field named "m<dev>_<ino>" (MakeModeName) holding the moveMode. FSClipboardHasRefs() validates only that the first ref field and first int32 field have matching suffixes (CompareModeAndRefName).
- BClipboardRefsWatcher is a BLooper that (a) node-monitors every clipboard node with B_WATCH_NAME plus B_WATCH_MOUNT globally, (b) watches be_clipboard, (c) keeps fNotifyList of BMessengers to notify. Mutations flow one way: FSClipboard* functions edit be_clipboard, then send kFSClipboardChanges to the Tracker app messenger; TTracker routes it to fClipboardRefsWatcher->UpdatePoseViews(), which adjusts node monitors and re-broadcasts the SAME message to every registered pose view. Pose views must never assume they see their own writes directly.
- kFSClipboardChanges payload: int32 "device", int64 "directory", bool "clearClipboard", and repeated "tcnode" data items of type T_CLIPBOARD_NODE ('TCNR') each a {node_ref node; uint32 moveMode} — moveMode == kDelete ('Tdel') means "removed from clipboard".
- Per-pose clipboard state is UI-only (BPose::fClipboardMode), reconstructed from the broadcast, and drives the dimmed rendering of cut items.

INVARIANTS THE CODE RELIES ON
- MoveTask assumes ALL source refs live on one volume (it reads srcList->FirstItem()->device only) — cross-volume multi-selection is undefined.
- CalcItemsAndSize likewise assumes one source volume for block-size purposes.
- Trash is PER VOLUME (FSGetTrashDir(dir, dev) via find_directory(B_TRASH_DIRECTORY, ..., &volume)); moving to Trash never crosses volumes, which is why it needs no size calculation and no free-space check.
- The pose-location attribute kAttrPoseInfo is the one attribute deliberately NOT copied (kSkipAttributes / TrackerCopyLoopControl::SkipAttribute); location is instead re-derived by SetupPoseLocation. A loc pointer value of (BPoint*)-1 is a magic "autoplace, do not inherit the source's location" sentinel threaded through MoveTask/MoveItem/CopyFile/CopyFolder/LowLevelCopy.
- Trash restore depends on a string attribute kAttrOriginalPath ("_trk/original_path") written at trash time and searched up the parent chain at restore time.
- Same-volume move is a rename (BEntry::MoveTo) and is effectively atomic; cross-volume move is copy-then-delete per entry and is NOT atomic at any granularity above one file.
