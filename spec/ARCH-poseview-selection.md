# Architecture notes — PoseView: selection, mouse, drag-and-drop

Read from the real source; structural facts the rewrite must
reproduce for this area.

OWNERSHIP / DATA MODEL
- BPoseView owns fPoseList (every pose) and fFilteredPoseList; CurrentPoseList() returns the filtered list while type-ahead filtering is on. Every scan in this area (hit test, hilite, rubber band, drag rect) iterates CurrentPoseList(), so filtered-out poses are unclickable and undraggable.
- Selection is stored TWICE: BPose::fIsSelected (authoritative for drawing, Pose.h BPose::Select) and BPoseView::fSelectionList (a non-owning PoseList used by every consumer: drag payload, Open, context menus, cross-window drops). The rewrite must keep them in sync everywhere EXCEPT during rubber-banding, where the divergence is deliberate: fSelectionList is frozen at its pre-drag contents and used as the "was selected before the band" oracle (BPoseView::SelectPoses(BRect,BList**)), then rebuilt wholesale by polling every pose in BPoseView::_EndSelectionRect (AddIfPoseSelected).
- TWO pivots: fSelectionPivotPose (live anchor for shift-range extension) and fRealPivotPose (stash used while the selection is hidden because the window is inactive — swapped in BPoseView::ShowSelection). Invariant enforced at the tail of AddRemoveSelectionRange: empty selection => both NULL; non-empty selection => pivot non-NULL. Every path that can remove a pose from the selection must null both pivots (DeletePose, EnsurePoseUnselected, RemovePoseFromSelection, AddRemovePoseFromSelection, DuplicateSelection, _EndSelectionRect).
- fDropTarget / fCursorCheck / fAlreadySelectedDropTarget are a separate, parallel "hilite" state that deliberately does NOT touch fSelectionList: HiliteDropTarget hilites by calling BPose::Select(true) transiently, so a drop target that was ALREADY user-selected must be remembered (fAlreadySelectedDropTarget) and not deselected when the drag leaves it.

GESTURE RECOGNITION IS A MESSAGE FILTER, NOT THE VIEW
- BPoseView::AttachedToWindow installs LongAndDragTrackingFilter(kMsgMouseLongDown, kMsgMouseDragged). It synthesizes drag and long-press events; BPoseView::MessageReceived dispatches them to MouseDragged()/MouseLongDown(). The filter also (a) calls SetMouseEventMask(B_POINTER_EVENTS) on mouse-down so moves outside the view still arrive, (b) starts a BMessageRunner armed at the system double-click speed, (c) injects "last_buttons" into B_MOUSE_UP because BView::MouseUp cannot tell which button was released — BPoseView::MouseUp depends on that field. A rewrite that folds this into MouseMoved must reproduce: threshold-before-timer race, clicks==1 gate, and cancel-on-any-button-release.

PHASE SPLIT ACROSS HANDLERS (must be preserved)
- MouseDown: focus, selection mutation, double-click open. MouseUp: rename arming (BPose::MouseUp -> BTextWidget::MouseUp), deferred window activation, secondary-button context menu. MouseMoved: drop-target tracking + drag lifecycle only (it never touches selection except via the rubber band). Pulse: auto-scroll state machine + the pending-rename expiry check (fTextWidgetToCheck->CheckExpiration).
- fTrackMouseUp / fTrackRightMouseUp are set in MouseDown and cleared in MouseDragged and MouseLongDown; that is how a drag or long-press suppresses both the context menu and the window raise.

DRAG AND DROP IS NEGOTIATED, CROSS-WINDOW, SAME-ADDRESS-SPACE
- Outbound: BView::DragMessage(B_SIMPLE_DATA + refs + "src_window" (raw BContainerWindow*) + "click_pt" + "buttons" + "TrackerViewToken" messenger, bitmap, B_OP_ALPHA, offset).
- Inbound: the drop arrives as an ordinary BMessage with WasDropped() at the TOP of BPoseView::MessageReceived, before any switch; B_ARCHIVED_OBJECT drops come through TPoseViewFilter.
- Cross-window operations LOCK THE SOURCE WINDOW (AutoLock<BWindow> in EachItemInDraggedSelection, CanHandleDragSelection, MoveSelectionInto) and read the source pose view's SelectionList directly through the raw pointer. The dragged set is therefore never serialized as data — it is read live from the other window at drop time. Any rewrite that crosses process/thread boundaries must decide what the dragged set means when the source changed mid-drag.
- The actual file work is asynchronous and delegated (FSMoveToFolder / FSDuplicate / FSMoveToTrash with an optional BList of BPoint drop locations); the pose view never blocks on I/O.

THREADING
- Poses are added by a separate thread (AddPosesTask) posting kAddNewPoses; drops, hit tests and band selection can run while poses are appearing, so index-based state (the BList of ints in SelectionRectInfo::selection) is only valid within one band and is discarded at _EndSelectionRect.
- All list mutation happens on the window thread under the window lock; ISR-free.

TIMING / PULSE
- Window pulse rate is reprogrammed as a mode signal: 20 ms while rubber-banding (_BeginSelectionRect), 100 ms while dragging (DragSelectedPoses, MouseMoved B_ENTERED_VIEW), 500 ms when buttons go up (HandleAutoScroll). fAutoScrollState is a 3-state machine (kWaitForTransition -> kDelayAutoScroll -> kAutoScrollOn) with a blocking snooze(600000) in the delay state.

GEOMETRY CONVENTIONS
- Every scan function has two branches: list mode derives the rect from index * fListElemHeight and walks forward until loc.y > bounds.bottom; icon mode uses pose->Location(this) and FirstIndexAtOrBelow() and breaks when a pose's y passes bounds.bottom (this relies on the VS list being y-sorted). Hit testing inverts this: icon mode iterates BACKWARD so the topmost drawn pose wins.
- Icon positions are stored scaled: BPose::MoveTo divides by (IconSizeInt()/32) in kIconMode, so fLocation is always in 32 px units.

CONSTANTS THAT ARE OBSERVABLE
kDoubleClickTresh = 6 px, drag radius threshold = 4.0 px (LongAndDragTrackingFilter.h default), long-press/rename delay = system get_click_speed(), kTransparentDragThreshold = ComposeIconSize(256), fade width = ComposeIconSize(32), drag bitmap alpha 192 (164 when faded), kSlowScrollBucket = 30, kBorderHeight = 20, fAutoScrollInc = 20, auto-scroll arm delay 600 ms, kMenuTrackMargin = 20.
