# Tracker's architecture — what the rewrite must reproduce

Read first-hand from the source in `~/kintsugi/src/kits/tracker`; every claim
cites where it was read.  This is the structural half of N0: the behavior
inventory lives in `FEATURES.md`, this file holds the shape those behaviors
hang on.  Where TikuOS forces a different answer it is marked **TIKU**.

## 1. Model — one entry abstraction, many kinds

`Model.h` is the wrapper every pose holds.  It answers what a thing IS
(`IsContainer/IsDirectory/IsFile/IsQuery/IsQueryTemplate/IsSymLink/IsVolume/
IsVirtualDirectory/IsTrash/IsDesktop/InTrash/InRoot`, Model.h:130-144), owns
the lazy node handle (`OpenNode/CloseNode/IsNodeOpen`, Model.h:103-107; the
`ModelNodeLazyOpener` stack helper at Model.h:277 exists so views can open a
node for one operation without leaking descriptors), tracks where its icon
came from (`SetIconFrom/ResetIconFrom`, Model.h:147-150), resolves symlinks
via a linked Model (`SetLinkTo`, Model.h:160), and answers change
notifications (`AttrChanged/IconAttrChanged/StatChanged`, Model.h:169-175).
It is also the drop-target authority (`IsDropTarget`, Model.h:180).

Rewrite consequence: Model is the ONLY place that knows what a backend is.
**TIKU** — a TikuOS namespace node becomes a Model kind alongside file and
directory: `IsContainer` for a VFS dir, typed descriptor for the "kind"
column, capability bits feeding the writable/refusal answers.

## 2. PoseView — one engine, both view modes

`BPoseView : public BView` (PoseView.h:86) is the heart: 10,636 lines that
serve icon, mini-icon and list mode from one state.  Its live state
(PoseView.h private section):

    fPoseList          every pose in the directory
    fFilteredPoseList  the subset surviving the type-ahead filter
    fVSPoseList        the visible-order list, rebuilt on relayout
                       (PoseView.cpp:3550 "relocate all poses in list")
    fSelectionList     selection, ordered
    fZombieList        Models that could not resolve yet
    fBrokenLinks       symlinks whose target vanished
    fColumnList        list-mode columns
    fActivePose / fDropTarget / fAlreadySelectedDropTarget
    fGrid, fOffset, fHintLocation      icon placement
    fAutoScrollState                   drag-edge scrolling
    selection-rect state (isDragging, rect, lastRect, startPoint, lastPoint)
    fFilterStrings, fLastFilterStringCount/Length   type-ahead

`CurrentPoseList()` (PoseView.h:1278) returns the filtered list while
filtering and the full list otherwise — every iterator in the view goes
through it, which is how filtering stays a one-line concern everywhere else.

Rewrite consequence: keep the list trio and the accessor.  A rewrite that
filters at each call site instead will drift.

## 3. Liveness — the view is told, never polls

`FSNotification(const BMessage*)` (PoseView.h:175) is the entry point for
node-monitor traffic; the whole "windows are never stale" property hangs on
it.  Poses are added by a **background thread** (`AddPosesTask`,
PoseView.cpp:1396, spawned at :1305, tracked in `fAddPosesThreads`) so a
directory with thousands of entries never blocks the window, and a directory
switch kills the old task (PoseView.cpp:1491 comment).

Rewrite consequence: directory reading is asynchronous from day one, and the
notification path is the same code path as the initial read.
**TIKU** — `watch` events are the node monitor; the device's own change
notifications feed exactly this entry point.

## 4. Spatial state lives ON the objects

Not in a config database (Attributes.h:99-139):

    _trk/pinfo      per-ENTRY pose info (icon position)
    _trk/columns    per-FOLDER column set
    _trk/viewstate  per-FOLDER view mode, sort, scroll

Each has `_be`/`_le` variants and a "foreign" fallback, so state written on
one endianness is still read on the other.  Sort keys live in the view state
(`PrimarySort/SecondarySort/ReverseSort`, ViewState.h:122-136), and a column
is title + width + alignment + attribute name + type + display-as +
statField + editable (ViewState.h:47-77).

Rewrite consequence: this is why BeOS folders "remember" — the memory is
attached to the thing remembered.  Local files: extended attributes.
**TIKU** — namespace nodes have no free-form attributes; per-node UI state
must go somewhere explicit (host-side store keyed by device+path, or a
device-side convention).  Decision required in N1; recorded in FEATURES.md.

## 5. TaskLoop — the never-freeze rule

`TaskLoop.h` provides `DelayedTask` with one-shot, periodic,
periodic-with-timeout and **run-when-idle** flavours (TaskLoop.h:52-122),
plus `AccumulatingFunctionObject` (TaskLoop.h:147) whose `CanAccumulate`/
`Accumulate` pair merges pending work rather than queuing it repeatedly —
that is how a storm of node-monitor updates becomes one refresh.

Rewrite consequence: long operations and update coalescing are architecture,
not optimisation.

## 6. ContainerWindow — the window is a command surface

`ContainerWindow.cpp` (4,362 lines) handles at least 23 distinct commands
directly (`kNewFolder`, `kCopySelectionTo`, `kMoveSelectionTo`,
`kCreateLink`, `kCreateRelativeLink`, `kSelectMatchingEntries`,
`kOpenParentDir`, `kSwitchDirectory`, `kShowSelectionWindow`,
`kRestoreBackgroundImage`, `kLoadAddOn`, `kSingleWindowBrowseChanged`,
`kShowNavigatorChanged`, `kWindowsShowFullPathChanged`, …), each with its own
enable/disable rules.  The window owns the menus; the pose view owns the
selection they act on.

## 7. What this means for the seams

- Model is the backend seam (files, TikuOS nodes).
- FSNotification is the liveness seam (inotify, `watch`).
- The drawing floor is replaceable (X11 window per Tracker window natively,
  one canvas in wasm) because PoseView draws through view primitives, not
  through a display server API.
