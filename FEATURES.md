# FEATURES.md — the Tracker fidelity contract

Extracted from the real source (`~/kintsugi/src/kits/tracker`, `src/apps/deskbar`) on 2026-08-11 by 12 readers, then adversarially verified by 3 agents that opened the cited files.

## The rules of this file

1. **A row is a promise.** Every row describes a behavior the real Tracker
   has. The new Tracker is not done until the row is closed.
2. **Rows are never deleted by the implementer.** If a behavior should not
   be built, only the user may waive it — by writing `WAIVED: <reason>` in
   the row's Status. Anything else is stripping, which is what this file
   exists to prevent.
3. **Status values**: `open` (default) · `done` (matches the source; cite
   the new-code file) · `partial` (some of the rule works and a stated clause
   does not; the missing clause must be named on a `MISSING:` line) ·
   `differs` (deliberately different for TikuOS; the difference must be
   written out on a `PORT:` line) · `WAIVED: reason`.
   `differs` REQUIRES AN ALTERNATIVE IMPLEMENTATION.  Absence is never
   `differs` -- that is what the fourth value exists to stop, because a
   verdict that reads as "handled" collected 292 rows of which only 48
   actually were.
4. **The source is the spec.** Where this file and the source disagree,
   the source wins and the row gets fixed.

## Audited state (2026-08-11)

Every row carries a verdict now: **done** means the specific rule the row
states is implemented and matches, **differs** means something in that area
exists but behaves differently (the row says how, in a `PORT:` line), **open**
means absent.

    done 210   partial 155   differs 63   open 461

The last pass built the pose engine: multi-term filtering over column text,
the selection commands (invert, select-by-expression, the context-menu
normalisation, deactivate/reactivate with a parked pivot), the band's toggle
against what it started with, primary and secondary sort keys, the icon-size
ladder and the remap between icon grids, the date column's four-format
degradation, and the rename armed on the slow second click.  553 checks run
in `make test`; every new one was shown to FAIL with its rule removed before
it was allowed to count.

Two habits are worth keeping from it.  The first is that mutation, not
inspection, is what tells you a check is real: of the fifteen rules removed
one at a time to see whether anything noticed, two were caught by nothing and
needed a check written.  The second is that a screenshot finds what a test
cannot -- the Select window came up on "starts with", case-sensitive, because
its defaults were never initialised, and every unit check passed anyway.

A skeptic pass over this pass's own claims moved eight rows back from `done`
to `differs`: an animation drawn over an approximate rectangle, a focus
border without its scroll-bar highlight, a bad expression reported in the
status line rather than a stop alert.  Each is recorded in the row rather
than rounded up.

The pass after it built the column headers: real column offsets (the strip a
row's icon owns is not a column, which is why an offset is absolute and not a
running total), the title strip's tracking state, and the saved layout.  The
gestures are where the work is -- a resize that pins the grabbed edge under
the pointer, a reorder with the term that stops the pair oscillating, a
removal with ten pixels of vertical hysteresis leaving and none returning, a
click that counts only if it was short AND still -- and every one of those
thresholds has a check on each side of it.  The source was extracted and
adversarially verified before a line was written; that pass raised 23
corrections to its own reading, several of which would have become defects.

### The finishing pass

Of the 85 rows the re-audit found `partial` at `small` effort, 33 were
finished.  A verification pass over those 33 upheld 21 as `done` and held 12
at `partial` -- and, more usefully, flagged 21 REGRESSIONS, four of them
serious and all of them mine:

- re-placing a row under a value sort renumbered the pose array without
  rebuilding the filter's index list, so a filtered listing began showing
  rows that did not match it
- the parent-of-Trash refusal was added to the queue builder every operation
  shares, so a plain copy of any folder holding a Trash silently did nothing
- replacing the hardcoded page size with the window's height made paging step
  ONE row in the shell that never told the view its height
- the value editor's ceiling stayed at 128 while the value it opens on grew
  to a kilobyte, so a long value opened already truncated and committed the
  truncation

All four are fixed, each with a check first shown to fail without it.  The
lesson is the one this file keeps teaching: the dangerous part of finishing a
half-built rule is not the rule, it is the code around it that was relying on
the half.

### The differs re-audit

`differs` had become a resting place.  292 rows carried it without saying how
they differed, assigned by an area-level pass that never opened the code each
row cites.  All 292 were re-read against the port and then re-checked by a
second pass that was told the failure mode by name and shown three worked
examples of it -- the tell being a justification that opens "there is no X,
but".  That second pass overturned 49 of 130, every single overturn in the
same direction: 31 rows from `differs` to `partial`, 14 to `open`.  `done`
survived it (36 of 40 upheld), so the verified column was not the problem.

The answer: of the 292, **36 were done, 48 genuinely differ, 180 are partial,
28 are absent**.  The word had been covering four different states, and the
one it sounded most like -- handled -- was the second rarest.

Calibration, since a re-audit is only worth its agreement: an independent
12-row sample taken before all this agreed with the first pass 4 times out of
12 and with the verified result 8 times out of 12, the four residual
disagreements all one step apart (partial vs differs, open vs partial).  Treat
the 180 and the 461 as accurate to a few tens, not exactly.

A second adversarial pass, this time over the finished code rather than over
the reading of the source, upheld 19 findings and was worth more than the
first.  Nine were real: a column dragged back into the strip landed BEFORE
the header it was dropped on rather than after it, so the last position was
unreachable; the drag origin was never rebased across a removal, so a
restored title was painted a column width from the pointer carrying it; a
double-click on an already-fitted divider armed a live resize on the column
just fitted; reordering fired while the pointer was below the strip; the
fit-to-widest measurement asked each cell how wide it wanted to be AT ITS
CURRENT WIDTH, which can only ratify the width it already has; a second copy
of "what does this cell say" had drifted from the first; the saved-layout
bounds were checked before scaling, so the ceiling moved with the font; the
sort triangle pointed the wrong way in both states; and two paints escaped
the list because a clip was replaced rather than intersected.  All nine are
fixed, each with a check first shown to fail without the fix.

## Inventory

- **889 rows** across 12 areas (core 199 · important 468 · polish 222)
- 68 of them were found by the verification pass, not the first read — i.e. would have been missed
- 442 rows had their citations opened and checked (49%); 30 carry a verifier correction; 6 are flagged unconfirmed
- **Verification coverage is partial and uneven.** Verifiers were told to check every `core` row, every row citing a specific number/threshold/modifier, and a sample of the rest. One verifier also reported that the inventory it was handed was truncated mid-batch, so part of the model-attributes area went unchecked. Unverified rows are still useful spec, but treat a surprising detail as unconfirmed until read in source.
- Architecture notes per area: `spec/ARCH-<area>.md`; the structural overview read first-hand: `spec/ARCHITECTURE.md`

| Area | Rows | core | important | polish |
|---|---:|---:|---:|---:|
| PoseView: view modes, layout, drawing, scrolling (`PVL`) | 103 | 20 | 56 | 27 |
| PoseView: selection, mouse, drag-and-drop (`PVS`) | 59 | 13 | 29 | 17 |
| PoseView: keyboard navigation, type-ahead, shortcuts, scripting (`PVK`) | 56 | 12 | 33 | 11 |
| PoseView: node monitoring and live updates (`PVN`) | 71 | 19 | 33 | 19 |
| ContainerWindow: window behavior, menus, navigation (`CW`) | 70 | 17 | 35 | 18 |
| File operations: copy/move/trash/undo/clipboard (`FS`) | 82 | 21 | 46 | 15 |
| Model, attributes, editable text widgets (`MA`) | 82 | 27 | 35 | 20 |
| Queries and the Find window (`Q`) | 60 | 15 | 34 | 11 |
| Icons, thumbnails, pose visuals (`IV`) | 61 | 13 | 32 | 16 |
| List columns, count view, settings (`TS`) | 65 | 11 | 32 | 22 |
| Application level: Tracker app, desktop, open-with, file panels, task loop (`AW`) | 105 | 18 | 61 | 26 |
| Deskbar: the whole application (`DB`) | 75 | 13 | 42 | 20 |


---

## PoseView: view modes, layout, drawing, scrolling

Architecture notes: [`spec/ARCH-poseview-layout.md`](spec/ARCH-poseview-layout.md)

#### PVL-001 · core · done
**A folder view can be shown in three modes: large-icon, mini-icon, or list.**  
Mode constants kIconMode ('Ticn'), kMiniIconMode ('Tmic'), kListMode ('Tlst'); the mode is a message `what` posted to the pose view, and SetViewMode() is the single entry point for the transition.  
`PoseView.cpp` → `BPoseView::SetViewMode / BPoseView::MessageReceived (cases kIconMode, kListMode, kMiniIconMode)`  
**TIKU:** Same three modes apply; device-namespace nodes need an icon/label representation for icon mode even though they are not files.  

#### PVL-002 · important · done
**In icon mode the user picks an icon size from 32, 40, 48, 64, 96 or 128 pixels.**  
Menu items post kIconMode with an int32 "size" field; the pose view calls fViewState->SetIconSize(size) then SetViewMode(kIconMode). Sizes go through be_control_look->ComposeIconSize() so the effective pixel size is display-scaled.  
`ContainerWindow.cpp` → `BContainerWindow::AddIconSizeMenu (kIconSizes[]) → BPoseView::MessageReceived case kIconMode`  
**PORT:** build_icon_size() now emits all six rungs — CMD_ICON_SIZE_32/40/48/64/96/128 with px[] = {32,40,48,64,96,128} — and radio-marks the one matching the live c->icon_size only while view_mode == MODE_ICON, so a 40 px view marks '40 x 40' and nothing is marked in list mode. All six commands are wired in menu_invoke() to set_icon_size(), and the ladder matches icon_ladder[] that Cmd+/- steps, so keyboard and menu can now reach the same set. Labels exist for all six in sc_label().

#### PVL-003 · polish · done
**Cmd-+ / Cmd-- step the icon size up or down one rung of the size ladder and stop at the ends.**  
kIconMode with "scale"==1 maps 32→40→48→64→96→128; "scale"==0 maps the reverse. Already at 128 (increase) or 32 (decrease) is a no-op; only applies while already in kIconMode.  
`PoseView.cpp` → `BPoseView::MessageReceived case kIconMode ("scale" branch)`  

#### PVL-004 · polish · done
**Choosing "Icon view" without a size restores the icon size that was last used.**  
Falls back to fViewState->LastIconSize(); if that is uninitialized (<32 or >128) it defaults to 32. LastIconSize is captured on every transition out of icon mode.  
`PoseView.cpp` → `BPoseView::MessageReceived case kIconMode (else branch) / BPoseView::SetViewMode`  

#### PVL-005 · core · done
**Entering list mode shows the column title bar and the Attributes menu; leaving list mode hides both and cancels type-ahead filtering.**  
SetViewMode: oldMode==kListMode → ClearTypeAheadFiltering(), window->HideAttributesMenu(), fTitleView->Hide(); newMode==kListMode → ShowAttributesMenu(), fTitleView->Show(). ContainerWindow::ViewModeChanged additionally removes the background image in list mode and restores it when leaving.  
`PoseView.cpp` → `BPoseView::SetViewMode / BContainerWindow::ViewModeChanged`  

#### PVL-006 · important · done
**Mini-icon and list modes always use the mini icon size regardless of the icon-mode size setting.**  
SetIconPoseHeight() forces fViewState->SetIconSize(B_MINI_ICON) for kMiniIconMode and kListMode; only kIconMode honours the user-chosen size.  
`PoseView.cpp` → `BPoseView::SetIconPoseHeight`  

#### PVL-007 · core · partial
**Row/pose height is derived per mode: icon = iconSize + fontHeight + 1, mini-icon = max(iconSize, fontHeight+1), list = a fixed font-derived row height.**  
fListElemHeight = ceilf(be_plain_font->Size() * 1.65f) set in the constructor; sFontHeight = ascent+descent+1 computed once statically in InitCommon().  
`PoseView.cpp` → `BPoseView::SetIconPoseHeight / BPoseView::BPoseView / BPoseView::InitCommon`  
**MISSING:** Heights do differ per mode, but none is font-derived: pose height is the grid cell (icon = iconSize*1.875, mini = 16*1.25) and the list row is the compile-time constant LIST_ROW_H 20, which set_font_size never touches (it only rescales column widths) — so the fontHeight term the row states is absent everywhere and a larger font does not grow a row.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1616-1646 (layout_info), :1709-1716 (pose_extent), :1893-1894 (pose_rect h = cell h), :3155-3165/:3200-3214 (font_px scales widths only); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:47-48,1090*

#### PVL-008 · important · open
**The Desktop can never be in list mode; a saved list-mode state on the Desktop is coerced back to icon mode.**  
Both RestoreState overloads: `if (IsDesktopView() && ViewMode() == kListMode) fViewState->SetViewMode(kIconMode);`  
`PoseView.cpp` → `BPoseView::RestoreState(AttributeStreamNode*) and BPoseView::RestoreState(const BMessage&)`  

#### PVL-009 · important · open
**List mode and icon mode each remember their own scroll position, so switching modes and back returns you to where you were.**  
ScrollTo() writes Extent().LeftTop() into fViewState->SetListOrigin() or SetIconOrigin() depending on mode; SetViewMode/InitCommon/SwitchDir read the matching origin back and PinPointToValidRange() it before scrolling.  
`PoseView.cpp` → `BPoseView::ScrollTo / BPoseView::SetViewMode / BPoseView::InitCommon`  

#### PVL-010 · polish · open
**Changing icon size keeps the visual centre of the view roughly fixed instead of jumping to the top-left.**  
SetViewMode converts the bounds centre into unscaled (32px) space with oldScale = lastIconSize/32, back out with newScale = iconSize/32, and applies the difference as scaleOffset added to the restored icon origin. Skipped on the Desktop.  
`PoseView.cpp` → `BPoseView::SetViewMode (scaleOffset block)`  

#### PVL-011 · important · done
**Switching between icon and mini-icon mode remaps existing icon positions into the new grid, preserving their relative arrangement.**  
Only when fOkToMapIcons is set and the mode or icon size actually changed. MapToNewIconMode pins the old location to the old grid, converts the whole-cell count and the intra-cell delta from old grid units to new grid units, then re-adds the new offset and flags the pose for location save.  
`PoseView.cpp` → `BPoseView::SetViewMode / BPoseView::MapToNewIconMode`  

#### PVL-012 · core · done
**Icon layouts scale proportionally with icon size: a layout arranged at 32px reappears in the same relative arrangement at 128px.**  
BPose stores fLocation in unscaled 32px space; Location() multiplies by IconSizeInt()/32 and SetLocation()/MoveTo() divide by it (floorf), but only in kIconMode — mini-icon and list mode use scale 1.0.  
`Pose.cpp` → `BPose::Location / BPose::SetLocation / BPose::MoveTo`  
**TIKU:** A device node's position is view state, not node state; TikuOS must keep this map somewhere other than the node itself.  

#### PVL-013 · core · done
**Icons snap to a grid whose cell size and origin offset differ per view mode.**  
kMiniIconMode: grid = (iconSize*6, ceil(iconSize*1.25)), offset = (ceil(iconSize*0.6), ceil(iconSize*0.3)). kIconMode: grid = iconSize + ceil(iconSize*0.875) on both axes, offset = ceil(iconSize*0.625). List/default: grid = (0,0), offset = (labelSpacing-1, labelSpacing-1). fGrid/fOffset are recomputed on every mode or size change.  
`PoseView.cpp` → `BPoseView::GetLayoutInfo`  

#### PVL-014 · core · done
**Grid snapping rounds to the nearest cell, and is a no-op when there is no grid.**  
PinToGrid subtracts the offset, rounds via floorf(v/grid + 0.5) for non-negative and ceilf(v/grid - 0.5) for negative coordinates, then re-adds the offset. Returns the point unchanged if grid.x or grid.y is 0 (i.e. list mode).  
`PoseView.cpp` → `BPoseView::PinToGrid`  

#### PVL-015 · core · done
**Icons that have no saved position are auto-placed left-to-right, wrapping to the next row at the right edge of the view.**  
PlacePose starts at fHintLocation, insets the candidate rect by (-3,0) so icons never touch, and advances by fGrid.x until an unoccupied slot is found; NextSlot wraps by adding fGrid.y and resetting x to viewBounds.left + fOffset.x, re-pinned to the grid. After placing, fHintLocation advances one grid cell to the right.  
`PoseView.cpp` → `BPoseView::PlacePose / BPoseView::NextSlot`  

#### PVL-016 · important · partial
**Occupancy testing only inspects icons vertically near the candidate slot, so placement stays fast in crowded folders.**  
SlotOccupied binary-searches fVSPoseList (poses sorted by y) via FirstIndexAtOrBelow(poseRect.top - IconPoseHeight()) and walks forward only while the candidate's y < poseRect.bottom. It also returns "occupied" when the candidate crosses the right edge and the hint x differs from the row-start x, forcing a wrap.  
`PoseView.cpp` → `BPoseView::SlotOccupied / BPoseView::FirstIndexAtOrBelow`  
**MISSING:** slot_occupied does skip poses outside the candidate's vertical band, but it walks the whole pose array linearly with no y-sorted binary search, so the "stays fast in crowded folders" clause is not met (the right-edge wrap is handled separately in next_slot).  
*src/tiku_trk_poseview.c:slot_occupied (1725-1750)*

#### PVL-017 · important · partial
**The auto-placement hint restarts at the top-left of the currently scrolled view, not at the top-left of the document.**  
fHintLocation = PinToGrid(LeftTop() + fOffset). Called from InitCommon, ClearPoses, Cleanup(true), SetViewMode, SwitchDir, DeletePose and after a window resize.  
`PoseView.cpp` → `BPoseView::ResetPosePlacementHint`  
**MISSING:** Re-seeded on a directory switch and a mode change; a bare reload and a window resize still leave the old hint.

#### PVL-018 · important · open
**On the Desktop, icons are never placed on top of the Deskbar, on a replicant, or off-screen.**  
IsValidLocation(rect) requires Bounds().Contains(rect), rejects intersection with the Deskbar frame inset by (-10,-10), and rejects intersection with any child view frame inset by (-5,-5). The Deskbar frame is re-queried at most every 500 ms and cached otherwise. Off the Desktop this check always returns true.  
`PoseView.cpp` → `BPoseView::IsValidLocation / BPoseView::GetDeskbarFrame`  

#### PVL-019 · polish · done
**If no valid free slot exists anywhere, the icon is dropped at the grid origin rather than left unplaced.**  
PlacePose breaks out of the search when the candidate rect no longer intersects viewBounds (Desktop only) and resets fHintLocation to PinToGrid(0,0).  
`PoseView.cpp` → `BPoseView::PlacePose`  
**PORT:** place_pose() now calls tiku_trk_poseview_reset_hint(v) when the probe budget is exhausted, BEFORE storing loc_x/loc_y from the hint, so the icon lands at the grid origin (ox,oy) instead of at whatever far slot the walk reached, and it is still marked placed/auto_placed rather than left unplaced. Two caveats worth naming, neither a failure of the stated rule: the exhaustion test `tries >= 4096` also fires in the one case where the 4096th probe found a FREE slot (the loop condition evaluates slot_occupied() first, so a success on the last iteration exits with tries == 4096), throwing away a valid slot; and because next_slot() wraps downward without bound the search always finds empty space, so the origin fallback is close to unreachable in practice — it is a safety net, not Haiku's 'candidate left viewBounds' condition.

#### PVL-020 · important · done
**Icons that were auto-placed during loading are re-placed once the folder finishes loading, so they do not sit under icons that arrived later with saved positions.**  
CreatePoses marks each auto-placed pose with SetAutoPlaced(true) (unless fAlwaysAutoPlace); AddPosesCompleted calls CheckAutoPlacedPoses which, for each flagged pose, removes it from the vertical-sort list, re-places it starting from its own location, clears the flag, extends the extent and invalidates both old and new rects.  
`PoseView.cpp` → `BPoseView::CreatePoses / BPoseView::AddPosesCompleted / BPoseView::CheckAutoPlacedPoses`  

#### PVL-021 · core · partial
**"Clean up" (Cmd-K) snaps every icon to the nearest grid point without reordering them.**  
Cleanup(false): for each pose compute PinToGrid(location); if the result differs from the current location or the location became invalid (Desktop obstacle), remove it from the vertical-sort list, MoveTo the grid point, and if that slot was already occupied (or invalid) fall back to a fresh PlacePose from a reset hint. Old and new rects are both invalidated.  
`PoseView.cpp` → `BPoseView::Cleanup (doAll == false)`  
**MISSING:** Every icon does end on a grid point, but the "without reordering" clause fails: cleanup re-sorts, clears every pose's placed flag and re-lays out from the top-left in sort order (Tracker's Cleanup(doAll=true)), and Cmd-K and Cmd-Shift-K both run it.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_cleanup (1826-1849); src/tiku_trk_desktop.c:menu_invoke (1040-1044)*

#### PVL-022 · core · done
**"Clean up all" (Cmd-Shift-K) re-sorts the icons and re-lays them out from the top-left of the view.**  
Cleanup(true): SortPoses, disable scrollbars, ClearExtent, ClearSelection, ScrollTo(B_ORIGIN), reset the placement hint, empty and rebuild fVSPoseList by PlacePose-ing every pose in sorted order, RecalcExtent, then set the horizontal scrollbar to its minimum and invalidate.  
`PoseView.cpp` → `BPoseView::Cleanup (doAll == true)`  
**VERIFIER CORRECTION:** The occupancy test is run against the pose's OLD rect, not the destination grid slot: `BRect oldBounds(pose->CalcRect(this)); pose->MoveTo(newLocation, this); if (SlotOccupied(oldBounds, viewBounds) || intersectsDesktopElements)` (PoseView.cpp:3576-3590). So the re-place fallback fires when some other pose overlapped where the icon *was*, which is not the same rule; and when it does not fire, AddToExtent is called with the stale oldBounds.  

#### PVL-023 · important · done
**Both clean-up commands do nothing in list mode.**  
Cleanup() returns immediately when ViewMode() == kListMode.  
`PoseView.cpp` → `BPoseView::Cleanup`  

#### PVL-024 · important · partial
**"Arrange by <attribute>" sets the sort key and re-lays out all icons in that order; choosing the same attribute again turns arrangement off.**  
kArrangeBy: if the attribute has no column yet, add one via HandleAttrMenuItemSelected; if it is already the primary sort, the hash is replaced by 0 (no sort); then SetPrimarySort, SetSecondarySort(0), Cleanup(true). kArrangeReverseOrder flips fViewState->ReverseSort() and also runs Cleanup(true).  
`PoseView.cpp` → `BPoseView::MessageReceived cases kArrangeBy and kArrangeReverseOrder`  
**TIKU:** Arrange-by must work over typed descriptor fields of device nodes, not only file attributes.  
**VERIFIER CORRECTION:** kArrangeBy does set `attrHash = 0` when it equals the current primary sort, but BPoseView::SetPrimarySort (PoseView.cpp:9529) only writes the view state when `ColumnFor(attrHash) != NULL`, and no column hashes to 0 — so the primary sort is left unchanged. The user-visible effect of re-choosing the same attribute is only SetSecondarySort(0) + Cleanup(true) (a re-layout in the same order), not "arrangement off".  
**MISSING:** The first half holds — each Sort by <attr> menu case sets the key (including the device Value field) and re-lays out via cleanup() — but re-choosing the same attribute runs sort_by()'s `sort_reverse = !sort_reverse`, i.e. it reverses the listing, which is neither the row's 'arrangement off' nor the corrected real behaviour (same order, secondary cleared); the secondary key is also left alone.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_desktop.c:999-1018 (SORT_BY_* -> sort_by + cleanup); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1370-1387 (sort_by toggles reverse on the same field)*

#### PVL-025 · polish · differs
**A new folder created from the context menu appears where you clicked; created by keyboard shortcut in an icon mode it appears under the mouse if the mouse is over the window.**  
PlaceFolder reads "be:invoke_origin" and converts from screen; otherwise (non-list mode) it uses GetMouse and only sets a position if the screen point is inside Window()->Frame(). The position is written with FSSetPoseLocation before the pose appears.  
`PoseView.cpp` → `BPoseView::PlaceFolder`  
**TIKU:** Requires a way to pre-seed a position for a node before it shows up in the view.  
**PORT:** The position is seeded on the VIEW (tiku_trk_poseview_set_drop_point) and consumed by the next entry to appear, not carried in the invoking message; nothing in this shell creates a folder yet, so the seed has no caller but the mechanism is proven.

#### PVL-026 · important · done
**Dragging icons inside their own icon-mode window moves them by the drag delta; holding Command snaps the drop positions to the grid.**  
MoveSelectionInto detects source folder == destination folder: list mode returns without moving, force-copy turns into DuplicateSelection, otherwise each selected pose is removed from the vertical-sort list, moved by (loc - where), optionally PinToGrid'd, then the extent is updated (RemoveFromExtent old, AddToExtent new) and the pose is re-inserted into the vertical-sort list. dropOnGrid comes from B_COMMAND_KEY in MoveSelectionTo.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto (static overload) / BPoseView::MoveSelectionTo`  

#### PVL-027 · polish · open
**Icons dropped from a list-mode window into an icon-mode window are stacked in a vertical column at the drop point.**  
GetDropPointList: if the source was in list mode, each pose gets dropEnd + (0, index * (IconPoseHeight() + 3)); otherwise it keeps its relative offset dropEnd + (poseLocation - dropStart). Grid pinning applied afterwards if requested. Returns NULL entirely when the destination is in list mode.  
`PoseView.cpp` → `BPoseView::GetDropPointList`  

#### PVL-028 · polish · done
**A saved icon position that is nonsense is ignored and the icon is auto-placed instead.**  
ReadPoseInfo rejects |x| or |y| > kSanePoseLocation (50000) and any pose info whose fInitedDirectory matches neither the item's parent directory nor the view's target node, by setting fInitedDirectory = -1. In icon modes only, a failed read on a file created within the last 5 seconds is retried up to 10 times at 10 ms intervals so freshly created items land where they were told to.  
`PoseView.cpp` → `BPoseView::ReadPoseInfo`  
**VERIFIER CORRECTION:** The loop is `for (int32 count = 10; count >= 0; count--)`, i.e. up to 11 attempts (10 retries after the first). Everything else checks out: kSanePoseLocation = 50000, list mode breaks out immediately, crtime window is `st_crtime < now - 5 || st_crtime > now`, snooze(10000).  

#### PVL-029 · important · done
**Icon positions persist per item and survive closing the window; on the Desktop a separate position is remembered per screen resolution.**  
SavePoseLocations writes kAttrPoseInfo (killing the foreign-endian twin) for every pose flagged NeedsSaveLocation() && HasLocation(); skipped entirely on read-only volumes or volumes without attribute support, and when fSavePoseLocations is off. Desktop additionally writes kAttrExtendedPoseInfo via SetLocationForFrame(location, desktopFrame). Root and Trash poses store their info on the Desktop directory (kAttrDisksPoseInfo / kAttrTrashPoseInfo).  
`PoseView.cpp` → `BPoseView::SavePoseLocations`  
**TIKU:** Device-namespace nodes have no attribute store to hang a position on; TikuOS needs a view-owned position map keyed by node path (and must decide whether it is durable).  

#### PVL-030 · important · open
**After a screen resolution change, Desktop icons move to the positions recorded for that resolution, and any icon that no longer fits is pulled back on screen.**  
CheckPoseVisibility (called from DeskWindow with the new frame) reads ExtendedPoseInfo and applies LocationForFrame if one exists and differs. Independently, any pose whose rect does not intersect Bounds() inset by (20,20) has its location ConstrainTo'd into those bounds. Moved poses are flagged for save, re-inserted into the vertical-sort list, and both old and new rects invalidated.  
`PoseView.cpp` → `BPoseView::CheckPoseVisibility`  

#### PVL-031 · core · done
**List rows are fixed height and pose N always occupies the strip starting at N * rowHeight.**  
CalcPoseRectList(pose, index) = pose->CalcRect(BPoint(0, index * fListElemHeight), ...). Every list-mode iteration derives its start index as (int32)(y / fListElemHeight), so the mapping row index ↔ y must stay exact.  
`PoseView.cpp` → `BPoseView::CalcPoseRectList / BPose::CalcRect(BPoint, const BPoseView*, bool)`  

#### PVL-032 · core · done
**The first list column starts after the row icon, and each following column starts at the previous column's right edge plus a fixed margin.**  
StartOffset() = fListOffset + ListIconSize() + kMiniIconSeparator(3) + 1, with fListOffset = ceilf(DefaultLabelSpacing()*3.3f) and ListIconSize() = ComposeIconSize(B_MINI_ICON)+1. Successive offsets are offset + width + kTitleColumnExtraMargin (16 = 11 left + 5 right). This recomputation is repeated identically in AddColumnList, AddColumn, ResizeColumn and MoveColumnTo.  
`PoseView.h` → `BPoseView::StartOffset (inline) / BPoseView::AddColumnList`  
**VERIFIER CORRECTION:** It is NOT identical. AddColumnList (PoseView.cpp:511) and MoveColumnTo (PoseView.cpp:9728) use `offset + width - kRoomForLine/2 + kTitleColumnExtraMargin`; AddColumn (PoseView.cpp:2841) and ResizeColumn (PoseView.cpp:9690) use `offset + width + kTitleColumnExtraMargin` with no kRoomForLine/2 term. A reimplementation that picks one formula will be off by 1 px depending on the path taken. kTitleColumnExtraMargin = 16 (11+5), kRoomForLine = 2 — those numbers are right.  
**PORT:** start_offset() reserves the row-icon strip (MINI_ICON 16 + PLACE_GAP 3 + 1) and reflow_columns() lays every subsequent column at the previous right edge plus the fixed COL_MARGIN (11+5=16); it is the single function called from construction, restore, add, remove, resize and reorder, and the drawing code positions cells from those absolute offsets.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:3381-3403 (start_offset/reflow_columns), callers at :515,:3119,:3143,:3362,:3438,:3474,:3528; /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.h:39-40,89-92 (constants); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:426-445 (icon strip left of first column)*

#### PVL-033 · important · done
**Restored column layout is re-derived from scratch: saved offsets only determine order, never final positions.**  
AddColumnList sorts the restored columns by their saved Offset() (CompareColumns), then overwrites each offset by walking from StartOffset(); the comment notes columns are always realigned because the title view snaps on resize anyway.  
`PoseView.cpp` → `BPoseView::AddColumnList / CompareColumns`  

#### PVL-034 · core · differs
**A folder with no saved column layout gets Name / Size / Modified columns.**  
Widths 145 / 80 / 150; Name is B_ALIGN_LEFT, editable and a stat field; Size is B_ALIGN_RIGHT. Adding a B_TIME_TYPE column starts watching for date-format changes. If the restored primary sort has no column, it falls back to the first column's attribute hash, and a secondary sort equal to the primary is cleared.  
`PoseView.cpp` → `BPoseView::SetupDefaultColumnsIfNeeded / BPoseView::RestoreColumnState`  
**TIKU:** Default columns for device nodes would be name plus typed-descriptor/capability columns rather than size/mtime.  
**PORT:** A view with no saved layout is constructed with a default column set — Name/Kind/Value/Cap with Size right-aligned when added — which is precisely the device-descriptor substitution for Size/Modified that the row's TIKU note prescribes, so the rule is implemented in adapted form; the deviations the auditor conceded (no date-format watch, which has no counterpart since the port's date column degrades by width rather than locale, and a restore fallback to Name instead of the first column) are secondary to the row's stated rule.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:506-516 (default columns), :3083-3089 (column_alignment: Size right), :3364-3378 (restore fallback to Name)*

#### PVL-035 · important · partial
**Removing a column shifts every column to its right leftwards and only repaints from that column onward; the last remaining column cannot be removed.**  
RemoveColumn with CountColumns()==1 shows the alert "You must have at least one attribute showing." (suppressed when runAlert is false) and returns false. Otherwise each pose drops that column's widget, later columns lose (width + kTitleColumnExtraMargin) from their offset, and Invalidate() is limited to Bounds() with left = the removed column's offset. Date-format watching stops if no B_TIME_TYPE column is left.  
`PoseView.cpp` → `BPoseView::RemoveColumn`  
**MISSING:** Removal drops the column and reflows every later one leftwards, but there is no partial repaint (the window is marked dirty and fully redrawn) and no last-column alert — the invariant is instead enforced by refusing to remove the Name column.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_toggle_column (3104-3135)*

#### PVL-036 · important · done
**Dragging a column divider resizes it live, repainting only the resized column and sliding the rest of the row content sideways.**  
ResizeColumn redraws just [column.Offset(), column.Offset()+newSize+margin] through ColumnRedraw's shared offscreen bitmap, CopyBits-shifts everything right of the column, draws/undraws the tracking line via the supplied callbacks, and SynchronousUpdate(clip=true)s only the tail that fell off. Shrink redraws before the copy, grow copies before the redraw. Minimum width is kMinColumnWidth (20).  
`PoseView.cpp` → `BPoseView::ResizeColumn / BPoseView::ColumnRedraw / ColumnResizeState::Moved`  
**PORT:** The row content really does slide: every offset is re-derived and the list repaints.  Tracker blits the untouched pixels sideways and repaints only the resized column and the tail; the visible result is the same and the copy is an optimisation this drawing floor has no need of yet.

#### PVL-037 · polish · done
**Double-clicking (or middle-clicking) a column divider sizes that column to its widest visible value.**  
BTitleView::MouseDown detects a second click inside the resize edge within the double-click interval (or B_TERTIARY_MOUSE_BUTTON, which forces it) and calls ResizeColumnToWidest, which scans the current pose list for the maximum BTextWidget::PreferredWidth with a floor of kMinColumnWidth (20).  
`PoseView.cpp` → `BPoseView::ResizeColumnToWidest / BTitleView::MouseDown`  

#### PVL-038 · important · done
**Column titles can be dragged to reorder columns, and dragged off the title bar to delete a column (dragging back in restores it).**  
ColumnDragState::Moved: leaving the title bounds outset by kRemoveTitleMargin (10) archives the column, calls RemoveColumn(false) and switches to a dotted rect-tracking outline; re-entering the title bar re-instantiates and AddColumn()s it after whatever title is under the mouse. Reorder calls MoveColumnTo, which re-lays out all offsets from StartOffset() and invalidates everything right of the leftmost affected column.  
`PoseView.cpp` → `BPoseView::MoveColumnTo / ColumnDragState::Moved`  
**VERIFIER CORRECTION:** The margin is applied vertically only: `titleBoundsWithMargin.InsetBy(0, -kRemoveTitleMargin)`. Horizontally the bounds are unchanged, so dragging a title left/right past the ends of the title bar never removes the column — only dragging up/down more than 10 px does (TitleView.cpp:721-740).  
**PORT:** Removal refuses the NAME column rather than the last remaining one -- a listing with no names identifies nothing.  A column dragged out and back keeps its width; its alignment comes back from the field's default rather than being carried, which only differs for a column whose alignment was restored from a saved layout.

#### PVL-039 · important · done
**Clicking a column title sorts by it; clicking the current sort column reverses; Shift-click manages the secondary sort.**  
ColumnDragState::Clicked: hitting the primary sort with Shift and a secondary set swaps primary and secondary, without Shift toggles reverse; Shift on a non-primary sets it as secondary (or clears it if it already was); plain click sets primary and clears reverse. Primary == secondary always collapses to secondary = 0. Then SortPoses() + Invalidate(). Titles draw an up/down triangle marker, dimmer for the secondary key.  
`TitleView.cpp` → `ColumnDragState::Clicked / BColumnTitle::Draw`  
**PORT:** sort_click reproduces ColumnDragState::Clicked clause for clause — Shift on the primary with a secondary swaps the two, primary without a usable swap reverses, Shift elsewhere sets or clears the secondary, primary==secondary collapses to none — then resorts and marks state dirty, and the title strip draws an up/down triangle whose ink is tinted 1.60 for the primary against 1.30 for the secondary.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1424-1459 (sort_click); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_titleview.c:379-385 (click routes to sort_click); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:181-204 (triangle, dimmer for secondary)*

#### PVL-040 · important · open
**Scrolling the list horizontally scrolls the column title bar in lockstep.**  
TScrollBar (the horizontal bar) holds the title view and mirrors its value into fTitleView->ScrollTo(value, origin.y) before forwarding to BScrollBar::ValueChanged.  
`PoseView.cpp` → `TScrollBar::ValueChanged / TScrollBar::SetTitleView`  

#### PVL-041 · core · partial
**Column layout, sort keys, view mode, icon size and scroll origins persist per folder and reload with it.**  
Columns are serialised to kAttrColumns and the view state to kAttrViewState (kAttrDesktopViewState for the Desktop, kAttrDisksColumns / kAttrDisksViewState for the Disks/root window), each with a byte-swapped "foreign" fallback attribute that is read when the native one is missing and deleted whenever the native one is written. Attributes larger than 10000 bytes are rejected as munged.  
`PoseView.cpp` → `BPoseView::SaveState / BPoseView::RestoreState / BPoseView::SaveColumnState / BPoseView::RestoreColumnState`  
**TIKU:** Folder-level view state must live somewhere other than a BFS attribute for device-namespace directories, which may be synthetic and unwritable.  
**MISSING:** View mode and the column layout do persist per folder (keyed on the folder path) and reload with it, but sort keys, icon size and scroll origin are never written to the store, so those three do not survive a reopen.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_save_state (2196-2200) / tiku_trk_poseview_save_columns (3230-3243)*

#### PVL-042 · polish · open
**A window's column set and sort order can be copied and pasted into another window.**  
kCopyAttributes flattens SaveState() into the clipboard as "application/tracker-columns"; kPasteAttributes removes all existing columns (keeping one alive until the new ones are added, then removing it), re-instantiates the pasted columns, restores primary/secondary/reverse sort, then SortPoses() and Invalidate().  
`PoseView.cpp` → `BPoseView::MessageReceived cases kCopyAttributes and kPasteAttributes`  

#### PVL-043 · core · done
**List rows are ordered by the primary sort attribute with ties broken by the secondary attribute, and reverse sort flips the comparison.**  
PoseCompareAddWidget swaps the operands when ReverseSort() is set, lazily creates the text widget for the sort column if missing, and loops exactly once more on the secondary key when the primary compares equal. The sort itself is std::stable_sort, so equal rows keep their previous relative order.  
`PoseView.cpp` → `PoseCompareAddWidget / BPoseView::SortPoses`  
**TIKU:** Sorting must work on typed descriptor values of device nodes, which may change under a watch notification and re-order rows live.  
**PORT:** pose_cmp compares the primary field, consults the secondary only when the primary ties, adds a natural-order name tie-break for determinism, and negates the whole result under sort_reverse; the Value key compares through the typed descriptor and a CHANGED notice re-checks sorted_slot and re-inserts the row, so a value change re-orders the listing live as the TIKU note requires.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:324-347 (field_cmp), :349-372 (pose_cmp), :375-386 (resort), :2639-2677 (re-sort on change notice)*

#### PVL-044 · important · partial
**While a folder is loading, each arriving item is inserted at its correct sorted position rather than appended and re-sorted at the end.**  
AddPoseToList binary-searches with BSearchList (which first checks whether the old index is still valid against its neighbours, then falls back to BSearch); the new row is inserted at that index. AddPosesCompleted still does a final SortPoses + Invalidate in list mode.  
`PoseView.cpp` → `BPoseView::AddPoseToList / BPoseView::BSearchList`  
**MISSING:** The folder load does exactly what the row excludes — collect() appends every entry and reload() calls resort() once at the end — and the binary-search insert_sorted() is reached only from the notify paths (create/rename/value change), so the sorted-on-arrival mechanism exists but not for the loading path the row is about.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:216-268 (collect appends), :389-405 (reload -> resort), :2327-2380 (sorted_slot/insert_sorted), callers at :2526,:2613,:2669 (notices only)*

#### PVL-045 · important · open
**Inserting a row into the visible part of the list slides the rows below down by one row height instead of repainting the whole list.**  
AddPoseToList builds srcRect from Extent() clipped to the view bounds starting at the new row's top, destRect offset by fListElemHeight, CopyBits src→dest, then SynchronousUpdate only the newly exposed strip. Rows added entirely below the visible bottom skip drawing altogether. InsertPoseAfter does the same for re-sorted rows.  
`PoseView.cpp` → `BPoseView::AddPoseToList / BPoseView::InsertPoseAfter`  

#### PVL-046 · polish · open
**Rows inserted above the visible area are batched: the accumulated scroll is applied in a single blit rather than one per item.**  
listViewScrollBy accumulates fListElemHeight per skipped insertion; FinishPendingScroll performs one CopyBits of the whole visible region and a SynchronousUpdate of the remainder. Flushed at the end of every CreatePoses chunk.  
`PoseView.cpp` → `BPoseView::FinishPendingScroll / BPoseView::AddPoseToList`  
**TIKU:** Watch-notification storms on live device values would drive this path; TikuOS should batch as this does or the list will thrash.  

#### PVL-047 · important · open
**When a value in the sort column changes, only that row moves — the gap closes at its old position and opens at its new one.**  
AttributeChanged hashes the changed attribute (or maps a stat field via sAttrColumnMap) and, only if it equals the primary or secondary sort, calls _CheckPoseSortOrder. That removes the pose, re-searches its position, and if it moved does CloseGapInList at the old index followed by InsertPoseAfter at the new one. If the new index equals the old, nothing is redrawn.  
`PoseView.cpp` → `BPoseView::AttributeChanged / BPoseView::_CheckPoseSortOrder`  
**TIKU:** A watch notification on a device node value is the analogue; re-sorting on every notify (no coalescing) will visibly churn rows.  

#### PVL-048 · important · open
**Deleting a row closes the gap by sliding the rows below up rather than repainting them.**  
DeletePose computes the invalid rect from the row index, calls CloseGapInList (which extends the rect to Extent().bottom + one row, CopyBits the region up by one row height, and returns the strip to invalidate), then updates the count, scroll range and placement hint. In icon mode it calls RemoveFromExtent instead.  
`PoseView.cpp` → `BPoseView::DeletePose / BPoseView::CloseGapInList`  

#### PVL-049 · core · open
**The scrollable area is the list's column width by row count in list mode, and the bounding box of all icons (outset by the layout offset) in icon modes.**  
ListModeExtent: right = lastColumn->Offset() + Width() + kTitleColumnRightExtraMargin - kRoomForLine/2, bottom = fListElemHeight * CurrentPoseList()->CountItems(), origin always (0,0). IconModeExtent: fExtent.InsetByCopy(-fOffset), collapsing to a point at LeftTop() when invalid.  
`PoseView.cpp` → `BPoseView::Extent / BPoseView::ListModeExtent / BPoseView::IconModeExtent`  

#### PVL-050 · important · open
**The icon-mode extent is grown incrementally as icons are added or moved and only fully recomputed when a removal touched one of its edges.**  
AddToExtent unions a rect in; RemoveFromExtent triggers RecalcExtent only if the removed rect touched or exceeded any edge of fExtent; ClearExtent sets the inverted sentinel (INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN). RecalcExtent walks fVSPoseList (or fFilteredPoseList when filtering).  
`PoseView.cpp` → `BPoseView::RemoveFromExtent / BPoseView::RecalcExtent / BPoseView::AddToExtent (PoseView.h inline)`  

#### PVL-051 · core · partial
**Scrollbar range, step and thumb proportion track the content: half a row per click, a full page per page-jump, thumb sized by visible fraction.**  
UpdateScrollRange: min = min(extent.leftTop, origin), max = (extent.rightBottom - bounds.rightBottom) + origin clamped to be at least origin; SetSteps(fListElemHeight/2, bounds.Width()/Height()); proportion = bounds dimension / (extent | bounds) dimension. Range and proportion are only written when they actually changed. The window is locked for the Bounds/Extent read and unlocked before touching the scrollbars.  
`PoseView.cpp` → `BPoseView::UpdateScrollRange`  
**MISSING:** The thumb proportion (rows/total) and position track the content and the range is clamped to it, but the scrollbar is draw-only with no click or drag handling, and scrolling is whole-row (wheel = 3 rows), so "half a row per click, a page per page-jump" does not exist.  
*src/tiku_trk_app.c:draw (500-507) and ButtonPress wheel handling (1213-1226); src/tiku_trk_poseview.c:tiku_trk_poseview_set_visible_rows (2244-2251)*

#### PVL-052 · important · open
**Scroll range refreshes on its own when content changes, but never while a mouse button is held.**  
Pulse compares Extent() and LeftTop() against fLastExtent/fLastLeftTop and calls UpdateScrollRange only when GetMouse reports buttons == 0. Pulse also drives UpdateCount(), auto-scroll and the pending text-widget double-click expiry.  
`PoseView.cpp` → `BPoseView::Pulse`  

#### PVL-053 · polish · open
**A corrupt or absurd saved scroll origin is silently reset to the top-left instead of scrolling into nowhere.**  
PinPointToValidRange zeroes any coordinate that is NaN (detected by the deliberate !(x>=0) && !(x<=0) test) or outside ±40000. Applied on InitCommon, SetViewMode, SwitchDir and to every restored pose location.  
`PoseView.cpp` → `BPoseView::PinPointToValidRange`  

#### PVL-054 · core · partial
**Selecting or arrow-keying to an item scrolls it into view, minimally, and in list mode only vertically.**  
ScrollIntoView(BRect): no-op on the Desktop; horizontal adjustment only when ViewMode() != kListMode; scrolls just far enough so the rect's leading edge is visible, and jumps to the leading edge when the pose is larger than the viewport. Uses SetScrollBarsTo so the scrollbars stay in sync.  
`PoseView.cpp` → `BPoseView::ScrollIntoView / BPoseView::AddPoseToSelection`  
**MISSING:** Every navigating key scrolls its target into view; a selection changed by type-ahead or by the Select window does not.

#### PVL-055 · important · done
**Home/End/PageUp/PageDown move the selection in list mode but scroll the view in the icon modes.**  
KeyDown: list mode maps Home/End to MoveOrChangePoseSelection(0 / count-1) and PageUp/PageDown to selecting the first/last visible pose, scrolling one page first if that pose is already selected. Icon modes call ScrollView(type), which sets the vertical scrollbar to 0, max, or ±max.  
`PoseView.cpp` → `BPoseView::KeyDown / BPoseView::ScrollView`  
**PORT:** Both named defects are addressed in the model layer — the icon-mode branch exists (selection untouched, scroll moved) and the page is now derived, `page = visible_rows > 1 ? visible_rows - 1 : 1`. But the derivation only works where something tells the view its height, and the single-window app never calls set_visible_rows(): it computes visible_rows(a) locally for drawing (app.c:107-113) and set_visible_rows has exactly one caller, desktop.c:217. So in app.c v->visible_rows stays 0 and page collapses to 1. Probed: with visible_rows unset, PageDown moves the selection one row; with it set to 3 it correctly moves two. This matters because app.c is the ONLY shell that binds the page keys — desktop.c maps just Up/Down/Home/End (:2236-2242). The icon-mode scroll half is also unrendered, as for PVK-010: draw_icons ignores v->scroll.

#### PVL-056 · important · open
**Dragging toward an edge auto-scrolls the view, faster the further past the edge the pointer is.**  
CheckAutoScroll defines a kBorderHeight (20) band above the top (extended by the title view height in list mode), a B_H_SCROLL_BAR_HEIGHT band below, a 6px band left and a B_V_SCROLL_BAR_WIDTH band right, and only scrolls in a direction where bounds still lie inside the extent. A drag scrolls at fAutoScrollInc (20); a rubber-band selection scrolls at fAutoScrollInc/4 within kSlowScrollBucket (30) of the edge and fAutoScrollInc/1.5 beyond it, and re-evaluates the selection rect afterwards so the rubber band follows the scroll without mouse movement.  
`PoseView.cpp` → `BPoseView::CheckAutoScroll`  
**VERIFIER CORRECTION:** Speed is variable only for rubber-band selection (fAutoScrollInc/4 within kSlowScrollBucket=30 px, fAutoScrollInc/1.5 beyond). During a *drag* the increment is the constant fAutoScrollInc (20) and the trigger is containment in a fixed border band outside the view — no distance scaling (BPoseView::CheckAutoScroll, PoseView.cpp:10023). The details field of both rows states this correctly; only the headline is wrong.  

#### PVL-057 · polish · open
**Auto-scroll does not start instantly: the pointer must leave the hot border and then sit in it for over half a second.**  
HandleAutoScroll state machine kWaitForTransition → kDelayAutoScroll → kAutoScrollOn, with a snooze(600000) re-check in the delay state; any button release returns to kAutoScrollOff and restores the 500 ms pulse rate. The pulse rate is raised to 20 ms when a rubber-band selection begins.  
`PoseView.cpp` → `BPoseView::HandleAutoScroll / BPoseView::_BeginSelectionRect`  

#### PVL-058 · important · open
**Enlarging a window whose content is scrolled past the extent scrolls the content back so it stays anchored to the window, instead of leaving blank space.**  
BContainerWindow::FrameResized (skipped for the Desktop) computes the negative offsets between extent and bounds and scrolls by at most the growth amount, then calls UpdateScrollRange and ResetPosePlacementHint so newly created icons land inside the new size.  
`ContainerWindow.cpp` → `BContainerWindow::FrameResized`  

#### PVL-059 · polish · open
**"Resize to fit" resizes the window to exactly hold the content and scrolls to the content's top-left.**  
ResizeToFit insets the screen frame by 5, adds the tab height, sizes the frame to Extent() plus the window chrome deltas, intersects with the screen frame, then (scrollbars disabled) scrolls by extent.leftTop - bounds.leftTop and re-enables the bars.  
`ContainerWindow.cpp` → `BContainerWindow::ResizeToFit`  

#### PVL-060 · important · open
**Bulk re-layouts (mode switch, clean-up all, folder switch, clearing) detach the scrollbars so intermediate positions never feed back into the view.**  
DisableScrollBars sets both scrollbar targets to NULL, EnableScrollBars restores them. The sequence used everywhere is Disable → ScrollTo(newOrigin) → UpdateScrollRange → SetScrollBarsTo(newOrigin) → Enable.  
`PoseView.cpp` → `BPoseView::DisableScrollBars / BPoseView::EnableScrollBars / BPoseView::ResetOrigin`  

#### PVL-061 · polish · done
**A folder that finishes loading while scrolled past its last row scrolls back so you are not left staring at an empty window.**  
AddPosesCompleted (list mode): if bounds.top > (count-1) * fListElemHeight, scroll to max(lastItemTop, 0). DeletePose applies the same idea after a removal, scrolling up one row when no pose occupies the bottom row and bounds.top > 0. UpdateAfterFilterChange does it for filtering.  
`PoseView.cpp` → `BPoseView::AddPosesCompleted / BPoseView::DeletePose / BPoseView::UpdateAfterFilterChange`  

#### PVL-062 · core · partial
**Drawing walks only the poses that can intersect the update rect and stops early.**  
DrawViewCommon list mode: startIndex = (updateRect.top - fListElemHeight) / fListElemHeight clamped to 0, walking down and breaking once location.y >= updateRect.bottom. Icon modes iterate CurrentPoseList() (= fVSPoseList, sorted by y) and break once a pose's location.y passes updateRect.bottom — which is why the vertical-sort list must stay ordered.  
`PoseView.cpp` → `BPoseView::DrawViewCommon`  
**MISSING:** List mode walks only from the scroll offset for the visible row count and stops, but icon mode iterates every pose with no early break, and there is no update-rect at all (the window is fully redrawn whenever dirty).  
*src/tiku_trk_app.c:draw (409-410) vs draw_icons (321-330)*

#### PVL-063 · important · partial
**In icon modes, hit testing scans the list backwards so the icon painted last (on top) is the one you click.**  
FindPose(BPoint) iterates from poseCount-1 downwards in icon modes and uses BPose::PointInPose, which does a real per-pixel icon hit test through IconCache::IconHitTest and otherwise tests the label rect. In list mode it maps y directly to a row index and tests only that row.  
`PoseView.cpp` → `BPoseView::FindPose(BPoint, int32*) / BPose::PointInPose`  
**MISSING:** Hit testing does scan backwards so the last-drawn icon wins, and list mode maps y straight to a row index, but the icon-mode test is a plain rectangle containment against the pose rect — there is no per-pixel icon hit test, so transparent icon corners still select.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_pose_at (1899-1921); src/tiku_trk_app.c:row_at (759)*

#### PVL-064 · important · done
**An icon-mode label is centred under the icon and truncated at about 30 characters; a mini-icon label sits to the right of the icon.**  
BTextWidget::CalcRectCommon icon mode: width = min(StringWidth("M") * 30, textWidth), left = poseLoc.x + round((iconSize - width)/2), bottom = poseLoc.y + ceil(IconPoseHeight()). Mini-icon mode: left = poseLoc.x + iconSize + kMiniIconSeparator (3), vertically centred on the icon. List mode honours the column alignment (left/center/right) and clips to the column width.  
`TextWidget.cpp` → `BTextWidget::CalcRectCommon`  
**PORT:** Both previously-failing clauses are now built. The icon-mode label is truncated to a CHARACTER budget (30 * width("n") = 240 px with the plain font, measured; Haiku's 30 * width("M") would be 300 px) instead of the old 1.5x-icon pixel budget, and it is centred under the art (lx = x + (size - tw)/2, box.x = x + (size-art)/2). In mini-icon mode the label is now drawn to the RIGHT of the icon (lx = box.x + art + 4, separator 4 vs Haiku's kMiniIconSeparator 3) and vertically centred on it (ly = box.y + (box.h - f->height)/2), which is what the hit rect at poseview.c:1959-1967 already assumed. List mode still fits to cols[j].width with TRUNC_END/TRUNC_MIDDLE and clips to the intersected cell rect (app.c:485-501).

#### PVL-065 · important · done
**An icon's clickable/selection rect widens to fit a long label rather than staying icon-sized.**  
BPose::CalcRect(poseView): in icon mode, when the label is wider than the icon the rect is re-centred and widened to the text width; in mini-icon mode the rect is extended right by kMiniIconSeparator + text width. Height is always the icon pose height.  
`Pose.cpp` → `BPose::CalcRect(const BPoseView*)`  

#### PVL-066 · core · partial
**Each pose draws its icon plus its label; in list mode a full draw paints every column, a partial draw only the first.**  
BPose::Draw takes columnsToDraw = fullDraw ? CountColumns() : 1, clips each column against the update rect, and draws the icon only if the update rect touches the icon rect. Columns after the first always draw unselected even when the pose is selected.  
`Pose.cpp` → `BPose::Draw(BRect, const BRect&, BPoseView*, BView*, bool, BPoint, bool)`  
**VERIFIER CORRECTION:** The update-rect test on the icon exists only in the list-mode branch (`if (updateRect.Intersects(iconRect))`). In icon/mini-icon mode BPose::Draw calls DrawIcon unconditionally (Pose.cpp:571-610). The rest of the row (columnsToDraw = fullDraw ? CountColumns() : 1, columns after the first drawn unselected) is correct.  
**MISSING:** Each pose draws its icon plus its label and all columns, but there is no full/partial draw distinction (every column is always painted) and columns after the first are drawn selected — the whole row is filled with the selection colour rather than only the first column.  
*src/tiku_trk_app.c:draw row loop (424-496)*

#### PVL-067 · important · partial
**A selected label is drawn as inverted (reverse-video) text; in an inactive window with "show selection when inactive" it is drawn as a translucent grey overlay instead.**  
BPose::DrawTextWidget inverts the label rect (ceil/floor-snapped) when the window is active or a selection rect is being drawn, and otherwise alpha-blends 128-grey over it. Cut items get an extra grey blend (or a lighter/darker one when inactive). BPoseView::ShowSelection(false)/(true) is what flips this on window deactivation.  
`Pose.cpp` → `BPose::DrawTextWidget / BPoseView::ShowSelection`  
**MISSING:** A selected label is drawn reverse-video, but draws_selected is a single boolean: an inactive window with show-selection-when-inactive gets the identical solid highlight, not a translucent grey overlay, and there is no cut-item blend.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_draws_selected (852-861); src/tiku_trk_app.c:424/482*

#### PVL-068 · important · open
**Items cut to the clipboard draw at half opacity until the cut is committed or cancelled.**  
fClipboardMode == kMoveSelectionTo switches the draw view to B_OP_ALPHA with constant alpha 128, using black or white depending on whether the view's high colour is dark. SetPosesClipboardMode invalidates each pose whose mode changed (row rect in list mode, icon rect otherwise).  
`Pose.cpp` → `BPose::Draw / BPoseView::SetPosesClipboardMode`  

#### PVL-069 · important · partial
**The rubber-band selection rectangle draws either as a translucent tinted rectangle over the icons or as an inverted XOR outline, following the "transparent selection" setting live.**  
Transparent: DrawAfterChildren strokes B_NAVIGATION_BASE_COLOR at alpha 128 and fills the interior with B_CONTROL_HIGHLIGHT_COLOR at alpha 90 (degenerate rects draw a single line). Non-transparent: _BeginSelectionRect/_UpdateSelectionRect/_EndSelectionRect stroke and re-stroke with B_OP_INVERT and B_MIXED_COLORS. The setting arrives via kTransparentSelectionChanged.  
`PoseView.cpp` → `BPoseView::DrawAfterChildren / BPoseView::_UpdateSelectionRect / BPoseView::_EndSelectionRect`  
**MISSING:** The rubber band draws as a single solid outline frame; the translucent tinted style, the B_OP_INVERT/mixed-colours outline and the live "transparent selection" setting that chooses between them are all absent.  
*src/tiku_trk_app.c:draw_icons band block (331-345)*

#### PVL-070 · polish · open
**Growing or shrinking the rubber band only repaints the sliver of difference, not the whole band.**  
_UpdateSelectionRect builds a region of (new minus old) unioned with (old minus new), then excludes the horizontal and vertical strips through the anchor point inside the intersection, and invalidates only that region followed by an immediate UpdateIfNeeded.  
`PoseView.cpp` → `BPoseView::_UpdateSelectionRect`  

#### PVL-071 · polish · open
**Over a background image, pose labels grow a contrast halo so they stay readable.**  
When fWidgetTextOutline is set (turned on by BackgroundImage), BTextWidget::Draw renders the text two or three extra times using B_FONT_FALSE_BOLD_WIDTH at 2.0/1.0 with low-alpha shine (dark text) or black at alpha 30/200 with a 1px offset (light text). Skipped for selected text, for cut items and when drawing into an offscreen (drag or column resize).  
`TextWidget.cpp` → `BTextWidget::Draw / BPoseView::SetWidgetTextOutline`  
**VERIFIER CORRECTION:** The halo is NOT skipped for cut items. The guard is `if (!selected && direct && view->WidgetTextOutline())` — only selection and non-direct (offscreen) drawing suppress it. clipboardMode == kMoveSelectionTo only suppresses the B_OP_ALPHA/B_ALPHA_OVERLAY mode switch around the halo passes (TextWidget.cpp:690-745). Alpha values are right: dark text 30 then 65 at false-bold 2.0/1.0; light text black 30 at 1.0 and 200 at +(1,1).  

#### PVL-072 · polish · open
**Volume icons carry a free-space bar drawn over the icon, which turns a warning colour below 5% free.**  
BPose::DrawBar draws a shadowed frame of width 7/32 of the icon (minimum 4px) at the icon's right edge, fills the free portion with FreeSpaceColor and the used portion with UsedSpaceColor, or WarningSpaceColor when fPercent < -1 (encoded as -2-percent for <5% free). Poses are re-polled periodically via gPeriodicUpdatePoses, and no bar is drawn on CDDA or read-only volumes.  
`Pose.cpp` → `BPose::DrawBar / CalcFreeSpace / BPose::UpdateVolumeSpaceBar`  
**TIKU:** A device node with a capacity descriptor could reuse this gauge; the periodic poll would be replaced by a watch notification.  

#### PVL-073 · polish · differs
**Opening an item plays a short expanding-rectangle animation over the item.**  
DrawOpenAnimation strokes seven concentric pairs of inverted mixed-colour rects outward from the pose rect's centre with a 10 ms snooze per step, drawn in list mode over CalcPoseRectList(clickedPose, index, firstColumnOnly=true) and in icon mode over the pose rect.  
`PoseView.cpp` → `BPoseView::DrawOpenAnimation / BPoseView::OpenSelectionCommon`  
**PORT:** As PVS-013: the list-mode animation is exact in count, step and timing; the icon-mode case and the measured first-column rect are not there.

#### PVL-074 · polish · open
**A volume pose showing a space bar is repainted by invalidation rather than drawn directly, so the bar never smears.**  
DrawPose: if TrackerSettings().ShowVolumeSpaceBar() and the target is a volume, Invalidate(rect); otherwise pose->Draw(rect, rect, this, fullDraw) directly. BPose::Draw itself also defers to Invalidate once when its background was not cleared and this is a partial draw (fBackgroundClean).  
`PoseView.cpp` → `BPoseView::DrawPose / BPose::Draw`  

#### PVL-075 · polish · open
**Changing an item's icon repaints only that icon, not the row or the label.**  
BPose::UpdateIcon tells the icon cache the icon changed and invalidates _ListIconRect (list mode) or _IconRect (icon modes) only. The list icon rect is at ListOffset() from the row's left, vertically centred in the row.  
`Pose.cpp` → `BPose::UpdateIcon / BPose::_ListIconRect / BPose::_IconRect`  
**TIKU:** Device-node icons are chosen from the typed descriptor/capability bits rather than a MIME type, and change on watch notifications.  

#### PVL-076 · polish · done
**A read-only volume's view background is tinted so the window looks visibly non-editable.**  
AdoptSystemColors uses B_DOCUMENT_BACKGROUND_COLOR with ReadOnlyTint() when TargetVolumeIsReadOnly(); the offscreen used by column redraw applies the same tint so resizing a column does not reveal an untinted strip.  
`PoseView.cpp` → `BPoseView::AdoptSystemColors / BPoseView::ColumnRedraw`  
**TIKU:** Device nodes carry capability bits (e.g. not writable); the same tint should be driven from those rather than from volume read-only state.  
**PORT:** Tinted from the FOLDER's write capability rather than the volume's, because a device namespace has writable and read-only regions side by side and a per-volume answer would be wrong for both.

#### PVL-077 · important · partial ⟨found by verification⟩
**With "Sort folder names first" on, folders sort ahead of files in list mode, and toggling the setting re-sorts every open window live.**  
B_OBSERVER_NOTICE_CHANGE / kSortFolderNamesFirstChanged: writes TrackerSettings, then NameAttributeText::SetSortFolderNamesFirst() and RealNameAttributeText::SetSortFolderNamesFirst(), then SortPoses() + Invalidate() — but only when ViewMode() == kListMode. The rule lives in the name attribute comparator, not in PoseCompareAddWidget.  
`PoseView.cpp` → `BPoseView::MessageReceived (B_OBSERVER_NOTICE_CHANGE, case kSortFolderNamesFirstChanged); NameAttributeText::SetSortFolderNamesFirst (WidgetAttributeText.cpp)`  
**TIKU:** A device namespace has directory-like and leaf nodes; the same "containers first" collation switch has to be a property of the comparator, not of the sort key.  
**MISSING:** Containers do sort ahead of leaves and the rule lives in the comparator where the row says it belongs, but it is unconditional — no "sort folder names first" setting exists, so there is nothing to toggle and no live re-sort of open windows.  
*src/tiku_trk_poseview.c:pose_cmp (355-363)*

#### PVL-078 · important · open ⟨found by verification⟩
**"Hide dot files" hides entries whose name starts with '.'; toggling it reloads the folder from scratch.**  
AddPosesTask reads TrackerSettings().HideDotFiles() once per load and skips `.`/`..` (or every leading-dot name); the kHideDotFilesChanged observer notice writes the setting and calls Refresh(), which does StopWatching + ClearPoses + StartWatching + AddPoses.  
`PoseView.cpp` → `BPoseView::AddPosesTask (hideDotFiles branch); BPoseView::MessageReceived (case kHideDotFilesChanged); BPoseView::Refresh`  
**TIKU:** The equivalent is a per-view visibility predicate over node names/flags; note the implementation is a full re-listing, not an incremental filter.  

#### PVL-079 · polish · open ⟨found by verification⟩
**Changing the system date/time format repaints every date column in every window.**  
AddColumnList / AddColumn / SetupDefaultColumnsIfNeeded call StartWatchDateFormatChange() the first time a B_TIME_TYPE column exists (BHandler::StartWatching on the Tracker messenger, kDateFormatChanged); RemoveColumn calls StopWatchDateFormatChange() when the last one goes. The notice runs UpdateDateColumns(), which re-renders those widgets.  
`PoseView.cpp` → `BPoseView::StartWatchDateFormatChange / StopWatchDateFormatChange / UpdateDateColumns; MessageReceived (case kDateFormatChanged)`  
**TIKU:** Any locale/unit-formatted descriptor column needs the same "formatting changed, re-render without re-reading" path.  

#### PVL-080 · important · differs ⟨found by verification⟩
**Picking an attribute in the Attributes menu toggles its column: absent adds it, present removes it (with the last-column alert).**  
HandleAttrMenuItemSelected: `BColumn* column = ColumnFor(attrHash); if (column != NULL) { RemoveColumn(column, true); return; }` otherwise it builds a BColumn from the message's attr_name/attr_type/attr_width/attr_align/attr_editable/attr_statfield/attr_display_as fields and AddColumn()s it (and deletes a detached MIME submenu).  
`PoseView.cpp` → `BPoseView::HandleAttrMenuItemSelected; BPoseView::MessageReceived (case kAttributeItem)`  
**TIKU:** The add path is fully data-driven from the menu message — the column set is not a fixed table, which is exactly what a descriptor-field-driven column set would need.  
**PORT:** Attributes-menu items are marked from the view's live column set and picking one calls toggle_column, which removes a present column or appends an absent one; the last-column guarantee is met by a real alternative mechanism (Name is always present, marked and disabled) rather than by an alert.  
*src/tiku_trk_poseview.c:3103-3148 (toggle_column: remove-if-present / append-if-absent, `field == TIKU_TRK_COL_NAME -> return 0`); src/tiku_trk_cwmenu.c:173-199 (marks from c->columns, Name marked+disabled); src/tiku_trk_desktop.c:1019-1030 (CMD_COL_* -> toggle_column); src/tiku_trk_desktop.c:166-176 (c.columns filled from poseview_has_column)*

#### PVL-081 · important · done ⟨found by verification⟩
**Adding or removing a column while type-ahead filtering is active re-evaluates the filter, because the filter matches against column text.**  
AddColumn ends with `if (IsFiltering()) RebuildFilteringPoseList();` (a new column can make hidden rows match again); RemoveColumn walks fFilteredPoseList backwards and RemoveFilteredPose()s anything that no longer passes FilterPose(). Both also set fStateNeedsSaving.  
`PoseView.cpp` → `BPoseView::AddColumn (IsFiltering tail) / BPoseView::RemoveColumn (IsFiltering tail)`  
**TIKU:** If descriptor fields become columns and filtering matches column text, showing/hiding a field silently changes which nodes are visible — decide whether that is wanted.  

#### PVL-082 · important · done ⟨found by verification⟩
**A selected pose draws a different icon bitmap (the "selected" icon variant), and in an inactive window without "show selection when inactive" the icon reverts to the normal variant.**  
BPose::Draw computes `drawIconUnselected = !windowActive && !showSelectionWhenInactive` and passes it to DrawIcon, which asks the icon cache for kSelectedIcon vs kNormalIcon. This is independent of the label's reverse-video treatment in DrawTextWidget.  
`Pose.cpp` → `BPose::Draw / BPose::DrawIcon`  
**TIKU:** Two visual states per node icon (normal/selected) must exist in whatever icon source device nodes use, or selection will read only on the label.  
**PORT:** One icon washed toward the selection colour rather than a second cached bitmap per icon; the two visual states are what the row is about.

#### PVL-083 · important · partial ⟨found by verification⟩
**Clearing a view (folder switch, refresh) saves icon positions first, then wipes every list, selection, pivot, zombie and broken-link record and resets scroll, extent and placement hint.**  
ClearPoses: CommitActivePose, SavePoseLocations, ClearTypeAheadFiltering, empties fPoseList/fFilteredPoseList/fVSPoseList/fZombieList/fSelectionList/fMimeTypesInSelectionCache/fBrokenLinks, nulls both pivots, then Disable→ScrollTo(B_ORIGIN)→UpdateScrollRange→SetScrollBarsTo→Enable, ResetPosePlacementHint, ClearExtent, and fires the selection-changed hook.  
`PoseView.cpp` → `BPoseView::ClearPoses / BPoseView::SwitchDir / BPoseView::Refresh`  
**TIKU:** Teardown order matters: positions are persisted before the model list is dropped, so a TikuOS view-owned position map must be flushed at the same point.  
**MISSING:** The wipe is there (poses, selection, both pivots, park, zombies, filter, scroll) and the app saves positions before switching, but the placement hint is never reset on a folder switch — only cleanup resets it — so the next folder places icons continuing from the old folder's hint, and no selection-changed hook is fired.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_switch_dir (533-548) + tiku_trk_poseview_reload (389-432); src/tiku_trk_app.c:748-750*

#### PVL-084 · polish · done ⟨found by verification⟩
**Window/column state is only written back when something actually changed it.**  
fStateNeedsSaving is set by AddColumn, RemoveColumn, MoveColumnTo, ResizeColumn and by FrameResized when the window is active; SaveState(AttributeStreamNode*) clears it after writing kAttrColumns + the view-state attribute.  
`PoseView.cpp` → `BPoseView::SaveState / fStateNeedsSaving assignments`  
**TIKU:** Relevant if folder view state lands in NVM — a dirty flag keeps a wear-sensitive store from being rewritten on every close.  

#### PVL-085 · important · differs ⟨found by verification⟩
**The Trash is silently dropped from the selection before a Delete or Move-to-Trash, so you cannot trash the Trash.**  
ExcludeTrashFromSelection() runs at the top of both DoDelete() and DoMoveToTrash(); it scans the selection for a pose satisfying CanTrashForeignDrag (i.e. the Trash itself) and RemovePoseFromSelection()s it (which also clears the pivots), then breaks out after the first hit.  
`PoseView.cpp` → `BPoseView::ExcludeTrashFromSelection / BPoseView::DoDelete / BPoseView::DoMoveToTrash`  
**TIKU:** The general shape — a destructive verb silently pruning nodes it must never act on — maps to capability-bit refusals on device nodes; note Tracker prunes rather than refusing the whole command.  
**PORT:** The port really does make the Trash untrashable, by an actual alternative implementation one layer down — every destructive/queued op drops paths containing "/.Trash" as the queue is built — though the pose selection itself is never pruned so the Trash stays selected and the status line still counts it.  
*src/tiku_trk_fs.c:332-338 (start(): `if (strstr(paths[i], "/.Trash") != NULL) continue;`), reached from tiku_trk_fs_trash at 372-376; src/tiku_trk_app.c:979-991 (Delete passes the whole selection, unpruned)*

#### PVL-086 · polish · open ⟨found by verification⟩
**An archived object (a replicant) dropped on the view is routed through the same drop handling as a file drop.**  
TPoseViewFilter::Filter intercepts B_ARCHIVED_OBJECT and calls fPoseView->HandleMessageDropped(message), skipping the message if handled; this is how replicants get dropped onto the Desktop.  
`PoseView.cpp` → `TPoseViewFilter::Filter`  

#### PVL-087 · important · partial ⟨found by verification⟩
**Every selection change notifies the window, which re-enables/disables menu items and updates the "N items selected" count.**  
fSelectionChangedHook gates calls to ContainerWindow()->SelectionChanged() from AddPoseToSelection, RemovePoseFromSelection, SelectAll, InvertSelection, ClearSelection, DeletePose, EnsurePoseUnselected, MouseDown and ShowContextMenu; the hook is deliberately switched off during bulk operations and re-fired once at the end.  
`PoseView.cpp` → `fSelectionChangedHook call sites; BContainerWindow::SelectionChanged`  
**TIKU:** The batch-then-notify-once discipline is what keeps a namespace view from re-evaluating command enablement per node during a select-all.  
**MISSING:** Menu enablement does track the selection, but it is pulled — menu_ctx re-reads the live selection count each time a menu is built — rather than pushed by a selection-changed hook, and the count view always shows the folder's total item count, never "N items selected".  
*src/tiku_trk_desktop.c:menu_ctx (161-196) and folder_draw count block (343); src/tiku_trk_shortcuts.c:tiku_trk_sc_enabled (199)*

#### PVL-088 · core · open ⟨found by verification⟩
**Folder contents load on a background thread and appear in batches, with a barber pole shown until the load completes.**  
AddPoses spawns AddPosesTask after ShowBarberPole(); the task accumulates up to kMaxAddPosesChunk (50) models, or flushes early once 300000 µs have passed since the last flush, sending each batch as kAddNewPoses (handled by CreatePoses) and finally kAddPosesCompleted; it snoozes 500 µs between batches and unlocks the window while reading each dirent. HideBarberPole runs in AddPosesCompleted or on any early failure.  
`PoseView.cpp` → `BPoseView::AddPoses / BPoseView::AddPosesTask / BPoseView::MessageReceived (case kAddNewPoses, kAddPosesCompleted) / ShowBarberPole`  
**TIKU:** Enumerating a device namespace is fast enough to be synchronous, but the batching contract (view never blocks, rows appear incrementally, a progress indicator covers the gap) is what makes a slow/remote mount tolerable — decide now whether listing is allowed to block the shell process.  

#### PVL-089 · important · open ⟨found by verification⟩
**Switching folders or refreshing invalidates the in-flight loading thread so a stale load cannot inject rows into the new listing.**  
fAddPosesThreads is a set of blessed thread ids; SwitchDir and DetachedFromWindow clear it, and AddPosesTask checks IsValidAddPosesThread(threadID) after every re-lock, hiding the barber pole and unwinding via failToLock when it is no longer blessed. AddPosesThreadValid additionally rejects a kAddNewPoses batch whose "ref" no longer matches the target model.  
`PoseView.cpp` → `BPoseView::IsValidAddPosesThread / BPoseView::AddPosesThreadValid / BPoseView::SwitchDir`  
**TIKU:** Any asynchronous listing needs a generation token; a watch notification arriving for the previous directory must be discarded the same way.  

#### PVL-090 · important · open ⟨found by verification⟩
**A node that the loader and the change notification both report is only ever added once.**  
CreatePoses keeps fInsertedNodes (a node_ref set): a model whose node_ref is already present (or is in the zombie list) is stopped-watching, deleted and skipped; DeletePose removes the node from the set, and SwitchDir/Refresh clear it. This is a second guard beyond EntryCreated's FindPose/FindZombie check, and it is what closes the load-thread vs. B_ENTRY_CREATED race.  
`PoseView.cpp` → `BPoseView::CreatePoses (fInsertedNodes) / BPoseView::DeletePose`  
**TIKU:** With a re-listing-on-notify design this dedup becomes mandatory, not optional.  

#### PVL-091 · polish · open ⟨found by verification⟩
**An attribute/stat notification for a node that is momentarily busy is retried rather than dropped, so a change landing during mimeset is still picked up.**  
AttributeChanged loops up to 100 times calling poseModel->OpenNode(), sleeping 10 ms while the result is B_BUSY, and gives up (skipping that pose) on any other error.  
`PoseView.cpp` → `BPoseView::AttributeChanged (B_BUSY retry loop)`  
**TIKU:** TikuOS VFS reads do not return B_BUSY, but a driver-backed node read that can fail transiently needs an equivalent policy — retry vs. leave the row stale.  

#### PVL-092 · important · done ⟨found by verification⟩
**A value change or rename can move a row into or out of the type-ahead filter live, not just re-sort it.**  
Both AttributeChanged and EntryMoved, after updating the widgets, do: if not visible and FilterPose(pose) now passes, AddPoseToList into fFilteredPoseList; if visible and it no longer passes, RemoveFilteredPose (which also deselects it); otherwise _CheckPoseSortOrder on the filtered list as well as the master list.  
`PoseView.cpp` → `BPoseView::AttributeChanged (IsFiltering block) / BPoseView::EntryMoved (IsFiltering block)`  
**TIKU:** A watch notification on a filtered listing must re-run the predicate for the changed node and reconcile selection — the row can vanish under the cursor.  

#### PVL-093 · important · done ⟨found by verification⟩
**Renaming the folder a window is showing updates that window's title (and the model's entry ref) rather than removing a row.**  
EntryMoved: after resolving the Trash special case it compares thisDirNode with itemNode and, on a match, calls targetModel->UpdateEntryRef(&dirNode, name) then BContainerWindow::UpdateTitle(). The same function handles the three other cases — rename in place (update widgets + re-sort), moved out (DeletePose), moved in (EntryCreated).  
`PoseView.cpp` → `BPoseView::EntryMoved`  
**TIKU:** One notification opcode covering rename/move-in/move-out is exactly the information a bare "node touched" event cannot carry.  

#### PVL-094 · important · open ⟨found by verification⟩
**MIME database changes are coalesced: a burst of icon/preferred-app notices produces one pass over the poses, not one per notice.**  
NoticeMetaMimeChanged decodes be:which (B_ICON_CHANGED / B_ICON_FOR_TYPE_CHANGED / B_APP_HINT_CHANGED / B_PREFERRED_APP_CHANGED) and posts a MetaMimeChangedAccumulator through the window's DelayedTaskLoop with AccumulatedRunLater(200000, 5000000) — 200 ms delay, 5 s cap, with CanAccumulate merging notices that share type+preferredApp. MetaMimeChanged then invalidates the icon cache for the type and walks every pose of that type.  
`PoseView.cpp` → `BPoseView::NoticeMetaMimeChanged / MetaMimeChangedAccumulator / BPoseView::MetaMimeChanged`  
**TIKU:** This is the coalescing tier the VFS watch bus lacks: TIKU_EVENT_VFS explicitly does not coalesce, so a notification storm on a live device value needs exactly this accumulate-with-cap wrapper before it reaches the view.  

#### PVL-095 · polish · open ⟨found by verification⟩
**A MIME type change on any pose rebuilds the window's list of types present, which drives the Add-ons and "Open with" menus.**  
AttributeChanged calls RefreshMimeTypeList() when the changed attribute is kAttrMIMEType; fMimeTypeListIsDirty is also set by every pose add/remove, and MimeTypeAt/CountMimeTypes rebuild lazily. AddPosesCompleted feeds the result into BContainerWindow::AddMimeTypesToMenu().  
`PoseView.cpp` → `BPoseView::RefreshMimeTypeList / BPoseView::AddMimeType / BPoseView::MimeTypeAt`  
**TIKU:** The analogue is a per-view set of node classes present, used to decide which shell verbs to offer.  

#### PVL-096 · important · open ⟨found by verification⟩
**"Copy layout" and "Paste layout" actually move the whole column set between windows via the system clipboard, and pasting replaces every column in the target window at once.**  
kCopyAttributes flattens PoseView()->SaveState(BMessage&) and puts it on be_clipboard as data of type "application/tracker-columns"; kPasteAttributes finds that data, removes all current columns (RemoveColumn refuses to remove the last one), then re-adds columns via BColumn::InstantiateFromMessage until it runs out, and restores the sort. Nothing in the inventory says what these two items do — they are listed only as labels in the Attributes-menu row.  
`PoseView.cpp` → `BPoseView::MessageReceived cases kCopyAttributes / kPasteAttributes`  
**TIKU:** A cross-window "view layout" clipboard is a second clipboard channel distinct from the file clipboard; on TikuOS both would have to share one clipboard service with typed payloads.  

#### PVL-097 · important · open ⟨found by verification⟩
**Creating a document from a "New >" template copies the template's contents and attributes, names it "New <template name>", and immediately opens the name for inline editing.**  
BPoseView::NewFileFromTemplate: name = "New " + message "name", uniquified with FSMakeOriginalName(..., " copy"); a template that is a directory goes through FSCreateNewFolderIn+Rename instead of a data copy; the data is copied in 1 KB chunks; FSCopyAttributesAndStats(src, dest, copyTimes=false) copies attributes, permissions, owner and group but NOT the timestamps; PlaceFolder positions it; then EntryCreated + SelectPose + EditFirstWidget start the rename. Explicitly not undoable ("todo: create an UndoItem").  
`PoseView.cpp` → `BPoseView::NewFileFromTemplate`  

#### PVL-098 · polish · differs ⟨found by verification⟩
**A newly created folder or template file lands where the user asked for it: at the click point when made from a context menu, or under the mouse pointer when made with the keyboard in icon view.**  
BPoseView::PlaceFolder writes a pose location with FSSetPoseLocation when the invoking message carries "be:invoke_origin" (converted from screen coords), or, in non-list view, when the current mouse position is inside the window frame. In list view a keyboard-created folder gets no stored position at all.  
`PoseView.cpp` → `BPoseView::PlaceFolder`  
**PORT:** Same seeding path as PVL-025: proven by the notice handler, without a New folder command to drive it.

#### PVL-099 · important · open ⟨found by verification⟩
**After "New folder" the new folder is selected and its name is immediately editable inline, so typing renames it.**  
BPoseView::NewFolder: FSCreateNewFolderIn -> PlaceFolder -> EntryCreated -> (re-add to the filtered list when filtering) -> UpdateScrollRange, CommitActivePose, SelectPose, pose->EditFirstWidget(). The inventory covers only the naming algorithm in FSCreateNewFolderIn.  
`PoseView.cpp` → `BPoseView::NewFolder`  

#### PVL-100 · important · open ⟨found by verification⟩
**Dropping data from another application that has no files yet negotiates a file into existence: Tracker creates an empty clipping file and replies B_COPY_TARGET/B_MOVE_TARGET so the originator fills it in; dropping such a drag on the Trash replies B_TRASH_TARGET instead.**  
BPoseView::HandleDropCommon, the srcWindow == NULL branch: FindDragNDropAction decodes be:actions; canErase + CanTrashForeignDrag -> reply B_TRASH_TARGET; otherwise CanCopyOrMoveForeignDrag + CreateClippingFile("Untitled clipping") and a reply carrying be:types, "directory", "name", the originator data and the offered be:filetypes. With the secondary button held, a menu of "Create %s clipping" types (or of be:actionspecifier strings) is run first and cancelling it aborts the drop.  
`PoseView.cpp` → `BPoseView::HandleDropCommon`  
**TIKU:** This is the inbound half of a drag protocol: on TikuOS it is "another process hands us a stream and we materialise a node for it" — the promise/negotiation shape matters more than the drag gesture.  

#### PVL-101 · important · open ⟨found by verification⟩
**Dropping selected text or a bitmap onto a folder writes it into a new "Untitled clipping" file; if the write fails the half-made file is removed again.**  
HandleDropCommon's kPlainTextMimeType branch (and the equivalent bitmap branch below it) requires a directory target, creates the clipping via CreateClippingFile, writes the payload with Seek/Write/SetSize and on any failure Unset()s the file and Remove()s the entry.  
`PoseView.cpp` → `BPoseView::HandleDropCommon`  

#### PVL-102 · important · partial ⟨found by verification⟩
**Queries, query templates and virtual directories count as read-only destinations even on a writable disk, which is what greys out New folder, Cut and Paste inside a query window.**  
BPoseView::TargetVolumeIsReadOnly returns true for IsQuery() || IsQueryTemplate() || IsVirtualDirectory() before it ever consults BVolume::IsReadOnly(). TShortcuts::TargetIsReadOnly() is a thin wrapper over it and feeds UpdateNewFolderItem, UpdateNewTemplatesItem and UpdateCutItem.  
`PoseView.cpp` → `BPoseView::TargetVolumeIsReadOnly`  
**TIKU:** "Writable" is a per-node capability, not a per-volume one — this is the case that proves it: same disk, different answer depending on what the window represents.  
**MISSING:** A per-window read-only destination flag exists and greys New folder/Cut/Paste, but it is set only from path prefixes (/sys, /dev, /proc); c.is_query is declared in the context struct and never assigned or read, and is_virtual blocks only Cut — so a query window is not a read-only destination.  
*src/tiku_trk_desktop.c:menu_ctx (187-193); src/tiku_trk_shortcuts.c:tiku_trk_sc_enabled (247-264); is_query unused (tiku_trk_shortcuts.h:100)*

#### PVL-103 · important · open ⟨found by verification⟩
**The root/Disks window keeps a separate saved column layout and view state from ordinary folders.**  
RestoreColumnState picks kAttrDisksColumns ("_trk/d_columns") + kAttrDisksColumnsForeign when `TargetModel()->IsRoot()`, otherwise kAttrColumns; the same split exists for view state (kAttrDisksViewState "_trk/d_viewstate") and for the Desktop (kAttrDesktopViewState "_trk/desk_viewstate").  
`PoseView.cpp` → `BPoseView::RestoreColumnState`  
**TIKU:** Any TikuOS namespace root ("/") likely wants its own saved layout too — the per-folder store needs a well-known key for the root rather than reusing the ordinary-folder key.  


---

## PoseView: selection, mouse, drag-and-drop

Architecture notes: [`spec/ARCH-poseview-selection.md`](spec/ARCH-poseview-selection.md)  
**CHECKED:** There is no root-specific saved layout at all: save_columns/restore_columns and save_state key every folder, root included, on v->path under the same TIKU_TRK_ATTR_COLUMNS/VIEW_STATE names, and the reserved TIKU_TRK_KEY_DEV_ROOT slot that would give root a separate record is referenced only by a test — storing root the same way as any folder is the absence of the row's rule, not another route to it.  
*src/tiku_trk_poseview.c:3239 and 3271-3275 (state read/write keyed on v->path, ATTR_COLUMNS); src/tiku_trk_poseview.c:2199-2201 (view state likewise); src/tiku_trk_state.h:34-35 (KEY_DEV_ROOT/KEY_DESKTOP) used only at tests/test_model.c:113*

#### PVS-001 · core · partial
**Clicking empty space in the view clears the whole selection.**  
Only when NOT (extendSelection && fSelectionRectEnabled && fMultipleSelection); also nulls fLastClickedPose, cancels a pending rename, and activates+updates the window immediately.  
`PoseView.cpp` → `BPoseView::MouseDown`  
**MISSING:** Empty-space click clears the selection, nulls the last-clicked/active pose and cancels the armed rename, but the window is never activated/updated by the click (the desktop deliberately makes content clicks click-through) and the modifier exception is honoured only on the icon-mode band path, not for a shift-click on empty space in list mode.  
*src/tiku_trk_poseview.c:686 (tiku_trk_poseview_click, index<0) + src/tiku_trk_app.c:1299*

#### PVS-002 · core · done
**Clicking an unselected pose deselects everything else and selects just that pose, which becomes the selection pivot.**  
else-branch: if (!extendSelection) { if (!pose->IsSelected()) { ClearSelection(); AddRemovePoseFromSelection(pose,index,true); fSelectionPivotPose = pose; } }  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  

#### PVS-003 · core · done
**Clicking a pose that is already part of a multi-selection leaves the entire selection intact, so the group can be dragged; mouse-up does not collapse it either.**  
The non-extend branch only acts when !pose->IsSelected(); an early return also fires when pose == fSelectionPivotPose && !extendSelection. BPoseView::MouseUp never mutates the selection.  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  

#### PVS-004 · core · done
**Command-click toggles one pose in or out of the selection without disturbing the rest.**  
extendSelection = (modifiers() & B_COMMAND_KEY) != 0 && fMultipleSelection, computed in MouseDown; the extend branch calls AddRemovePoseFromSelection(pose, index, !pose->IsSelected()) and flushes fMimeTypesInSelectionCache.  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  

#### PVS-005 · important · partial
**Shift-click extends the selection from the pivot: in list mode the whole index range pivot..clicked is selected; in icon mode everything intersecting the rectangle between the click point and the pivot's location is selected.**  
Icon-mode rect is normalized (left/right and top/bottom swapped if inverted) and forced to at least 1x1 px before intersection testing.  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  
**MISSING:** The list-mode clause is implemented — shift-click clears, keeps the pivot and selects the whole displayed range pivot..clicked — but the icon-mode clause is missing: an icon-mode shift-click runs the same index-range code, never the normalised 1x1-minimum rectangle between the click point and the pivot's location.  
*src/tiku_trk_poseview.c:725 (tiku_trk_poseview_click shift branch) / select_range:661; icon path src/tiku_trk_app.c:1265*

#### PVS-006 · important · done
**Plain shift-click first clears the selection but preserves the pivot, so successive shift-clicks always re-anchor on the original pivot rather than growing monotonically.**  
savedPivotPose is stashed across ClearSelection() (which nulls the pivot) and restored immediately after.  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  

#### PVS-007 · polish · done
**Shift+Command-click DESELECTS the range between the pivot and the clicked pose instead of selecting it.**  
bool select = !pose->IsSelected() || !extendSelection; with Command held and the clicked pose already selected, select becomes false and the whole range is removed.  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  
**PORT:** The unconditional deselect is gone; the port now computes Haiku's exact expression. With Cmd held, extendSelection is true so select = !pose->IsSelected(), and the code is `int on = !p->selected;` followed by select_range(v, v->pivot, p->index, on) (falling back to the single pose when there is no pivot). I linked a probe against the real engine to confirm both directions: shift+cmd on an already-selected pose clears the pivot..clicked range (as test_poseview.c:142-144 checks), and shift+cmd on an UNSELECTED pose now SELECTS rows 0..3 rather than clearing them — the case the row said was wrong and which no test covers.

#### PVS-008 · important · partial
**The pivot invariant is maintained after every click: an empty selection has no pivot, and a non-empty selection always has one.**  
Tail of the function: if (fSelectionList->IsEmpty()) { pivot=NULL; realPivot=NULL; } else if (fSelectionPivotPose == NULL) { pivot = realPivot = pose; }  
`PoseView.cpp` → `BPoseView::AddRemoveSelectionRange`  
**MISSING:** The invariant holds on the plain, Cmd and Shift branches (empty -> pivot -1 at :610/:721, non-empty -> pivot set at :718/:730/:743), but the Shift+Cmd branch returns without re-checking, so deselecting the whole range leaves a stale pivot on an empty selection, and select_none never clears real_pivot.  
*src/tiku_trk_poseview.c:705-713 vs :605-613*

#### PVS-009 · important · open
**Multiple selection can be disabled per view (single-select file panels): Command-toggle, shift-range and additive rubber-banding all degrade to single selection.**  
fMultipleSelection gates extendSelection in MouseDown/MouseDragged and the shift branch in AddRemoveSelectionRange; set via BPoseView::SetMultipleSelection (called from FilePanelPriv.cpp).  
`PoseView.cpp` → `BPoseView::MouseDown`  

#### PVS-010 · core · partial
**Double-clicking a pose opens the selection.**  
Requires clicks==2, same pose as the previous click, |dx| and |dy| < kDoubleClickTresh (6 px), buttons == fLastClickButtons, buttons == B_PRIMARY_MOUSE_BUTTON, Control not held, and extendSelection not set. On a match the click state is reset (fLastClickPoint = INT32_MAX, fLastClickedPose = NULL) so a triple click does not re-open.  
`PoseView.cpp` → `BPoseView::WasDoubleClick / BPoseView::MouseDown`  
**UNCONFIRMED:** Every cited symbol in the four batches resolved to a real function/case in /home/ambuj/kintsugi/src/kits/tracker/{PoseView.cpp,Pose.cpp,TextWidget.cpp,TitleView.cpp,ContainerWindow.cpp,Shortcuts.cpp,PoseViewScripting.cpp,PendingNodeMonitorCache.cpp,Utilities.cpp,NavMenu.cpp,Tracker.cpp}, and LongAndDragTrackingFilter.cpp resolved to src/kits/shared/. No row was unverifiable for lack of the symbol; the defects found are misstated rules, listed under corrections.  
**MISSING:** A second click on the same row/pose within 400 us-window opens it and the click state is reset so a triple click does not re-open, but none of the guards are applied (modifiers, Control, button identity and the 6 px movement threshold are all ignored) and only containers/query results open — double-clicking a plain file does nothing, since the port has no launch path.  
*src/tiku_trk_app.c:1266 and :1346 (open_selection at :740); src/tiku_trk_desktop.c:1835*

#### PVS-011 · polish · differs
**Double-clicking inside the Path column of a list-mode row opens the containing folder instead of the item.**  
Widget's AttrHash must equal AttrHashString(kAttrPath, B_STRING_TYPE); the widget text is resolved to an entry_ref and sent as B_REFS_RECEIVED to the Tracker signature. If it resolves, OpenSelection is skipped.  
`PoseView.cpp` → `BPoseView::WasClickInPath`  
**TIKU:** Path column over a device namespace needs a node path -> descriptor resolver; decide whether opening a /dev path means "open the parent directory node".  
**PORT:** A query result is identified by its path in the name column rather than by a separate Path column, so a double-click on a non-container result opens the folder that holds it -- the row's intent, without the column hash test.

#### PVS-012 · important · partial
**Holding Option while opening closes the parent window; with single-window-browse enabled, opening a folder navigates the current window instead of spawning a new one.**  
Option adds "nodeRefsToClose" (the parent node_ref) per ref, suppressed on Desktop, file panels, and in single-window mode. Single-window path posts kSwitchDirectory and is itself suppressed when Option is down.  
`PoseView.cpp` → `BPoseView::OpenSelection / BPoseView::OpenSelectionCommon`  
**MISSING:** Option-open closing the source window is genuinely implemented, but single-window browsing exists nowhere as a mode that suppresses new windows (no setting, no Desktop/file-panel/Option suppression rules) — a second binary that always navigates in place is not the row's conditional behaviour, so one clause is built and one is absent.  
*src/tiku_trk_desktop.c:1844-1851 (open_folder + Mod1Mask -> close_window); src/tiku_trk_app.c:739-753 open_selection (unconditional switch_dir); grep for any single-window setting across src/ returns nothing*

#### PVS-013 · polish · differs
**Opening from a double-click plays a zoom animation of expanding inverted rectangles out of the clicked pose.**  
7 iterations, B_OP_INVERT mixed-colors rects growing by 2 px, snooze(10000) per step; only drawn when a pose was actually clicked (not for keyboard open).  
`PoseView.cpp` → `BPoseView::DrawOpenAnimation`  
**PORT:** Seven inverted rectangles, 10 ms apart, growing 2 px, only on a mouse open.  The rectangle is the row's first 200 px rather than the measured first column, and icon mode has no animation.

#### PVS-014 · important · done
**A second, slow click on the name of an already-selected pose starts inline rename after the double-click interval elapses with no further click.**  
BTextWidget::MouseUp records fLastClickedTime and registers itself via BPoseView::SetTextWidgetToCheck; BPoseView::Pulse calls CheckExpiration, which calls StartEdit once (system_time() - fLastClickedTime) > get_click_speed(). Arming is refused if the pose was selected less than one click-speed ago (fLastClickedTime - doubleClickSpeed < pose->SelectionTime()), i.e. the click that selected it does not count.  
`TextWidget.cpp` → `BTextWidget::MouseUp / BTextWidget::CheckExpiration`  

#### PVS-015 · important · done
**A pending rename is cancelled by clicking a different pose, right-clicking, clicking empty space, or beginning a drag.**  
MouseDown: fTextWidgetToCheck->CancelWait() when pose != fLastClickedPose || secondaryMouseButtonDown, and unconditionally on an empty-space click; MouseDragged cancels first thing. Rename is also skipped entirely when pose editing is disabled (fAllowPoseEditing, cleared by Open With and file panels) or when the mouse-up is a tracked right-button up.  
`PoseView.cpp` → `BPoseView::MouseDown / BPoseView::MouseDragged / BPoseView::MouseUp`  

#### PVS-016 · core · partial
**Right-click (or Control+primary click) on a pose selects it if it was unselected, but leaves an existing multi-selection untouched; the context menu appears on mouse UP, not down, so right-button dragging still works.**  
MouseDown: AddRemoveSelectionRange is skipped when (pose->IsSelected() && secondaryMouseButtonDown). MouseUp re-checks SecondaryMouseButtonDown(modifiers(), lastButtons) using the "last_buttons" field injected by the tracking filter, force-selects the pose if still unselected, then ShowContextMenu. Secondary = B_SECONDARY_MOUSE_BUTTON or primary+Control.  
`PoseView.cpp` → `BPoseView::MouseDown / BPoseView::MouseUp / SecondaryMouseButtonDown`  
**MISSING:** The selection rule is exactly right (context_click selects an unselected pose, leaves an existing multi-selection untouched, clears on empty space), but the menu is built on the button-3 PRESS rather than on release, and Control+primary is mapped to the Cmd modifier instead of counting as a secondary click.  
*src/tiku_trk_poseview.c:818 (tiku_trk_poseview_context_click); src/tiku_trk_desktop.c:1788-1813*

#### PVS-017 · important · done
**Holding any mouse button still for the long-press interval opens the context menu without releasing.**  
LongAndDragTrackingFilter fires kMsgMouseLongDown after get_click_speed() if the pointer stayed within the 4 px radius; MouseLongDown clears fTrackRightMouseUp/fTrackMouseUp so no second menu appears on release.  
`PoseView.cpp` → `BPoseView::MouseLongDown`  

#### PVS-018 · important · done
**Opening a context menu normalizes the selection: on a pose it selects that pose alone if it was unselected; on empty space it clears the selection.**  
Also activates and synchronously updates the window before the menu goes up, and fires the SelectionChanged hook afterwards.  
`PoseView.cpp` → `BPoseView::ShowContextMenu`  

#### PVS-019 · important · partial
**In icon mode a click only hits a pose on opaque icon pixels or inside the name text rect - clicking the transparent corner of an icon misses.**  
IconCache::sIconCache->IconHitTest(where - location, ResolvedModel(), kNormalIcon, IconSize()) for the icon rect; otherwise the first column's widget rect, and only if the widget IsVisible().  
`Pose.cpp` → `BPose::PointInPose(const BPoseView*, BPoint)`  
**TIKU:** Device-namespace nodes need an icon source; per-pixel hit testing implies a real alpha bitmap per node type.  
**MISSING:** Icon-mode hit testing exists and covers the label (the pose rect is widened and re-centred to the measured name width), but it is a plain rectangle test — there is no per-pixel alpha test against the rendered icon, so a click on a transparent icon corner still hits.  
*src/tiku_trk_poseview.c:1898 (tiku_trk_poseview_pose_at) / pose_rect:1861*

#### PVS-020 · important · partial
**In list mode a click hits the row if it lands in the icon rect or in any column's click rect, and reports which text widget was hit.**  
Icon rect is vertically centred in the row (_ListIconRect); the loop walks all columns and returns the hit widget through hitWidget for the path-column and rename cases.  
`Pose.cpp` → `BPose::PointInPose(BPoint, const BPoseView*, BPoint, BTextWidget**)`  
**MISSING:** The row is hit by a y-only band (no icon rect, no per-column click rects), and the hit-widget report exists only as a boolean 'was it the NAME column' in tiku_trk_app.c — the spatial shell calls name_clicked for any row press, so a click on the size column there arms a rename the original would not.  
*src/tiku_trk_app.c:756-770 row_at and :1329-1342 on_name column scan; src/tiku_trk_desktop.c:1150-1163 folder_row_at and :1828-1834 unconditional tiku_trk_poseview_name_clicked*

#### PVS-021 · polish · done
**Where icons overlap in icon mode, the topmost (last-drawn) pose receives the click; in list mode the row is computed directly from the y coordinate.**  
Icon mode iterates poseCount-1 down to 0; list mode index = (int32)(where.y / fListElemHeight) and *poseIndex is written even when the point misses the pose.  
`PoseView.cpp` → `BPoseView::FindPose`  

#### PVS-022 · important · open
**Clicking a pose in an inactive window raises the window only on mouse-up, and not at all if the click turned into a drag - so you can drag an item out of a background window without bringing it forward. Clicking empty space raises immediately.**  
fTrackMouseUp = !secondaryMouseButtonDown set in MouseDown; MouseUp does Window()->Activate() if still set; MouseDragged and MouseLongDown clear it. The empty-space branch of MouseDown calls window->Activate() + UpdateIfNeeded() directly.  
`PoseView.cpp` → `BPoseView::MouseDown / BPoseView::MouseUp / BPoseView::MouseDragged`  
**CHECKED:** The port has no deferred-raise mechanism at all: a content click never activates the window on press or release, there is no fTrackMouseUp-style flag for a drag to clear, and an empty-space click does not raise either — the drag-out-of-a-background-window property is a byproduct of never raising, not an implementation of the rule.  
*src/tiku_trk_desktop.c:1663-1671 (activate only when hit != TIKU_TRK_HIT_CONTENT); ButtonRelease at :1907-1913 does no activation*

#### PVS-023 · important · done
**When the window deactivates the selection stops being drawn as selected (unless "show selection when inactive" is on) and is restored on reactivation; the pivot survives the round trip.**  
WindowActivated -> ShowSelection(active); ShowSelection moves fSelectionPivotPose into fRealPivotPose on hide and back on show. With fShowSelectionWhenInactive the pose flags are left set and only redrawn. Deactivating also commits any active inline edit.  
`PoseView.cpp` → `BPoseView::WindowActivated / BPoseView::ShowSelection`  
**PORT:** Membership is the pose's flag and PAINTING is the view's, where Tracker keeps a separate selection list and clears the flag; one flag plus a visible bit cannot drift out of step with the other.

#### PVS-024 · core · partial
**Dragging from empty space with the primary button starts a rubber-band selection rectangle.**  
MouseDragged: FindPose(where) == NULL && buttons == B_PRIMARY_MOUSE_BUTTON -> _BeginSelectionRect; the band is anchored at the ORIGINAL click point carried in "be:view_where", not the current pointer. _BeginSelectionRect also switches the window pulse to 20 ms and turns auto-scroll on.  
`PoseView.cpp` → `BPoseView::MouseDragged / BPoseView::_BeginSelectionRect`  
**MISSING:** A band anchored at the original press point does start from empty space and sweeps the selection, but only in icon mode (list mode has no band at all), it starts on the press rather than after the drag threshold, and there is no pulse change or auto-scroll.  
*src/tiku_trk_app.c:1293-1307 and :1426-1437*

#### PVS-025 · important · done
**The rubber band INVERTS selection state relative to the selection that existed before the drag: sweeping over a previously-selected pose deselects it, and a pose that leaves the rectangle flips back to its prior state.**  
pose->Select(!fSelectionList->HasItem(pose)) for newly enclosed poses (fSelectionList is frozen during the band); for poses in the old enclosed list but not the new one, pose->Select(!pose->IsSelected()).  
`PoseView.cpp` → `BPoseView::SelectPoses(BRect, BList**)`  
**PORT:** set_band(v,1) snapshots every pose's selected flag into band_was, and select_rect sets each pose to !was while inside the rect and back to was once outside, so a previously-selected pose sweeps to deselected and flips back on leaving — exactly the inversion the row states, and a unit test exercises it.  
*src/tiku_trk_poseview.c:905-919 set_band snapshot; :1949-1962 select_rect (set_selected(p, inside ? !was : was)); tests/test_poseview.c:789-798*

#### PVS-026 · important · done
**A pose is caught by the band when its rect INTERSECTS the rectangle, not when it is fully enclosed - grazing an icon selects it.**  
selectionRect.Intersects(poseRect); scanning starts at the first index at or below the rect top and stops once a pose passes the rect bottom.  
`PoseView.cpp` → `BPoseView::SelectPoses(BRect, BList**)`  

#### PVS-027 · important · done
**Command-dragging from empty space keeps the existing selection and toggles the swept poses on top of it; a plain drag from empty space starts from nothing.**  
The retention comes from MouseDown NOT calling ClearSelection when extendSelection && fSelectionRectEnabled && fMultipleSelection; _BeginSelectionRect's shouldExtend parameter is itself unused, so do not "fix" it into a second mechanism.  
`PoseView.cpp` → `BPoseView::MouseDown / BPoseView::_BeginSelectionRect`  

#### PVS-028 · polish · done
**The first pose the band newly selects becomes the selection pivot for later shift-clicks.**  
if (fSelectionPivotPose == NULL && selected == false) fSelectionPivotPose = pose; inside the enclosed-pose loop.  
`PoseView.cpp` → `BPoseView::SelectPoses(BRect, BList**)`  

#### PVS-029 · core · partial
**Releasing the band rebuilds the selection list from scratch by polling every pose, and drops pivots that are no longer selected.**  
_EndSelectionRect: fSelectionList->MakeEmpty(); fMimeTypesInSelectionCache.MakeEmpty(); CurrentPoseList()->EachListItem(AddIfPoseSelected, fSelectionList); then nulls fSelectionPivotPose / fRealPivotPose if absent from the list. The rect is invalidated and reset to (0,0,-1,-1).  
`PoseView.cpp` → `BPoseView::_EndSelectionRect`  
**MISSING:** Selection being a per-pose flag genuinely removes the need for the list rebuild, but the row's second clause is simply missing: band release only clears the band flag, saves state and marks dirty, so a pivot the band left unselected survives and still anchors the next shift-click.  
*src/tiku_trk_app.c:1477-1485 (ButtonRelease: set_band(0), save_state, dirty — no pivot revalidation); src/tiku_trk_poseview.c:1955-1959 pivot only ever set, never dropped, on band exit*

#### PVS-030 · polish · partial
**The band is drawn either as a translucent filled rectangle (default) or as an XOR mixed-colors outline, switchable live from Tracker settings.**  
Transparent: DrawAfterChildren strokes with B_NAVIGATION_BASE_COLOR alpha 128 and fills the 1 px-inset interior with B_CONTROL_HIGHLIGHT_COLOR alpha 90; a zero-width/height rect degrades to a line. Opaque: B_OP_INVERT StrokeRect erase/redraw in _BeginSelectionRect/_UpdateSelectionRect/_EndSelectionRect. fTransparentSelection is seeded from TrackerSettings and updated on kTransparentSelectionChanged.  
`PoseView.cpp` → `BPoseView::DrawAfterChildren / BPoseView::_UpdateSelectionRect`  
**MISSING:** Only one style exists: a solid 1 px outline stroked in the selection colour; the default translucent fill (and its degenerate line case), the B_OP_INVERT erase/redraw variant, and the live TrackerSettings toggle are all absent.  
*src/tiku_trk_app.c:331-345 (draw_icons band block)*

#### PVS-031 · polish · done
**While a band is in progress, pose labels are drawn in the selected (inverted) style even if the window is not active.**  
if (windowActive || isDrawingSelectionRect) InvertRect(invertRect); isDrawingSelectionRect comes from BPoseView::IsDrawingSelectionRect().  
`Pose.cpp` → `BPose::DrawTextWidget`  

#### PVS-032 · core · partial
**A drag starts only after the pointer moves more than the radius threshold while a button is held, and only if that happens before the long-press timer fires.**  
squaredDelta >= radiusThreshold^2 with radiusThreshold defaulting to 4.0 px; tracking is armed only for clicks == 1 (a double-click never starts a drag), any B_MOUSE_UP cancels tracking, and the synthesized message carries the ORIGINAL click point as "be:view_where" so the drag frame is positioned from where the press began.  
`LongAndDragTrackingFilter.cpp` → `LongAndDragTrackingFilter::Filter`  
**MISSING:** A 4 px movement threshold does arbitrate against the long press and the armed rename (crossing it cancels both before the long-press timer fires), but crossing the threshold never STARTS a drag — the icon drag is armed on the press itself, and there is no clicks==1 gating.  
*src/tiku_trk_app.c:1417-1425 and :1289; src/tiku_trk_desktop.c:1887-1899 vs :2206*

#### PVS-033 · core · partial
**Dragging a pose drags the whole current selection; the drag message carries every selected item's ref plus the source window, the click point and the button state.**  
B_SIMPLE_DATA with "src_window" (raw BContainerWindow*), "click_pt", "buttons", a "TrackerViewToken" BMessenger for scripting, and one "refs" entry per selected pose in forward selection order. Each ref's MIME type is forced to exist first (update_mime_info if BNodeInfo::GetType fails). The drag aborts silently if the pose is no longer selected (it was Command-deselected by the same press) or if buttons == 0, and never starts when fDragEnabled is false (file panels).  
`PoseView.cpp` → `BPoseView::DragSelectedPoses / AddPoseRefToMessage`  
**TIKU:** Refs are entry_refs; a namespace-aware drag needs a typed descriptor handle for /dev nodes, and "src_window as raw pointer" cannot survive a process boundary.  
**MISSING:** Dragging does carry the whole selection — every selected pose moves by the same delta from its own press-time location — but this is an in-view icon move only: no drag message exists, so there are no refs, no source window, click point or button state, and nothing can be dragged to another view.  
*src/tiku_trk_app.c:1275-1291 and :1438-1458*

#### PVS-034 · important · open
**The drag image is a translucent rendering of the actually-selected poses, cropped to a box around the cursor with faded edges when the selection is larger than that box.**  
Union of visible selected pose rects = drag rect (GetDragRect); an inner box of kTransparentDragThreshold (ComposeIconSize(256)) is centred on the cursor and nudged inside the drag rect; each clipped side is extended by a ComposeIconSize(32) fade band (FadeRGBA32Horizontal/Vertical). Alpha is 192, or 164 when faded; white or black tint depending on HighColor().IsDark(). If the cursor is outside the drag rect no bitmap is produced and BView::DragMessage falls back to the plain outline rect.  
`PoseView.cpp` → `BPoseView::MakeDragBitmap / BPoseView::GetDragRect`  

#### PVS-035 · core · open
**While a drag hovers the view, the folder pose under the cursor is highlighted as the drop target, and poses that are part of the dragged selection are never valid targets.**  
UpdateDropTarget: targetPose is nulled if DragSelectionContains(targetPose); symlinks are resolved before validity testing; the target is accepted only if CanHandleDragSelection(...) passes, otherwise fDropTarget is cleared. HiliteDropTarget draws by transiently BPose::Select()ing the pose and remembers via fAlreadySelectedDropTarget that an already-user-selected target must not be deselected on exit. While a drop/context menu is tracking, moving off a pose does not un-hilite (trackingContextMenu && !targetPose returns "no change").  
`PoseView.cpp` → `BPoseView::UpdateDropTarget / BPoseView::HiliteDropTarget`  
**TIKU:** Drop-target validity is MIME-type based (Model::IsDropTargetForList); for device nodes this must become a capability-bit test (accepts-write / accepts-payload-type).  

#### PVS-036 · polish · open
**The cursor changes to the copy cursor when the drag is over a directory on a different device than the source.**  
Only when the target is a directory, the message has refs, !CheckDevicesEqual(&srcRef, targetModel), and the source is neither a volume nor root; otherwise the system default cursor is restored. fCursorCheck suppresses redundant cursor churn.  
`PoseView.cpp` → `BPoseView::UpdateDropTarget`  
**TIKU:** "Different device" has no analogue for namespace nodes; needs a same-store predicate to decide whether a transfer is a move or a copy.  

#### PVS-037 · core · partial
**Dropping items on a folder moves them when source and destination are on the same volume, and copies them when they are on different volumes.**  
moveMode selection order in MoveSelectionInto: forceCopy > forceMove > createRelativeLink > createLink > (!CheckDevicesEqual(srcList->ItemAt(0), destFolder) ? copy : move). Virtual-directory sources with no explicit mode default to creating links instead.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**MISSING:** The rule itself is implemented and unit-tested — same volume gives MOVE, different volume gives COPY, force_copy overrides — but no UI drop reaches it (nothing calls tiku_trk_fs_drop_action outside tests), and the link / relative-link / virtual-directory modes of the ladder are absent.  
*src/tiku_trk_fs.c:274 tiku_trk_fs_drop_action (tests/test_fs.c:94)*

#### PVS-038 · core · open
**Holding Option during a drop forces a copy even within the same volume.**  
forceCopy = forceCopy || (modifiers() & B_OPTION_KEY) != 0, evaluated at drop time, after the same-folder branch (so Option-dropping into the same folder duplicates instead).  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**VERIFIER CORRECTION:** Backwards. `forceCopy = forceCopy || (modifiers() & B_OPTION_KEY) != 0` sits AFTER the same-folder branch returns (PoseView.cpp:5040 vs 5085). Option-dropping inside the source folder therefore just moves the icons by the drag delta; it does not duplicate. Option only forces a copy for drops into a *different* folder.  

#### PVS-039 · important · open
**Dropping with the secondary button (or Control held) pops a menu offering Move here / Copy here / Create link here / Cancel; Cancel abandons the drop.**  
SecondaryMouseButtonDown(modifiers(), buttons) where buttons came from the drag message. ShowDropContextMenu disables every item when the target volume is read-only, and additionally disables "Move here" when the SOURCE volume is read-only. kCancelButton and any unrecognized result return without acting. Shift on the Create-link menu item selects a relative link (kCreateRelativeLink).  
`PoseView.cpp` → `BPoseView::MoveSelectionInto / BContainerWindow::ShowDropContextMenu`  

#### PVS-040 · important · done
**Dropping a selection inside its own folder in icon mode just repositions the icons by the drag delta; in list mode nothing happens.**  
Detected by *srcWindow->PoseView()->TargetModel()->NodeRef() == *destFolder->NodeRef(). Each pose is removed from the VS list, moved by (loc - where), re-added, and the old/new extents are updated. Link and relative-link are refused outright in this case; list mode returns immediately.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**TIKU:** Icon positions are persisted as BFS attributes; a device namespace has no attribute store, so pose locations for /dev nodes need a sidecar.  

#### PVS-041 · important · partial
**Forcing a copy (Option or the drop menu) onto the selection's own folder duplicates the items instead of erroring.**  
targetView->DuplicateSelection(&where, &loc); volumes and Trash are stripped from the selection first because they cannot be duplicated, and the pivots are nulled if they pointed at a stripped pose.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto / BPoseView::DuplicateSelection`  
**VERIFIER CORRECTION:** Only the drop context menu's "Copy here" (kCopySelectionTo) — or a caller passing forceCopy explicitly — reaches DuplicateSelection; the Option modifier does not (see the ordering above). The rest of the row (volumes and Trash stripped from the selection, pivots nulled) is correct (PoseView.cpp:6070).  
**MISSING:** Duplicate exists as its own operation with the 'copy'-suffixed free-name rule and destructive selections strip the Trash, but a copy whose destination is the item's own parent is silently SKIPPED rather than turned into a duplicate, and there is no drop menu / forceCopy caller to reach it.  
*src/tiku_trk_fs.c:477-486 (step_one same-dir skip) vs :364 tiku_trk_fs_duplicate and :333 Trash strip*

#### PVS-042 · important · open
**Dropping onto one of the poses being dragged is treated as a drop on the window background, not on that item.**  
In the same-window case, DragSelectionContains(targetPose, message) nulls targetModel so control falls through to MoveSelectionTo; the same-window case also activates the window and resets the pose placement hint.  
`PoseView.cpp` → `BPoseView::HandleDropCommon`  

#### PVS-043 · important · open
**Refused drops raise a specific alert instead of failing silently: onto the Disks/root window, onto a read-only volume, a copy into Trash, or a link into Trash.**  
Four separate BAlerts in MoveSelectionInto, each B_WARNING_ALERT with Cancel and B_CLOSE_ON_ESCAPE; okToMove is cleared and the destination BEntry is deleted.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  

#### PVS-044 · important · open
**Dropping files on an application or document pose launches it with those files as arguments, passing only the files that target claims to handle; holding Control bypasses the type check and forces all of them through.**  
HandleDropCommon: ignoreTypes = (modifiers() & B_CONTROL_KEY) != 0; LaunchAppWithSelection filters via Model::IsDropTarget(pose->TargetModel(), true) per pose and launches only if at least one ref survived.  
`PoseView.cpp` → `BPoseView::HandleDropCommon / BPoseView::LaunchAppWithSelection`  

#### PVS-045 · important · open
**Icons dropped on the background of an icon-mode window land at the drop point preserving their relative layout; a selection dragged out of a list-mode window stacks vertically at the drop point.**  
GetDropPointList: sourceInListMode -> dropEnd + (0, index * (IconPoseHeight() + 3)); else dropEnd + (pose->Location(this) - dropStart), where dropStart is the drag's "click_pt". Returns NULL (no positioning) when the destination is in list mode. Foreign ref drops onto a pose instead lay out in rows of 4 grid cells.  
`PoseView.cpp` → `BPoseView::GetDropPointList / BPoseView::MoveSelectionTo`  

#### PVS-046 · polish · done
**Holding Command while dropping snaps the dropped icons to the icon grid.**  
MoveSelectionTo computes pinToGrid = (modifiers() & B_COMMAND_KEY) != 0 and passes it as dropOnGrid; TrySettingPoseLocation applies PinToGrid for clipping files and skips positioning entirely in list mode, then removes the stale kAttrPoseInfoForeign attribute.  
`PoseView.cpp` → `BPoseView::MoveSelectionTo / BPoseView::TrySettingPoseLocation`  

#### PVS-047 · important · open
**Drags originating outside Tracker are honoured through the negotiated protocol: a promise drop into a folder creates an empty clipping file and replies with B_COPY_TARGET/B_MOVE_TARGET, and a drop on Trash replies B_TRASH_TARGET.**  
FindDragNDropAction reads "be:actions"; CanTrashForeignDrag requires the target to be Trash; CanCopyOrMoveForeignDrag requires a directory/virtual directory AND B_FILE_MIME_TYPE in "be:types". The reply carries directory+name, echoes be:filetypes, and forwards be:originator-data. With Control/secondary button held and multiple offered types or action specifiers, a popup menu lets the user pick which one (cancelling the menu cancels the drop).  
`PoseView.cpp` → `BPoseView::HandleDropCommon / BPoseView::FindDragNDropAction / RunMimeTypeDestinationMenu`  
**TIKU:** The promise protocol assumes a writable local directory to hand back; over a device namespace the reply target must be a real file node.  

#### PVS-048 · polish · open
**Dropping plain text or a bitmap from another app into a folder window creates a clipping file.**  
Name from "be:clip_name" or "Untitled clipping"/"Untitled bitmap", uniquified with FSMakeOriginalName; text keeps StyledEdit-compatible "styles" attribute from be-text_run_array; both get the kAttrClippingFile attribute and the right BNodeInfo type; bitmaps over 1 MB flattened size are rejected; a failed write removes the partial file. Only allowed when the target is a directory.  
`PoseView.cpp` → `BPoseView::HandleDropCommon / BPoseView::CreateClippingFile`  

#### PVS-049 · important · open
**A foreign drag carrying plain refs copies by default; with the secondary button it offers the Move/Copy/Link/Cancel menu, and it is refused if the target is not a directory or is the root disk.**  
canCopy = true fallback when no be:actions were negotiated; kCreateRelativeLink/kCreateLink/kMoveSelectionTo/kCopySelectionTo map to moveMode, default/kCancelButton returns true without acting. Work is dispatched asynchronously via FSMoveToFolder.  
`PoseView.cpp` → `BPoseView::HandleDropCommon`  

#### PVS-050 · important · open
**Dragging (or rubber-banding) toward a view edge auto-scrolls, after a hold-still delay, faster the further past the edge the pointer is.**  
Two trigger geometries: while dragging, the pointer must be in a border band OUTSIDE the view (20 px above the top plus the title view height, B_H_SCROLL_BAR_HEIGHT below, 6 px left, B_V_SCROLL_BAR_WIDTH right); while rubber-banding, simply past the bounds edge, at fAutoScrollInc/1.5 beyond kSlowScrollBucket (30 px) and fAutoScrollInc/4 within it. State machine kWaitForTransition (must first NOT be in a scroll zone) -> kDelayAutoScroll (condition true, snooze 600 ms, re-check) -> kAutoScrollOn, driven from Pulse; releasing all buttons resets to off and restores a 500 ms pulse. Band scrolling re-runs _UpdateSelectionRect so the rectangle grows without pointer motion.  
`PoseView.cpp` → `BPoseView::CheckAutoScroll / BPoseView::HandleAutoScroll`  

#### PVS-051 · important · open
**Spring-loaded folders: hovering a drag over a folder pose without moving pops up a navigable menu of that folder's contents, and choosing an entry drops the dragged items there.**  
B_MOUSE_IDLE -> MouseIdle requires buttons != 0 and IsDragging(); with fDropTarget set it records the target frame (FrameForPose) and calls ShowContextMenu, which in the dragging case opens fDragContextMenu (BPopUpNavMenu) asynchronously on the resolved directory ref - never for Printers, never for a non-directory, and never twice (IsShowing guard). Selecting an item posts B_REFS_RECEIVED, which BContainerWindow answers by calling HandleDropCommon with the cached DragMessage() against that folder, then DragStop.  
`PoseView.cpp` → `BPoseView::MouseIdle / BContainerWindow::ShowContextMenu / BContainerWindow::MessageReceived case B_REFS_RECEIVED`  
**TIKU:** Spring-loading a device directory means enumerating live nodes on hover; decide whether that is allowed to have side effects, and whether watch notifications should refresh an open menu.  

#### PVS-052 · polish · open
**Entries in the spring-loaded menu are enabled or greyed by whether they can accept the dragged types; containers are always enabled.**  
SpringLoadedFolderSetMenuStates walks ModelMenuItems and calls Model::SupportsMimeType(NULL, typeslist) for files/executables; directories, roots and volumes are unconditionally enabled; symlinks are resolved first and a broken symlink is disabled.  
`NavMenu.cpp` → `SpringLoadedFolderSetMenuStates`  
**TIKU:** Type support becomes a capability-bit query on the typed descriptor.  

#### PVS-053 · polish · open
**The dragged message and its unique MIME-type list are cached once per drag and reused; re-entering the view with the same drag does not rebuild them.**  
DragStart returns early when IsDragging() && SpringLoadedFolderCompareMessages(...), which compares every ref AND the "click_pt" so a fresh drag of the same files is still treated as new. IsDragging() == (fDragMessage != NULL && fCachedTypesList != NULL). Symlink refs contribute both the link type and the resolved type.  
`PoseView.cpp` → `BPoseView::DragStart / SpringLoadedFolderCompareMessages / SpringLoadedFolderCacheDragData`  

#### PVS-054 · polish · open
**A spring-loaded menu closes when the pointer leaves it (and all of its open submenus) by more than a margin.**  
kMenuTrackMargin = 20 px inset applied to the menu bounds and to each visible submenu window frame; returns true (go away) only when the pointer is outside all of them. Installed via InitTrackingHook with BPoseView::MenuTrackingHook.  
`PoseView.cpp` → `BPoseView::MenuTrackingHook`  

#### PVS-055 · polish · open
**Hovering a drag over the empty part of an inactive window raises that window.**  
MouseIdle else-branch: with a button down and a drag in progress but no drop target under the pointer, Window()->Activate().  
`PoseView.cpp` → `BPoseView::MouseIdle`  

#### PVS-056 · polish · open
**Leaving the view with a drag drops the highlight, restores the default cursor and discards the cached drag state; entering (re)arms auto-scroll.**  
MouseMoved B_EXITED_VIEW -> DragStop(), SetViewCursor(B_CURSOR_SYSTEM_DEFAULT), fCursorCheck = false, and if a context menu is up, HiliteDropTarget(false) + fDropTarget = NULL. B_ENTERED_VIEW/B_INSIDE_VIEW -> UpdateDropTarget and, if auto-scroll was off, kWaitForTransition at a 100 ms pulse. Drop handling and the next MouseDown also call DragStop to dispose of drag data lazily.  
`PoseView.cpp` → `BPoseView::MouseMoved / BPoseView::DragStop`  

#### PVS-057 · important · done
**When a pose disappears (deleted, unmounted, or filtered out) it is silently removed from the selection, and any drop-target or pivot reference to it is dropped so later shift-clicks and drops do not act on a dead item.**  
DeletePose and EnsurePoseUnselected both clear fDropTarget, commit the active pose, remove from fSelectionList, null fSelectionPivotPose/fRealPivotPose, and fire the SelectionChanged hook if the removed pose was selected.  
`PoseView.cpp` → `BPoseView::DeletePose / BPoseView::EnsurePoseUnselected`  
**TIKU:** Node-monitor removal becomes a VFS watch notification; the same invariant must hold when a device node vanishes mid-drag or mid-band.  
**PORT:** remove_pose splices the row out and clears or renumbers active, pivot, real_pivot, last_added and the armed rename in one place, and it is the single door for every removal notice (removed, moved-out, re-sorted); the filter paths deselect rows that leave the listing, and the drop point is a coordinate so no pose-valued drop target can dangle.  
*src/tiku_trk_poseview.c:2389-2412 remove_pose; :2565/:2589/:2607/:2667 call sites; :1099-1112 rebuild_filter and :1160-1170 filter_changed; :74-75 drop_x/drop_y as the drop state*

#### PVS-058 · polish · open
**After a drop on the Desktop, icons that would land off-screen are pushed back inside the visible area and their new positions are saved.**  
HandleDropCommon calls CheckPoseVisibility when fEnsurePosesVisible (set only by BDeskWindow); the bounds are inset by 20 px, off-screen poses are ConstrainTo'd, SetSaveLocation is set and the VS list is rebuilt.  
`PoseView.cpp` → `BPoseView::HandleDropCommon / BPoseView::CheckPoseVisibility`  

#### PVS-059 · polish · open
**A colour dropped on the view is not treated as a file drop - it is forwarded to the containing window (which sets the desktop colour).**  
HandleMessageDropped checks message->HasData("RGBColor", 'RGBC') before anything else and re-sends the message to the window; BDeskWindow::MessageReceived then sets the screen/desktop colour and notifies the Backgrounds app. HandleMessageDropped also resets the cursor and returns false immediately when fDropEnabled is clear (file panels).  
`PoseView.cpp` → `BPoseView::HandleMessageDropped`  


---

## PoseView: keyboard navigation, type-ahead, shortcuts, scripting

Architecture notes: [`spec/ARCH-poseview-keyboard.md`](spec/ARCH-poseview-keyboard.md)

#### PVK-001 · core · done
**Down/Right arrow selects the next pose in the list; Up/Left selects the previous one.**  
List mode only. resultIndex = poseList->IndexOf(fSelectionList->LastItem()) +/- 1 against CurrentPoseList(); the 'cursor' is the LAST item added to the selection, not a separate focus.  
`PoseView.cpp` → `BPoseView::FindNearbyPose (kListMode branch, case B_DOWN_ARROW/B_RIGHT_ARROW, case B_UP_ARROW/B_LEFT_ARROW)`  
**TIKU:** Cursor-as-last-selected must be reproduced; TikuOS namespace nodes need the same stable ordered list to index into.  
**PORT:** key_nav walks the current (filter-aware) list by ±1 from the row holding v->active, which the click, shift-click, type-select and arrow paths all set to the pose just added, so Down/Right advance and Up/Left retreat exactly as the row states.  
*src/tiku_trk_poseview.c:947-1018 (tiku_trk_poseview_key_nav, active_row 932-945); poses served through count/at 100-116*

#### PVK-002 · important · done
**Left arrow wraps from the first pose to the last; Right arrow wraps from the last to the first. Up and Down do NOT wrap.**  
Only the horizontal arrows have the `poseToSelect == NULL && arrowKey == B_LEFT_ARROW/B_RIGHT_ARROW` fallback; Up/Down simply find nothing and the key is ignored.  
`PoseView.cpp` → `BPoseView::FindNearbyPose (kListMode branch)`  
**PORT:** At row 0 LEFT targets n-1 while UP clamps to 0, and at row n-1 RIGHT targets 0 while DOWN clamps to n-1 — horizontal-only wrap, and all four cases are asserted in the test suite.  
*src/tiku_trk_poseview.c:964-983; tests/test_poseview.c:160-171*

#### PVK-003 · important · done
**Arrow key with an empty selection seeds one: Up/Left selects the last pose, Down/Right selects the first.**  
`else` branch when selectedPose == NULL in list mode.  
`PoseView.cpp` → `BPoseView::FindNearbyPose`  

#### PVK-004 · core · done
**A plain arrow key replaces the whole selection with the single target pose and scrolls it into view.**  
SelectPose(pose, index) -> ClearSelection() then AddPoseToSelection(..., scrollIntoView = true by default).  
`PoseView.cpp` → `BPoseView::KeyDown (case B_LEFT_ARROW..B_DOWN_ARROW) -> BPoseView::SelectPose`  
**PORT:** The unmodified branch calls clear_selection() then set_selected(target) and updates pivot/last_added/active, and the tracker front-end then calls scroll_into_view on the active row, so the selection is replaced by one pose and scrolled to.  
*src/tiku_trk_poseview.c:1006-1018; src/tiku_trk_app.c:851-868; tests/test_poseview.c:154-157*

#### PVK-005 · important · done
**Shift+arrow grows the selection by one pose; if the pose in that direction is ALREADY selected it instead shrinks the selection by dropping the most recently added pose (walking back).**  
if (pose->IsSelected()) { RemovePoseFromSelection(fSelectionList->LastItem()); fSelectionPivotPose = pose; ScrollIntoView(pose, index); } else AddPoseToSelection(pose, index, true).  
`PoseView.cpp` → `BPoseView::KeyDown (arrow case, B_SHIFT_KEY branch)`  
**PORT:** The walk-back branch now moves its mark: on a shift-arrow into an already-selected pose it clears the pose named by last_added and then sets `v->last_added = t->index` (the target the walk is standing on), so successive steps drop different poses. Probe: click row 0 then three Shift+Down gives 4 selected, three Shift+Up gives 3, 2, 1 — it keeps shrinking. The index-space mix-up is also gone: last_added is stored as a POSE index everywhere (click(), select_all(), context_click(), the insert/remove fixups at :2469/:2505) and is read back through display_of(), so it survives filtering — probed with a filter on (7 of 12 rows shown): grow 3 -> back 2 -> back 1. Residual nit (in the citation, not the row's bold rule): Haiku's shrink branch also does `fSelectionPivotPose = pose`, while the port only seeds the pivot when it is unset (`if (v->pivot < 0 ...)`), so the anchor does not follow the walk.

#### PVK-006 · important · open
**Shift+arrow does nothing extra in single-selection views (file panels opened without multiple selection) - it behaves as a plain arrow.**  
Guarded by `fMultipleSelection &&` before the shift test; fMultipleSelection is set via SetMultipleSelection().  
`PoseView.cpp` → `BPoseView::KeyDown (arrow case); BPoseView::SetMultipleSelection (PoseView.h)`  

#### PVK-007 · important · open
**In icon/mini-icon modes the arrow keys navigate geometrically: the chosen pose is the nearest one in the arrow's direction whose rectangle overlaps the current pose's horizontal band (for Left/Right) or vertical band (for Up/Down).**  
LinesIntersect(top,bottom,...) for Left/Right and LinesIntersect(left,right,...) for Up/Down; among candidates the one with the closest facing edge wins (max left / min right / max top / min bottom), tracked in bestRect. No wraparound.  
`PoseView.cpp` → `BPoseView::FindNearbyPose (icon-mode loop over fPoseList) + static LinesIntersect`  
**TIKU:** A namespace-node view with no free icon placement can drop this, but grid-mode navigation must still be spatial if icons are ever positioned.  
**CHECKED:** Arrow keys are purely list-index in every view mode; nothing consults pose rectangles, bands or facing edges, and pose_rect/loc_x are never read from the key path.  
*none found (src/tiku_trk_poseview.c:tiku_trk_poseview_key_nav is index-only)*

#### PVK-008 · polish · open
**Arrow key in icon mode with nothing selected picks the upper-left-most pose.**  
Scans fVSPoseList comparing CalcRect tops/lefts, stopping at the first pose on a lower row. (The loop double-increments `index`, so it inspects every other entry.)  
`PoseView.cpp` → `BPoseView::FindNearbyPose (`if (fSelectionList->IsEmpty())` branch)`  
**CHECKED:** With an empty selection the port seeds at list index 0 (Down/Right) or n-1 (Up/Left) with no geometry consulted anywhere in key_nav, so on a hand-dragged icon layout (move_pose is reachable from the drag path) the upper-left-most pose is simply not what gets picked — the mechanism is absent, not replaced.  
*src/tiku_trk_poseview.c:960-981 (no CalcRect/geometry in key_nav); drag placement src/tiku_trk_app.c:1449-1457 via tiku_trk_poseview_move_pose*

#### PVK-009 · polish · differs
**An arrow key in icon mode that finds no pose in that direction leaves the selection exactly as it was (no deselect, no beep).**  
`if (poseToSelect != NULL) return poseToSelect; return selectedPose;` - the current pose is re-selected onto itself.  
`PoseView.cpp` → `BPoseView::FindNearbyPose (tail)`  
**PORT:** There is a real alternative implementation to point at — the end-of-list clamps (UP at row 0 -> 0, DOWN at row n-1 -> n-1) feed the same plain-arrow path, so the pose is re-selected onto itself with no deselect and no beep, the same outcome Tracker gets from FindNearbyPose returning selectedPose.  
*src/tiku_trk_poseview.c:966-983 combined with 1006-1018*

#### PVK-010 · important · partial
**Home selects the first entry in list mode; in icon modes it only scrolls the view to the top without changing the selection. End is the mirror image.**  
kListMode -> MoveOrChangePoseSelection(0) / (CountItems()-1); otherwise ScrollView(B_HOME) / ScrollView(B_END) which sets the vertical scrollbar to 0 / max.  
`PoseView.cpp` → `BPoseView::KeyDown (case B_HOME, case B_END) -> BPoseView::ScrollView`  
**MISSING:** The selection no longer moves in an icon mode; the scroll it sets instead is not yet read by the icon renderer, which does not scroll.

#### PVK-011 · important · partial
**Page Up selects the first fully visible pose; if that pose is already the first selected one, the view scrolls up one page first and then selects the new first-visible pose. Page Down is the mirror image using the last visible pose.**  
Compares FirstVisiblePose()'s index with CurrentPoseList()->IndexOf(fSelectionList->FirstItem()) (Page Down uses LastItem()); ScrollView(B_PAGE_UP/DOWN) moves the scrollbar by its own `max` step. In icon modes Page Up/Down only scroll.  
`PoseView.cpp` → `BPoseView::KeyDown (case B_PAGE_UP, case B_PAGE_DOWN); BPoseView::FirstVisiblePose / LastVisiblePose`  
**MISSING:** Page Up/Down move the selection by a hardcoded 20 rows; neither the 'select the first/last fully visible pose' step nor the 'already selected -> scroll a page first' step exists, and icon modes are not scroll-only.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_key_nav lines 987-989*

#### PVK-012 · important · partial
**Home/End/PageUp/PageDown with Shift held extend the selection from the existing anchor to the destination instead of replacing it.**  
MoveOrChangePoseSelection(to): when a first selection exists, fMultipleSelection is set and Shift is down, it walks from firstIndex (if moving up) or lastIndex (if moving down) to `to` selecting every pose in between; it only ADDS (select = true, shrinking is a TODO in the source) and scrolls the target into view.  
`PoseView.cpp` → `BPoseView::MoveOrChangePoseSelection`  
**MISSING:** Shift is honoured on these keys but only ever adds the single destination pose — the walk from the existing anchor to the destination selecting everything in between is missing, so Shift+End extends by one row, not to the end.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_key_nav lines 994-1010 (no select_range call)*

#### PVK-013 · polish · partial
**Scrolling a pose into view never happens on the Desktop, and in list mode only vertical scrolling occurs.**  
ScrollIntoView(BRect) returns immediately if IsDesktopView(); the horizontal adjustment block is skipped when ViewMode() == kListMode. A pose taller/wider than the view is aligned to its top/left.  
`PoseView.cpp` → `BPoseView::ScrollIntoView(BRect)`  
**MISSING:** The list-mode half is genuinely implemented — scroll_into_view is a row-index clamp that only touches v->scroll and aligns an above-window target to the top — but the Desktop exemption has no counterpart at all: the shell's backdrop holds no pose view to exempt, and the auditor's own wording concedes the absence rather than naming an alternative.  
*src/tiku_trk_poseview.c:3664-3679; src/tiku_trk_desktop.c:1096 (the only poseview_new, for a folder window)*

#### PVK-014 · core · done
**Return opens the current selection.**  
case B_RETURN -> OpenSelection(); with no selection OpenSelectionCommon returns immediately (selectCount == 0).  
`PoseView.cpp` → `BPoseView::KeyDown (case B_RETURN) -> BPoseView::OpenSelection / OpenSelectionCommon`  
**TIKU:** Opening a device node in the TikuOS namespace needs a defined 'open' action (read view? shell verb?); decide per typed descriptor.  
**PORT:** Return reaches open_selection/the desktop handler, which returns immediately when there is no active pose, opens a folder window for a container and the value editor for a leaf — the 'defined open action' the row's TIKU note asks for.  
*src/tiku_trk_desktop.c:2163-2177; src/tiku_trk_app.c:738-755 and 869-880; tiku_trk_poseview_active src/tiku_trk_poseview.c:132-138*

#### PVK-015 · important · done
**Return while type-ahead filtering with nothing selected first selects the top filtered pose, then opens it - so type-a-few-letters-then-Enter works.**  
`if (IsFiltering() && CountSelected() == 0) SelectPose(fFilteredPoseList->FirstItem(), 0);` before OpenSelection().  
`PoseView.cpp` → `BPoseView::KeyDown (case B_RETURN)`  

#### PVK-016 · polish · done
**Shift+Return opens the selection AND dismisses the type-ahead filter, returning the window to the unfiltered listing.**  
After OpenSelection(): `if (IsTypeAheadFiltering() && (modifiers() & B_SHIFT_KEY) != 0) StopTypeAheadFiltering();`.  
`PoseView.cpp` → `BPoseView::KeyDown (case B_RETURN)`  

#### PVK-017 · polish · open
**Option+Return also opens the selection, even though it has no Command key.**  
Installed as a ShortcutFilter (B_RETURN, B_OPTION_KEY -> kOpenSelection) because BWindow::AddShortcut cannot express Command-less shortcuts. ShortcutFilter requires EXACT modifier equality after stripping caps/num lock.  
`PoseView.cpp` → `BPoseView::AttachedToWindow (AddFilter(new ShortcutFilter(B_RETURN, B_OPTION_KEY, kOpenSelection, this))); ShortcutFilter::Filter (Utilities.cpp)`  
**CHECKED:** No Alt/Option modifier is inspected anywhere in the single-window key handler; the only Mod1Mask use in the port is Option-click to close the source window on a double-click open.  
*none found (grep Mod1Mask in src/tiku_trk_app.c returns nothing; only src/tiku_trk_desktop.c:1845)*

#### PVK-018 · important · done
**Holding Option while opening closes the parent window as the items open.**  
OpenSelectionCommon adds a "nodeRefsToClose" B_RAW_TYPE node_ref per ref, suppressed when the app is not Tracker, Option is up, the view is a file panel, the Desktop, or SingleWindowBrowse is on.  
`PoseView.cpp` → `BPoseView::OpenSelectionCommon`  
**PORT:** Closes the window the item was opened FROM, which is the case a spatial desktop has; there is no batch of refs to attach a close list to, so the suppressions (file panel, Desktop, single-window browse) have nothing to suppress.

#### PVK-019 · important · partial
**With Single Window Browse on, opening a folder navigates the current window instead of spawning one; holding Option overrides back to a new window.**  
Requires settings.SingleWindowBrowse(), not desktop/file panel, `(modifiers() & B_OPTION_KEY) == 0`, target and resolved pose both directories; posts kSwitchDirectory to the window.  
`PoseView.cpp` → `BPoseView::OpenSelection`  
**MISSING:** Navigate-in-place exists (the single-window app's open_selection switch_dirs the current window) but it is hardwired per binary — there is no Single Window Browse setting and no Option-held override back to a new window (the desktop shell always spawns, and Option there closes the source window instead).  
*src/tiku_trk_app.c:open_selection lines 740-756; src/tiku_trk_desktop.c:1840-1851*

#### PVK-020 · important · open
**Tab jumps to the next pose whose name sorts after the current selection's name (case-insensitive), Shift+Tab to the previous one, wrapping at either end.**  
sMatchString is seeded from fSelectionList->FirstItem()'s name (or emptied when nothing is selected); FindNextMatch scans fPoseList with sMatchString.ICompare()/strcasecmp keeping the smallest name greater than it (or largest smaller, when reverse). Wraparound re-runs the search after setting sMatchString to "" (forward) or to the single char 0x7f (reverse).  
`PoseView.cpp` → `BPoseView::KeyDown (case B_TAB); BPoseView::FindNextMatch`  
**CHECKED:** Nothing performs a next-name-after / previous-name-before search: Tab is bound to toggling filter mode and no case-insensitive ordered scan over the pose list exists anywhere.  
*none found (src/tiku_trk_app.c:handle_key case XK_Tab line 908 toggles filtering; no strcasecmp name-order scan in src/)*

#### PVK-021 · polish · open
**Tab does nothing in list mode when the Type-ahead filtering setting is enabled (the key is swallowed so it cannot fight the filter).**  
`if (ViewMode() == kListMode && TrackerSettings().TypeAheadFiltering()) break;`  
`PoseView.cpp` → `BPoseView::KeyDown (case B_TAB)`  
**CHECKED:** Tab is not swallowed: it is bound to toggling the filter on and off, so while filtering is active Tab is the one key that tears the filter down — the opposite of the row's inertness rule, and no other code path makes Tab inert.  
*src/tiku_trk_app.c:908-912 (case XK_Tab -> tiku_trk_poseview_set_filtering(!is_filtering)); no XK_Tab pose handling in src/tiku_trk_desktop.c (2040 is the Select dialog)*

#### PVK-022 · polish · open
**In a file panel, and in the Open With window, Tab moves keyboard focus between controls instead of walking poses.**  
File panel: `if (IsFilePanel()) _inherited::KeyDown(bytes, count);`. Open With: OpenWithPoseView::KeyDown routes B_TAB to BView::KeyDown, bypassing BPoseView entirely.  
`PoseView.cpp` → `BPoseView::KeyDown (case B_TAB); OpenWithPoseView::KeyDown (OpenWithWindow.cpp)`  

#### PVK-023 · important · partial
**While a pose name is being edited, Return commits the edit, Escape cancels it, and Tab / Shift+Tab commit and move to the next / previous editable text widget of the same pose.**  
view->CommitActivePose(key == B_RETURN) for Return/Escape; ActivePose()->EditNextWidget/EditPreviousWidget for Tab. Both return B_SKIP_MESSAGE so the keys never reach BPoseView::KeyDown. The edit BTextView also DisallowChar(B_TAB) and DisallowChar(B_ESCAPE).  
`TextWidget.cpp` → `TextViewKeyDownFilter`  
**TIKU:** Renaming a namespace node is a write to a typed descriptor; the commit/cancel split still applies.  
**MISSING:** Return commits and Escape cancels the inline rename ahead of any view key handling, but the Tab / Shift+Tab clause is missing entirely — Tab reaches the editor as a control char and is ignored, and there is no next/previous editable widget for a pose.  
*src/tiku_trk_app.c:handle_key lines 800-835; src/tiku_trk_edit.c:tiku_trk_edit_key line 97*

#### PVK-024 · important · done
**Backspace deletes the last character of the type-ahead match string and re-selects the best match for the shortened string; it also restarts the type-ahead timeout.**  
sMatchString.TruncateChars(CountChars()-1); fLastKeyTime = system_time(); fCountView->SetTypeAhead(...); FindBestMatch. Does nothing when the buffer is already empty.  
`PoseView.cpp` → `BPoseView::KeyDown (case B_BACKSPACE)`  
**PORT:** type_backspace returns untouched on an empty buffer, otherwise drops the last character, restamps typed_at (restarting the lapse window) and re-runs type_select_best on the shortened string.  
*src/tiku_trk_poseview.c:1251-1262; tests/test_poseview.c:198-200*

#### PVK-025 · important · done
**Backspace while type-ahead FILTERING deletes one filter character; when the current filter term is already empty it removes the whole term (undoing a Shift+Space split); at the single empty term it does nothing.**  
Operates on fFilterStrings.LastItem(): TruncateChars, else RemoveItemAt(stringCount-1) when more than one term exists, else break. Mirrors the CountView display via RemoveFilterCharacter().  
`PoseView.cpp` → `BPoseView::KeyDown (case B_BACKSPACE, IsTypeAheadFiltering branch)`  
**PORT:** The filtering branch truncates the last term, drops the whole term when it is already empty and more than one exists, and does nothing at the single empty term; the '|'-joined display is rebuilt from the same terms.  
*src/tiku_trk_poseview.c:1235-1250 (type_backspace filtering branch, last_term 1122-1127); display src/tiku_trk_poseview.c:1283-1301*

#### PVK-026 · core · done
**Typing printable characters selects the best-matching pose; characters accumulate into one search string as long as the gap between keystrokes is under twice the system double-click speed, otherwise a new search starts.**  
`if (eventTime - fLastKeyTime < (doubleClickSpeed * 2)) sMatchString.AppendChars(bytes,1); else sMatchString.SetToChars(bytes,1);` - eventTime comes from the message's "when" field, falling back to system_time(). The timeout is get_click_speed()*2, NOT a fixed 1 s.  
`PoseView.cpp` → `BPoseView::KeyDown (default case)`  
**TIKU:** TikuOS has no get_click_speed(); the window must become an explicit configurable constant (and be documented as such).  
**PORT:** Printable keys append and jump the selection to the best match, and the accumulate-or-restart window is the documented TIKU_TRK_TYPEAHEAD_US constant enforced by the per-loop type_tick, which clears the buffer after a quiet second so the next key starts a fresh search — exactly the explicit-constant substitution the row's TIKU note mandates.  
*src/tiku_trk_poseview.c:1206-1229 and 1306-1315; constant documented at src/tiku_trk_poseview.h:30-31; tick called each loop at src/tiku_trk_app.c:1557*

#### PVK-027 · important · done
**The in-progress type-ahead string is displayed in the count view at the bottom-left, replacing the item count, on a document-coloured background; it is cleared automatically once the timeout lapses.**  
fCountView->SetTypeAhead(sMatchString.String()); a BMessageRunner (fKeyRunner) fires kCheckTypeahead every doubleClickSpeed, and when system_time() - fLastKeyTime exceeds doubleClickSpeed*2 it blanks the display and deletes itself (fKeyRunner = NULL).  
`PoseView.cpp` → `BPoseView::KeyDown (default case); BPoseView::MessageReceived (case kCheckTypeahead); BCountView::SetTypeAhead / Draw (CountView.cpp)`  
**TIKU:** Needs a periodic timer + a status line equivalent; TikuOS timers make the runner a tiku_timer in EVENT mode.  
**PORT:** draw_count paints the typed string in the bottom-left count strip on a sunken TIKU_DESK_C_DOC ground in place of the 'N items' text, and the lapse tick blanks it automatically (the only gap is that the tick does not itself set a->dirty, so the blank lands on the next redraw).  
*src/tiku_trk_app.c:209-238; lapse src/tiku_trk_poseview.c:1306-1315*

#### PVK-028 · core · done
**Type-ahead scoring: a name STARTING with the typed text always wins; otherwise a name containing it scores 1/offset so earlier occurrences win; a name that does not contain the text at all is not a candidate.**  
ComputeTypeAheadScore returns kExactMatchScore (INFINITY) when strcasestr finds the match at position 0, 1.f/(found-text) otherwise, and -1 when absent (wordMode defaults to false, so the word-beginning heuristic is NOT used from the pose view). bestScore starts at -1 and comparison is strictly `>`, so a no-match leaves the selection untouched and ties go to the earliest pose.  
`Utilities.cpp` → `ComputeTypeAheadScore; BPoseView::FindBestMatch (PoseView.cpp)`  

#### PVK-029 · important · partial
**In list mode type-ahead matches against column text, not just the name: it tries each column in order and stops at the first column that produces any match; in icon modes only the pose name is matched.**  
Outer loop over CountColumns() with `if (bestScore > 0 || ViewMode() != kListMode) break;`; list mode reads pose->WidgetFor(column,...)->Text(this), icon modes read pose->TargetModel()->Name().  
`PoseView.cpp` → `BPoseView::FindBestMatch`  
**TIKU:** Column text becomes typed descriptor fields of a namespace node; decide which fields participate in type-ahead.  
**MISSING:** The icon-mode half is right (matching is against model.name), but the list-mode clause is missing: type_select_best never walks the columns, so column text only participates in FILTERING, not in type-ahead selection.  
*src/tiku_trk_poseview.c:type_select_best lines 1184-1203 (vs tiku_trk_poseview_filter_pose lines 1075-1094 which does walk columns)*

#### PVK-030 · core · done
**With the 'Type-ahead filtering' setting on and in list mode, typing FILTERS the listing (hiding non-matching poses) instead of selecting a pose.**  
Gate is `ViewMode() == kListMode && TrackerSettings().TypeAheadFiltering()` in the default KeyDown case; characters are appended to fFilterStrings.LastItem() and TypeAheadFilteringChanged() re-evaluates.  
`PoseView.cpp` → `BPoseView::KeyDown (default case); BPoseView::TypeAheadFilteringChanged`  
**PORT:** With v->filtering set, a typed character is appended to the last filter term and filter_changed() re-evaluates the listing instead of selecting; count()/at() then serve the filtered index array, so the drawn rows really do shrink.  
*src/tiku_trk_poseview.c:1210-1221, filter_changed 1152-1180, count/at 100-116; tests/test_poseview.c:208-217*

#### PVK-031 · important · done
**Shift+Space starts an additional filter term; a pose must match EVERY term (each term may match a different column). Terms are shown separated by '|' in the count view. A new term is refused while the current one is empty.**  
`if (key == ' ' && (modifiers() & B_SHIFT_KEY) != 0)` adds a new BString to fFilterStrings and AddFilterCharacter("|"). FilterPose keeps a `found[]` array and only returns true when matchesLeft hits 0; matching is case-insensitive substring (strcasestr) over every column's widget text.  
`PoseView.cpp` → `BPoseView::KeyDown (default case, filtering branch); BPoseView::FilterPose`  

#### PVK-032 · important · done
**Narrowing a filter prunes the visible list incrementally; deleting characters (widening) rebuilds the filtered list from scratch so previously hidden poses come back.**  
TypeAheadFilteringChanged compares fLastFilterStringCount/fLastFilterStringLength with the current values: shrink -> RebuildFilteringPoseList() + Invalidate(); grow -> walk fFilteredPoseList backwards removing poses that no longer FilterPose().  
`PoseView.cpp` → `BPoseView::TypeAheadFilteringChanged; BPoseView::RebuildFilteringPoseList`  
**TIKU:** Watch notifications must re-run FilterPose for a changed node, since a value change can move a node in or out of the filter.  
**PORT:** Typing a character appends to the last term and calls filter_changed(v,1), which walks the existing filtered index list and thins it in place; backspace (and dropping a term) calls filter_changed(v,0), which runs rebuild_filter() over every pose so rows hidden by the longer string come back — exactly the row's grow/shrink split, and the TIKU note is met because notify() re-runs rebuild_filter when a change redraws while filtering.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1151-1180 (filter_changed), :1098-1113 (rebuild_filter), :1220 (type_ahead grew=1), :1249 (backspace grew=0), :2683-2687 (notify re-filters)*

#### PVK-033 · core · done
**Poses filtered out of view are deselected, and all keyboard navigation, Select All, and the item count operate on the filtered set only.**  
RebuildFilteringPoseList/RemoveFilteredPose call EnsurePoseUnselected(); CurrentPoseList() returns fFilteredPoseList whenever IsFiltering() in list mode, and CountItems()/IndexOfPose()/PoseAtIndex() all route through it.  
`PoseView.cpp` → `BPoseView::RebuildFilteringPoseList; BPoseView::CurrentPoseList (PoseView.h)`  
**PORT:** Both filter paths clear ->selected on any pose that leaves the listing, and count()/at() return the filtered index list while filtering, with key_nav, select_all and the count-view label all going through those two accessors rather than the raw array.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1099-1113 and :1160-1168 (deselect on leaving), :100-116 (count/at), :616-630 (select_all), :947-1007 (key_nav); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:216,231-233 (count view)*

#### PVK-034 · important · partial
**Escape cancels type-ahead filtering (and aborts a pending clipboard cut); Shift+Escape removes the selection from the clipboard, or every pose in the folder when nothing is selected.**  
Two ShortcutFilters installed in AttachedToWindow map B_ESCAPE/0 -> B_CANCEL and B_ESCAPE/B_SHIFT_KEY -> kCancelSelectionToClipboard. case B_CANCEL: FSClipboardClear() if refs exist, then StopTypeAheadFiltering(). In a file panel Escape first commits an active name edit, then cancels filtering, then closes the panel.  
`PoseView.cpp` → `BPoseView::AttachedToWindow; BPoseView::MessageReceived (case B_CANCEL, case kCancelSelectionToClipboard); key_down_filter (FilePanelPriv.cpp)`  
**VERIFIER CORRECTION:** key_down_filter calls `view->CommitActivePose(false)` (FilePanelPriv.cpp:151) — saveChanges is false, so the pending rename is DISCARDED, not committed. Order and fall-through (edit → filtering → kCancelButton) are otherwise right.  
**MISSING:** Escape clears the filter terms and restores the listing, but it leaves filtering mode itself engaged, and neither the clipboard clauses exist at all — there is no cut/copy/paste state in the port and no Shift+Escape binding.  
*src/tiku_trk_app.c:handle_key lines 904-907 -> tiku_trk_poseview_type_clear (poseview.c:1262-1275)*

#### PVK-035 · polish · differs
**Filtering is torn down automatically when the user leaves list mode, and live when the Type-ahead filtering setting is switched off in preferences.**  
SetViewMode calls ClearTypeAheadFiltering() when leaving kListMode; the kTypeAheadFilteringChanged observer notice calls StopTypeAheadFiltering() when the setting goes false. ClearTypeAheadFiltering deletes all but the first filter term, truncates it, resets fLastFilterString{Count,Length} to 1/0 and cancels the count-view display.  
`PoseView.cpp` → `BPoseView::SetViewMode; BPoseView::MessageReceived (B_OBSERVER_NOTICE_CHANGE, case kTypeAheadFilteringChanged); BPoseView::ClearTypeAheadFiltering`  
**PORT:** The leave-list-mode clause is exact (set_mode calls type_clear + set_filtering(0)), but the port has no preferences at all — TIKU_TRK_CMD_PREFERENCES has no handler — so the 'setting switched off' clause is served instead by the Tab toggle, whose off path does perform the live teardown (terms collapsed to one empty term, listing rebuilt, scroll reset); that is a real alternative control, not the stated one, so differs rather than done.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1599-1604 (set_mode teardown), :1317-1334 (set_filtering off path); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:908-912 (Tab is the only on/off switch; no CMD_PREFERENCES case exists in tiku_trk_desktop.c)*

#### PVK-036 · important · done
**F2 begins editing the name of the selected pose.**  
case B_FUNCTION_KEY reads the "key" field of the current message and posts kEditName to the window when it equals B_F2_KEY; kEditName is refused if a pose is already being edited or nothing is selected, and it edits the FIRST selected pose's first widget. Cmd+E is the equivalent menu shortcut; enablement is CanEditName() (exactly one selection, not Desktop/root/Trash, no active edit).  
`PoseView.cpp` → `BPoseView::KeyDown (case B_FUNCTION_KEY); BPoseView::MessageReceived (case kEditName); BPoseView::CanEditName`  
**PORT:** XK_F2 calls begin_rename(), which opens the inline editor on the active (selected) pose, returns silently when nothing is active, and refuses device-namespace/virtual nodes; the whole key handler is short-circuited by the a->editing branch above it, so F2 cannot re-enter an edit already in progress.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:975-977 (XK_F2), :647-670 (begin_rename), :800-836 (editing branch returns first)*

#### PVK-037 · polish · done
**The Insert key is swallowed and does nothing (it must not fall through into type-ahead).**  
`case B_INSERT: break;`  
`PoseView.cpp` → `BPoseView::KeyDown (case B_INSERT)`  

#### PVK-038 · core · partial
**The Delete key with no Command moves the selection to the Trash; Shift+Delete deletes outright; inside the Trash, Delete deletes immediately without confirmation.**  
AddShortcut(B_DELETE, B_NO_COMMAND_KEY, kMoveSelectionToTrash) and AddShortcut(B_DELETE, B_NO_COMMAND_KEY|B_SHIFT_KEY, kDeleteSelection). DoMoveToTrash re-reads the live Shift state from the message and calls DeleteSelection() instead of MoveSelectionToTrash(); when TargetModel()->IsTrash() it goes straight to DeleteSelection(true, false).  
`ContainerWindow.cpp` → `BContainerWindow::AddShortcuts / BContainerWindow::Init; BPoseView::DoMoveToTrash (PoseView.cpp)`  
**TIKU:** No Trash in a device namespace - the destructive/undoable split needs a decision, and read-only nodes must refuse.  
**MISSING:** Delete acts on the selection but always destructively — OP_TRASH is handled identically to OP_DELETE (unlink/remove) — with only a status-line warning and no confirmation; the Shift modifier is never read on the Delete key, and the in-Trash case does not exist, so only the 'Shift+Delete deletes outright' clause holds while the no-modifier trash clause is not implemented by any route (the Shift 'Delete' relabel in tiku_trk_shortcuts.c maps to a command no window handles).  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:978-993 (XK_Delete, no shift test); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_fs.c:372-376 and :438-462 (OP_TRASH == OP_DELETE); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_shortcuts.c:65,129-130*

#### PVK-039 · important · partial
**When a text field has focus, the Delete key and the Cut/Copy/Paste/Select-all shortcuts are forwarded to that text view instead of acting on files.**  
kMoveSelectionToTrash synthesises a B_DELETE KeyDown on the focused BTextView; BContainerWindow::MessageReceived re-posts B_CUT/B_COPY/B_PASTE/B_SELECT_ALL to CurrentFocus() when it is a BTextView, otherwise to the PoseView. B_SELECT_ALL inside the pose view selects the active pose's text widget when one is being edited.  
`PoseView.cpp` → `BPoseView::MessageReceived (case kMoveSelectionToTrash, case B_SELECT_ALL); BContainerWindow::MessageReceived (ContainerWindow.cpp)`  
**MISSING:** While the inline editor has focus it swallows the keys first and Delete and Ctrl+A are routed to it as text verbs, but Cut/Copy/Paste are not forwarded (the port has no clipboard at all, so Ctrl+X/C/V simply fall through and are dropped as control chars).  
*src/tiku_trk_app.c:handle_key lines 800-835; src/tiku_trk_edit.c:tiku_trk_edit_key case TIKU_TRK_EDIT_SELECT_ALL line 147*

#### PVK-040 · important · done
**Command+Up opens the parent folder; it is refused at the root (and on the Desktop) unless Control is also held, which forces it.**  
CanOpenParent(): false for Desktop view or root; `if ((modifiers() & B_CONTROL_KEY) != 0) return true;` overrides; file panels with DesktopFilePanelRoot stop at Desktop; the parent-is-root case is allowed only when the Show Disks icon setting is on.  
`PoseView.cpp` → `BPoseView::OpenParent / BPoseView::CanOpenParent; AddShortcut(B_UP_ARROW, B_COMMAND_KEY, kOpenParentDir) in BContainerWindow::AddShortcuts`  
**PORT:** Bound to Cmd+Up in both shells.  The menu shows no accelerator for it: this drawing floor has one glyph per byte and no arrow among them.

#### PVK-041 · important · open
**Command+Down opens the selection (equivalent to Return), and in Single Window Browse mode Command+Left/Right/Up become Back/Forward/Up navigation with Command+L focusing the location bar.**  
SetSingleWindowBrowseShortcuts removes the Cmd+Up / Cmd+Opt+Up / Cmd+Opt+Down bindings and installs kNavigatorCommandBackward/Forward/Up on the Navigator (both with and without Option), Cmd+Opt+Down -> kOpenSelection, and 'L' -> kNavigatorCommandSetFocus. It is a no-op on the Desktop.  
`ContainerWindow.cpp` → `BContainerWindow::SetSingleWindowBrowseShortcuts; BContainerWindow::Init (AddShortcut(B_DOWN_ARROW, B_COMMAND_KEY, kOpenSelection))`  

#### PVK-042 · core · partial
**The standard Command shortcut set is registered on the window and targets the pose view: X/C/V clipboard, Shift+X/Shift+C cut-more/copy-more, F find, N new folder, O open, Ctrl+O open with, I get info, E edit name, D duplicate, K clean up (Shift+K clean up all), A select all, S invert selection, Shift+A Select..., G edit query, U unmount, +/- icon size, and Cmd+Opt+Shift+Ctrl+Q to quit Tracker.**  
BWindow::AddShortcut implies B_COMMAND_KEY unless B_NO_COMMAND_KEY is passed. The same list is added explicitly for the menu-less Desktop window (AddShortcuts()); Cmd+1 / Cmd+2 / Cmd+'+' / Cmd+'-' switch icon modes and sizes on the Desktop; Cmd+Z / Cmd+Shift+Z are undo/redo. A keymap scan adds the unshifted key that produces '+' as an extra zoom-in shortcut if free.  
`ContainerWindow.cpp` → `BContainerWindow::AddShortcuts; BContainerWindow::Init; TDeskWindow::Init (DeskWindow.cpp)`  
**TIKU:** Shortcut vocabulary is fine to keep, but any command touching a device node must be gated on the node's capability bits, not just on selection emptiness.  
**VERIFIER CORRECTION:** Plain Cmd+X / Cmd+C / Cmd+V are not AddShortcut entries at all — they are menu-item shortcuts created in Shortcuts.cpp (CutItem 'X', CopyItem 'C', PasteItem 'V') and targeted at the container window. AddShortcuts() (the menuless Desktop path) registers only Shift+X and Shift+C, plus F/N/O/I/E/D/K/A/S/Shift+A/G/U/Cmd+Up/Ctrl+O/'+'/'-' and Delete. The rest of the row (Shift+K, Cmd+Down, Cmd+Opt+Shift+Ctrl+Q, Cmd+Z/Shift+Z, keymap scan for an extra zoom-in key) is correct.  
**MISSING:** Only an ad-hoc subset is actually bound to keys — A, Shift+A, S, D, K, Z, F, W, Q, 1/2/3, +/- and Delete across the two shells — while X/C/V (and Shift+X/Shift+C), N, O, Ctrl+O, I, E, G, U, Shift+K and Cmd+Up are label-only entries in the action table with no key dispatcher behind them, and there is no keymap scan for an extra zoom-in key.  
*src/tiku_trk_app.c:handle_key lines 913-1035 and src/tiku_trk_desktop.c:2117-2159 (vs the full table in src/tiku_trk_shortcuts.c:tiku_trk_sc_key)*

#### PVK-043 · important · done
**Menu items relabel themselves live while Shift is held and fire a different command: Copy/Cut -> Copy more/Cut more, Paste -> Paste links, Close -> Close all, Clean up -> Clean up all, Create link -> Create relative link, Move to Trash -> Delete, Identify -> Force identify.**  
Each pair is a *Label()/*Command() method reading modifiers() at call time; TShortcuts::Update() rewrites item->SetLabel(), item->Message()->what and item->SetShortcut(..., B_COMMAND_KEY | (modifiers() & B_SHIFT_KEY)) for every item it recognises by command code. Identify rewrites the message's "force" bool instead of its what.  
`Shortcuts.cpp` → `TShortcuts::Update; TShortcuts::CopyLabel/CopyCommand, CutCommand, PasteCommand, CloseCommand, CleanupCommand, CreateLinkCommand, MoveToTrashCommand, IdentifyItem`  

#### PVK-044 · important · open
**Menu items enable/disable from live state: Copy needs a selection or a focused text view, Cut/Move to/Delete additionally need a writable volume and are off in root/Trash/virtual dirs, Paste needs clipboard refs and a writable non-Trash target, Unmount needs an unmountable (non-boot) volume selected, Open parent needs CanOpenParent(), Empty Trash needs a full Trash, Arrange by is only enabled outside list mode, Make active printer needs exactly one selection.**  
TShortcuts::Update* methods each call item->SetEnabled(...) plus item->SetTarget(PoseView() or fContainerWindow or be_app). Predicates: HasSelection(), SelectionIsReadOnly() (SelectedVolumeIsReadOnly), TargetIsReadOnly(), IsRoot/IsTrash/InTrash/IsQuery/IsVirtualDirectory, IsCurrentFocusOnTextView().  
`Shortcuts.cpp` → `TShortcuts::UpdateCutItem / UpdatePasteItem / UpdateCopyItem / UpdateMoveToItem / UpdateUnmountItem / UpdateOpenParentItem / UpdateEmptyTrashItem / UpdateArrangeByItem / UpdateMakeActivePrinterItem`  
**TIKU:** Read-only volume checks become capability-bit checks on the namespace node; "read-only" is per-node there, not per-volume.  

#### PVK-045 · core · done
**Command+A selects every pose (in the filtered set when filtering); the anchor for later Shift-extension becomes the first pose.**  
SelectAll() empties fSelectionList and the mime-type cache, nulls both pivots, then adds every pose of CurrentPoseList(), setting fSelectionPivotPose to the pose at index 0, and fires the selection-changed hook once at the end.  
`PoseView.cpp` → `BPoseView::SelectAll; BPoseView::MessageReceived (case B_SELECT_ALL)`  
**PORT:** select_all() now seeds the anchor unconditionally: `v->pivot = at(v,0)->index` whenever n > 0, no `pivot < 0` guard. Probed the exact failing path — click row 4, Select All, Shift+click row 2 — and the result is rows 0,1,2 selected (extension from the FIRST row), not 2..4. It still walks the current (filtered) list via count()/at(), so the filtered-set clause holds. Two residuals from the citation rather than the bold rule: real_pivot is not nulled (Haiku nulls both pivots), so a select-all done while the window is parked would be overwritten by window_activated() restoring the old anchor; and the previous selection is not cleared first, so under an active filter a pose selected before the filter was typed stays selected even though it is outside the current list.

#### PVK-046 · important · done
**Command+S inverts the selection: every previously selected pose is deselected and every unselected one is selected; both selection anchors are dropped.**  
InvertSelection() walks CurrentPoseList() toggling pose->Select() and adding/removing from fSelectionList; fSelectionPivotPose/fRealPivotPose are cleared first and only set if index 0 ends up selected. There is no 'select none' command in Tracker - clearing happens through ClearSelection() from clicks and from SelectPose().  
`PoseView.cpp` → `BPoseView::InvertSelection; BPoseView::MessageReceived (case kInvertSelection)`  

#### PVK-047 · important · differs
**Command+Shift+A opens the Select... dialog: a text expression matched as starts-with / ends-with / contains / glob / regexp, with an Ignore case checkbox (on by default) and an Invert selection option; it replaces the selection with all matching poses and re-activates the window.**  
SelectMatchingEntries reads "ExpressionType", "Expression", "InvertSelection", "IgnoreCase" from the message, forces SetMultipleSelection(true), clears the selection, and XORs each TrackerString::Matches() result with invertSelection. A bad regular expression pops a stop alert (closeable with Escape) and selects nothing. Window()->Activate() at the end is required for the Desktop case. Tracker also drives this path internally to reveal a ref in its parent window using an anchored "^name$" regexp.  
`PoseView.cpp` → `BPoseView::SelectMatchingEntries; SelectionWindow::MessageReceived / ExpressionType (SelectionWindow.cpp); TTracker::SelectRefs (Tracker.cpp)`  
**PORT:** The five modes, Ignore case on by default, Invert, and the re-activation of the target window are all there, and a bad expression selects nothing.  It is reported in the status line rather than a stop alert, and nothing yet drives this path internally to reveal a ref.

#### PVK-048 · core · open
**The pose view exposes a scripting suite, suite/vnd.Be-TrackerPoses, discoverable through GetSupportedSuites, with an interface described by a property_info table.**  
kPosesPropertyList declares Path, Entry and Selection with their allowed specifiers and result types; ResolveSpecifier matches against it and falls back to BView for anything else. Scripting messages are intercepted at the very top of BPoseView::MessageReceived, before the normal command switch.  
`PoseViewScripting.cpp` → `kPosesPropertyList; BPoseView::GetSupportedSuites; BPoseView::ResolveSpecifier; BPoseView::HandleScriptingMessage`  
**TIKU:** This is the natural seam onto a TikuOS shell/VFS control plane: the same get/set/count/create/delete/execute verbs over namespace paths.  

#### PVK-049 · important · open
**Scripting can read the window's folder: 'get Path' returns the target folder's entry_ref, or an error when the view has no directory.**  
kPropertyPath, B_GET_PROPERTY, B_DIRECT_SPECIFIER only; replies with "result" B_REF_TYPE or error B_NOT_A_DIRECTORY when TargetModel() is NULL.  
`PoseViewScripting.cpp` → `BPoseView::GetProperty (kPropertyPath branch)`  

#### PVK-050 · important · open
**Scripting can enumerate the listing: 'count Entry' returns the number of poses and 'get Entry' returns every entry_ref, an entry by index, or the entry before/after a given ref.**  
CountProperty replies with CurrentPoseList()->CountItems() (so it honours the active filter). GetProperty handles B_DIRECT_SPECIFIER, B_INDEX_SPECIFIER (B_BAD_INDEX when out of range) and kPreviousSpecifier/kNextSpecifier ('sprv'/'snxt'), which also return an "index" field.  
`PoseViewScripting.cpp` → `BPoseView::CountProperty; BPoseView::GetProperty (kPropertyEntry branch); BPoseView::FindPose(const entry_ref*, int32 specifierForm, int32*)`  

#### PVK-051 · core · open
**Scripting can read and rewrite the selection: 'count Selection', 'get Selection' (all, or the selected item before/after a ref), 'set Selection' to a list of refs or to an index range, 'create Selection' to add to it, and 'delete Selection' to remove refs or indices from it.**  
Set with two int32 "data" values calls SelectPoses(selStart, selEnd) - the loop is `index < end`, so the end index is EXCLUSIVE even though both bounds are range-checked as valid indices (out of range -> B_BAD_INDEX). Set with refs calls SelectPose for the first (which clears the old selection) and AddPoseToSelection for the rest. create/delete never clear. get Selection previous/next walks poses from the given ref until it finds a selected one, returning B_ENTRY_NOT_FOUND when it runs off the end.  
`PoseViewScripting.cpp` → `BPoseView::SetProperty; BPoseView::CreateProperty; BPoseView::DeleteProperty; BPoseView::GetProperty (kPropertySelection branch); BPoseView::SelectPoses(int32,int32) (PoseView.cpp)`  

#### PVK-052 · important · open
**'execute Entry' opens the specified entries (by path or index) exactly as a double-click would, and 'delete Entry' moves them to the Trash rather than deleting them.**  
ExecuteProperty builds a B_REFS_RECEIVED message and posts it to fSelectionHandler (be_app by default). DeleteProperty builds a BObjectList<entry_ref> and calls MoveListToTrash(entryList, false, false) - the list is owned and freed by the trashing thread; a bad index aborts and frees the list instead.  
`PoseViewScripting.cpp` → `BPoseView::ExecuteProperty; BPoseView::DeleteProperty (kPropertyEntry branch)`  
**TIKU:** With no Trash, 'delete Entry' must map to an explicit destructive verb, and the async ownership hand-off needs a TikuOS equivalent (worker or deferred queue).  

#### PVK-053 · polish · open
**Every launch/open message carries a "TrackerViewToken" messenger so the application being launched can send scripting messages straight back to the originating pose view.**  
Added in ExecuteProperty, OpenSelectionCommon, and the drag-and-drop / context-menu paths; TTracker::RefsReceived re-attaches it to bundled messages, and the file panel uses HasMessenger("TrackerViewToken") to tell pose-view-originated messages apart.  
`PoseViewScripting.cpp` → `BPoseView::ExecuteProperty (launchMessage.AddMessenger("TrackerViewToken", ...)); BPoseView::OpenSelectionCommon (PoseView.cpp)`  

#### PVK-054 · important · open
**Scripting failures are reported in-band: the reply always carries an "error" int32 when the operation failed, and unhandled specifiers leave the message for the default BView handling rather than replying.**  
Each of Create/Delete/Get/Set/Execute/CountProperty ends with `if (result != B_OK) reply->AddInt32("error", result);`, and HandleScriptingMessage only SendReply()s when handled is true, returning false otherwise so MessageReceived continues into the normal switch.  
`PoseViewScripting.cpp` → `BPoseView::HandleScriptingMessage and the *Property methods`  

#### PVK-055 · important · open
**Scripting indices are documented as unstable: they follow the current sort order and are arbitrary in icon mode, and any add/remove/attribute change can invalidate them - clients must tolerate a stale index or ref.**  
Stated in the file header comment; enforced by PoseAtIndex/IndexOfPose routing through CurrentPoseList() and by every index path returning B_BAD_INDEX / B_ENTRY_NOT_FOUND instead of misbehaving.  
`PoseViewScripting.cpp` → `file header comment block ("notes on PoseView scripting interface"); BPoseView::PoseAtIndex / IndexOfPose (PoseView.h)`  
**TIKU:** Namespace paths are stable where BFS indices were not - the rewrite can offer a path-addressed API and keep indices only for compatibility.  

#### PVK-056 · polish · done
**The type-ahead search string is shared by every pose view in the application, while the timeout clock is per-view, and switching a window to a new folder resets that clock.**  
`BString BPoseView::sMatchString` is a static class member (declared in PoseView.h); fLastKeyTime is a per-instance member, zeroed at the end of SwitchDir().  
`PoseView.cpp` → `BPoseView::sMatchString definition (end of file); BPoseView::SwitchDir (fLastKeyTime = 0)`  
**TIKU:** A single-window TikuOS browser makes the shared static harmless, but if two views can exist the state should be per-view.  


---

## PoseView: node monitoring and live updates

Architecture notes: [`spec/ARCH-poseview-live.md`](spec/ARCH-poseview-live.md)

#### PVN-001 · core · done
**Node monitor, path monitor and live-query notifications all reach the view through a single handler, so a query window updates by exactly the same code path as a folder window.**  
BPoseView::MessageReceived collapses `case B_NODE_MONITOR: case B_PATH_MONITOR: case B_QUERY_UPDATE:` into one call to FSNotification(message).  
`PoseView.cpp` → `BPoseView::MessageReceived (case B_NODE_MONITOR / B_PATH_MONITOR / B_QUERY_UPDATE)`  
**TIKU:** TikuOS watch posts TIKU_EVENT_VFS with data = const tiku_vfs_node_t* and no opcode -- 'touched, re-read'. Either the VFS watch record must be extended to carry created/removed/moved/changed + name, or the view must diff a re-listing on every event. Decide before anything else; every row below depends on it.  

#### PVN-002 · core · done
**A notification naming a node the view does not know about yet is not dropped -- it is parked and replayed later, so a file changed the instant after it was created still shows the change.**  
If FSNotification() returns false the whole BMessage is copied into pendingNodeMonitorCache via Add(), keyed on device+node.  
`PoseView.cpp` → `BPoseView::MessageReceived (case B_NODE_MONITOR), PendingNodeMonitorCache::Add`  
**TIKU:** Requires the event to be replayable, i.e. self-describing. A bare 'node touched' ping cannot be parked usefully -- park the node ref and re-read on replay instead.  

#### PVN-003 · polish · done
**A parked notification is discarded if the pose never appears within 10 seconds.**  
kDelayedNodeMonitorLifetime = 10000000 us; TooOld() is checked on every Add-adjacent sweep and inside PoseCreatedOrMoved.  
`PendingNodeMonitorCache.cpp` → `kDelayedNodeMonitorLifetime / PendingNodeMonitorEntry::TooOld`  
**VERIFIER CORRECTION:** PendingNodeMonitorCache::Add() does no sweeping, and PendingNodeMonitorCache::RemoveOldEntries() / RemoveEntries() are never called from anywhere in the tree (only definitions exist). Expiry is checked exclusively inside PoseCreatedOrMoved, so a parked message for a node that never appears is only freed when some *other* pose is created or renamed. kDelayedNodeMonitorLifetime = 10000000 µs is correct.  

#### PVN-004 · important · done
**When a pose is finally created or renamed into place, every parked notification for that node is replayed in arrival order before the view continues.**  
PoseCreatedOrMoved walks the list, removes matching entries and re-enters poseView->FSNotification(item->NodeMonitor()); called from EntryMoved after a successful rename/zombie conversion.  
`PendingNodeMonitorCache.cpp` → `PendingNodeMonitorCache::PoseCreatedOrMoved (called from BPoseView::EntryMoved)`  

#### PVN-005 · core · differs
**Opening a folder installs one watch covering the directory and all its children -- creation, deletion, rename, stat and attribute changes -- so no per-file watch is needed for a normal folder.**  
Mask is B_WATCH_DIRECTORY | B_WATCH_CHILDREN | B_WATCH_NAME | B_WATCH_STAT | B_WATCH_INTERIM_STAT | B_WATCH_ATTR, installed on the source model's node_ref.  
`PoseView.cpp` → `BPoseView::InitDirentIterator`  
**TIKU:** TikuOS tiku_vfs_watch() is per-node with only TIKU_VFS_WATCH_MAX (8) slots and no children flag. A directory-subtree watch must be added, or the view must poll -- 8 slots cannot cover a folder listing.  
**PORT:** Whole-folder liveness with no per-file subscriptions is achieved by the route the row's own TIKU note names: poll asks the device's change ring once per tick for the whole desktop, then re-lists the directory and diffs it, and the diff reports creation, removal, rename (paired by node_id) and fact/attribute changes.  
*src/tiku_trk_poseview.c:2804 tiku_trk_poseview_poll (ring first, then re-list); src/tiku_trk_notify.c:106-161 tiku_trk_notify_diff (CREATED/REMOVED/MOVED/CHANGED, facts_differ :78-90); wired at src/tiku_trk_desktop.c:2261-2291*

#### PVN-006 · important · open
**Per-entry watches are only established where the container watch cannot reach: symlink targets outside the folder, volume poses on the Desktop, the Trash icon, and the Open-With list.**  
BPoseView::WatchNewNodeMask() returns 0 ('B_WATCH_CHILDREN suffices'); BQueryPoseView::WatchNewNodeMask() returns 0 ('B_QUERY_WATCH_ALL suffices'); OpenWithPoseView::WatchNewNodeMask() returns B_WATCH_STAT|B_WATCH_INTERIM_STAT|B_WATCH_ATTR.  
`PoseView.cpp` → `BPoseView::WatchNewNodeMask / BQueryPoseView::WatchNewNodeMask / OpenWithPoseView::WatchNewNodeMask`  
**TIKU:** With only 8 watch slots the per-entry escape hatch is unavailable; symlink-target and volume-pose freshness needs a different mechanism.  

#### PVN-007 · core · differs
**A node is watched BEFORE its model is read, so a change that lands while the entry is being read still produces a notification and the row is never stale.**  
Both AddPosesTask and EntryCreated call WatchNewNode(&itemNode, ...) with the comment 'have to node monitor ahead of time because Model will cache up the file type and preferred app'.  
`PoseView.cpp` → `BPoseView::AddPosesTask / BPoseView::EntryCreated`  
**TIKU:** Same ordering rule applies: tiku_vfs_watch() the node before reading its descriptor, or a descriptor change during the read is lost.  
**PORT:** The guarantee is met by an always-on recorder plus a strict ordering rather than a per-node watch: the ring is drained before anything is re-read, and because the device records changes whether or not anyone is asking, a change landing during the listing read stays in the ring and is reported on the next drain, so no row goes stale.  
*src/tiku_trk_poseview.c:2827-2840 (changes consulted before any re-read); src/tiku_trk_backend_tiku.c:163-186 tk_changes; /home/ambuj/tikuOS/applications/desktop/src/tiku_desk_ns.c:242 tiku_desk_ns_events (the read drains the device ring)*

#### PVN-008 · core · open
**Beyond the folder watch, the view also subscribes to all volume mount/unmount events, to attribute changes on the folder itself, and to MIME database changes.**  
StartWatching(): TTracker::WatchNode(NULL, B_WATCH_MOUNT, this); TTracker::WatchNode(targetModel->NodeRef(), B_WATCH_ATTR, this); BMimeType::StartWatching(BMessenger(this)).  
`PoseView.cpp` → `BPoseView::StartWatching`  
**TIKU:** 'Volume mounted' maps to a device node appearing in the TikuOS device namespace; there is no mount opcode, so this becomes a watch on the namespace root's children.  

#### PVN-009 · core · differs
**Tearing down a view drops every watch it holds in one call, so switching folders must re-establish all of them from scratch.**  
StopWatching() is stop_watching(this) + BMimeType::StopWatching; SwitchDir and Refresh call StopWatching then StartWatching.  
`PoseView.cpp` → `BPoseView::StopWatching / BPoseView::SwitchDir / BPoseView::Refresh`  
**TIKU:** TikuOS already has this hazard: rules_rearm() calls unwatch_all() wholesale, so a pose view sharing the subscriber process must re-subscribe idempotently every tick (the `watch` shell command already does this).  
**PORT:** There is a real one-call wholesale teardown of everything the view was holding for the old folder — park_clear plus zombie_count = 0 — followed by a from-scratch reload, which is the polling-design equivalent of dropping every watch and re-establishing.  
*src/tiku_trk_poseview.c:533-547 tiku_trk_poseview_switch_dir (park_clear, zombie_count = 0, reload); go_up routes through it at :569*

#### PVN-010 · important · differs
**Running out of kernel watch slots does not silently stop live updates -- Tracker raises its own limit and retries the watch once.**  
TTracker::WatchNode retries after NeedMoreNodeMonitors(), which bumps fNodeMonitorCount by kNodeMonitorBumpValue (512) via setrlimit(RLIMIT_NOVMON); only B_NO_MEMORY triggers the retry.  
`Tracker.cpp` → `TTracker::WatchNode / TTracker::NeedMoreNodeMonitors`  
**TIKU:** No analogue: TikuOS's watch table is a fixed 8-slot static array. Exhaustion must either fail loudly or fall back to polling -- silently losing liveness is the failure mode to avoid.  
**PORT:** The port has no watch table to exhaust, and the analogous delivery resource does fail loudly rather than silently: a moved drop counter clears `complete`, which forces a wholesale re-list, and an overflowed event batch triggers a full reload — the loud-or-poll answer the row's TIKU note demands.  
*src/tiku_trk_backend_tiku.c:183-188 (dropped != last_dropped -> *complete = 0) with the local over-max drop count at desktop/src/tiku_desk_ns.c:285-295; src/tiku_trk_poseview.c:2843 (relist = !complete) and :2979-2984 (evs.overflowed -> poseview_reload)*

#### PVN-011 · core · done
**A file created in some other folder does not add a row: creation notices for a directory other than the one on screen are ignored.**  
In case B_ENTRY_CREATED, createPose is forced false when dirNode != *targetModel->NodeRef(); exceptions are query views, virtual directories, the root window, and the Desktop volumes-root when ShowDisksIcon is off. If no broken links are pending the case breaks outright.  
`PoseView.cpp` → `BPoseView::FSNotification (case B_ENTRY_CREATED)`  

#### PVN-012 · core · done
**A duplicate creation notice never produces a second row for the same file.**  
EntryCreated returns NULL immediately if fPoseList->FindPose(itemNode) or FindZombie(itemNode) hits.  
`PoseView.cpp` → `BPoseView::EntryCreated`  

#### PVN-013 · important · done
**A newly created entry marked invisible in its saved pose info is silently not shown, and its watch is released again.**  
if (!PoseVisible(model, &poseInfo)) { watch_node(model->NodeRef(), B_STOP_WATCHING, this); delete model; return NULL; }  PoseVisible is `return !poseInfo->fInvisible`.  
`PoseView.cpp` → `BPoseView::EntryCreated / BPoseView::PoseVisible`  
**TIKU:** fInvisible is a BFS attribute (kAttrPoseInfo). In TikuOS this is a per-node capability/visibility bit on the typed descriptor, or a per-folder view-state record; decide which.  

#### PVN-014 · important · done
**A file whose model cannot be initialized (busy, mid-write, transient error) is not lost -- it is held as a 'zombie' and promoted to a real row as soon as any later notification proves it readable.**  
Failed Model goes to fZombieList in both AddPosesTask and EntryCreated; ConvertZombieToPose (called from AttributeChanged when no pose matched, and from EntryMoved on rename) does UpdateStatAndOpenNode then CreatePose. FindZombie scans fZombieList by node_ref.  
`PoseView.cpp` → `BPoseView::EntryCreated / BPoseView::ConvertZombieToPose / BPoseView::FindZombie`  
**TIKU:** TikuOS VFS reads are synchronous against a static node table -- there is no mimeset-style transient failure. The zombie tier may collapse to a single retry, but a node whose descriptor read fails must still not vanish from the listing.  
**PORT:** An entry the backend cannot describe (KIND_UNKNOWN) is held in v->zombie[] rather than shown or dropped, on both the initial load and the live re-list path, and the moment any later listing describes it the hold is released and the model enters the diff set, where it surfaces as a creation and becomes a real row.  
*src/tiku_trk_poseview.c:217-243 collect (hold + release) and :2747-2789 relist_one (same hold, promote into r->set for tiku_trk_notify_diff)*

#### PVN-015 · core · done
**Deleting the folder that a window is showing closes that window, and its position is deliberately not saved.**  
case B_ENTRY_REMOVED: when *targetModel->NodeRef() == itemNode, DisableSaveLocation() then Window()->Close().  
`PoseView.cpp` → `BPoseView::FSNotification (case B_ENTRY_REMOVED)`  

#### PVN-016 · polish · done
**The root ('/') window is exempted from the auto-close: it stays open when a removal notice names its node.**  
Guarded by `if (!targetModel->IsRoot())` -- the comment records that in / the notification is ambiguous because the vnode is the volume's but the device is the parent's, which aliases volumes with vnode 1 (iso9660).  
`PoseView.cpp` → `BPoseView::FSNotification (case B_ENTRY_REMOVED)`  

#### PVN-017 · important · partial
**A query window is not closed when a result stops matching the query -- only when the query file itself is deleted.**  
The close branch additionally requires message->what == B_NODE_MONITOR; a B_QUERY_UPDATE removal only means 'no longer matches'.  
`PoseView.cpp` → `BPoseView::FSNotification (case B_ENTRY_REMOVED)`  
**MISSING:** The 'never close on stop-matching' clause holds -- query refresh goes through apply_results/notify which only removes the row, and poll returns early for is_query so GONE is never raised for a query view -- but the second clause is missing: nothing closes a query window when the saved query itself is deleted (tiku_trk_query_forget has no link to any open window).  
*src/tiku_trk_poseview.c:2818-2828 (poll: is_query early return) and src/tiku_trk_desktop.c:554 refresh_results*

#### PVN-018 · important · done
**Deleting the target of a symlink that is on screen turns that row into a broken-link icon instead of removing it.**  
When FindPose misses, DeepFindPose matches poses whose model->LinkTo() is the removed node; DeleteSymLinkPoseTarget then stops watching the target, calls WatchParentOf() on the link, clears SetLinkTo(NULL) and repaints via UpdateBrokenSymLink.  
`PoseView.cpp` → `BPoseView::FSNotification (case B_ENTRY_REMOVED) / BPoseView::DeleteSymLinkPoseTarget / PoseList::DeepFindPose`  
**TIKU:** A device-namespace 'link' (alias node) losing its target is the analogue; needs a defined broken state in the typed descriptor rather than node removal.  

#### PVN-019 · core · done
**Removing a row stops its watch, drops it from the selection, clears it as drop target / active pose / selection pivot, closes the gap in the list, and updates the item count and scroll range.**  
DeletePose: watch_node(B_STOP_WATCHING), fInsertedNodes.Remove, fSelectionList->RemoveItem, fSelectionPivotPose/fRealPivotPose nulled, CommitActivePose if active, CloseGapInList/RemoveFromExtent, UpdateCount, UpdateScrollRange, ResetPosePlacementHint.  
`PoseView.cpp` → `BPoseView::DeletePose`  

#### PVN-020 · polish · done
**If deleting the last visible row would leave an empty window, the list scrolls up by one row instead.**  
In kListMode, after removal, if CurrentPoseList()->ItemAt(bounds.bottom / fListElemHeight) is NULL and bounds.top > 0, ScrollTo(max(bounds.top - fListElemHeight, 0)).  
`PoseView.cpp` → `BPoseView::DeletePose`  

#### PVN-021 · polish · done
**A removal notice for a file that only ever existed as a zombie deletes the zombie rather than being ignored.**  
DeletePose's else branch: FindZombie(itemNode) then fZombieList->RemoveItemAt(index).  
`PoseView.cpp` → `BPoseView::DeletePose`  
**PORT:** The REMOVED branch, when pose_index() misses, walks the zombie array by path, compacts the entry out and reports a redraw — the exact FindZombie/RemoveItemAt behaviour the row states.  
*src/tiku_trk_poseview.c:2545-2566 (case TIKU_TRK_EV_REMOVED, 'It may only ever have been a zombie; that still goes.')*

#### PVN-022 · core · done
**Renaming a file in place re-reads every column of that row, not just the name, because a rename implies other change notices may have been missed.**  
EntryMoved, oldDir == dirNode.node branch: poseModel->UpdateEntryRef then, with the node open, pose->UpdateAllWidgets(index, loc, this) -- comment: 'if we get a rename then we need to assume that we might have missed some other attr changed notifications so we recheck all widgets'.  
`PoseView.cpp` → `BPoseView::EntryMoved`  

#### PVN-023 · important · done
**After a rename the row moves to its correct sorted position without a full re-sort of the folder.**  
_CheckPoseSortOrder(fPoseList, pose, poseListIndex) and, when filtering and visible, _CheckPoseSortOrder(fFilteredPoseList, pose, index). _CheckPoseSortOrder removes, BSearches, blits the gap and reinserts; it is a no-op outside kListMode.  
`PoseView.cpp` → `BPoseView::EntryMoved / BPoseView::_CheckPoseSortOrder`  

#### PVN-024 · important · partial
**Renaming the folder a window is showing updates the window title live.**  
EntryMoved: if thisDirNode == itemNode, targetModel->UpdateEntryRef(&dirNode, name) then ContainerWindow()->UpdateTitle().  
`PoseView.cpp` → `BPoseView::EntryMoved`  
**MISSING:** The retitle rule is implemented in the handler (a MOVED notice naming the view's own path adopts the new path and returns RETITLE, and desktop.c rewrites win->title/fl->path), but no live path ever derives such a notice: poll re-lists v->path, the listing fails, stat fails and the window is closed with GONE, so a real folder rename closes the window instead of retitling it.  
*src/tiku_trk_poseview.c:2450-2463 (RETITLE) vs src/tiku_trk_poseview.c:2960-2972 (failed listing -> GONE); src/tiku_trk_desktop.c:2300*

#### PVN-025 · polish · open
**Renaming a mounted volume is handled specially: the notification's device is the parent's, so the volume's real device and inode are recovered by stat()ing the new name under /.**  
EntryMoved stats '/' and, if the notice's dirNode matches it, stats "/" + name and overwrites itemNode.node/.device from that stat.  
`PoseView.cpp` → `BPoseView::EntryMoved`  

#### PVN-026 · core · done
**Moving a file out of the displayed folder removes its row; moving one in adds a row.**  
EntryMoved tail: `else if (oldDir == thisDirNode.node) DeletePose(&itemNode); else if (dirNode.node == thisDirNode.node) EntryCreated(&dirNode, &itemNode, name);`  
`PoseView.cpp` → `BPoseView::EntryMoved`  

#### PVN-027 · important · open
**The Trash window shows the union of every volume's trash, so a move into any volume's trash appears there.**  
EntryMoved resolves thisDirNode via FSGetTrashDir(&trashDir, itemNode.device) when TargetModel()->IsTrash(), instead of using the target model's node_ref.  
`PoseView.cpp` → `BPoseView::EntryMoved`  

#### PVN-028 · important · done
**In query and virtual-directory windows every move is treated as an in-place rename, because 'the folder' has no single parent directory.**  
EntryMoved's first branch condition is `oldDir == dirNode.node || targetModel->IsQuery() || targetModel->IsVirtualDirectory()`.  
`PoseView.cpp` → `BPoseView::EntryMoved`  
**TIKU:** Directly relevant: a TikuOS view merging device-namespace nodes with local files has the same 'no single parent' property and should take this branch.  

#### PVN-029 · important · open
**In a file panel, moving the folder being browsed re-points the panel at the new location rather than closing it; deleting it falls back to the boot volume's root.**  
TFilePanel::FSFilter, installed as a B_NODE_MONITOR common filter: case B_ENTRY_MOVED updates the target model ref and calls SwitchDirectory, returning B_SKIP_MESSAGE; case B_ENTRY_REMOVED switches to the boot root.  
`FilePanelPriv.cpp` → `TFilePanel::FSFilter`  

#### PVN-030 · core · done
**Stat changes and attribute changes are handled by one routine, so a size change and an attribute edit update a row identically.**  
`case B_STAT_CHANGED: case B_ATTR_CHANGED: return AttributeChanged(message);` -- the two are distinguished inside only by the presence of the "attr" string.  
`PoseView.cpp` → `BPoseView::FSNotification (case B_STAT_CHANGED / B_ATTR_CHANGED)`  
**TIKU:** BFS attribute name + type becomes a typed descriptor field id; the 'attr present or not' discriminator must be replaced by an explicit field selector.  

#### PVN-031 · polish · differs
**A file that is busy (being mimeset) is retried for up to one second rather than skipped, so the freshly-sniffed type shows up.**  
AttributeChanged loops up to 100 times calling poseModel->OpenNode(), breaking on anything other than B_BUSY, snooze(10000) between tries.  
`PoseView.cpp` → `BPoseView::AttributeChanged`  
**TIKU:** No B_BUSY equivalent on a static VFS node table; this retry loop can be dropped -- but note it is a blocking snooze on the window thread today, which a cooperative protothread rewrite cannot copy verbatim.  
**PORT:** A real alternative retry exists rather than an absence: collect() holds a KIND_UNKNOWN entry in the zombie array instead of skipping it, and relist_one() strips it from the hold and feeds it to the diff the moment a later listing describes it, which is the row's 'retried, not skipped' outcome without the blocking snooze the TIKU note waives.  
*src/tiku_trk_poseview.c:216-240 (collect, zombie hold) with src/tiku_trk_poseview.c:2747-2785 (relist_one promotion)*

#### PVN-032 · important · done
**One attribute change updates every row that refers to that node, including symlink rows pointing at it.**  
AttributeChanged iterates fPoseList->FindAllPoses(&itemNode), which matches direct node_ref, LinkTo() node_ref, and (for unresolved links) a freshly resolved Model; symlink poses redirect the update to poseModel->ResolveIfLink().  
`PoseView.cpp` → `BPoseView::AttributeChanged / PoseList::FindAllPoses`  

#### PVN-033 · polish · open
**Deleting an icon attribute reverts the row to its generic icon instead of leaving the stale custom icon painted.**  
When Model::IconAttrChanged(attrName) is true the info.type is synthesized by name (kAttrIcon->B_VECTOR_ICON_TYPE, kAttrLargeIcon->'ICON', kAttrMiniIcon->'MICN', kAttrThumbnail->B_RAW_TYPE) with info.size = 0, because GetAttrInfo would fail on a removed attribute.  
`PoseView.cpp` → `BPoseView::AttributeChanged`  

#### PVN-034 · important · differs
**A row is re-sorted after a change only when the changed field is actually the primary or secondary sort key.**  
For attribute changes, AttrHashString(attrName, info.type) is compared against PrimarySort()/SecondarySort(). For stat changes the "fields" int32 is matched against sAttrColumnMap, which maps kAttrStatModified->B_STAT_MODIFICATION_TIME, kAttrStatSize->B_STAT_SIZE, kAttrStatCreated->B_STAT_CREATION_TIME, kAttrStatMode->B_STAT_MODE. A stat message with no "fields" causes no re-sort at all.  
`PoseView.cpp` → `BPoseView::AttributeChanged / sAttrColumnMap`  
**TIKU:** The attribute-hash trick is explicitly noted in-source as aliasing-prone ('this sort of hashing may overlap'). With typed descriptors, use field ids and drop the hash.  
**PORT:** An actual mechanism is pointed at — after a CHANGED/ATTR notice the row's sorted_slot() is recomputed and remove+insert runs only when the slot actually moved — so a change to a non-sort-key field leaves the row in place, the same visible rule decided by resulting position instead of by AttrHashString/sAttrColumnMap field identity.  
*src/tiku_trk_poseview.c:2656-2685 (sorted_slot compare, 'if (slot != at && slot != at + 1)')*

#### PVN-035 · polish · open
**When a file's MIME type changes, the window's type-dependent menus (Open With, add-ons) are rebuilt.**  
AttributeChanged: `if (strcmp(attrName, kAttrMIMEType) == 0) RefreshMimeTypeList();`  
`PoseView.cpp` → `BPoseView::AttributeChanged / BPoseView::RefreshMimeTypeList`  

#### PVN-036 · polish · open
**Changing the folder's own icon updates the draggable folder icon in that window's title bar area immediately.**  
AttributeChanged: when the notice names the target model and ShouldHaveDraggableFolderIcon() and targetModel->AttrChanged(attrName), it finds "MenuBar"/"DraggableContainerIcon", calls IconCache::sIconCache->IconChanged(targetModel) and Invalidate().  
`PoseView.cpp` → `BPoseView::AttributeChanged`  

#### PVN-037 · important · differs
**An attribute notice for a file that has no row but is a zombie promotes it to a real row -- the mechanism by which files that were unreadable at load time eventually appear.**  
When FindAllPoses returns an empty list, AttributeChanged falls back to FindZombie + ConvertZombieToPose (source cites bug #4130).  
`PoseView.cpp` → `BPoseView::AttributeChanged`  
**PORT:** Promotion of a held (zombie) entry to a real row genuinely happens and is driven by the notice: an ATTR/CHANGED record for a node the view does not hold sets relist=1, the re-list's relist_one() removes it from the zombie hold once describable, and the diff emits it as a creation — a different route to ConvertZombieToPose, not its absence.  
*src/tiku_trk_poseview.c:2886-2935 (unheld node -> relist) with src/tiku_trk_poseview.c:2747-2785 (relist_one) and :2645-2650 (park)*

#### PVN-038 · important · done
**A change can make a row appear in or disappear from a filtered (type-ahead or ref-filtered) view without touching the underlying folder listing.**  
AttributeChanged and EntryMoved both re-test FilterPose(pose) and then either AddPoseToList(fFilteredPoseList, ...) or RemoveFilteredPose(pose, index); the two lists carry separate indices (poseListIndex vs index).  
`PoseView.cpp` → `BPoseView::AttributeChanged / BPoseView::EntryMoved`  

#### PVN-039 · polish · done
**In a query window, changes to last_modified or size are treated as stat changes rather than attribute changes so the Size/Modified columns update.**  
BQueryPoseView::AttributeChanged copies the message and RemoveName("attr") when attrName is "last_modified" or "size", then delegates to BPoseView::AttributeChanged.  
`QueryPoseView.cpp` → `BQueryPoseView::AttributeChanged`  

#### PVN-040 · important · open
**Mounting a volume adds its icon to the Desktop / Disks window live, honoring the disks-icon and shared-volume settings; a Trash window additionally gains that volume's trashed items.**  
case B_DEVICE_MOUNTED: CreateVolumePose when targetModel->IsRoot(), or when IsVolumesRoot() && MountVolumesOntoDesktop() && (!volume.IsShared() || MountSharedVolumesOntoDesktop()); if IsTrash(), FSGetTrashDir for the new device and AddPoses(&model).  
`PoseView.cpp` → `BPoseView::FSNotification (case B_DEVICE_MOUNTED)`  
**TIKU:** In TikuOS the device namespace is the mount analogue: a device node appearing must add a row in the same live way, gated by a capability bit rather than volume.IsPersistent().  

#### PVN-041 · polish · open
**Only persistent volumes that are not mounted inside another persistent volume get an icon.**  
CreateVolumePose returns early if !volume->IsPersistent(), and again if BVolume(ref.device) of the mount point is itself persistent (comment: 'the file system root, /pipe, /dev, etc. are all non-persistent').  
`PoseView.cpp` → `BPoseView::CreateVolumePose`  

#### PVN-042 · polish · open
**Broken-symlink repair after a mount is deliberately delayed half a second so that a link to a volume resolves as a volume rather than a plain directory.**  
taskLoop->RunLater(NewMemberFunctionObject(&BPoseView::TryUpdatingBrokenLinks, this), 500000) at the end of case B_DEVICE_MOUNTED.  
`PoseView.cpp` → `BPoseView::FSNotification (case B_DEVICE_MOUNTED)`  

#### PVN-043 · core · open
**Unmounting the volume a window is showing closes that window (a file panel jumps to home instead); unmounting any other volume removes that volume's rows and breaks symlinks pointing into it.**  
case B_DEVICE_UNMOUNTED: if targetModel device matches -> IsFilePanel() ? PostMessage(kSwitchToHome) : DisableSaveLocation()+Window()->Close(); else EachPoseAndModel(fPoseList, &PoseHandleDeviceUnmounted, this, device), which DeletePose()s matching models and DeleteSymLinkPoseTarget()s links whose LinkTo() is on that device.  
`PoseView.cpp` → `BPoseView::FSNotification (case B_DEVICE_UNMOUNTED) / BPoseView::PoseHandleDeviceUnmounted`  
**TIKU:** Device-node removal must produce the same two-part behavior: close views rooted at the departed device, and mark aliases into it as broken.  

#### PVN-044 · important · partial
**A symlink whose target does not exist is registered as broken and the nearest existing ancestor of its target path is watched, so the link repairs itself the moment the missing folder is created.**  
CreateSymlinkPoseTarget adds to fBrokenLinks and calls WatchParentOf(); WatchParentOf walks MakeLinkedPath()->GetParent() up past B_BAD_VALUE and watch_node(&nref, B_WATCH_DIRECTORY, this) unless it is the target model itself.  
`PoseView.cpp` → `BPoseView::CreateSymlinkPoseTarget / BPoseView::WatchParentOf`  
**TIKU:** Path-prefix watching has no VFS equivalent; TikuOS paths are static, so this whole subsystem may reduce to 'target node absent' checked on the parent's watch event.  
**MISSING:** The registration clause is implemented (facts.link_broken set by the backend, 'Broken link' kind, included in facts_differ so a re-list flips the row back) but the row's second clause — watching the nearest existing ancestor of the target path so the link repairs the moment the folder appears — has no implementation at all; repair only comes from the next periodic re-list, which is absence of the watch rather than another form of it.  
*src/tiku_trk_backend_posix.c:86 (facts.link_broken) and src/tiku_trk_notify.c:87 (facts_differ); grep for a broken-link registry/watch across src returns nothing but these fact sites*

#### PVN-045 · polish · open
**When a directory is created, any pending broken link whose target path begins with that new directory switches its watch down one level, incrementally converging on the missing target.**  
case B_ENTRY_CREATED, when fBrokenLinks is non-empty and the created node is a directory: for each broken link, MakeLinkedPath, prefix-compare against the created path (rejecting a partial component match via pathStr[createdPath.Length()] != '/'), then StopWatchingParentsOf(link) + watch_node(&itemNode, B_WATCH_DIRECTORY, this).  
`PoseView.cpp` → `BPoseView::FSNotification (case B_ENTRY_CREATED) / BPoseView::StopWatchingParentsOf`  
**CHECKED:** There is no pending-broken-link list, no created-directory prefix comparison and no watch that descends a level — the auditor's own sentence concedes the mechanism does not exist and substitutes the generic 3-second/400 ms re-list, which is not an alternative way of moving a watch down one level.  
*src/tiku_trk_poseview.c:2940-2977 (generic poll re-list + diff); no fBrokenLinks-equivalent anywhere in src (grep 'broken' hits only model facts)*

#### PVN-046 · important · done
**Every create, remove and move re-evaluates all symlink rows, so links that just became valid or just broke repaint immediately.**  
TryUpdatingBrokenLinks() is called at the end of the CREATED, REMOVED and MOVED paths; it runs UpdateWasBrokenSymlinkBinder over fPoseList (which calls BPose::UpdateWasBrokenSymlink -> CreateSymlinkPoseTarget or stop-watching the vanished target) and then StopWatchingParentsOf every link that left fBrokenLinks.  
`PoseView.cpp` → `BPoseView::TryUpdatingBrokenLinks / UpdateWasBrokenSymlinkBinder`  

#### PVN-047 · core · open
**A folder's contents stream in progressively -- rows appear in batches while a barber pole spins, rather than the window staying blank until the whole directory is read.**  
AddPosesTask ships a chunk when it has kMaxAddPosesChunk (50) models or 300000 us have passed since the last chunk, whichever first, as a kAddNewPoses message; snooze(500) between chunks 'be nice'. ShowBarberPole on AddPoses, HideBarberPole in AddPosesCompleted (kAddPosesCompleted).  
`PoseView.cpp` → `BPoseView::AddPosesTask / kMaxAddPosesChunk / BPoseView::AddPoses / BPoseView::AddPosesCompleted`  
**TIKU:** TikuOS is cooperative protothreads: the loader must be a yielding protothread, not a spawned thread with a per-chunk window lock. Watch the known trap -- protothread locals do not survive a yield, so the chunk cursor must live in the process struct.  

#### PVN-048 · core · partial
**Switching a window to a different folder makes the in-flight loader abandon its work at the next chunk, and any chunk that still arrives for the old folder is discarded rather than mixed into the new listing.**  
SwitchDir/Refresh/BQueryPoseView::Refresh clear fAddPosesThreads; AddPosesTask checks IsValidAddPosesThread(threadID) under the lock each chunk and throws failToLock; MessageReceived's kAddNewPoses case only calls CreatePoses if AddPosesThreadValid(&ref) (ref equals the target's entry_ref, or the view is Trash; DesktopPoseView overrides it to always true).  
`PoseView.cpp` → `BPoseView::IsValidAddPosesThread / BPoseView::AddPosesThreadValid / BPoseView::SwitchDir`  
**MISSING:** The discard clause is real (switch_dir clears park and zombies, and CREATED notices whose e->dir is not the view's path are dropped), but there is no chunked or in-flight loader to abandon — reload() fills the pose array in one synchronous backend list() call, so the row's first clause has no counterpart and a folder switch blocks instead of abandoning work at a chunk boundary.  
*src/tiku_trk_poseview.c:389-420 (reload, single synchronous list) and :533-548 (switch_dir clears park/zombies) with :2470-2476 (CREATED: strcmp(e->dir, v->path) != 0 -> NOTHING)*

#### PVN-049 · core · done
**A file created while the folder is still loading appears exactly once, never twice, regardless of whether the node monitor or the loader gets there first.**  
CreatePoses skips (and deletes + unwatches) any model whose node_ref is already in fInsertedNodes or in the zombie list; fInsertedNodes is added to on insert, pruned in DeletePose, and cleared in SwitchDir and Refresh.  
`PoseView.cpp` → `BPoseView::CreatePoses / fInsertedNodes`  

#### PVN-050 · polish · differs
**Directory reads are batched and issued in inode order to cut disk seeking during a folder load.**  
CachedDirectoryEntryList is a CachedEntryIterator(0, 40, true) -- 40-entry cache with sortInodes on; GetNextDirents fills a kDirentBufferSize buffer then sorts the batch with _CompareInodes before handing entries out one at a time.  
`EntryIterator.cpp` → `CachedDirectoryEntryList::CachedDirectoryEntryList / CachedEntryIterator::GetNextDirents / CachedEntryIterator::_CompareInodes`  
**TIKU:** Inode-order batching is a spinning-disk optimization with no payoff on NVM; drop it, but keep the batching itself for UART/console-bound namespace enumeration.  
**PORT:** Batching is really implemented on the device backend — tk_list makes one tiku_desk_ns_ls_kinds round trip into a 128-name buffer and then hands entries to the callback one at a time — with only the inode ordering dropped, which the row's TIKU note explicitly directs.  
*src/tiku_trk_backend_tiku.c:296-360 (tk_list: char names[128][64] + kinds, single ns_ls_kinds call, per-entry fn())*

#### PVN-051 · important · open
**The Desktop shows the union of several directories by chaining iterators, so entries from every integrated volume's Desktop folder appear in one view.**  
DesktopPoseView::InitDesktopDirentIterator builds a CachedEntryIteratorList and AddItem()s a CachedDirectoryEntryList per source, watching each source node with the full directory mask; EntryIteratorList::GetNextDirents advances fCurrentIndex to the next sub-iterator on exhaustion.  
`DesktopPoseView.cpp` → `DesktopPoseView::InitDesktopDirentIterator / EntryIteratorList::GetNextDirents`  
**TIKU:** This is exactly the shape the TikuOS requirement needs: device-namespace nodes and local files as two chained iterators feeding one view, each with its own watch registration.  

#### PVN-052 · important · open
**Dot files are filtered out at load time and toggling the setting re-reads the whole folder rather than just repainting.**  
AddPosesTask skips per `(!hideDotFiles && (name=="." || name=="..")) || (hideDotFiles && name[0]=='.')`, reading TrackerSettings().HideDotFiles() once per load; the kHideDotFilesChanged observer case calls Refresh().  
`PoseView.cpp` → `BPoseView::AddPosesTask / BPoseView::MessageReceived (case kHideDotFilesChanged)`  

#### PVN-053 · core · done
**A full refresh drops every watch, empties the view, re-establishes watches and re-reads the folder -- the guaranteed way back to a non-stale listing.**  
Refresh(): OpenNode, StopWatching, fInsertedNodes.Clear, ClearPoses, StartWatching, AddPoses, RebuildFilteringPoseList if ref-filtering, Invalidate, ResetOrigin. ClearPoses also empties the zombie, broken-link, selection and visual-sort lists and commits/saves first.  
`PoseView.cpp` → `BPoseView::Refresh / BPoseView::ClearPoses`  

#### PVN-054 · important · open
**A flood of MIME-database icon changes repaints the window once, not once per notice.**  
NoticeMetaMimeChanged reacts to B_ICON_CHANGED / B_ICON_FOR_TYPE_CHANGED / B_APP_HINT_CHANGED / B_PREFERRED_APP_CHANGED by queueing a MetaMimeChangedAccumulator on the window's DelayedTaskLoop with AccumulatedRunLater(..., 200000, 5000000); CanAccumulate merges notices with the same type+preferredApp.  
`PoseView.cpp` → `BPoseView::NoticeMetaMimeChanged / MetaMimeChangedAccumulator`  
**TIKU:** This is the ONLY coalescing in the live-update path. TikuOS watch events explicitly do not coalesce ('same-value writes ring'), so a debounce of this shape must be added by the view for high-rate device nodes.  

#### PVN-055 · polish · open
**Icons for applications and preferences are pre-warmed at startup and kept correct afterwards, so those folders open with icons already drawn.**  
NodePreloader::PreloadOne watches each preloaded directory with B_WATCH_DIRECTORY and each file model with B_WATCH_STAT|B_WATCH_ATTR, then IconCache::sIconCache->Preload(); MessageReceived drops the model from the cache on B_ENTRY_REMOVED and re-caches its icon on B_ATTR_CHANGED / B_STAT_CHANGED.  
`NodePreloader.cpp` → `NodePreloader::PreloadOne / NodePreloader::MessageReceived`  

#### PVN-056 · important · open
**The Trash icon flips between full and empty live as items are trashed or emptied, on any volume.**  
BTrashWatcher watches every volume's trash with B_WATCH_DIRECTORY plus B_WATCH_MOUNT; B_ENTRY_CREATED sets full immediately, ENTRY_REMOVED / DEVICE_UNMOUNTED / cross-directory ENTRY_MOVED re-run CheckTrashDirs (a same-directory move is explicitly ignored); UpdateTrashIcon writes kAttrIcon on the boot trash dir, which in turn reaches pose views as a normal B_ATTR_CHANGED.  
`TrashWatcher.cpp` → `BTrashWatcher::MessageReceived / BTrashWatcher::UpdateTrashIcon`  
**TIKU:** Note the indirection: state change is published by writing an attribute, and the views learn about it through ordinary attribute watching. TikuOS's tiku_vfs_notify() on a driver-updated node is the direct analogue and is cleaner.  

#### PVN-057 · polish · open
**Renaming any ancestor folder updates an open window's title, when path watching is on.**  
BContainerWindow::SetPathWatchingEnabled walks GetParent() up to '/' installing watch_node(&ref, B_WATCH_NAME, this) on each ancestor; BContainerWindow::MessageReceived's case B_NODE_MONITOR simply calls UpdateTitle().  
`ContainerWindow.cpp` → `BContainerWindow::SetPathWatchingEnabled / BContainerWindow::MessageReceived (case B_NODE_MONITOR)`  

#### PVN-058 · important · open
**Toggling the Disks-icon or volumes-on-Desktop setting adds or removes the / icon immediately, and closes the Disks window if it was open.**  
DesktopPoseView::AdaptToVolumeChange (and BFilePanelPoseView::AdaptToVolumeChange) hand-build a B_NODE_MONITOR message with opcode B_ENTRY_CREATED or B_ENTRY_REMOVED for '/' and post it to the view, then ToggleDisksVolumes(); BPoseView::AddVolumePoses does the same synthesis when ShowDisksIcon is set.  
`DesktopPoseView.cpp` → `DesktopPoseView::AdaptToVolumeChange / BPoseView::AddVolumePoses / BPoseView::ToggleDisksVolumes`  
**TIKU:** The pattern -- synthesize a notification and post it to yourself so that a settings change takes the same code path as a real filesystem event -- is worth keeping; it collapses two update paths into one.  

#### PVN-059 · important · open
**A virtual directory watches each of its contributing real directories by path, plus its own definition file and the root definition file.**  
StartWatching: BPathMonitor::StartWatching(path, B_WATCH_DIRECTORY|B_WATCH_CHILDREN|B_WATCH_NAME|B_WATCH_STAT|B_WATCH_INTERIM_STAT|B_WATCH_ATTR, this) per entry in fDirectoryPaths; TTracker::WatchNode(TargetModel()->NodeRef(), B_WATCH_NAME|B_WATCH_STAT|B_WATCH_ATTR); TTracker::WatchNode(&fRootDefinitionFileRef, B_WATCH_STAT) when not root. StopWatching does BPathMonitor::StopWatching + stop_watching.  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::StartWatching / VirtualDirectoryPoseView::StopWatching`  
**TIKU:** Path-based watching is the closest existing model to watching a VFS subtree that may not exist yet; TikuOS needs an equivalent if device nodes can appear under a path.  

#### PVN-060 · important · open
**When one of a virtual directory's contributing directories itself appears, all of its children show up at once.**  
_EntryCreated: if the message's "path" is in fDirectoryPaths, the new directory is enumerated with GetNextDirents and one synthetic B_ENTRY_CREATED is dispatched per child (skipping "." and "..").  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_EntryCreated`  

#### PVN-061 · core · open
**In a virtual directory, a name is supplied by the first contributing directory that has it -- an entry created in a lower-priority directory is invisible and produces no row.**  
_EntryCreated calls _GetEntry (VirtualDirectoryManager::GetEntry, which lstat()s the name under each path in order and returns the first hit) and returns without acting unless the winning ref equals the created ref; the iterator uses BMergedDirectory::B_ALWAYS_FIRST.  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_EntryCreated / VirtualDirectoryManager::GetEntry`  
**TIKU:** This is the shadowing rule a merged device-namespace + local-files view needs. Pick and document a precedence order (device wins, or local wins) before writing the merge.  

#### PVN-062 · important · open
**An entry created in a higher-priority directory replaces the currently shown one of the same name in a single visible step.**  
_EntryCreated finds the existing pose by name (FindPoseByFileName); if the node differs it tells the manager DirectoryRemoved (for directories) and synthesizes a B_ENTRY_REMOVED for the old node before dispatching the create.  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_EntryCreated`  

#### PVN-063 · important · open
**Removing the winning entry in a virtual directory reveals the shadowed entry of the same name from a lower-priority directory rather than leaving a hole.**  
_EntryRemoved re-runs _GetEntry for the name; if another entry is found it synthesizes a B_ENTRY_CREATED for it after the removal (and returns early if the replacement resolves to the same translated directory node, i.e. nothing actually changed).  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_EntryRemoved`  

#### PVN-064 · important · open
**Removing an entire contributing directory removes every row that came from it.**  
_EntryRemoved: when "path" matches an entry in fDirectoryPaths, it collects all poses whose entry ref's DirectoryNodeRef() equals the removed node and dispatches a synthetic B_ENTRY_REMOVED for each.  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_EntryRemoved`  

#### PVN-065 · polish · open
**Moves inside a virtual directory are shown as a removal followed by a creation rather than as a rename.**  
_EntryMoved dispatches B_ENTRY_REMOVED for the from-ref/from-path and B_ENTRY_CREATED for the to-ref/path; the source comments this as 'the lazy approach' versus forwarding a real B_ENTRY_MOVED.  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_EntryMoved`  
**TIKU:** Consequence a rewrite inherits: selection, in-place rename and scroll position are not preserved across a move in a virtual directory. Decide whether to keep that.  

#### PVN-066 · important · open
**Subdirectories of a virtual directory open as virtual directories themselves, merging the same-named subdirectory from every contributing path.**  
VirtualDirectoryEntryList::GetNextDirents rewrites any dirent that is a directory via VirtualDirectoryManager::TranslateDirectoryEntry, which manufactures a per-parent UUID directory under kTemporaryDefinitionFileBaseDirectoryPath and a child definition file, and returns that node instead of the real one.  
`VirtualDirectoryManager.cpp` → `VirtualDirectoryManager::TranslateDirectoryEntry / VirtualDirectoryEntryList::GetNextDirents`  

#### PVN-067 · important · open
**Editing the definition file of a virtual directory re-reads its source paths and reloads the window.**  
_NodeStatChanged: for fRootDefinitionFileRef with B_STAT_MODIFICATION_TIME in "fields", it calls manager->DefinitionFileChanged (which re-reads and prunes the tree via _UpdateTree, and returns false if the definition is gone -- in which case the message is ignored and the pending remove notice is relied on), then compares GetDefinitionFileChangeTime against fFileChangeTime and calls Refresh().  
`VirtualDirectoryPoseView.cpp` → `VirtualDirectoryPoseView::_NodeStatChanged / VirtualDirectoryManager::DefinitionFileChanged`  

#### PVN-068 · polish · open
**A virtual directory that disappears cleans up its generated definition files and temporary directories.**  
VirtualDirectoryManager::DirectoryRemoved -> _RemoveDirectory recurses over children, rmdir()s the UUID directory under /var/tmp-style base path, removes the generated definition file for non-root infos, and unregisters the node from fInfos.  
`VirtualDirectoryManager.cpp` → `VirtualDirectoryManager::DirectoryRemoved / VirtualDirectoryManager::_RemoveDirectory`  

#### PVN-069 · important · differs
**A recursive walk visits a subdirectory after returning it, so entries created in a folder the walk has already passed are missed while entries in a not-yet-visited folder are included.**  
TNodeWalker::GetNextEntry returns the entry, then PushDirCommon(&ref) if fTopDir->Contains(ref.name, B_DIRECTORY_NODE); the stack is popped by PopDirCommon on exhaustion, giving depth-first order with no snapshotting.  
`NodeWalker.cpp` → `TNodeWalker::GetNextEntry / TNodeWalker::PushDirCommon / TNodeWalker::PopDirCommon`  
**PORT:** The no-snapshot, visit-after-returning semantics are actually implemented — visit() enqueues each container as it hands the entry out and step() lists a directory live when it pops it, so already-passed folders miss later creations and not-yet-popped folders include them — with a FIFO queue giving breadth-first rather than the original's depth-first stack.  
*src/tiku_trk_query.c:452-462 (visit enqueues containers) with :439-449 (enqueue ring, full queue cuts a subtree) and :552-575 (step lists on pop)*

#### PVN-070 · polish · open
**Pointing a walker at a plain file instead of a directory yields exactly that one file rather than failing.**  
The path/entry_ref constructors fall back to `fJustFile = new BEntry(path)` when fOriginalDirCopy.InitCheck() != B_OK; GetNextEntry/GetNextRef/GetNextDirents return it once and null fJustFile. Rewind restores fJustFile from fOriginalJustFile.  
`NodeWalker.cpp` → `TNodeWalker::TNodeWalker(const char*, bool) / TNodeWalker::GetNextEntry / TNodeWalker::Rewind`  

#### PVN-071 · important · open
**A whole-system walk skips volumes that cannot serve the request -- volumes without attribute support, or read-only volumes when a writable one is required -- and a query walk skips volumes that do not support queries.**  
TVolWalker::NextVolume loops while ((fKnowsAttr && !fVol.KnowsAttr()) || (fWritable && fVol.IsReadOnly())); TQueryWalker::NextVolume loops while (!fVol.KnowsQuery()) and re-Clear/SetVolume/SetPredicate/Fetch per volume.  
`NodeWalker.cpp` → `TVolWalker::NextVolume / TQueryWalker::NextVolume`  
**TIKU:** Direct mapping: TikuOS capability bits on the device descriptor replace KnowsAttr/KnowsQuery/IsReadOnly, and a walk must skip nodes lacking the needed capability rather than erroring.  


---

## ContainerWindow: window behavior, menus, navigation

Architecture notes: [`spec/ARCH-container-window.md`](spec/ARCH-container-window.md)

#### CW-001 · core · open
**A folder window reopens at the exact position and size it had when it was last closed, remembered per folder.**  
Frame is read from the kAttrWindowFrame attribute on the folder's own node and multiplied by be_plain_font->Size()/12.0 (saved divided by the same factor), so the layout scales with font size. If the attribute is missing the shared default rect is used and staggered.  
`ContainerWindow.cpp` → `BContainerWindow::RestoreWindowState(AttributeStreamNode*)`  
**TIKU:** BFS attributes are the store; TikuOS needs a sidecar keyed by node id (VFS path/inode) since device namespace nodes carry typed descriptors, not arbitrary attributes.  
**CHECKED:** Folder windows always open at a cascade origin (frame.w/h hardcoded 460x300) and no code path ever reads or writes a saved window frame — the state store carries only view mode, pose info and column layout.  
*src/tiku_trk_desktop.c:open_folder (none found for frame persistence)*

#### CW-002 · important · open
**Window geometry is written back only when something actually changed, and only changes made while the window was the active window count.**  
FrameResized/FrameMoved/WorkspacesChanged set fStateNeedsSaving only `if (IsActive())`; StateNeedsSaving() ORs that with PoseView()->StateNeedsSaving(); Quit() writes state only if StateNeedsSaving().  
`ContainerWindow.cpp` → `BContainerWindow::FrameResized / FrameMoved / WorkspacesChanged / StateNeedsSaving`  

#### CW-003 · polish · done
**Closing a window feels instant even on a busy disk: the window disappears first, then its state is written.**  
SaveState(hide=true) writes the window attributes, calls Hide(), then writes the (slower) pose view state. QuitRequested() also calls Hide() before returning true.  
`ContainerWindow.cpp` → `BContainerWindow::SaveState(bool hide)`  

#### CW-004 · important · open
**The root "Disks" window remembers its own position separately from the Desktop, since both would otherwise share one node.**  
For TargetModel()->IsRoot() the attribute names switch to kAttrDisksFrame/kAttrDisksWorkspace, and WindowStateNodeOpener redirects reads/writes to the Desktop directory node.  
`ContainerWindow.cpp` → `BContainerWindow::RestoreWindowState / WindowStateNodeOpener::WindowStateNodeOpener`  
**TIKU:** Equivalent aliasing problem if the device-namespace root has no writable node of its own.  

#### CW-005 · important · open
**A folder opened for the first time inherits its parent folder's window size and column layout, offset diagonally so it doesn't sit exactly on top of the parent.**  
SetupDefaultState copies only kAttrWindowFrame, kAttrWindowWorkspace, kAttrViewState(+Foreign), kAttrColumns(+Foreign) from the parent node; OffsetFrameOne staggers the copied rect by sWindowStaggerBy; ClearViewOriginOne zeroes the copied scroll origin. The Desktop folder is excluded as a state source.  
`ContainerWindow.cpp` → `BContainerWindow::SetupDefaultState`  

#### CW-006 · important · open
**If the parent has no saved state either, the folder falls back to a user-editable "DefaultFolderTemplate" in the Tracker settings directory (created on demand).**  
DefaultStateSourceNode(kDefaultFolderTemplate, &node, createNew=true) creates the template folder if absent; the same node also supplies the default background image.  
`ContainerWindow.cpp` → `BContainerWindow::SetupDefaultState / BContainerWindow::DefaultStateSourceNode`  
**VERIFIER CORRECTION:** BPoseView::CanOpenParent (PoseView.cpp:8639) returns false for the Desktop/root BEFORE it looks at B_CONTROL_KEY (line 8659). Control only overrides the deeper checks (the parent-is-root / show-Disks-icon rules); it cannot re-enable Open parent on the Desktop or in the Disks window.  

#### CW-007 · polish · done
**Successive new windows with no remembered position cascade down-right instead of stacking.**  
sNewWindRect is shared across all windows and offset by sWindowStaggerBy (3 * DefaultLabelSpacing) each time a window opens without a saved frame; the initial rect is ~ (14, 8, 91, 46) label-spacings.  
`ContainerWindow.cpp` → `BContainerWindow::InitialWindowRect / RestoreWindowState`  

#### CW-008 · polish · open
**A window whose remembered position is entirely off-screen (smaller monitor, unplugged display) is pulled back onto the screen.**  
CheckScreenIntersect: if Frame() does not intersect the screen, MoveTo(sNewWindRect.LeftTop()); the cascade origin itself resets to (85,50) when it runs past the screen bottom or right.  
`ContainerWindow.cpp` → `BContainerWindow::CheckScreenIntersect`  

#### CW-009 · important · open
**A window reopens on the workspace it was on, and with the window decorations (tab position/size) it had - but only when Tracker is restoring a session, not on a normal open.**  
Workspace restore is gated on the kRestoreWorkspace open flag, decorator settings on kRestoreDecor; kIsHidden makes the restored window come up minimized. Flags are passed by TTracker::OpenContainerWindow.  
`ContainerWindow.cpp` → `BContainerWindow::RestoreWindowState(AttributeStreamNode*)`  

#### CW-010 · important · open
**The Desktop never remembers or restores a window frame, and never picks up per-folder background image state meant for the Desktop folder.**  
RestoreWindowState/SaveWindowState both bail on PoseView()->IsDesktopView(); UpdateBackgroundImage refuses when TargetModel()->IsDesktop() && !isDesktop, and for root; SetupDefaultState skips attribute copying for the Desktop.  
`ContainerWindow.cpp` → `BContainerWindow::SaveWindowState / UpdateBackgroundImage`  

#### CW-011 · important · done
**Zoom toggles: first click sizes the window to fit its contents, a second click restores the previous size.**  
Zoom() saves Frame() into fSavedZoomRect and calls ResizeToFit(); if the frame did not change and the old saved rect is valid it resizes back to it.  
`ContainerWindow.cpp` → `BContainerWindow::Zoom`  

#### CW-012 · important · done
**"Resize to fit" (Cmd+Y) grows the window to the extent of its icons, clamped to the screen, and scrolls content flush to the top-left.**  
Screen frame is inset by 5px and the title tab height is subtracted from the top; the window is first constrained onto the screen, then intersected with it; scroll bars are disabled during the resize and re-enabled afterwards.  
`ContainerWindow.cpp` → `BContainerWindow::ResizeToFit`  

#### CW-013 · polish · differs
**Growing a window that is scrolled past its content scrolls the content back into view instead of leaving empty space.**  
FrameResized compares Extent() against Bounds() and the previous bounds and scrolls by at most the growth amount, per axis; also resets the pose placement hint.  
`ContainerWindow.cpp` → `BContainerWindow::FrameResized`  
**PORT:** The scroll is clamped to the content whenever the window's height is reported, which covers the case the row is about; it is not limited to the growth amount per axis, and the pose placement hint is not reset.

#### CW-014 · polish · differs
**Holding Option while minimizing minimizes the whole Tracker team, not just this window.**  
Minimize(true) with B_OPTION_KEY calls do_minimize_team(BRect(0,0,0,0), be_app->Team(), true).  
`ContainerWindow.cpp` → `BContainerWindow::Minimize`  
**PORT:** Minimising the whole team is genuinely reachable, just by a different gesture: a right-click on a Deskbar team row opens a drawn Hide all / Show all / Close all menu whose Hide all sets minimized on every window, while Ctrl-clicking a single window row minimises just that one — an actual alternative to the Option-modified minimize, not its absence.  
*src/tiku_trk_deskbar.c:670 (HIDE_ALL pick), :734 (MOD_CMD -> MINIMIZE_WINDOW), :581-588 (menu drawn); src/tiku_trk_desktop.c:1624-1643*

#### CW-015 · core · partial
**Closing the last Tracker window quits Tracker; Control+close (or a Deskbar quit) closes every window instead of just this one.**  
Quit() posts B_QUIT_REQUESTED to be_app when the window list becomes empty. QuitRequested() posts kCloseAllWindows to be_app when the incoming message carries B_CONTROL_KEY.  
`ContainerWindow.cpp` → `BContainerWindow::Quit / BContainerWindow::QuitRequested`  
**MISSING:** The Control+close half is implemented (HIT_CLOSE with ControlMask closes every folder window plus the panel), but the first half is deliberately dropped and documented in close_window's comment — nothing quits when the last window closes, and a documented decision not to do it is an absence, not an alternative mechanism.  
*src/tiku_trk_desktop.c:1687-1705 (Ctrl+close closes all) vs :397-421 close_window comment "that rule does NOT carry over"*

#### CW-016 · important · partial
**The title bar shows the folder name, or the full path when "show full path in title bar" is on, and it updates live when any ancestor folder is renamed or moved.**  
UpdateTitle() also pushes the location into the Navigator with kActionUpdatePath. SetPathWatchingEnabled(true) walks every parent up to "/" calling watch_node(B_WATCH_NAME); any B_NODE_MONITOR message just calls UpdateTitle().  
`ContainerWindow.cpp` → `BContainerWindow::UpdateTitle / SetPathWatchingEnabled / MessageReceived case B_NODE_MONITOR`  
**TIKU:** Ancestor-chain watching maps onto VFS watch notifications; TikuOS watch table is only TIKU_VFS_WATCH_MAX (8) slots, so watching every ancestor of every open window needs a different scheme (single path-prefix watch or re-derive on demand).  
**MISSING:** The title is the folder's full path and is re-set live when the folder itself is renamed/moved (NOTIFY_RETITLE), but there is no ancestor-chain watching (notify matches only the folder's own path or its old path) and no name-vs-full-path preference — it is always the full path.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_notify (RETITLE branch) + src/tiku_trk_desktop.c:main pump loop*

#### CW-017 · important · open
**The folder's icon sits at the right end of the menu bar and can be dragged out to move the folder itself (Option-drag copies).**  
_AddFolderIcon adds a DraggableContainerIcon sized to the menu bar height; the drag starts only after 3px of movement and only if the click hit the icon's opaque pixels (IconCache IconHitTest); message carries "be:actions" B_COPY_TARGET when Option is down, else B_MOVE_TARGET. Shown only when the window has a menu bar (ShouldHaveDraggableFolderIcon), and hidden/re-shown as SwitchDirectory changes folders.  
`DraggableContainerIcon.cpp` → `DraggableContainerIcon::MouseDown / MouseMoved`  

#### CW-018 · polish · done
**Clicking anywhere in the window chrome (menu bar, count view, scroll bars) activates the window; clicking in the pose view or on the folder icon does not, so a click-through drag or selection is not eaten.**  
A common B_MOUSE_DOWN filter activates the window unless the target view is a BPoseView or DraggableContainerIcon.  
`ContainerWindow.cpp` → `ActivateWindowFilter`  

#### CW-019 · polish · differs
**The pose view draws a coloured focus border when it has focus and the window is active, and the scroll bars highlight with it.**  
BorderedView::PoseViewFocused switches the view colour between B_DOCUMENT_BACKGROUND_COLOR/B_DARKEN_2_TINT and B_KEYBOARD_NAVIGATION_COLOR, and calls SetBorderHighlighted on both scroll bars; highlight can be disabled per window (EnableBorderHighlight(false) for query windows and the main container).  
`ContainerWindow.cpp` → `BorderedView::PoseViewFocused / BorderedView::WindowActivated`  
**PORT:** The focus border is drawn, but the scroll bar is not highlighted with it and there is no per-window way to turn the highlight off.

#### CW-020 · polish · open
**A folder's background image is shown in icon/mini-icon view and removed when switching to list view, and comes back when switching away from list view.**  
ViewModeChanged: newMode == kListMode -> Remove(); oldMode == kListMode -> Show(). Image is looked up on the folder node first, then on the DefaultFolderTemplate node; suppressed entirely in safe mode and in file panels.  
`ContainerWindow.cpp` → `BContainerWindow::ViewModeChanged / UpdateBackgroundImage`  

#### CW-021 · core · partial
**The menu bar has File and Window always, and Attributes only in list view; Attributes appears/disappears as the view mode changes.**  
AddMenus creates File (index 0), Window (index 1), fAttrMenu (added at index 2 only when ViewMode()==kListMode) and the detached "Arrange by" item. ShowAttributesMenu/HideAttributesMenu insert/remove at index 2. RebuildAddOnMenus assumes add-on menus start at index 3 in list mode, 2 otherwise.  
`ContainerWindow.cpp` → `BContainerWindow::AddMenus / ShowAttributesMenu / HideAttributesMenu`  
**MISSING:** The bar composition rule is implemented exactly (File+Window always, Attributes inserted at index 2 only in list mode and removed when the mode changes) and unit-tested, but neither front end draws or opens a menu bar, so a user only ever sees the context menus.  
*src/tiku_trk_cwmenu.c:tiku_trk_cwmenu_rebuild (menubar_insert/remove at index 2)*

#### CW-022 · core · partial
**File menu inventory: Find… (Cmd+F), New folder (Cmd+N), | Open (Cmd+O), [Edit query], [Open with…], Get info (Cmd+I), Edit name (Cmd+E), [Mount>/Unmount], Duplicate (Cmd+D), Move to Trash (Del), | [Move to/Copy to/Create link], Cut (Cmd+X), Copy (Cmd+C), Paste (Cmd+V), | Identify, Add-ons>.**  
Trash windows prepend "Empty Trash" + separator and show Delete/Restore instead of Duplicate/Move to Trash; the printers directory prepends "Add printer…" and ends after "Make active printer" (no cut/copy/paste, no identify, no add-ons); the root window has no Duplicate/Trash items, no clipboard block and no Identify.  
`ContainerWindow.cpp` → `BContainerWindow::AddFileMenu`  
**TIKU:** "Mount/Unmount" and Trash have no obvious analogue for device nodes; decide whether a device node can be "deleted" at all (probably capability-bit gated).  
**MISSING:** Find/New folder/Open/Open with/Get info/Edit name/Duplicate/Move to Trash/Move to/Copy to/Create link/Cut/Copy/Paste/Identify are built in order with the Trash (Empty Trash first, Delete+Restore) and root (no Duplicate/Trash, no clipboard, no Identify) variants; missing are Add-ons>, Mount/Unmount, Edit query and the printers-folder variant (and the menu is never presented, no bar is drawn).  
*src/tiku_trk_cwmenu.c:build_file*

#### CW-023 · core · partial
**Window menu inventory: Icon view> (Cmd+1, with sizes), Mini icon view (Cmd+2), List view (Cmd+3), | Resize to fit (Cmd+Y), [Arrange by>], Select… (Shift+Cmd+A), Select all (Cmd+A), Invert selection (Cmd+S), Open parent (Cmd+Up), Close (Cmd+W), Close all in workspace (Cmd+Q), | Preferences… (Cmd+,).**  
Preferences and Close-all-in-workspace target be_app, not the window. Query windows replace this with a shorter menu: Resize to fit, Select…, Select all, Invert selection, Close, Close all in workspace (no view modes, no Open parent).  
`ContainerWindow.cpp` → `BContainerWindow::AddWindowMenu (and BQueryContainerWindow::AddWindowMenu)`  
**MISSING:** Open parent carries Cmd+Up; the query-window variant of the menu does not exist.

#### CW-024 · important · partial
**Icon view has a submenu of fixed icon sizes (32/40/48/64/96/128) in radio mode plus Decrease size (Cmd+-) and Increase size (Cmd++); the current size is check-marked only while actually in icon mode, and the three view-mode items are radio-marked to the current mode.**  
UpdateWindowMenuOrWindowContextMenu marks the item whose message "size" equals PoseView()->UnscaledIconSizeInt() and unmarks everything when not in icon mode; MarkNamedMenuItem marks kIconMode/kListMode/kMiniIconMode. Invoking the "Icon view" super item itself switches to icon mode at the current size.  
`ContainerWindow.cpp` → `BContainerWindow::AddIconSizeMenu / UpdateWindowMenuOrWindowContextMenu`  
**MISSING:** All six sizes are offered and marked; choosing one does not also switch into icon mode, and the submenu holder carries no command.

#### CW-025 · important · partial
**Attributes menu lists Copy layout / Paste layout, then one check-markable item per column (Name, [Real name], Size, Modified, Created, Kind, Location, Permissions); checking one adds that column, unchecking removes it.**  
Each item carries attr_name/attr_type/attr_hash/attr_width/attr_align/attr_editable/attr_statfield and is targeted at the PoseView with kAttributeItem. "Real name" appears only when gLocalizedNamePreferred. In Trash (or in-trash folders) "Original name" (kAttrOriginalPath) replaces "Location". Marks are recomputed from PoseView()->ColumnFor(attr_hash) != 0, one level deep into submenus.  
`ContainerWindow.cpp` → `BContainerWindow::NewAttributesMenu(BMenu*) / MarkAttributesMenu(BMenu*)`  
**TIKU:** Column attributes map to typed VFS descriptors; the device namespace would contribute its own per-type columns instead of BFS attr metadata.  
**MISSING:** One check-markable item per column with marks recomputed from the columns the view really has, and checking/unchecking toggles the column through poseview_toggle_column; the Copy layout / Paste layout pair at the head of the menu is absent (column set is the device-namespace one, per the row's TIKU note).  
*src/tiku_trk_cwmenu.c:build_attributes + src/tiku_trk_desktop.c:menu_invoke (TIKU_TRK_CMD_COL_*)*

#### CW-026 · polish · open
**The Attributes menu grows extra submenus for the MIME types actually present in the folder, exposing that type's user-visible attributes as columns.**  
AddMimeTypesToMenu builds supertype menus then subtype menus from BMimeType::GetAttrInfo, skipping non-viewable attributes; empty supertype menus are removed and their subtypes promoted to the top level; a trailing separator is deleted if nothing followed it. A window inside the query-templates directory also injects its own demangled MIME type (underscore -> slash).  
`ContainerWindow.cpp` → `BContainerWindow::AddMimeTypesToMenu(BMenu*) / AddMimeMenu`  
**TIKU:** Replaced by per-descriptor-type column sets from the VFS type registry.  

#### CW-027 · important · done
**"Arrange by >" offers the same attribute list as sort keys plus Reverse order and Clean up; the current primary sort key and reverse flag are check-marked; the whole menu is disabled in list view (where clicking column titles sorts instead).**  
NewArrangeByMenu clones fAttrMenu items from index 3 onward (skipping Copy/Paste layout + separator) with what changed to kArrangeBy. SetupArrangeByMenu inserts it just before "Select…", marks by attr_hash == PoseView()->PrimarySort() and kArrangeReverseOrder == ReverseSort(). On the Desktop there is no Arrange by at all - only "Clean up" is updated.  
`ContainerWindow.cpp` → `BContainerWindow::NewArrangeByMenu / SetupArrangeByMenu / TShortcuts::UpdateArrangeByItem`  
**PORT:** The stated clause was fixed but the fix breaks a clause that used to hold. build_arrange now offers all five keys the Attributes menu offers, Capability included, so the two menus no longer disagree. But the radio group was not extended with it: mark_radio is still called with n=4 over a 5-entry keys[] array, and the chosen-key switch has no TIKU_TRK_COL_CAP arm, so `chosen` falls through to SORT_BY_NAME. Dumped with sort_field=COL_CAP: the Arrange menu marks "Name" while the view is sorted by Capability, and "Capability" can never be marked at all. So "the current primary sort key is check-marked" now fails for exactly the key that was just added. Reverse order, Clean up, the reverse mark and the disabled-in-list-view holder are all still correct.

#### CW-028 · important · done
**Holding Shift rewrites menu items live while the menu is open: Clean up -> Clean up all, Close -> Close all, Copy -> Copy more, Cut -> Cut more, Paste -> Paste links, Create link -> Create relative link, Move to Trash -> Delete, Identify -> Force identify - and the item's command and shortcut modifiers change with the label.**  
TLiveMenu/TLivePopUpMenu catch B_MODIFIERS_CHANGED and call Update(), which re-runs the TShortcuts::Update* methods; each Label()/Command() pair is derived from modifiers() at call time. "Close all" also retargets from the window to be_app.  
`Shortcuts.cpp` → `TShortcuts::CleanupLabel / CloseLabel / CopyLabel / CutLabel / PasteLabel / CreateLinkLabel / MoveToTrashLabel / IdentifyLabel (+ LiveMenu.cpp TLiveMenu::MessageReceived)`  

#### CW-029 · polish · differs
**Opening a menu with the mouse commits an in-progress inline rename; opening it with a keyboard shortcut does not (so Cmd+C during a rename still copies text).**  
MenusBeginning calls PoseView()->CommitActivePose() only when CurrentMessage()->what == B_MOUSE_DOWN.  
`ContainerWindow.cpp` → `BContainerWindow::MenusBeginning`  
**PORT:** The mouse-opened context menus commit the rename.  The other half of the rule -- that a KEYBOARD-opened menu does not -- has nothing to act on: this shell has no menu bar to open from the keyboard.

#### CW-030 · important · differs
**After a menu closes, all dynamically built submenus (Move to, Copy to, Create link, Open with, navigation, Mount) are emptied so the next open re-enumerates the file system rather than showing stale folders.**  
MenusEnded calls DeleteSubmenu() on each shared item, which removes and deletes all items of that item's submenu. ShowContextMenu also calls MenusEnded() before rebuilding a popup.  
`ContainerWindow.cpp` → `BContainerWindow::MenusEnded`  
**PORT:** Freshness is achieved by a real mechanism rather than left to chance: every menu the user can open goes through tiku_trk_cwmenu_context, which calls cwmenu_rebuild first, and rebuild clears and re-adds every menu (file, window, attributes, arrange, icon size, all four contexts) from the live context — the same end as MenusEnded's teardown, reached from the other side.  
*src/tiku_trk_cwmenu.c:267-291 rebuild (menu_clear + rebuild of each), called at :299 from cwmenu_context; only call sites are src/tiku_trk_desktop.c:1502, 1521, 1807, 2218*

#### CW-031 · core · partial
**Every menu item's enabled state and target are recomputed on each menu open according to the selection, the volume's read-only state and the folder kind.**  
Representative rules from TShortcuts::Update: Open/Identify/Restore/Copy to = selection non-empty; Move to / Move to Trash / Delete = selection non-empty AND selection volume not read-only; Cut = disabled in root/Trash/virtual dirs, else selection && target writable; Paste = clipboard has refs && target writable && not Trash (or focus is a text view); New folder = not root/Trash/in-Trash && target writable; Duplicate = PoseView()->CanMoveToTrashOrDuplicate(); Edit name = CanEditName(); Open parent = CanOpenParent(); Empty Trash = tracker->TrashFull(); Unmount = CanUnmountSelection(); Make active printer = exactly one item selected; Add-ons = submenu non-empty. Cut/Copy/Paste are additionally enabled whenever focus is in a text view.  
`Shortcuts.cpp` → `TShortcuts::Update and the TShortcuts::Update*Item family`  
**TIKU:** Read-only volume checks become capability bits on the VFS node (writable? deletable?); "target volume" becomes the owning tier/mount.  
**MISSING:** The Update-rule family is ported faithfully (Open/Identify/Restore/Copy to on selection; Move to/Trash on selection && !selection_ro; Cut refused in root/Trash/virtual dirs; Paste on clipboard && writable && !Trash; New folder; Duplicate; Edit name; Open parent) and re-resolved on every open, but the live front end never fills clipboard_has_refs, trash_full, text_focus, is_trash/in_trash or is_query, so those rules can never fire, and Unmount/Add-ons/Make-active-printer have no items at all.  
*src/tiku_trk_shortcuts.c:tiku_trk_sc_enabled + src/tiku_trk_desktop.c:menu_ctx*

#### CW-032 · important · partial
**"Open with…" appears directly under Open and lists applications that can handle the current selection; it is absent for the Trash, trashed items, volumes, the root, the printers folder and file panels.**  
SetupOpenWithMenu rebuilds an OpenWithMenu from a fresh B_REFS_RECEIVED message containing every selected ref plus a "TrackerViewToken" messenger, inserts it at IndexOf(Open)+1, and gives it the Ctrl+Cmd+O shortcut. Gate: ShouldHaveOpenWithMenu(ref).  
`ContainerWindow.cpp` → `BContainerWindow::SetupOpenWithMenu / ShouldHaveOpenWithMenu`  
**TIKU:** "Applications that handle this type" has no analogue unless TikuOS keeps a type->handler registry; may collapse to shell verbs registered per descriptor type.  
**MISSING:** "Open with…" is added directly after Open in both the File and pose menus with the Ctrl+Cmd+O shortcut and is absent from the volume menu, but it is a plain item with no application submenu and no handler in menu_invoke, and it is not gated off for the Trash, trashed items or the root.  
*src/tiku_trk_cwmenu.c:build_file / build_contexts + src/tiku_trk_shortcuts.c:tiku_trk_sc_mods (CMD|CTRL)*

#### CW-033 · important · open
**"Edit query" (Cmd+G) is inserted between Open and Open with… when the clicked item is a query or query template, or when the window itself is the Queries folder; query windows always have it.**  
SetupEditQueryItem inserts at IndexOf(kOpenSelection)+1. ShouldHaveEditQueryItem: with no ref, FSIsQueriesDir(window ref); with a ref, model.IsQuery() || model.IsQueryTemplate(). BQueryContainerWindow overrides it to always true. Enabled when the window is a query or something is selected.  
`ContainerWindow.cpp` → `BContainerWindow::SetupEditQueryItem / ShouldHaveEditQueryItem`  

#### CW-034 · important · open
**Volume, root and Desktop menus grow a "Mount >" submenu listing every partition (mounted ones check-marked) plus Mount all and Settings…, and an "Unmount" item (Cmd+U) - except on the Desktop and on the root popup, which get no Unmount. The boot volume's entry is disabled so it cannot be unmounted.**  
SetupMountMenu inserts after "Select all"+separator on the Desktop, or after "Edit name"+separator on volume/root menus; DetachMountMenu removes the trio again by looking for the 'U' shortcut. MountMenu enumerates BDiskDeviceList partitions that contain a file system (naming fallback: content name -> partition name -> "(size type)"), plus shared network volumes, and targets be_app.  
`ContainerWindow.cpp` → `BContainerWindow::SetupMountMenu / DetachMountMenu (and MountMenu.cpp AddMenuItemVisitor::Visit)`  
**TIKU:** Maps to mounting tiers/devices in the VFS; "boot volume cannot be unmounted" becomes "the root namespace cannot be detached".  

#### CW-035 · core · partial
**"Move to >", "Copy to >" and "Create link >" are hierarchical folder pickers offering Current folder, Recent folders, Desktop, Home, then volumes; Move to only offers the item's own volume (no cross-device move), Copy to and Create link offer every writable persistent volume.**  
PopulateMoveCopyNavMenu(navMenu, what, ref, addLocalOnly): addLocalOnly (Move) or fewer than 2 writable volumes -> only the ref's own volume, submenu not pre-populated; otherwise every non-read-only persistent volume with populated submenus. The three items are inserted after "Move to Trash"/"Delete" + separator, start disabled and are enabled only once built. Holding Shift switches Create link to kCreateRelativeLink. Absent for file panels, the printers folder, the Trash and trashed items.  
`ContainerWindow.cpp` → `BContainerWindow::SetupMoveCopyMenus / PopulateMoveCopyNavMenu / ShouldHaveMoveCopyMenus`  
**TIKU:** "volumes" become tiers/mounts; a cross-tier move may be legal where a cross-device move was not, so the addLocalOnly rule needs an explicit decision.  
**MISSING:** The three items are inserted just after the Move to Trash/Delete block behind a separator, Shift turns Create link into Create relative link, and they are omitted for the Trash/in-Trash/root; there is no hierarchical picker at all (no Current folder/Recent/Desktop/Home/volumes submenu, no addLocalOnly rule, no build-then-enable).  
*src/tiku_trk_cwmenu.c:build_file / build_contexts + src/tiku_trk_shortcuts.c:tiku_trk_sc_command (CREATE_REL_LINK)*

#### CW-036 · important · open
**Choosing "Copy to" the folder the items already live in duplicates them instead of failing, and every Move/Copy/Create-link destination is remembered in the Recent folders list.**  
kCopySelectionTo compares model.NodeRef() with TargetModel()->NodeRef() and calls DuplicateSelection() when equal; kCopySelectionTo/kMoveSelectionTo/kCreateLink all call BRoster().AddToRecentFolders(&ref) first. kCreateLink with no "refs" creates the link in the current folder, unless the window is a query or virtual directory.  
`ContainerWindow.cpp` → `BContainerWindow::MessageReceived cases kCopySelectionTo / kMoveSelectionTo / kCreateLink`  

#### CW-037 · important · open
**When the user has stored document templates, the "New folder" item is replaced by a "New >" submenu (New folder, then the templates, then Edit templates…); with no templates the plain "New folder" item stays.**  
SetupNewTemplatesMenu scans the templates dir on first use and swaps items in place, keeping the position of the old New folder item; the submenu is disabled when the target volume is read-only. Template folders carrying the kAttrTemplateSubMenu attribute become nested submenus, and each level offers "Add new submenu…" which creates the folder, tags it and reveals it selected in Tracker.  
`ContainerWindow.cpp` → `BContainerWindow::SetupNewTemplatesMenu / _NewTemplateSubmenu (and TemplatesMenu.cpp TemplatesMenu::BuildMenu / IterateTemplateDirectory)`  

#### CW-038 · important · open
**The Add-ons submenu lists Tracker add-ons, with those declaring support for the selected items' MIME types listed first, separated from the generic ones; each group is alphabetically sorted, and invoking one runs it on the selection in a separate thread.**  
BuildMimeTypeList gathers the types of the selection (mimesetting unknown ones and resolving symlinks) or of the folder itself when nothing is selected; EachAddOn classifies each add-on as primary (declared type contains a selected type) or secondary (declares B_FILE_MIME_TYPE) and skips ones matching neither. The invocation message carries "refs" (selection), "dir_ref" and a "TrackerViewToken" messenger. Add-ons exporting populate_menu can also inject their own items into the menu bar.  
`ContainerWindow.cpp` → `BContainerWindow::BuildAddOnsMenu / EachAddOn / AddOnMessage / LoadAddOn (and AddOnMenuGenerate)`  
**TIKU:** Loading shared objects is not available; the equivalent is capability-gated shell verbs / VFS-exposed commands, discovered from the namespace instead of an add-ons directory.  

#### CW-039 · polish · open
**Add-ons get a keyboard shortcut (Option+Cmd+key by default, taken from the add-on's BEOS:default_shortcut resource) which the user can rebind through the Shortcuts preferences file; changes take effect without restarting.**  
LoadAddOnDir installs the default shortcut on the Desktop window; ApplyShortcutPreferences reverts everything to defaults then re-applies bindings from the shortcuts_settings file (watched with BPathMonitor); adding or removing an add-on (node monitor on the add-on dirs) rebuilds the list and broadcasts kRebuildAddOnMenus to every container window.  
`DeskWindow.cpp` → `BDeskWindow::InitAddOnsList / ApplyShortcutPreferences / MessageReceived case B_NODE_MONITOR`  
**UNCONFIRMED:** Confirmed in DeskWindow.cpp (kDefaultShortcut "BEOS:default_shortcut", kDefaultModifiers = B_OPTION_KEY|B_COMMAND_KEY, LoadAddOnDir, ApplyShortcutPreferences, BPathMonitor on shortcuts_settings, B_NODE_MONITOR -> InitAddOnsList + kRebuildAddOnMenus broadcast) EXCEPT one claim: the row says ApplyShortcutPreferences "reverts everything to defaults then re-applies bindings" — RevertToDefault exists, but I did not read enough of ApplyShortcutPreferences (lines 334-420) to confirm the revert-then-apply ordering. Everything else in the row is confirmed.  

#### CW-040 · core · done
**Right-clicking picks one of four different context menus depending on what is under the pointer: a file/folder pose, a volume or the root, the Trash, or empty window background.**  
ShowContextMenu(where, ref): ref != NULL -> model.IsTrash() ? fTrashContextMenu : (IsVolume()||IsRoot()) ? fVolumeContextMenu : fPoseContextMenu; ref == NULL -> fWindowContextMenu. Re-entry is refused while the popup already has a window. The menu is run synchronously with Go(global, true, true, true) and fContextMenu is cleared afterwards.  
`ContainerWindow.cpp` → `BContainerWindow::ShowContextMenu`  
**TIKU:** A fifth case is likely needed for device nodes (typed descriptor) whose verbs come from capability bits rather than file semantics.  
**PORT:** cwmenu_context picks exactly the four menus from what is under the pointer — window_ctx for a NULL model (background), trash_ctx for KIND_TRASH, volume_ctx for KIND_VOLUME (the device roots, which is what "volume or root" means in this namespace), pose_ctx otherwise — and while app.popup is non-NULL the next ButtonPress is consumed for choose-or-dismiss and clears popup/popup_sub, so a second right-click cannot stack a popup.  
*src/tiku_trk_cwmenu.c:293-310 cwmenu_context; src/tiku_trk_desktop.c:1545-1585 (open popup eats the click, then cleared), :1807 open-on-right-click*

#### CW-041 · core · done
**Right-clicking a pose that is not selected selects it alone first; right-clicking empty space clears the selection - so the context menu always acts on what the user just pointed at.**  
BPoseView::ShowContextMenu clears the selection and selects the hit pose when it was not already selected, activates the window and forces a redraw before handing off to BContainerWindow::ShowContextMenu.  
`PoseView.cpp` → `BPoseView::ShowContextMenu`  

#### CW-042 · core · partial
**Context menu inventories: pose = Open/[Edit query]/[Open with…]/Get info/Edit name/Duplicate/Move to Trash (or Delete+Restore in Trash), | [Move to/Copy to/Create link], Cut/Copy/Paste, | Identify, Add-ons. Volume/root = Open, Get info, Edit name, | [Mount>/Unmount], Add-ons. Trash = Empty Trash, Open, Get info. Window background = [Empty Trash], [New folder/New>], [icon sizes on Desktop], [Paste], [Clean up on Desktop | Arrange by>], Select…, Select all, [Open parent], | [Mount>], [Add-ons].**  
Clipboard items are omitted from the pose menu for printers/root/Trash/in-Trash, and from the window menu for printers/root/Trash/in-Trash; "Open parent" is omitted on the Desktop; the Desktop gets the icon-size menu inline instead of an Arrange by menu.  
`ContainerWindow.cpp` → `BContainerWindow::AddPoseContextMenu / AddVolumeContextMenu / AddTrashContextMenu / AddWindowContextMenu`  
**MISSING:** All four inventories are built in the stated order with the Trash/root clipboard omissions and the Delete+Restore substitution, but Add-ons and Mount are absent everywhere, the pose menu omits Paste and Edit query, and the background menu has no Desktop icon-size block.  
*src/tiku_trk_cwmenu.c:build_contexts*

#### CW-043 · core · partial
**Dropping files with the secondary mouse button pops up a Move here / Copy here / Create link here / Cancel menu, and the chosen verb is applied; "Create link here" becomes "Create relative link here" with Shift.**  
ShowDropContextMenu disables every verb when the destination volume is read-only, and additionally disables "Move here" when the SOURCE volume is read-only; the trailing separator and Cancel item are skipped by the enable loop. The command of the chosen item is returned to the drop handler in BPoseView.  
`ContainerWindow.cpp` → `BContainerWindow::ShowDropContextMenu`  
**MISSING:** The drop menu is built exactly as described (Move/Copy/Create link here + Cancel, every verb disabled for a read-only destination, only Move disabled for a read-only source, Cancel never disabled, Shift makes the link relative) but nothing ever opens it: there is no inter-view drag-and-drop, so no drop ever happens and no chosen verb is applied.  
*src/tiku_trk_cwmenu.c:tiku_trk_cwmenu_drop (no caller outside tests/test_menu.c)*

#### CW-044 · important · open
**Hovering a drag over a folder (spring-loaded folders) pops up a navigable hierarchy of that folder's contents so the user can drop deep inside without opening windows; items that cannot accept the dragged types are greyed out.**  
In the dragging branch of ShowContextMenu the drag popup resolves symlinks and only opens for directories, never for the printers dir, and never twice at once (fDragContextMenu->IsShowing()); it is opened asynchronously on its own tracking thread (BPopUpNavMenu::Go spawns _TrackThread) to avoid deadlocking Window::Quit. The cached drag types list drives SpringLoadedFolderSetMenuStates: containers always enabled, files enabled only if they support one of the dragged types, broken links disabled. BPoseView::MenuTrackingHook closes the popup once the mouse leaves the menu and all its open submenus (with a margin).  
`ContainerWindow.cpp` → `BContainerWindow::ShowContextMenu (PoseView()->IsDragging() branch) / NavMenu.cpp SpringLoadedFolderSetMenuStates / BPopUpNavMenu::Go`  

#### CW-045 · important · open
**Context menus carry, as their first item, a hierarchical "navigate into this folder" submenu with a separator under it, letting the user open any descendant without leaving the window.**  
SetupNavigationMenu builds a fresh ModelMenuItem + BNavMenu targeted at be_app with B_REFS_RECEIVED; it only appears for containers (symlinks resolved to a container first) and never for queries or file panels (ShouldHaveNavigationMenu). Item and separator are removed together.  
`ContainerWindow.cpp` → `BContainerWindow::SetupNavigationMenu / ShouldHaveNavigationMenu`  

#### CW-046 · core · open
**In browser (single-window) mode the window shows a navigator bar with Back, Forward, Up buttons and an editable location field; Back/Forward are greyed out when there is nothing to go back or forward to, Up is greyed out at the file system root.**  
History is per window: fBackHistory/fForwHistory, capped at kMaxHistory = 32 (oldest dropped). Back enabled when fBackHistory has more than 1 entry, Forward when fForwHistory is non-empty, Up when the current path has a parent. Any new location (kActionSet) clears the forward history.  
`Navigator.cpp` → `BNavigator::UpdateLocation / BNavigator::AttachedToWindow`  

#### CW-047 · polish · open
**Holding Option while clicking Back/Forward/Up (or pressing the equivalent shortcut) opens the destination in a NEW window instead of navigating this one, and selects the folder you came from inside it.**  
SendNavigationMessage: with option the message becomes B_REFS_RECEIVED to be_app carrying "nodeRefToSelect" (the current folder's node ref); without option it is kSwitchDirectory to this window plus TTracker::SelectChildInParentSoon so the child folder is selected after the switch.  
`Navigator.cpp` → `BNavigator::SendNavigationMessage`  

#### CW-048 · important · open
**Cmd+L focuses the location field; typing a path and pressing Enter navigates there; an empty field means "/"; an invalid path silently reverts the field to the current folder's path. Dropping a file or folder on the navigator bar navigates to it (to its parent, if a file was dropped).**  
GoTo() posts kSwitchDirectory with action kActionLocation, which is the one action that does NOT overwrite the field text (so the caret and selection survive). The unhandled-message branch of MessageReceived handles the dropped refs.  
`Navigator.cpp` → `BNavigator::GoTo / BNavigator::MessageReceived`  
**TIKU:** A location field over the merged namespace must accept device paths (/dev/..., /sys/...) as first-class destinations.  

#### CW-049 · core · partial
**Double-clicking a folder opens a new window in spatial mode but navigates the same window in browser mode; holding Option inverts this (Option-open in browser mode spawns a new window, Option-open in spatial mode closes the parent window behind it).**  
BPoseView::OpenSelection sends kSwitchDirectory only when SingleWindowBrowse is on, the view is not the Desktop or a file panel, no Option key is down and both this folder and the target are directories. BContainerWindow's kSwitchDirectory handler, when NOT in browser mode, rewrites the message to B_REFS_RECEIVED for be_app, attaching "nodeRefToSelect" (this folder, so it gets selected in the new window) and, if Option is down, "nodeRefsToClose" (this folder, so this window closes).  
`ContainerWindow.cpp` → `BContainerWindow::MessageReceived case kSwitchDirectory (and PoseView.cpp BPoseView::OpenSelection)`  
**MISSING:** Double-click opens the folder's own window in the spatial shell and Option-open closes the window behind it; in-place navigation exists only as a separate single-window binary, so there is no SingleWindowBrowse setting and no Option-inverts-to-a-new-window in browser mode.  
*src/tiku_trk_desktop.c:main ButtonPress (double-click + Mod1Mask close_window) and src/tiku_trk_app.c:open_selection*

#### CW-050 · core · partial
**Navigating within one window saves the old folder's state, loads the new folder's state (columns, sort, scroll, background), retitles the window, and rebuilds the menus when the kind of folder changes (into/out of Trash, root or a volume).**  
SwitchDirectory: SaveState(false) if dirty -> UpdateBackgroundImage -> PoseView()->SwitchDir(ref, streamNode) -> RepopulateMenus() only when the trash/root/volume-ness flipped -> RebuildAddOnMenus -> re-arm path watching -> re-apply browse shortcuts -> show/hide/refresh the draggable folder icon -> UpdateTitle. File panels stop after SwitchDir.  
`ContainerWindow.cpp` → `BContainerWindow::SwitchDirectory`  
**MISSING:** The single-window app saves the old folder's state, switches directory, restores the new folder's state, re-lays out and retitles; missing are column reload (restore_columns is called only once at startup), scroll restore (switch_dir zeroes it), background image, and any menu rebuild — the front end that has menus has no in-window navigation.  
*src/tiku_trk_app.c:open_selection + src/tiku_trk_poseview.c:tiku_trk_poseview_switch_dir*

#### CW-051 · core · partial
**"Open parent" (Cmd+Up) goes to the enclosing folder - opening a new window in spatial mode, navigating in place in browser mode; Cmd+Option+Up additionally closes the window you left. It is disabled at the root and on the Desktop, and forced enabled while Control is held.**  
BContainerWindow::OpenParent resolves the parent virtual-directory-aware and feeds a kSwitchDirectory message back through MessageReceived, so it inherits the spatial/browser split and the Option-closes-parent rule. BPoseView::CanOpenParent denies the Desktop and root, allows one extra level when "show Disks icon" is on, and is overridden by the Control key.  
`ContainerWindow.cpp` → `BContainerWindow::OpenParent (and PoseView.cpp BPoseView::CanOpenParent)`  
**MISSING:** Open parent exists and is disabled at the root (can_open_parent), opens a new window from the menu in the spatial shell and navigates in place on Backspace in the single-window app; there is no Cmd+Up binding, no Cmd+Option+Up closing the window left, and no Control-forces-enabled override.  
*src/tiku_trk_desktop.c:menu_invoke (TIKU_TRK_CMD_OPEN_PARENT) + src/tiku_trk_app.c:handle_key (XK_BackSpace → poseview_go_up)*

#### CW-052 · important · open
**Switching between spatial and browser mode rebinds the arrow-key shortcuts: browser mode gives Cmd+Left/Right/Up = Back/Forward/Up and Cmd+L = focus location; spatial mode gives Cmd+Up = open parent, Cmd+Option+Up = open parent and close this window, and Cmd+Option+Down = open selection.**  
SetSingleWindowBrowseShortcuts removes the shortcuts of the other regime before installing its own; the browser bindings are duplicated with and without Option (Option = open in new window). It is a no-op on the Desktop, and browser bindings are skipped when the window has no Navigator.  
`ContainerWindow.cpp` → `BContainerWindow::SetSingleWindowBrowseShortcuts`  

#### CW-053 · important · open
**Turning browser mode off closes redundant windows: any two open windows showing the same folder collapse to one, and the "Desktop" folder window closes (the Desktop itself stays).**  
The kSingleWindowBrowseChanged observer sorts all container windows by node ref (CompareContainerWindowNodeRef) and sends B_QUIT_REQUESTED to the second of every adjacent equal pair, walking backwards. Turning browser mode ON instead creates a hidden Navigator for directory windows. Windows subscribe via TTracker StartWatching in the constructor.  
`ContainerWindow.cpp` → `BContainerWindow::MessageReceived case kSingleWindowBrowseChanged`  

#### CW-054 · polish · open
**The navigator bar can be shown or hidden globally without losing history; hiding/showing it re-computes the scroll range so the pose view resizes correctly.**  
ShowNavigator refuses on the Desktop, on non-directories and in file panels; it creates the Navigator lazily if the window never had one, and calls PoseView()->UpdateScrollRange() around the show/hide. Path watching is switched on/off with it (kShowNavigatorChanged observer).  
`ContainerWindow.cpp` → `BContainerWindow::ShowNavigator / MessageReceived case kShowNavigatorChanged`  

#### CW-055 · core · open
**Hierarchical folder menus fill in progressively while the mouse hovers them, so a huge folder never freezes the UI, and re-open always re-reads the folder rather than showing a stale listing.**  
BSlowMenu::AddDynamicItem adds at most 20 items or 200 ms per call and returns "call me again"; B_ABORT tears down the build state. BNavMenu::SetNavDir/ForceRebuild clear fMenuBuilt so the next open re-enumerates; ClearMenuBuildingState deletes items that were built but never added.  
`SlowMenu.cpp` → `BSlowMenu::AddDynamicItem`  
**TIKU:** The equivalent constraint is the shell/VFS enumerating a directory without blocking the kernel tick; TikuOS protothread yields replace the chunked menu build.  

#### CW-056 · core · open
**A folder menu lists sub-folders as further submenus and plain files as (disabled) leaf items, sorted with folders first when that setting is on, names truncated to a quarter of the screen width, and shows "Empty folder" when there is nothing to list.**  
NewModelItem creates a nested BNavMenu for containers (resolving symlinks to their targets, and hiding a link whose target's pose info says invisible) and a plain ModelMenuItem otherwise, disabled unless the invoke message is B_REFS_RECEIVED. DoneBuildingItemList sorts with CompareFolderNamesFirstOne or CompareOne (natural compare) and optionally prepends a "parent folder" entry when SetShowParent(true) - never at the file system root. GetMaxMenuWidth = max(screen width / 4, 150).  
`NavMenu.cpp` → `BNavMenu::NewModelItem / BNavMenu::DoneBuildingItemList / BNavMenu::GetMaxMenuWidth`  

#### CW-057 · important · open
**Folder menus hide the same things the folder window hides: dot files (when that setting is on), items marked invisible in their pose info, the Trash, and query results that live in the Trash; a menu on the root lists volumes; a menu on the Desktop lists volumes (or the Disks icon) plus Trash; a menu on the Trash lists the union of every volume's trash.**  
AddNextItem applies HideDotFiles, model.IsTrash(), the query-results-in-trash rule and BPoseView::PoseVisible(kAttrPoseInfo). StartBuildingItemList picks the container: QueryEntryListCollection / VirtualDirectoryEntryList / desktop dirent iterator (+AddVolumeItems or AddRootItem, +AddTrashItem) / per-volume trash EntryIteratorList / plain DirectoryEntryList, and sets the volumes-only flag when the entry has no parent.  
`NavMenu.cpp` → `BNavMenu::AddNextItem / BNavMenu::StartBuildingItemList / BNavMenu::BuildVolumeMenu`  
**TIKU:** "Volumes" and "the union of all trashes" have direct analogues only if TikuOS has multiple mounts; device nodes may need an explicit visible/hidden bit like pose info's fInvisible.  

#### CW-058 · important · open
**"Recent folders" inside the Move to/Copy to pickers, and the file panel's Favorites menu, list recently used items with the counts the user configured; an empty list shows a disabled "<No recent items>" placeholder.**  
RecentsMenu reads its cap from TrackerSettings().RecentCounts(apps, docs, folders) selected by fWhich (0=documents, 1=apps, 2=folders) and pulls from BRoster::GetRecent*; DoneBuildingItemList adds the disabled placeholder when nothing was added. FavoritesMenu builds three titled sections in order - Favorites (links in the user's Go directory), Recent documents (suppressed in save panels), Recent folders (skipping refs already listed under Favorites) - and re-filters everything through the panel's BRefFilter.  
`FavoritesMenu.cpp` → `RecentsMenu::AddRecents / RecentsMenu::DoneBuildingItemList / FavoritesMenu::AddNextItem`  
**VERIFIER CORRECTION:** RecentItems.cpp:154 returns fCount < fMaxCount - 1 && fSanityCount < fMaxCount + 20, so at most maxItems-1 items are ever added (one fewer than the configured recent count). The +20 sanity cap is correct.  

#### CW-059 · polish · open
**Recent-item menus show folders as navigable submenus when asked, resolve symlinks to their targets, and truncate long names the same way folder menus do.**  
BRecentItemsList::GetNextMenuItem resolves a symlink to its target model, picks the container-open or file-open message accordingly, truncates with BNavMenu::GetMaxMenuWidth, and wraps containers in a BNavMenu when fNavMenuFolders is set. RecentItemsMenu::AddNextItem stops at maxItems and has a sanity cap of maxItems + 20 iterations to survive lots of stale entries.  
`RecentItems.cpp` → `BRecentItemsList::GetNextMenuItem / RecentItemsMenu::AddNextItem`  

#### CW-060 · important · partial
**Cut/Copy/Paste/Select all go to the file list normally, but go to the text field when one has focus - so renaming a file or typing in a panel behaves like a text editor.**  
MessageReceived forwards B_CUT/B_COPY/B_PASTE/B_SELECT_ALL to PoseView() unless CurrentFocus() is a BTextView, in which case it forwards to that view (the custom kCutMoreSelectionToClipboard / kCopyMoreSelectionToClipboard / kPasteLinksFromClipboard variants always go to the PoseView to avoid recursion).  
`ContainerWindow.cpp` → `BContainerWindow::MessageReceived cases B_CUT/B_COPY/B_PASTE/B_SELECT_ALL`  
**MISSING:** Select all is routed correctly — Ctrl+A becomes TIKU_TRK_EDIT_SELECT_ALL while an inline editor has focus and selects poses otherwise — but Cut/Copy/Paste have no key handlers and no menu_invoke cases at all, and menu_ctx never sets text_focus, so the text-view routing the row describes exists for only one of the four verbs.  
*src/tiku_trk_app.c:handle_key (editing branch XK_a → EDIT_SELECT_ALL; XK_a → poseview_select_all)*

#### CW-061 · important · partial
**Cmd+Z undoes the last file operation and Shift+Cmd+Z redoes it, unless a text field has focus, in which case the text view handles it.**  
Shortcuts are installed in the constructor; the B_UNDO/B_REDO handlers call FSUndo()/FSRedo() when CurrentFocus() is not a BTextView.  
`ContainerWindow.cpp` → `BContainerWindow::MessageReceived cases B_UNDO / B_REDO`  
**MISSING:** Ctrl+Z undoes the last file operation and the inline-editor branch returns before it so a focused text field keeps the key, but there is no redo at all (no fs_redo in tiku_trk_fs.h) and Shift+Cmd+Z does nothing.  
*src/tiku_trk_app.c:handle_key (XK_z → tiku_trk_fs_undo); src/tiku_trk_fs.h has can_undo/undo only*

#### CW-062 · important · partial
**Window-level keyboard shortcuts exist for actions with no menu item: Shift+Del deletes without the Trash, Shift+Cmd+K cleans up all, Cmd+Down opens the selection, and Ctrl+Option+Shift+Cmd+Q quits Tracker outright.**  
Installed in Init(). Additionally, whichever unshifted key produces '+' on the current keymap is bound to Cmd+<key> = increase icon size, so semantic zoom works on non-US layouts (bug #6692). The menu-less Desktop window installs equivalents of the whole menu set as bare shortcuts in AddShortcuts().  
`ContainerWindow.cpp` → `BContainerWindow::Init / BContainerWindow::AddShortcuts`  
**MISSING:** All four menu-less actions are bound (Del delete, Ctrl+K cleanup, Return open, Ctrl+Q quit) so the bold rule holds on adapted chords, but the row's keymap-derived '+' binding is replaced by fixed keysyms and the menu-less Desktop window that installs the whole menu set as bare shortcuts does not exist at all.  
*src/tiku_trk_app.c:913 (XK_q), :932-941 (XK_plus/XK_equal/XK_KP_Add, no XGetKeyboardMapping anywhere in the file), :979 (XK_Delete), :1027 (XK_k)*

#### CW-063 · polish · differs
**"Select…" opens a persistent pattern-matching selection window per container window; reopening it just brings the existing one back near the mouse rather than making a second one.**  
ShowSelectionWindow creates fSelectionWindow once, then MoveCloseToMouse()+Show() on subsequent invocations; the window is quit in the container window's destructor. Matches are applied via the kSelectMatchingEntries message to PoseView()->SelectMatchingEntries.  
`ContainerWindow.cpp` → `BContainerWindow::ShowSelectionWindow / MessageReceived case kSelectMatchingEntries`  
**PORT:** One Select window serves the desktop rather than one per container window; it remembers which window it was opened from and applies there. Its state survives closing, which is the behaviour the row is about.

#### CW-064 · important · partial
**When a window changes into or out of a special folder (Trash, root, a volume) its whole menu bar and its pose/window context menus are rebuilt, so e.g. "Empty Trash" and "Restore" appear and disappear correctly during in-window navigation.**  
RepopulateMenus detaches the shared items, deletes and re-adds File/Window/Attributes (re-adding Attributes at index 2 only in list mode), then recreates fPoseContextMenu and fWindowContextMenu. Called from SwitchDirectory only when the trash/root/volume predicates changed.  
`ContainerWindow.cpp` → `BContainerWindow::RepopulateMenus`  
**MISSING:** Every menu — bar menus and both context menus — is cleared and rebuilt from the current context each time one opens, so a stale menu is impossible and the Trash variants are unit-tested; but the live context never computes is_trash/in_trash/is_query, and the shell with menus has no in-window navigation, so at runtime a window never changes kind and Empty Trash/Restore never actually appear.  
*src/tiku_trk_cwmenu.c:tiku_trk_cwmenu_rebuild + src/tiku_trk_desktop.c:menu_ctx*

#### CW-065 · important · partial ⟨found by verification⟩
**Cmd-1 / Cmd-2 / Cmd-3 switch to icon / mini-icon / list view from the Window menu, and the icon-size submenu is radio-marked to the current size.**  
AddIconSizeMenu builds an "Icon view" submenu in radio mode with the six sizes, gives its superitem the shortcut '1' and a bare kIconMode message (so invoking the parent restores LastIconSize); "Mini icon view" is '2' (kMiniIconMode) and "List view" is '3' (kListMode), all targeted at the pose view. Decrease/Increase size items ('-' / '+') live inside the same submenu.  
`ContainerWindow.cpp` → `BContainerWindow::AddIconSizeMenu / BContainerWindow::AddWindowMenu`  
**MISSING:** Both shells bind Ctrl+1/2/3; the Icon view row is a submenu holder, so it has no shortcut of its own.

#### CW-066 · important · open ⟨found by verification⟩
**Window state can travel in a BMessage instead of node attributes, which is how Tracker reopens the exact set of windows (including minimized ones) after a restart.**  
SaveState(BMessage&) / RestoreState(const BMessage&) / RestoreWindowState(const BMessage&) mirror the attribute-stream versions; TTracker::_OpenPreviouslyOpenedWindows passes the saved "window state" message plus kRestoreWorkspace|kRestoreDecor|(kIsHidden when kOpenWindowMinimized) to OpenContainerWindow, and the window is built through the kRestoreState message case in MessageReceived. RestoreWindowState(BMessage) also clears fStateNeedsSaving afterwards so the restore itself is not treated as a user move.  
`ContainerWindow.cpp` → `BContainerWindow::SaveState(BMessage&) / RestoreState(const BMessage&) / MessageReceived case kRestoreState`  
**TIKU:** Session state as a serialized message rather than per-node attributes is exactly the sidecar shape TikuOS needs, since VFS nodes carry typed descriptors, not arbitrary attributes.  

#### CW-067 · polish · open ⟨found by verification⟩
**Editing and re-saving a query keeps that query window's remembered position and columns instead of resetting them.**  
BContainerWindow::GetLayoutState reads only kAttrWindowFrame, kAttrColumns(+Foreign) and kAttrViewState(+Foreign) off a node into a message; SetLayoutState writes them back. FindPanel.cpp:782/605 uses the pair around rewriting the query file.  
`ContainerWindow.cpp` → `BContainerWindow::GetLayoutState / SetLayoutState`  

#### CW-068 · polish · open ⟨found by verification⟩
**An add-on can send messages back to the window while it runs; each one re-loads the add-on and calls its message_received entry point on its own thread.**  
MessageReceived case B_TRACKER_ADDON_MESSAGE -> _PassMessageToAddOn -> LaunchInNewThread("Add-on-Pass-Message", RunAddOnMessageThread), which load_add_on()s the ref in "addon_ref", looks up "message_received", calls it, unloads, and on any failure shows "Error %error loading add-On %name.". AddOnMenuGenerate similarly loads each add-on to call its optional populate_menu and caches the lookup result in info->has_populate_menu.  
`ContainerWindow.cpp` → `BContainerWindow::_PassMessageToAddOn / RunAddOnMessageThread / AddOnMenuGenerate`  
**TIKU:** No dynamic loading on-device; the equivalent is a registered shell verb answering messages, and the has_populate_menu caching becomes capability discovery done once.  

#### CW-069 · polish · open ⟨found by verification⟩
**The Disks (root) window never gets template default state, even on first open.**  
BContainerWindow::NeedsDefaultStateSetup returns false immediately for TargetModel()->IsRoot(), so SetupDefaultState never runs there and the root window falls back to the shared cascade rect plus its own kAttrDisksFrame attribute on the Desktop node.  
`ContainerWindow.cpp` → `BContainerWindow::NeedsDefaultStateSetup`  

#### CW-070 · important · done ⟨found by verification⟩
**"Arrange by" also carries "Reverse order" and "Clean up" below a separator; Reverse order flips the sort direction and re-lays out the icons.**  
BContainerWindow::NewArrangeByMenu appends a separator, Shortcuts()->ReverseOrderItem(), another separator and Shortcuts()->CleanupItem() after the copied attribute items. BPoseView::MessageReceived case kArrangeReverseOrder does SetReverseSort(!ReverseSort()) followed by Cleanup(true) — the same Cleanup(true) the kArrangeBy path runs.  
`ContainerWindow.cpp` → `BContainerWindow::NewArrangeByMenu / BPoseView::MessageReceived (case kArrangeReverseOrder)`  


---

## File operations: copy/move/trash/undo/clipboard

Architecture notes: [`spec/ARCH-fsutils.md`](spec/ARCH-fsutils.md)

#### FS-001 · core · partial
**A drag-and-drop between two different folders moves the items when source and destination are on the same volume, and copies them when they are on different volumes.**  
moveMode chosen by a fixed ladder: forceCopy -> kCopySelectionTo, forceMove -> kMoveSelectionTo, createRelativeLink, createLink, else !CheckDevicesEqual(srcList->ItemAt(0), destFolder) -> kCopySelectionTo, else kMoveSelectionTo. Device equality is tested on the FIRST source ref only.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**TIKU:** "Same volume" must be redefined: a device-namespace node and a local file are never the same store, so the default for any drop involving /dev must be an explicit decision, not a device-id compare.  
**MISSING:** The same-volume/cross-volume decision ladder is implemented and tested (st_dev compare, force_copy first rung, device backends treated as one volume), but nothing invokes it: no item drag-and-drop between folders exists and neither tiku_trk_fs_copy nor tiku_trk_fs_move is called anywhere in the UI.  
*src/tiku_trk_fs.c:274 tiku_trk_fs_drop_action*

#### FS-002 · core · open
**Holding Option while dropping forces a copy instead of a move.**  
forceCopy = forceCopy || (modifiers() & B_OPTION_KEY) != 0, evaluated after the destination checks and after any drop context menu.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**VERIFIER CORRECTION:** PoseView.cpp BPoseView::MoveSelectionInto:5091 — forceCopy |= (modifiers() & B_OPTION_KEY) happens after the same-folder shortcut and the drop context menu but BEFORE the destination checks (root / read-only volume / Trash). That ordering is load-bearing: Option-dropping onto the Trash trips the "Sorry, you can't copy items to the Trash." refusal at line 5117 instead of silently moving.  
**CHECKED:** No modifier is ever bound to a drop: the drag path in tiku_trk_trk_app reads only Ctrl (grid snap) and Mod1 is bound to Option-open-closes-window, the force_copy parameter of tiku_trk_fs_drop_action is passed non-zero only by tests, and there is no Trash-copy refusal at all.  
*none found*

#### FS-003 · important · open
**Dropping with the secondary mouse button opens a context menu offering Move here / Copy here / Create link here / Cancel; picking Cancel abandons the drop entirely.**  
AddDropContextMenu builds the four items (kMoveSelectionTo, kCopySelectionTo, kCreateLink, kCancelButton); MoveSelectionInto maps the result to forceMove/forceCopy/createLink/createRelativeLink and returns on kCancelButton or any unrecognised result. Shift on the Create-link menu item yields kCreateRelativeLink.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  

#### FS-004 · important · partial
**Dropping a selection into the folder it already lives in does not copy: in icon view it just repositions the icons, in list view it does nothing, and with Option held it duplicates instead.**  
Guarded by *srcWindow->PoseView()->TargetModel()->NodeRef() == *destFolder->NodeRef(). forceCopy -> DuplicateSelection(&where,&loc); createLink/createRelativeLink -> return (no link in same folder); kListMode -> return; otherwise each selected pose is moved by delta = loc - where, pinned to grid if Command is held, and re-inserted into the vertical-sort list.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**MISSING:** Dragging within a folder repositions every selected pose by the same delta with Ctrl pinning to grid, and the whole drag path is gated off in list mode so list view does nothing, but the Option-held clause (duplicate instead of reposition) is missing — no modifier other than the snap key is read.  
*src/tiku_trk_app.c:1438 (drag_active branch) + src/tiku_trk_app.c:1248 (list-mode gate)*

#### FS-005 · important · done
**Even when a drop is not caught by the same-folder shortcut, any individual item whose parent already is the destination is silently skipped rather than copied.**  
In the per-item loop: if moveMode is not a link mode and not kDuplicateSelection and the destination is not Trash and srcRef->device == destRef.device && srcRef->directory == deststat.st_ino, the item is skipped. This is what makes dragging out of a query window into the item's own folder a no-op.  
`FSUtils.cpp` → `MoveTask`  

#### FS-006 · core · done
**Duplicating an item names the copy "<name> copy", then "<name> copy 2", "<name> copy 3" and so on; duplicating something already called "<name> copy" gives "<name> copy 2", not "<name> copy copy".**  
FSMakeOriginalName: returns immediately if the name is already free. It walks back over trailing digits, then trailing spaces, then tests for the localized suffix (default " copy"); if found it truncates after the suffix (copycopy = true) and saves the root. Otherwise it truncates the name to B_FILE_NAME_LENGTH-(suffixLength+4) and appends the suffix. It then appends " %d" with fnum starting at 2 until the name is free, and if the result exceeds B_FILE_NAME_LENGTH-1 it shortens the saved root by one character and retries.  
`FSUtils.cpp` → `FSMakeOriginalName`  
**PORT:** split_suffix walks back over trailing digits then the ' copy' word and advances the counter, so the three stated cases ('note.txt copy', 'copy 2', and 'x copy' -> 'x copy 2') all hold and Ctrl+D routes through it.  
*src/tiku_trk_fs.c:171-231 split_suffix/free_suffixed_name, :234 tiku_trk_fs_duplicate_name, :468 use in step_one; tests/test_fs.c:68-81*

#### FS-007 · core · done
**Duplicate never asks about name conflicts — it always invents a fresh name.**  
CheckName returns B_OK immediately when moveMode == kDuplicateSelection ("when duplicating, we will never have a conflict"); MoveItem sets makeOriginalName = (moveMode == kDuplicateSelection) so CopyFile/CopyFolder rename instead.  
`FSUtils.cpp` → `CheckName`  

#### FS-008 · important · done
**Duplicate always writes into each item's own parent folder, even when several selected items come from different folders.**  
MoveTask re-points destDir/deststat/volume to BEntry(srcRef).GetParent(&destDir) at the top of every loop iteration when moveMode == kDuplicateSelection.  
`FSUtils.cpp` → `MoveTask`  

#### FS-009 · important · partial
**A symbolic link created by Create Link is named "<name> link", "<name> link 2", ... only when the plain name is taken, so linking into a different folder keeps the original name.**  
MoveItem's link branch always calls FSMakeOriginalName(name, destDir, " link") — which no-ops unless destDir already contains the name — and reports the chosen name to the undo record via undo.UpdateEntry.  
`FSUtils.cpp` → `MoveItem`  
**MISSING:** The naming rule is exactly right and tested (plain name kept when free, else "<name> link", "link 2"), but no link is ever created: there is no symlink() call in the port and TIKU_TRK_CMD_CREATE_LINK is only added to menus, never dispatched.  
*src/tiku_trk_fs.c:241 tiku_trk_fs_link_name*

#### FS-010 · important · open
**A relative link is computed against the destination folder, but silently degrades to an absolute link when source and destination are on different devices.**  
kCreateRelativeLink only takes the relative path if statbuf.st_dev == destNode.device; otherwise moveMode is reassigned to kCreateLink. The relative path is built by chdir()ing to the destination, walking source and destination path strings while they match, prepending "../" per remaining destination component, then appending the leaf. The working directory is restored with chdir(oldwd).  
`FSUtils.cpp` → `MoveItem`  
**TIKU:** chdir-based path math has no equivalent; relative-link resolution across a device namespace and a local FS needs an explicit mount-relative form or must be refused.  

#### FS-011 · polish · open
**Creating a link on a file system that cannot store symlinks reports "The target disk does not support creating links."**  
err == B_UNSUPPORTED from CreateSymLink throws FailWithAlert with that message; any other failure throws FailWithAlert with "Error creating link to \"%name\"."  
`FSUtils.cpp` → `MoveItem`  
**TIKU:** Capability bits should gate the link menu item up front rather than failing at execution time.  

#### FS-012 · core · open
**Copying refuses to start when the destination volume is read-only, and dropping onto a read-only volume shows "You can't move or copy items to read-only volumes."**  
Two separate checks: InitCopy returns B_READ_ONLY_DEVICE with NO alert (so a paste onto a read-only volume fails silently), while BPoseView::MoveSelectionInto raises the alert before launching the job.  
`FSUtils.cpp` → `InitCopy`  
**TIKU:** Replace volume read-only with a per-node writable capability bit; the check belongs at one layer so the silent-failure path disappears.  

#### FS-013 · important · open
**Moving items off a read-only volume copies them instead of moving.**  
MoveTask captures sourceIsReadOnly = BVolume(srcVolumeDevice).IsReadOnly() before anything else and rewrites moveMode kMoveSelectionTo -> kCopySelectionTo.  
`FSUtils.cpp` → `MoveTask`  

#### FS-014 · important · open
**An attempted copy into the Trash is turned into a move; a paste of copied items into Trash is refused outright with "Sorry, you can't copy items to the Trash."**  
MoveTask: if (moveMode == kCopySelectionTo && destIsTrash) moveMode = kMoveSelectionTo. FSClipboardPaste and BPoseView::MoveSelectionInto instead show the alert and abort. MoveSelectionInto also refuses links into Trash with "Sorry, you can't create links in the Trash."  
`FSUtils.cpp` → `MoveTask`  

#### FS-015 · important · open
**Before a copy starts, Tracker sums every file and folder to be copied and refuses with "there is not enough free space on the destination volume" if the total plus 4 KB exceeds the destination's free bytes.**  
InitCopy calls CalcItemsAndSize (which recurses via FSRecursiveCalcSize and charges one block per directory plus st_size+blockSize per file, block size clamped to [1024,8192], 2048 when unknown) then tests (totalSize + 4*kKBSize) >= dstVol->FreeBytes(). The whole calculation is skipped when the move is same-volume or the destination is Trash (needSizeCalculation false).  
`FSUtils.cpp` → `InitCopy`  
**TIKU:** A device-namespace destination has no FreeBytes(); the precheck needs a capability query or must be skipped, and skipping it changes the failure mode from a dialog to a mid-copy error.  

#### FS-016 · important · open
**Each individual file copy re-checks free space and aborts the whole job with a device-full error if the file plus 1 KB would not fit.**  
CopyFile computes BVolume from the destination directory's node_ref and, if (srcStat->st_size + kKBSize) >= volume.FreeBytes(), calls FileError(kNoFreeSpace, "", B_DEVICE_FULL, false) and throws B_DEVICE_FULL.  
`FSUtils.cpp` → `CopyFile`  

#### FS-017 · core · open
**When more than one name would collide in the destination, Tracker asks once up front with Cancel / Prompt / Skip all / Replace all before copying anything.**  
PreFlightNameCheck counts collisions (only for items whose parent differs from destDir) and shows kReplaceManyStr with the verb "moving" or "copying" only when collisionCount > 1; the four buttons map to kCanceled/kPrompt/kSkipAll/kReplaceAll and Escape is button 0. A single collision deliberately falls through to the per-item prompt.  
`FSUtils.cpp` → `PreFlightNameCheck`  

#### FS-018 · important · partial
**A single file-vs-file collision asks "You are trying to replace the item: <name> <size, date> with: <name> <size, date>" and offers only Cancel / Replace.**  
CheckName builds kReplaceStr and calls FileStatToString for both sides (byte count via a plural BStringFormat plus strftime "%b %d %Y, %I:%M:%S %p"); the two-button alert is used when !multipleCollisions && !sourceIsDirectory, with Escape bound to Cancel.  
`FSUtils.cpp` → `CheckName`  
**MISSING:** The engine does stop on a file-vs-file collision and publishes the colliding path for an answer, but nothing asks the user: there is no alert, no name/size/date text for the two sides and no Cancel/Replace choice — the app auto-answers "keep both" (CONFLICT_RENAME).  
*src/tiku_trk_fs.c:495 step_one conflict branch; src/tiku_trk_app.c:1517-1537*

#### FS-019 · core · partial
**A folder collision, or any collision while multiple collisions exist, asks with four buttons Skip / Skip all / Replace / Replace all, and Skip all suppresses every later prompt in the job.**  
CheckName: alert used when multipleCollisions || sourceIsDirectory. Button 0 sets conflictResolution = kCanceled and returns B_ERROR (skips this item), 1 sets kSkipAll, 2 kReplace, 3 kReplaceAll. conflictResolution is threaded by reference through the whole MoveTask loop, so kSkipAll returns B_ERROR immediately on every subsequent item and kReplaceAll bypasses the prompt.  
`FSUtils.cpp` → `CheckName`  
**MISSING:** Job-wide resolution exists in the policy field — answering REPLACE sticks for every later item (Replace all) — but there is no four-button prompt, no Skip all (resolve() resets SKIP to ASK after one item) and no folder-vs-file / multiple-collision distinction.  
*src/tiku_trk_fs.c:567 tiku_trk_fs_resolve*

#### FS-020 · core · open
**Replacing a folder merges into it rather than deleting it: existing files with non-conflicting names survive, conflicting ones are overwritten.**  
CheckName returns B_OK without removing the destination when destIsDir. CopyFolder then calls OverwriteOnConflict and, for kMerge (or kReplace on a directory, which falls through), sets newDir to the existing entry and skips CreateDirectory, so directory attributes are NOT re-copied (the CopyAttributes call is guarded by createDirectory).  
`FSUtils.cpp` → `CopyFolder`  

#### FS-021 · important · partial
**Replacing a file first deletes the existing file; if the delete fails the user is told "There was a problem trying to replace \"%name\". The item might be open or busy."**  
CheckName ends with entry.Remove() for the non-directory case and alerts on failure, returning the status so MoveTask skips the item.  
`FSUtils.cpp` → `CheckName`  
**MISSING:** Replace happens by overwriting rather than deleting first (copy_bytes fopen(dst,"wb") truncates, move replaces via rename()), and the "There was a problem trying to replace" failure message does not exist anywhere.  
*src/tiku_trk_fs.c:510 (CONFLICT_REPLACE falls through) and src/tiku_trk_fs.c:407*

#### FS-022 · polish · done
**A skipped item still advances the progress bar so the count stays honest.**  
MoveTask calls loopControl.UpdateStatus(srcRef->name, *srcRef, 1) on the CheckName != B_OK path, with the comment that the move call will not update it.  
`FSUtils.cpp` → `MoveTask`  

#### FS-023 · core · open
**Moving or copying a folder into itself or into one of its own sub-folders is refused with "You can't move a folder into itself or any of its own sub-folders."**  
CheckName tests srcDirectory == *destDir || srcDirectory.Contains(&destEntry) for directory sources, and exempts kCreateLink/kCreateRelativeLink.  
`FSUtils.cpp` → `CheckName`  

#### FS-024 · important · open
**Replacing a folder with one of its own sub-folders is refused with "You can't replace a folder with one of its sub-folders."**  
CheckName: after finding a conflicting destination entry that is a directory, BDirectory(&entry).Contains(sourceEntry).  
`FSUtils.cpp` → `CheckName`  

#### FS-025 · important · open
**A file may not replace a folder or symlink and vice versa: "You cannot replace a file with a folder or a symbolic link." / "You cannot replace a folder or a symbolic link with a file."**  
CheckName rejects when destIsDir != sourceIsDirectory, again exempting the two link modes.  
`FSUtils.cpp` → `CheckName`  

#### FS-026 · important · partial
**The root directory, the Trash directory and the Printers directory cannot be copied or moved, with a mode-specific message for each.**  
InitCopy pre-scans the whole source list: FSIsRootDir -> "You cannot copy or move the root directory." (or "...create a link to the root directory."), FSIsTrashDir -> the Trash variants, FSIsPrintersDir -> "You cannot copy or move the Printers directory." but ONLY for kCopySelectionTo/kMoveSelectionTo, so links to Printers are allowed. CheckName repeats the Trash guard per item with "You can't move or copy the trash."  
`FSUtils.cpp` → `InitCopy`  
**TIKU:** The equivalent list becomes namespace roots and device-namespace mount points; these need a "not movable" capability bit rather than hard-coded well-known paths.  
**MISSING:** Only the Trash guard exists, and silently: start() drops any queued path containing "/.Trash" from every operation; there is no root-directory or Printers guard in the engine and no mode-specific message for any of the three.  
*src/tiku_trk_fs.c:333-337 start()*

#### FS-027 · important · open
**Moving the system folder, the home folder, the config or settings folders (or anything inside them) demands confirmation, and for system/home the confirmation button only works while Shift is held.**  
ConfirmChangeIfWellKnownDirectory: returns false outright for Desktop/Printers/root/Trash; short-circuits true unless the entry is in B_SYSTEM_DIRECTORY or B_USER_DIRECTORY, and always true inside B_SYSTEM_TEMP_DIRECTORY. Uses OverrideAlert with B_SHIFT_KEY required for system/home (requireOverride true) and no modifier for MIME settings/config/settings folders. The %osName token comes from uname(). The confirmedAlready state machine (kNotConfirmed/kConfirmedHomeMove/kConfirmedAll) means a Shift-confirmed system move is not asked again for later items in the same job, while a non-override confirmation is only remembered for other non-override cases. MoveTask reaches this only via InitCopy and only for kMoveSelectionTo.  
`FSUtils.cpp` → `ConfirmChangeIfWellKnownDirectory`  
**VERIFIER CORRECTION:** FSUtils.cpp ConfirmChangeIfWellKnownDirectory: the system folder uses DirectoryMatchesOrContains(B_SYSTEM_DIRECTORY) (folder *and* contents), but home uses DirectoryMatches(entry, B_USER_DIRECTORY) — an EXACT match. An ordinary file inside home falls through to the config/settings tests and, failing those, returns true with no prompt at all. Only home itself is protected.  

#### FS-028 · core · done
**A same-volume move is a rename, so it is instant regardless of file size and the progress bar counts one unit per item rather than bytes.**  
MoveItem: when statbuf.st_dev == destNode.device and the mode is neither kCopySelectionTo nor kDuplicateSelection, it calls loopControl->UpdateStatus(ref.name, ref, 1) with the comment that size is irrelevant, then BEntry::MoveTo(destDir, newName).  
`FSUtils.cpp` → `MoveItem`  

#### FS-029 · important · open
**A same-volume move of a folder onto an existing folder of the same name merges recursively instead of failing, moving each file individually and removing the emptied source directories.**  
RecursiveMove: if destDir->Contains(name) it opens the existing sub-directory and, for each source entry, recurses on directories (then Remove()s them) and for files consults OverwriteOnConflict — skipping on kSkip, otherwise BEntry::MoveTo(&subDir, NULL, clobber=true). Finally the source directory is removed. If the name is free it is a plain MoveTo.  
`FSUtils.cpp` → `RecursiveMove`  

#### FS-030 · core · partial
**A cross-volume move is a copy followed by a delete of each source item, performed per-item, so a cancelled cross-volume move leaves the already-transferred items on the destination and gone from the source.**  
MoveItem's else-branch: directories go to CopyFolder(..., removeSource = (moveMode == kMoveSelectionTo)) which removes each child right after copying it and then removes the source folder; files go to CopyFile and then entry->Remove(). Nothing is transactional above the level of one file.  
`FSUtils.cpp` → `MoveItem`  
**TIKU:** Durable-media targets make the ordering matter more: the rewrite should decide whether the source delete is allowed before the destination write is committed/flushed.  
**MISSING:** For files the rule holds exactly — rename() first, and on failure copy_bytes then remove(src) per item, with cancel leaving already-transferred items on the destination and gone from the source — but there is no recursive folder case (copy_bytes only streams one regular file's bytes), so a cross-volume folder move is not performed.  
*src/tiku_trk_fs.c:520-533 step_one move branch; src/tiku_trk_fs.c:582 tiku_trk_fs_cancel*

#### FS-031 · core · open
**File copy preserves every attribute except the icon-position attribute, plus permissions, owner, group, modification time and creation time.**  
LowLevelCopy copies data in a buffer sized 128 KB minimum, 1 MB maximum, otherwise a quarter of free RAM rounded to 16 KB, calls destFile.Sync() after every write, then CopyAttributes, then SetPermissions/SetOwner/SetGroup/SetModificationTime/SetCreationTime from the source stat. CopyAttributes skips names for which SkipAttribute() is true — TrackerCopyLoopControl::SkipAttribute matches kSkipAttributes, which contains only kAttrPoseInfo — and specially writes zero-length attributes so they are not lost.  
`FSUtils.cpp` → `LowLevelCopy`  
**TIKU:** With typed VFS descriptors instead of BFS attributes, this is the row that decides what "copying a node" even means: which descriptor fields travel, which are re-derived at the destination, and what happens copying a device node onto a plain file.  

#### FS-032 · important · open
**Copying a symbolic link copies the link itself, not its target, and stamps it with the link MIME type.**  
LowLevelCopy's S_ISLNK branch reads the link text with ReadLink into a MAXPATHLEN buffer, calls destDir->CreateSymLink, sets B_LINK_MIMETYPE via BNodeInfo, and copies permissions/owner/group/mtime/crtime. Attributes are NOT copied for symlinks.  
`FSUtils.cpp` → `LowLevelCopy`  
**CHECKED:** Nothing in the port creates or preserves a symlink on copy: there is no symlink()/readlink() call anywhere in src, and copy_bytes opens the source with fopen so a link's target bytes are copied into a plain file.  
*none found*

#### FS-033 · important · open
**Cancelling during a file copy removes the half-written destination file.**  
The read/write loop checks CheckUserCanceled() before every chunk; on cancel it Unset()s the destination BFile, re-finds it by name in destDir and Remove()s it, then throws kCopyCanceled. The same cleanup runs when ChecksumFile() returns false.  
`FSUtils.cpp` → `LowLevelCopy`  

#### FS-034 · important · open
**Cancelling during a folder copy leaves the partially-copied folder in place; only the current file is cleaned up.**  
CopyFolder's per-entry loop throws kUserCanceled on CheckUserCanceled() with no rollback of the directory already created or of the children already written.  
`FSUtils.cpp` → `CopyFolder`  

#### FS-035 · important · partial
**An error copying one file offers "Error copying file \"<name>\": <error>. Would you like to continue?" and continuing keeps the rest of the job going.**  
CopyFile catches status_t from LowLevelCopy, re-throws kCopyCanceled unchanged, otherwise calls FileError(kFileErrorString, destName, err, allowContinue=true); TrackerCopyLoopControl::FileError builds a two-button Cancel/OK stop-alert and returns true only for OK, in which case the status bar is advanced by the whole file size. Directory-creation failures use kFolderErrorString the same way; delete failures use kFileDeleteErrorString with allowContinue=false (single Cancel button).  
`FSUtils.cpp` → `CopyFile`  
**MISSING:** A failed file copy is caught, counted in prog.failed with the backend's own message kept in prog.error, and the job continues to the next item, but the user is never asked "Would you like to continue?" and gets no Cancel — the per-file error text is not even displayed (the status line only appends "-- some failed").  
*src/tiku_trk_fs.c:535-541; src/tiku_trk_app.c:1538*

#### FS-036 · important · open
**Copying a folder does not descend into mounted volumes underneath it, and non-file/folder/symlink entries are silently ignored.**  
CopyFolder records the source directory's st_dev and skips any child directory whose st_dev differs ("Avoiding mount point"); the else branch of the S_ISREG/S_ISLNK test is an explicit "Ignore special files".  
`FSUtils.cpp` → `CopyFolder`  
**TIKU:** With a device namespace mounted alongside files, this rule is what stops a folder copy from trying to serialize /dev; capability bits should make the skip explicit rather than device-id based.  

#### FS-037 · polish · open
**A copied item lands at the same icon position as the original when copied into a different folder and no drop position was supplied.**  
SetupPoseLocation only inherits the source's position when loc == NULL, sourceParentIno != destParentIno, and the source has a valid kAttrPoseInfo; a loc of (BPoint*)-1 means "autoplace" and writes nothing. MoveTask defaults loc to (BPoint*)-1 and only reads pointList when one was supplied (and, for the pre-write of the source's own position, only when moveMode != kCopySelectionTo).  
`FSUtils.cpp` → `SetupPoseLocation`  
**TIKU:** Icon position is a per-folder view-state concern; if it lives in a view-state store instead of a node attribute, this must be re-implemented as a view-state copy keyed by destination folder.  

#### FS-038 · core · open
**Sending items to the Trash uses the Trash folder of the item's own volume, and colliding names get the "<name> copy" treatment there.**  
MoveEntryToTrash calls FSGetTrashDir(&trashDir, nodeRef.device) — which find_directory()s per-volume, creates the directory with mode 0755 if missing and marks it invisible by writing a PoseInfo with fInvisible = true — then FSMakeOriginalName(name, &trashDir, " copy") when trashDir.Contains(name), reporting the new name to the undo record.  
`FSUtils.cpp` → `MoveEntryToTrash`  
**TIKU:** A device namespace has no Trash; nodes there must be non-trashable (capability bit) rather than falling back to delete.  
**CHECKED:** There is no Trash directory, no per-volume trash lookup and no '<name> copy' renaming on collision — the trash op is an immediate unlink/remove, which is the absence of the row's rule rather than another route to it.  
*src/tiku_trk_fs.c:438-461 (TRASH/DELETE branch: unlink/remove, no destination), src/tiku_trk_app.c:979-990 ("no Trash -- this is final")*

#### FS-039 · core · open
**Trashing an item records its full original path so Restore can put it back, and Restore removes that record on success.**  
MoveEntryToTrash writes the pre-move BPath into the string attribute kAttrOriginalPath ("_trk/original_path"). _RestoreTask reads it back through FSGetOriginalPath, which first tries the entry's own attribute and otherwise walks up parents (stopping at the Trash root, returning B_ENTRY_NOT_FOUND) and re-appends the suffix path below the folder that carried the attribute. On a successful MoveTo the attribute is removed with node.RemoveAttr.  
`FSUtils.cpp` → `MoveEntryToTrash`  
**TIKU:** Without per-node attributes this needs a durable side table in the Trash store; it must survive reboot, so it is a persistence decision, not an in-RAM map.  

#### FS-040 · important · open
**Restore recreates missing parent folders, and refuses to overwrite: if something already occupies the original path the item stays in the Trash.**  
_RestoreTask: if the parent does not exist it calls FSRecursiveCreateFolder(parentPath) (which recurses to the parent, returns B_FILE_EXISTS if the entry already exists, and CreateDirectory's the leaf); the move only happens inside if (!originalEntry.Exists()). Items whose original path cannot be determined are skipped with continue. Cancel breaks the loop.  
`FSUtils.cpp` → `_RestoreTask`  

#### FS-041 · important · open
**Dragging a whole volume to the Trash unmounts it instead of deleting it, and the boot volume refuses with "Cannot unmount the boot volume \"<name>\"."**  
MoveEntryToTrash tests BDirectory(entry).IsRootDirectory(); compares against BVolumeRoster().GetBootVolume(); otherwise posts kUnmountVolume with "device_id" to be_app and returns B_OK.  
`FSUtils.cpp` → `MoveEntryToTrash`  

#### FS-042 · polish · open
**Trashing a folder closes its window and all windows of folders inside it.**  
MoveEntryToTrash posts kCloseWindowAndChildren to be_app with the folder's own node_ref as raw data before performing the move.  
`FSUtils.cpp` → `MoveEntryToTrash`  

#### FS-043 · important · done
**The Trash folder itself, and any folder that contains the Trash, cannot be put in the Trash: "You cannot put the selected item(s) into the trash."**  
MoveEntryToTrash compares dir == trashDir || dir.Contains(&trashEntry) and returns B_OK deliberately so the caller does not raise a second error dialog.  
`FSUtils.cpp` → `MoveEntryToTrash`  
**PORT:** Scoped to the destructive op, as the source scopes it to MoveEntryToTrash -- applied to every queue it silently dropped plain copies of any folder holding a Trash.

#### FS-044 · important · partial
**Items on a volume that has no Trash cannot be trashed; the user is offered outright deletion instead, and the two groups are executed as two separate jobs.**  
CopyOneTrashedRefAsEntry splits the selection into entriesToTrash and entriesToDeleteOnTheSpot, caching the FSGetTrashDir result per device in a std::map; volumes (root directories) always go to the trash list because they will be unmounted. If the delete-on-the-spot list is non-empty the user gets "...cannot be moved to the Trash. Would you like to delete it/them instead? (This operation cannot be reverted.)" with Cancel/Delete, Escape = Cancel; then MoveListToTrash runs once for each list.  
`PoseView.cpp` → `BPoseView::MoveSelectionOrEntryToTrash`  
**MISSING:** Outright deletion is what happens when there is no Trash (which here is always), but the user is never offered the Cancel/Delete choice — the message is a status line printed as the delete starts, not a prompt — and there is no split of the selection into a trash list and a delete-on-the-spot list run as two jobs.  
*src/tiku_trk_app.c:983-991*

#### FS-045 · important · open
**Files on a read-only volume can be neither moved nor deleted: "Files cannot be moved or deleted from a read-only volume."**  
CheckVolumeReadOnly raises a B_STOP_ALERT and returns false; it gates both MoveSelectionOrEntryToTrash and DeleteSelection, and is tested on the first selected item's device only.  
`PoseView.cpp` → `CheckVolumeReadOnly`  

#### FS-046 · core · partial
**Delete asks a three-way question — Cancel / Move to Trash / Delete — with Escape, 'm' and 'd' as shortcuts, and warns the operation cannot be reverted.**  
_DeleteTask shows kDeleteConfirmationStr when confirm is set; case 0 deletes the list and returns B_CANCELED, case 1 (and default) diverts to FSMoveToTrash(list, NULL, false) — running it synchronously on the delete thread — and only case 2 falls through to the real delete.  
`FSUtils.cpp` → `_DeleteTask`  
**MISSING:** Only the irreversibility warning survives: Delete writes "deleting N items (no Trash -- this is final)" and immediately runs the delete; there is no three-way Cancel / Move to Trash / Delete question and no Escape/'m'/'d' shortcuts.  
*src/tiku_trk_app.c:981-993 (XK_Delete)*

#### FS-047 · important · open
**Deleting from inside the Trash window skips the confirmation entirely, and Shift+Delete anywhere deletes instead of trashing.**  
BPoseView::DoDelete and DoMoveToTrash both call ExcludeTrashFromSelection() first and then, if TargetModel()->IsTrash(), DeleteSelection(true, confirm=false). DoMoveToTrash reads B_SHIFT_KEY out of the window's current message and calls DeleteSelection() (with confirmation) when it is down. The bindings are Delete -> kMoveSelectionToTrash and Shift+Delete -> kDeleteSelection.  
`PoseView.cpp` → `BPoseView::DoMoveToTrash`  

#### FS-048 · core · open
**Empty Trash empties the Trash of every mounted volume at once, skipping read-only and non-persistent volumes, and leaves the Trash folders themselves in place.**  
empty_trash does two passes over BVolumeRoster: one summing items via CalcItemsAndSize (decrementing totalCount by one per volume so the Trash folder is not counted) and one calling FSDeleteFolder(&entry, &loopControl, updateStatus=true, deleteTopDir=false). Failures other than kTrashCanceled/kUserCanceled produce "Error emptying Trash". FSEmptyTrash refuses to start if the thread "_tracker_empty_trash_" already exists.  
`FSUtils.cpp` → `empty_trash`  

#### FS-049 · important · open
**Deleting a folder tree continues past individual failures, reporting each one, rather than aborting the whole delete.**  
FSDeleteFolder recurses, calls loopControl->FileError(kFileDeleteErrorString, ref.name, status, allowContinue=false) for any non-OK, non-cancel status and keeps iterating; only kTrashCanceled propagates. CheckUserCanceled is polled per entry and again before removing the top directory.  
`FSUtils.cpp` → `FSDeleteFolder`  

#### FS-050 · polish · open
**After a delete or restore in list view, the next item down is selected automatically.**  
BPoseView::Delete / MoveListToTrash / RestoreItemsFromTrash build a two-element task list run in order by ThreadSequence::Launch: the FS call first (FSDeleteRefList/FSMoveToTrash/FSRestoreRefList with async=false), then TTracker::SelectPoseAtLocationSoon with the target folder's node_ref and a point computed from the deleted pose's list index. Only done when ViewMode() == kListMode.  
`PoseView.cpp` → `BPoseView::Delete`  

#### FS-051 · core · open
**Cut and Copy put the selection on the system clipboard; Shift turns them into "Cut more"/"Copy more", which add to what is already there instead of replacing it.**  
B_CUT -> FSClipboardAddPoses(dir, selection, kMoveSelectionTo, clearClipboard=true); kCutMoreSelectionToClipboard -> same with false; B_COPY / kCopyMoreSelectionToClipboard -> kCopySelectionTo with true/false. TShortcuts::CutLabel/CopyLabel swap the menu text on B_SHIFT_KEY and CutCommand/CopyCommand swap the command.  
`FSClipboard.cpp` → `FSClipboardAddPoses`  
**TIKU:** There is no system clipboard; the rewrite needs a clipboard service (a namespace node would let other processes read it, which is a deliberate exposure decision).  

#### FS-052 · important · open
**Volumes, the root, the Trash and the Desktop are silently excluded from a Cut or Copy.**  
FSClipboardAddPoses skips any pose whose model IsVolume() || IsRoot() || IsTrash() || IsDesktop() before touching the clipboard; the returned refsAdded count therefore can be lower than the selection size.  
`FSClipboard.cpp` → `FSClipboardAddPoses`  

#### FS-053 · important · open
**An item can appear on the clipboard only once: cutting something that was already copied changes its mode rather than adding a second entry.**  
In the non-clearing path FSClipboardAddPoses first tries ReplaceInt32/ReplaceRef on the "m<dev>_<ino>"/"r<dev>_<ino>" field pair and only falls back to AddRef/AddInt32 when the replace fails; failed replaces emit a tcnode with moveMode kDelete so listeners drop the stale highlight.  
`FSClipboard.cpp` → `FSClipboardAddPoses`  

#### FS-054 · important · open
**Cut items are drawn semi-transparent in every window that shows them, and the dimming clears as soon as the clipboard is cleared or the item is pasted.**  
BPose::Draw switches to B_OP_ALPHA with B_CONSTANT_ALPHA and alpha 128 when fClipboardMode == kMoveSelectionTo; BPose::DrawTextWidget additionally blends the selection rectangle with grey for cut items. fClipboardMode is set only from the kFSClipboardChanges broadcast via BPoseView::UpdatePosesClipboardModeFromClipboard / SetPosesClipboardMode.  
`Pose.cpp` → `BPose::Draw`  
**TIKU:** The broadcast becomes a VFS watch notification; every window showing the node must re-render, which is exactly the watch semantics ("touched, re-read").  

#### FS-055 · core · open
**Paste moves the items when they were Cut and copies them when they were Copied, and a second Paste always copies — Cut is consumed by the first paste.**  
FSClipboardPaste sorts each clipboard ref into moveList (linksMode or moveMode == kMoveSelectionTo), copyList (kCopySelectionTo) or duplicateList, then for any entry whose moveMode was kMoveSelectionTo sets newMoveMode = kCopySelectionTo and rewrites the clipboard field with clip->ReplaceInt32(modeName, kCopySelectionTo), emitting a kDelete tcnode so the dimming clears.  
`FSClipboard.cpp` → `FSClipboardPaste`  

#### FS-056 · important · open
**Pasting copied items into the folder they already live in duplicates them ("<name> copy"); pasting cut items into their own folder does nothing.**  
sameDirectory is destNodeRef->device == ref.device && destNodeRef->node == ref.directory; in that case the item goes to duplicateList only when moveMode != kMoveSelectionTo, and duplicateList is dispatched as FSMoveToFolder(..., kDuplicateSelection). Note that in links mode moveMode is never read (the FindInt32 is skipped), so it stays 0 and a Paste-links into the item's own folder falls into the duplicate path rather than creating a link.  
`FSClipboard.cpp` → `FSClipboardPaste`  

#### FS-057 · important · open
**Shift+Paste ("Paste links") creates symbolic links to the clipboard items instead of moving or copying them, regardless of whether they were cut or copied.**  
kPasteLinksFromClipboard -> FSClipboardPaste(TargetModel(), kCreateLink); inside, linksMode routes every existing entry into moveList and the dispatch is FSMoveToFolder(moveList, entry, linksMode). TShortcuts::PasteLabel/PasteCommand swap on B_SHIFT_KEY.  
`FSClipboard.cpp` → `FSClipboardPaste`  

#### FS-058 · important · open
**Clipboard entries whose files no longer exist are dropped from the clipboard at paste time instead of producing an error.**  
FSClipboardPaste: !entry.Exists() -> clip->RemoveName(refName); clip->RemoveName(modeName); newMoveMode = kDelete, which also emits a tcnode so the poses stop being drawn as cut.  
`FSClipboard.cpp` → `FSClipboardPaste`  

#### FS-059 · polish · open
**Paste is refused into the Disks root with "You must drop items on one of the disk icons in the \"Disks\" window."**  
FSClipboardPaste checks model->IsRoot() after processing the clipboard (so the clipboard has already been mutated) and, together with the copy-into-Trash check, sets okToMove = false and deletes all three lists before returning false. BPoseView::MoveSelectionInto raises the identical alert for drops.  
`FSClipboard.cpp` → `FSClipboardPaste`  

#### FS-060 · important · open
**Clipboard entries follow their files: renaming or moving a cut file updates the clipboard, deleting it or unmounting its volume removes it.**  
BClipboardRefsWatcher node-monitors every clipboard node with B_WATCH_NAME plus a global B_WATCH_MOUNT. B_ENTRY_MOVED -> UpdateNode(node, new ref) which ReplaceRefs, or removes both fields and stops watching if the replace fails; B_ENTRY_REMOVED -> RemoveNode(node, removeFromClipboard=true); B_DEVICE_UNMOUNTED -> RemoveNodesByDevice(device), which matches clipboard field names by the "r<dev>_" prefix.  
`FSClipboard.cpp` → `BClipboardRefsWatcher::MessageReceived`  
**TIKU:** Maps onto VFS watch notifications, but VFS watches are per-boot and capped (TIKU_VFS_WATCH_MAX 8) while the clipboard can hold arbitrarily many nodes — the rewrite needs either a bigger watch table or a lazy revalidation-on-paste strategy.  

#### FS-061 · important · open
**When another application replaces the clipboard contents, all cut/copy highlighting in Tracker disappears.**  
BClipboardRefsWatcher watches be_clipboard; on B_CLIPBOARD_CHANGED, if it previously held refs and FSClipboardHasRefs() is now false, Clear() stops all node watching, re-arms B_WATCH_MOUNT, and broadcasts kFSClipboardChanges with clearClipboard = true to every registered pose view.  
`FSClipboard.cpp` → `BClipboardRefsWatcher::Clear`  

#### FS-062 · polish · open
**Escape clears the file clipboard; Shift+Escape removes just the selected items from it.**  
B_CANCEL -> if (FSClipboardHasRefs()) FSClipboardClear(); kCancelSelectionToClipboard -> FSClipboardRemovePoses on the selection, or on the whole pose list when nothing is selected. The Shift+Escape binding is installed as ShortcutFilter(B_ESCAPE, B_SHIFT_KEY, kCancelSelectionToClipboard).  
`PoseView.cpp` → `BPoseView::MessageReceived`  

#### FS-063 · core · partial
**Undo (Cmd+Z) reverses the last file operation and Redo (Shift+Cmd+Z) replays it; the stack holds at most 20 operations and any new operation wipes the redo stack.**  
kUndoRedoListMaxCount = 20 on each of sUndoList/sRedoList under one BLocker; AddUndoItem drops the tail when full and calls sRedoList.MakeEmpty(). FSUndo pops the head of the undo list, runs Undo() (return code explicitly ignored) and pushes it on the redo list, and vice versa. The stack is process-global and RAM-only. BContainerWindow routes B_UNDO/B_REDO to FSUndo/FSRedo only when the focused view is not a BTextView.  
`FSUndoRedo.cpp` → `FSUndo`  
**TIKU:** An undo stack over durable nodes could itself be durable; also, undoing a write to a device-namespace node is generally not possible, so those operations must be marked non-undoable.  
**MISSING:** Ctrl+Z undoes the last operation and names it, but the stack is one operation deep (undo_reset() clears the records at the start of every new operation) and there is no Redo at all — no redo list, no Shift+Ctrl+Z, no TIKU_TRK_CMD_UNDO/REDO in the action table.  
*src/tiku_trk_fs.c:294 undo_reset; src/tiku_trk_app.c:1009-1023*

#### FS-064 · important · partial
**Undoing a copy or a link deletes the copies that were made, without asking for confirmation.**  
UndoItemCopy::Undo calls FSDeleteRefList(new list(fTargetList), async=true, confirm=false). fTargetList is the source list with every ref's device/directory rewritten to the destination folder by ChangeListSource, and individual names patched by UndoItemCopy::UpdateEntry whenever FSMakeOriginalName renamed a destination. Redo re-runs FSMoveToFolder with the original moveMode, stamped as an undo-mode operation.  
`FSUndoRedo.cpp` → `UndoItemCopy::Undo`  
**MISSING:** Undoing a copy does delete exactly what the copy created, with no confirmation (each copied destination is pushed with created=1 and removed on undo), but the link half cannot happen because the port has no Create Link operation.  
*src/tiku_trk_fs.c:539 undo_push(created=1); src/tiku_trk_fs.c:664 tiku_trk_fs_undo*

#### FS-065 · important · partial
**Undoing a move (including a move to the Trash) moves the items back to the folder the first item came from.**  
UndoItemMove records fSourceRef = parent of sourceList->ItemAt(0) and fTargetRef = the destination; Undo() copies the source list, ChangeListSource()s it to the target folder and FSMoveToFolder()s it back to fSourceRef. Because only the first item's parent is recorded, a selection spanning several folders is restored into one folder. MoveTask builds this undo item even for the Trash case, since the trash move uses kMoveSelectionTo.  
`FSUndoRedo.cpp` → `UndoItemMove::Undo`  
**MISSING:** Undoing an ordinary move works and records each item's own source (a real, better variant of the first-item-parent rule), but the row's 'including a move to the Trash' clause cannot fire: the trash branch pushes no undo record so can_undo returns 0.  
*src/tiku_trk_fs.c:532 undo_push(fs, dstpath, src, 0) vs :438-461 trash branch (no undo_push) and :637 can_undo returning 0 on undo_count==0*

#### FS-066 · important · partial
**Undoing New Folder deletes the folder; undoing a rename restores the old name; undoing a volume rename restores the old volume name.**  
UndoItemFolder::Undo -> FSDelete(new entry_ref(fRef), async=false, confirm=false), Redo -> FSCreateNewFolder(&fRef). UndoItemRename stores both refs and Rename()s back and forth. UndoItemRenameVolume captures the old name via BVolume::GetName in its constructor and uses BVolume::SetName. The rename undo items are created inside EditModelName and removed again (undo.Remove()) if the rename itself failed.  
`FSUndoRedo.cpp` → `UndoItemFolder::Undo`  
**MISSING:** Only the rename clause works (tiku_trk_fs_rename records the reversal and Ctrl+Z renames back, tests/test_fs.c:159-166); there is no New Folder operation at all — TIKU_TRK_CMD_NEW_FOLDER is only added to menus and never dispatched — and no volume rename.  
*src/tiku_trk_fs.c:620-621 tiku_trk_fs_rename*

#### FS-067 · important · done
**Delete, Empty Trash and Restore from Trash are not undoable at all.**  
Only MoveTask (via MoveCopyUndo), FSCreateNewFolderIn (NewFolderUndo) and EditModelName (RenameUndo/RenameVolumeUndo) construct Undo objects; _DeleteTask, empty_trash and _RestoreTask construct none, which is why the delete confirmation says "This operation cannot be reverted."  
`FSUtils.cpp` → `_DeleteTask`  

#### FS-068 · important · done
**An operation replayed by Undo or Redo is not itself pushed onto the undo stack.**  
UndoItem replays call FSMoveToFolder with FSUndoMoveMode(mode), which sets the top byte to 'U'; MoveTask evaluates fromUndo = FSIsUndoMoveMode(moveMode), strips the tag with FSMoveMode(), and calls undo.Remove() when fromUndo is set so the Undo destructor never registers it.  
`FSUtils.cpp` → `MoveTask`  

#### FS-069 · core · partial
**A progress window appears for copies, moves, deletes, Empty Trash and Restore, showing the current file name, an "n of N" counter, the destination folder as "To: <dir>", and a progress bar.**  
BStatusView's caption per StatusWindowState: kCopyState "Preparing to copy items..."/"Copying: ", kMoveState "Moving: ", kCreateLinkState "Creating links: ", kTrashState "Emptying Trash... ", kDeleteState "Deleting: ", kRestoreFromTrashState "Restoring: ". InitStatus builds the trailing "of %items" text and sets max value 1; _DestinationString renders "To: %dir" from the destination entry's leaf name.  
`StatusWindow.cpp` → `BStatusView::InitStatus`  
**TIKU:** On a shell/console front end this is a line of progress output; the per-job identity and the cancel channel still have to exist.  
**MISSING:** Progress is a single status line carrying the "n of N" counter (plus skipped/failed suffixes) while the operation is stepped without blocking the window; the current file name is tracked in prog.current but never shown, and there is no per-operation caption, no "To: <dir>", no progress bar and no cancel channel wired (tiku_trk_fs_cancel has no caller).  
*src/tiku_trk_app.c:1509-1546; src/tiku_trk_fs.c:435*

#### FS-070 · polish · open
**Creating links only shows a progress window when more than ten items are involved.**  
InitCopy's kCreateLink case: if (numItems > 10) it calls Init(kCreateLinkState) and Init(numItems, numItems, destRef) — otherwise no status item is ever created and the operation runs invisibly.  
`FSUtils.cpp` → `InitCopy`  

#### FS-071 · polish · open
**The status line alternates between transfer speed and estimated finish time, showing speed for 8 seconds then the estimate for 4 seconds.**  
StatusBackgroundView::Pulse (pulse rate 1 s) toggles the file-static sShowSpeed using kShowSpeedInterval = 8 s and kShowEstimatedFinishInterval = 4 s; the toggle state is shared by all concurrent jobs and reset when the first status item is created. Speed is a rolling mean over kBytesPerSecondSlots samples taken every kSpeedReferenceInterval = 2 s. Long estimates degrade to "Finish: after several years", estimates beyond a day include the date, and both speed and time strings have short forms used when the full one does not fit; the tooltip carries both.  
`StatusWindow.cpp` → `BStatusView::_TimeStatusString`  

#### FS-072 · important · open
**Each concurrent file operation gets its own row in one shared status window; the window appears when the first job starts and hides when the last one finishes.**  
BStatusWindow::CreateStatusItem stacks a new BStatusView below the last one and grows the window; RemoveStatusItem removes the row, shifts the ones below up, shrinks the window and Hide()s it when the count reaches zero. Rows are matched to jobs by thread_id.  
`StatusWindow.cpp` → `BStatusWindow::CreateStatusItem`  

#### FS-073 · polish · open
**If the Desktop was the active window when an operation started, focus is handed back to it when the status window closes.**  
CreateStatusItem scans be_app's windows for an active BDeskWindow and records fRetainDesktopFocus; a second job ANDs into the same flag, and WindowActivated(false) clears it. RemoveStatusItem re-Activate()s the desk window when the last row goes away.  
`StatusWindow.cpp` → `BStatusWindow::RemoveStatusItem`  

#### FS-074 · core · open
**Each progress row has a pause and a stop button; pause freezes the operation and the row reads "Paused: click to resume or stop", stop cancels it.**  
kPauseButton toggles fIsPaused, zeroes the speed samples and, on resume, resets the estimate reference and resume_thread(Thread()). kStopButton sets fWasCanceled and also resumes a paused thread so the copy loop can notice the cancel and unwind. BStatusWindow::CheckCanceledOrPaused returns wasCanceled immediately, and otherwise suspend_thread()s the calling worker.  
`StatusWindow.cpp` → `BStatusView::MessageReceived`  
**TIKU:** suspend_thread on the worker has no cooperative equivalent — pause must become a flag the copy protothread checks at its yield points.  

#### FS-075 · polish · open
**Progress text updates are throttled to roughly ten per second, but the name of the file being processed is never lost — it is stashed and shown at the next real update.**  
BStatusView::UpdateStatus only redraws when !optional or (now - fLastUpdateTime) > kMaxUpdateInterval (100000 us); otherwise a supplied curItem is copied into fPendingStatusString (127 chars) and used on the next update. Bytes are always accumulated into fItemSize/fSizeProcessed regardless of throttling. LowLevelCopy splits chunks larger than 32 KB into two status updates, one after the read and one after the write, for smoother movement.  
`StatusWindow.cpp` → `BStatusView::UpdateStatus`  

#### FS-076 · important · open
**Quitting Tracker cancels every running copy/move/Empty Trash job rather than killing it mid-write.**  
BStatusWindow::AttemptToQuit returns true only when no status items exist; otherwise it calls SetWasCanceled() on every row and returns false so the quit is retried after the jobs unwind.  
`StatusWindow.cpp` → `BStatusWindow::AttemptToQuit`  

#### FS-077 · important · open
**Dropping files from another application onto a folder copies them there, laid out in rows of four starting at the drop point.**  
BPoseView::HandleDropCommon's HasRef("refs") branch: defaults to canCopy (kCopySelectionTo) unless a drop context menu chose otherwise, bails when the target is not a directory or virtual directory, collects every "refs" entry into a list, builds a pointList that advances four columns per row using fGrid and PinToGrid, and hands it to FSMoveToFolder.  
`PoseView.cpp` → `BPoseView::HandleDropCommon`  
**TIKU:** Cross-application drops presuppose a drag protocol; on TikuOS this is where an incoming stream from another process/tier lands, and the "copy by default" rule should still hold.  

#### FS-078 · polish · open
**Dragging items out of a virtual directory creates links rather than moving the underlying files, unless a copy/move/relative-link was explicitly requested.**  
BPoseView::MoveSelectionInto: when srcWindow->TargetModel()->IsVirtualDirectory() it resolves each pose to its ResolvedModel entry_ref and then sets createLink = true if none of forceCopy/forceMove/createRelativeLink was requested.  
`PoseView.cpp` → `BPoseView::MoveSelectionInto`  
**TIKU:** A device namespace is exactly this case: dragging a /dev node out should produce a reference, not a byte copy, unless the user explicitly asks to snapshot it.  
**VERIFIER CORRECTION:** That formula (FSUtils.cpp:2636-2647) applies only to the top-level entries in the ref list. Recursion through FSRecursiveCalcSize (line 2580) instead charges statbuf.st_blocks * 512 per entry, files and directories alike, and does not add a block per directory. The block-size clamps ([1024,8192], 2048 when the volume block size is unavailable) are correct.  

#### FS-079 · polish · open
**New folder is named "New folder", then "New folder 2" ... "New folder 9", then "New folder10" without a space.**  
FSCreateNewFolderIn increments fnum before formatting and switches format strings at >9, producing the missing space; the folder gets B_DIR_MIMETYPE and a NewFolderUndo is registered. Failure raises "Sorry, could not create a new folder." The separate FSCreateNewFolder (used by Redo) instead uniquifies with FSMakeOriginalName(name, &dir, " -") and writes the chosen name back into the caller's entry_ref.  
`FSUtils.cpp` → `FSCreateNewFolderIn`  

#### FS-080 · important · partial
**Renaming refuses names longer than the file-name limit, names already used in the folder, and rename of the Desktop, root, Trash, virtual directories or localized-name items; renaming a read-only volume's item fails with a read-only error.**  
ShouldEditRefName alerts "The entered folder/file name is too long." (B_NAME_TOO_LONG), returns B_OK unchanged when the name is identical, defers to ConfirmChangeIfWellKnownDirectory(kRename) (B_CANCELED), and alerts "An item named '<name>' already exists." (B_NAME_IN_USE). EditModelName returns B_NOT_ALLOWED for localized-name/Desktop/root/Trash/virtual-directory models, checks BVolume::IsReadOnly for both the volume-rename and normal cases, and clears the query-temporary flag when renaming a query.  
`FSUtils.cpp` → `ShouldEditRefName`  
**TIKU:** Namespace nodes need a renamable capability bit; the "already exists" test is a directory-scoped uniqueness rule that must hold across the merged file + device view.  
**MISSING:** Rename refuses empty/"."/".."/slash names, refuses a name already used in the folder with "'<name>' already exists here", and refuses device-namespace and virtual-directory nodes up front, but there is no name-too-long alert (an over-long join just returns -1 silently) and no read-only-volume error path.  
*src/tiku_trk_fs.c:592 tiku_trk_fs_rename; src/tiku_trk_app.c:657-664 begin_rename*

#### FS-081 · polish · open ⟨found by verification⟩
**Every mounted volume gets a Trash folder created at startup, and the Trash folder is (re)marked invisible every time it is looked up.**  
FSCreateTrashDirs walks BVolumeRoster skipping read-only, non-persistent and zero-capacity volumes and calls FSGetTrashDir for each; FSGetTrashDir create_directory()s the path with mode 0755 when missing and then unconditionally writes a PoseInfo with fInvisible = true (icon attributes are left to TrashWatcher).  
`FSUtils.cpp` → `FSCreateTrashDirs / FSGetTrashDir`  

#### FS-082 · polish · open ⟨found by verification⟩
**The Desktop folder gets its icon written into it the first time Tracker looks it up, so the Desktop always has the right icon even on a foreign disk.**  
FSGetDeskDir writes kAttrIcon (B_VECTOR_ICON_TYPE, resource R_DeskIcon) when absent, falling back to writing kAttrLargeIcon ('ICON') and kAttrMiniIcon ('MICN').  
`FSUtils.cpp` → `FSGetDeskDir`  


---

## Model, attributes, editable text widgets

Architecture notes: [`spec/ARCH-model-attributes.md`](spec/ARCH-model-attributes.md)

#### MA-001 · core · differs
**Every entry is classified into one of eleven kinds, and that kind decides icon, sort bucket, what window it opens, and whether it can be renamed.**  
From st_mode alone: S_IFDIR -> kDirectoryNode, S_IFREG -> kExecutableNode if S_IXUSR is set else kPlainNode, S_IFLNK -> kLinkNode, anything else -> kUnknownNode. Kinds added later by FinishSettingUpType: query, query template, virtual directory, volume, root, trash, desktop. IsDirectory() is true for directory/volume/root/trash/desktop; IsFile() for plain/query/query-template/executable/virtual-directory; IsContainer() = query || directory || virtual directory (Model.h inlines).  
`Model.cpp` → `Model::SetupBaseType`  
**TIKU:** TikuOS VFS nodes are typed descriptors already; the kind should come from the descriptor type + capability bits rather than being re-derived from a stat mode. Device nodes need at least one new kind (e.g. kDeviceNode) that is neither file nor directory.  
**PORT:** The port classifies every entry into a kind enum and that kind really does decide the icon, container-ness (hence what window opens), the sort bucket and rename refusal; only its derivation moves — from the backend descriptor (is_dir + path + manifest) instead of st_mode, with DEVICE_NODE/VIRTUAL_DIR standing in for the Be-specific kinds, which is the TIKU note's adaptation.  
*src/tiku_trk_model.h:36-45 (kinds); src/tiku_trk_backend_tiku.c:73-85 (kind_of); src/tiku_trk_model.c:19-33 (is_container), :176-210 (icon_name); src/tiku_trk_poseview.c:355-360 (containers-first sort); src/tiku_trk_app.c:655-663 (rename refusal by kind)*

#### MA-002 · important · open
**Opening a node upgrades its kind and fills in MIME type and preferred application; a file whose BEOS:TYPE says query / query-template / virtual-directory is treated as that kind, not as a plain file.**  
Only runs when the node is open and the kind is not directory/volume/symlink. BEOS:TYPE == "application/x-vnd.Be-query" -> kQueryNode; "application/x-vnd.Be-queryTemplate" -> kQueryTemplateNode; "application/x-vnd.haiku-virtual-directory" -> kVirtualDirectoryNode. Preferred app is read from BNodeInfo::GetPreferredApp; for executables it instead comes from the BEOS:APP_SIG attribute via GetAppSignatureFromAttr. Presence of a Media:Thumbnail attribute, or a MIME type that ShouldGenerateThumbnail() accepts (image/*, and only when the Generate image thumbnails setting is on), sets the icon source to kNode.  
`Model.cpp` → `Model::FinishSettingUpType`  
**TIKU:** MIME-from-attribute becomes type-from-VFS-descriptor. "Preferred app" has no analogue unless TikuOS grows a handler registry; decide whether to drop it or map it to a launcher capability.  

#### MA-003 · important · differs
**Each kind gets a default MIME type even when the node carries none, so the Kind column is never blank.**  
kDirectoryNode -> application/x-vnd.Be-directory (overridden by the node's own BEOS:TYPE if present), kVolumeNode -> application/x-vnd.Be-volume, kRootNode -> application/x-vnd.Be-root, kLinkNode -> application/x-vnd.Be-symlink, kExecutableNode with no type -> application/x-vnd.Be-app, everything else -> application/octet-stream.  
`Model.cpp` → `Model::FinishSettingUpType`  
**TIKU:** Needs a synthetic type string per VFS node class (device, stream, sensor...) so the Kind column stays populated for namespace entries.  
**PORT:** The row's outcome — the Kind column is never blank whatever the node carries — is delivered by a per-kind/per-type string table that always returns a non-empty label (falling through to "Unknown"), backed by synthetic type strings the backends stamp on every node, instead of by synthesising a MIME type per kind.  
*src/tiku_trk_model.c:136-166 (kind_string, exhaustive with default) reached from src/tiku_trk_poseview.c:178-180 (COL_KIND); src/tiku_trk_backend_tiku.c:130-135 (synthetic type strings)*

#### MA-004 · important · partial
**A directory that is the root of its device is shown as a volume, and a volume that is its own parent is shown as the filesystem root ("Disks").**  
OpenNodeCommon promotes kDirectoryNode -> kVolumeNode when BDirectory::IsRootDirectory() is true. FinishSettingUpType promotes kVolumeNode -> kRootNode when NodeRef()->node == fEntryRef.directory && device matches. A volume's displayed name is fetched with BVolume::GetName and cached in the union field.  
`Model.cpp` → `Model::OpenNodeCommon`  
**TIKU:** Mount points become durable mounts in the TikuOS namespace; the promotion rule should key off a mount-root flag on the VFS node instead of an inode identity trick.  
**MISSING:** The device root is promoted to a volume (and draws the "root" icon) by a path=="/" test rather than a mount-root flag, but the second clause is missing entirely: nothing promotes a volume to a filesystem root, there is no "Disks" pose, and the posix backend never yields a volume at all.  
*src/tiku_trk_backend_tiku.c:76 kind_of / src/tiku_trk_model.c:202 tiku_trk_model_icon_name*

#### MA-005 · important · open
**Trash and Desktop folders are recognized as special kinds regardless of their path, which is what makes them undeletable/unrenameable and gives them their own icons.**  
In the kDirectoryNode branch, FSIsTrashDir(&entry) -> kTrashNode, FSIsDeskDir(&entry) -> kDesktopNode.  
`Model.cpp` → `Model::FinishSettingUpType`  
**TIKU:** TikuOS has no Trash today; either provide one or mark these kinds absent and drop the associated rename/delete refusals.  

#### MA-006 · core · open
**The name shown to the user is not always the on-disk name: the filesystem root shows "Disks", volumes show the volume name, Trash and Desktop show their translated names, and other entries show their localized name when the system prefers localized filenames.**  
Switch on fBaseType: kRootNode -> B_TRANSLATE(B_DISKS_DIR_NAME), kVolumeNode -> fVolumeName, kTrashNode -> B_TRANSLATE(B_TRASH_DIR_NAME), kDesktopNode -> B_TRANSLATE(B_DESKTOP_DIR_NAME). Otherwise fLocalizedName if fHasLocalizedName && gLocalizedNamePreferred, else fEntryRef.name. The "Real name" column deliberately bypasses all of this.  
`Model.cpp` → `Model::Name`  
**TIKU:** Device namespace nodes will want display names distinct from their path leaf ("/dev/led0" -> "LED 0"); this is the hook for it.  

#### MA-007 · polish · open
**A localized display name is looked up at most once per model and only when the system-wide "prefer localized filenames" flag is set.**  
fLocalizedNameIsCached latches on first call; BLocaleRoster::GetLocalizedFileName decides fHasLocalizedName. Called from OpenNodeCommon only when gLocalizedNamePreferred. The flag itself is set at Tracker startup and updated on B_LOCALE_CHANGED (Tracker.cpp).  
`Model.cpp` → `Model::CacheLocalizedName`  
**TIKU:** Likely dropped; no locale roster on-device. If dropped, the corresponding rename refusal for localized names must also go.  

#### MA-008 · core · partial
**A symlink pose behaves as its target for icons, sorting, drops and size, but keeps its own name and its own underline decoration.**  
ResolveIfLink() returns fLinkTo when the model is a symlink and the target model has been created, otherwise returns this (never NULL). SetLinkTo() deletes any previous target model. The copy constructor deep-copies the link target.  
`Model.cpp` → `Model::ResolveIfLink`  
**TIKU:** If the namespace exposes aliases/mounts, decide whether they resolve like symlinks or stay opaque.  
**VERIFIER CORRECTION:** Shortcuts.cpp TShortcuts::UpdatePasteItem: isPastable = FSClipboardHasRefs() && !SelectionIsReadOnly() && !IsTrash(). SelectionIsReadOnly() is BPoseView::SelectedVolumeIsReadOnly(), which checks the SELECTION's volume (PoseView.cpp:8867) and only falls back to the target volume when nothing is selected. Consequence the row hides: with a volume or the root pose selected, Paste is disabled (a selected root pose is unconditionally "read-only"), even in a writable folder.  
**MISSING:** A symlink keeps its own name and gets its underline decoration, and its size/mtime come from the target's stat, but there is no ResolveIfLink equivalent — icon, container-ness (sort bucket) and drop targeting all use the link's own SYMLINK kind, so a link to a folder sorts and drops as a file.  
*src/tiku_trk_backend_posix.c:76 px_stat / src/tiku_trk_app.c:485 (underline) / src/tiku_trk_model.c:194 icon_name*

#### MA-009 · important · partial
**With "sort folder names first" on, sorting puts the Disks root first, then real volumes, then folders, then files; only within a bucket are names compared.**  
Both sides are resolved through symlinks first. Root beats everything. A volume whose BVolume::Capacity() == 0 is demoted and sorted as a plain directory. Directories and virtual directories share the folder bucket. Ties fall through to NaturalCompare(Name(), compare->Name()) — a locale collator with numeric sorting on and B_COLLATE_SECONDARY strength, so "file10" sorts after "file9" and case is largely ignored.  
`Model.cpp` → `Model::CompareFolderNamesFirst`  
**TIKU:** Device namespace directories should join the folder bucket; decide where non-file typed nodes (streams, sensors) sort.  
**MISSING:** Sorting buckets containers ahead of non-containers and falls through to a natural, case-folded name compare, but the bucket ladder is one bit: no Disks-root-first tier, no volumes-before-folders tier, and no zero-capacity volume demotion.  
*src/tiku_trk_poseview.c:355 pose_cmp / src/tiku_trk_poseview.c:283 natural_cmp*

#### MA-010 · important · open
**A model's node is opened lazily and closed again afterwards, and asking for write access on an already-open read-only node re-opens it.**  
OpenNode(writable) returns B_OK immediately if already open at the requested writability, otherwise calls OpenNodeCommon which deletes and recreates the BNode for file-like kinds but only creates it if absent for directory/symlink kinds. ModelNodeLazyOpener restores the previous open state on scope exit (close if it was closed, demote to read-only if it was read-only).  
`Model.cpp` → `Model::OpenNode`  
**TIKU:** Maps to opening a VFS descriptor; the read-only vs writable distinction should come from the node's capability bits, and a write open on a read-only node must fail rather than silently succeed.  

#### MA-011 · important · done
**An entry that could not be initialized when first seen is kept as a "zombie" and retried later; when it becomes real it turns into a normal pose.**  
UpdateStatAndOpenNode re-runs BEntry::GetStat before opening, unlike OpenNode which trusts the cached stat. BPoseView keeps fZombieList and calls this from the node-monitor path.  
`Model.cpp` → `Model::UpdateStatAndOpenNode`  
**TIKU:** Namespace nodes that appear before their driver is ready are the same situation; the watch notification should drive the retry instead of polling.  

#### MA-012 · core · open
**When a file's type or preferred application attribute changes, the displayed Kind updates and the icon is re-fetched — unless the icon comes from the node itself, in which case the icon is left alone.**  
Called on a node-monitor attribute change. Returns true (icon needs updating) when attrName is NULL, or is BEOS:TYPE or BEOS:PREF_APP *and* fIconFrom != kNode. On those attributes it re-reads the type into fMimeType (empty string if the read fails) and re-reads the preferred app, skipping the preferred-app update for volumes and symlinks. A NULL attrName means "force a full update".  
`Model.cpp` → `Model::AttrChanged`  
**TIKU:** TIKU_EVENT_VFS carries only "this node was touched", with no coalescing and no attribute name — so the rewrite gets the attrName==NULL (full re-read) path for every notification unless the event grows a field.  

#### MA-013 · important · open
**Changing any of the four icon-bearing attributes forces an icon refresh even if nothing else about the file changed.**  
Exactly BEOS:ICON (kAttrIcon), BEOS:L:STD_ICON (kAttrLargeIcon), BEOS:M:STD_ICON (kAttrMiniIcon), Media:Thumbnail (kAttrThumbnail). Returns false for a NULL attrName. BPoseView::AttributeChanged synthesizes an attr_info type for each of these so removal of the attribute is handled too.  
`Model.cpp` → `Model::IconAttrChanged`  
**TIKU:** No BFS attributes; the equivalent is a per-node icon descriptor or a fixed icon per node type. The four-name list collapses to one.  

#### MA-014 · important · done
**Changing a file's permissions can change what it is: making a file executable reclassifies it and re-reads its icon and type.**  
Re-reads the stat buffer; if st_mode differs from the cached one it closes and re-opens the node at the same writability and returns true so the caller updates the icon. Deliberately does NOT re-run SetupBaseType (the comment claims node type can't change on a stat update, though the executable bit is exactly what does change the branch taken by OpenNodeCommon).  
`Model.cpp` → `Model::StatChanged`  
**TIKU:** Permission bits become capability bits on the VFS node; the same "capabilities changed -> re-classify and redraw" rule should hold.  
**PORT:** The change notice carries the new permission bits; the row re-applies them, re-parses the descriptor and redraws with the icon its kind now asks for.  There is no node handle to re-open at the same writability, because a read is a request rather than an open file.

#### MA-015 · polish · open
**Tracker avoids a disk hit for icons on files that obviously have none, by noting at open time whether the node carries icon attributes at all.**  
CheckAppIconHint returns true if BEOS:ICON exists, or if BOTH BEOS:M:STD_ICON and BEOS:L:STD_ICON exist; a mini icon alone is not enough. If it returns false, fIconFrom is set to kUnknownNotFromNode. ResetIconFrom re-derives: Desktop/Trash with an icon attribute -> kNode; directory/volume without one that is a well-known directory -> kTrackerSupplied, or a root directory -> kVolume; otherwise kUnknownSource.  
`Model.cpp` → `Model::CheckAppIconHint`  
**TIKU:** Becomes "does this VFS node advertise an icon descriptor"; the two-attribute mini+large rule disappears.  

#### MA-016 · important · open
**Whether a pose highlights as a drop target depends on what it is: folders take anything, symlinks defer to their target, applications take only the types they declare, and everything else refuses.**  
CanHandleDrops: directory or virtual directory -> kCanHandle; symlink -> resolve and recurse, refusing a self-referencing link to avoid infinite recursion; executable -> kNeedToCheckType; anything else -> kCannotHandle. IsDropTarget with a NULL document accepts any kCanHandle/kNeedToCheckType target; with traverse it re-reads the dropped file's type through the resolved entry.  
`Model.cpp` → `Model::CanHandleDrops`  
**TIKU:** Writable VFS nodes are drop targets for text/values, not for files. The capability bit (writable) replaces the executable/supported-types test.  

#### MA-017 · important · open
**Application type matching is wildcard-aware and ranked: an exact type beats a supertype match, which beats a superhandler that accepts everything.**  
MatchMimeTypeString: a handler type with no '/' is a supertype pattern and matches any document type whose supertype is equal (case-insensitive), returning kMatchSupertype; otherwise a full case-insensitive compare returns kMatch. IsSuperHandlerSignature is a case-insensitive compare against "application/octet-stream". SupportsMimeType returns the ranked constants kDoesNotSupportType < kSuperhandlerModel < kModelSupportsSupertype < kModelSupportsType, and with exactReason=false returns as soon as any match is found.  
`Model.cpp` → `MatchMimeTypeString`  
**TIKU:** Only needed if TikuOS keeps an Open-With concept; otherwise this whole ranking can be dropped with the OpenWithRelation column.  

#### MA-018 · important · open
**On a filesystem that does not store MIME types, the type is guessed from content so the Kind column is still meaningful — but the guess is never written back to the file.**  
Only acts when the cached type is still the generic application/octet-stream. Checks BVolume::KnowsMime(); if false, BMimeType::GuessMimeType(&fEntryRef, &mimeType) fills fMimeType in memory only. Model::Mimeset(force) is the opposite: it calls update_mime_info (with 2 as the force argument) which does persist, then re-reads via AttrChanged(NULL) and returns whether the type actually changed.  
`Model.cpp` → `Model::SniffMimeIfNeeded`  
**TIKU:** Sniffing content is out of budget on-device; the namespace node should declare its own type, and non-typed local files fall back to a fixed generic type.  

#### MA-019 · core · differs
**Editing an attribute works even on a file whose node is currently closed or open read-only: the write transiently promotes the node to writable and then restores its previous state.**  
BModelWriteOpener wraps the call; if the node still cannot be opened the write returns 0 rather than failing loudly. This exists specifically because file nodes are normally held read-only.  
`Model.cpp` → `Model::WriteAttr`  
**TIKU:** Becomes a VFS write; the transient-promotion trick is unnecessary if writes are stateless calls, but the "write refused, report 0 bytes" contract must be preserved so the widget can show its error alert.  
**PORT:** Writes are stateless backend calls with no node open/closed state to promote, so an edit is attempted regardless of how the node is held, and the TIKU note's real requirement — a refusal the widget can surface — is preserved by the write op returning non-zero with the device's own message, which commit_edit puts in the status line.  
*src/tiku_trk_desktop.c:362-394 (commit_edit: validate, then write, err -> app->status); src/tiku_trk_model.h:160-162 (write op with err buffer)*

#### MA-020 · important · open
**Writing a byte-order-sensitive attribute deletes the opposite-endian twin, so a disk moved between architectures never picks up stale state.**  
Only removes the foreign attribute when the write wrote exactly `length` bytes. Same discipline in the attribute stream layer: AttributeStreamFileNode::Read tries the native name, then the foreign name with a swap callback, and AttributeStreamFileNode::Write removes the foreign name after a full-size write.  
`Model.cpp` → `Model::WriteAttrKillForeign`  
**TIKU:** Single-architecture target: the native/foreign attribute pair collapses to one name and this rule can be deleted — but only deliberately, since it also silently deletes data.  

#### MA-021 · polish · open
**A volume that is renamed under Tracker's feet updates its displayed name, and the boot volume additionally watches its mount point so renames there are noticed.**  
UpdateEntryRef replaces fVolumeName for volume models before updating the entry ref. WatchVolumeAndMountPoint watches the node with B_WATCH_NAME|B_WATCH_STAT|B_WATCH_ATTR, and when the entry is named "boot" it additionally builds "/" + volume name and watches that node too.  
`Model.cpp` → `Model::WatchVolumeAndMountPoint`  
**TIKU:** Watch registration is idempotent in TikuOS and the table is only 8 slots, per-boot; a second watch for the mount point is affordable but the slot budget must be checked.  

#### MA-022 · core · differs
**Which formatter a column uses is decided by the column's attribute name first and its "display as" hint second; unknown attributes fall back to a generic reader.**  
Name-keyed: _trk/path -> Path, BEOS:TYPE -> Kind, _stat/name -> Name, _stat/realname -> RealName, _stat/size -> Size, _stat/modified -> ModificationTime, _stat/created -> CreationTime, _stat/mode -> Mode, _trk/open_with_relation -> OpenWithRelation, _trk/app_version and _trk/system_version -> short version, _trk/original_path -> OriginalPath. Then DisplayAs() prefix matches: "checkbox", "duration", "rating". Everything else -> GenericAttributeText.  
`WidgetAttributeText.cpp` → `WidgetAttributeText::NewWidgetText`  
**TIKU:** The dispatch table is the extension point for namespace columns (e.g. a /dev value column rendered per typed descriptor).  
**PORT:** Formatter selection is still two-level and still has a generic fallback, just keyed differently: cell_text_at switches on the column's field id, the Value column then hands off to attr_format which dispatches on the node's own typed descriptor (unit/vtype) and falls back to the raw string when the descriptor says nothing — the namespace-shaped version of the name-then-display-as table.  
*src/tiku_trk_poseview.c:168-201 (cell_text_at field switch, VALUE -> attr_format); src/tiku_trk_attr.c:198-246 (descriptor dispatch with raw fallback at :212-215)*

#### MA-023 · core · done
**File sizes are shown human-readable, shrink gracefully as the column narrows, and show "-" when there is no meaningful size.**  
kUnknownSize (-1) renders "-". Otherwise string_for_size(). Below 1024 bytes, if the localized form does not fit the width it falls back to a plain "%lld B". At or above 1024 bytes an insignificant trailing zero in the second decimal place is stripped (1.00 -> 1.0) by finding the locale decimal separator and testing ByteAt(position+2)=='0'. If it still does not fit, the string is ellipsized at the END (B_TRUNCATE_END), unlike names which ellipsize in the middle.  
`WidgetAttributeText.cpp` → `TruncFileSizeBase`  
**PORT:** COL_SIZE no longer prints a raw "%lld". cell_text_at now routes it through tiku_trk_attr_size_fit with the column's OWN width and the view's measure fn, and containers pass -1. size_fit implements every clause: -1 renders "-" (distinct from "0 B"), <1024 renders "%lld B", >=1024 scales through B/KiB/MiB/GiB/TiB, an insignificant SECOND decimal zero is stripped by finding the '.' and testing dot[2]=='0' (1.00->1.0, 1.50->1.5, 1.46 kept), a too-narrow column degrades to the plain byte count, and what still does not fit is ellipsized at the END. Verified by probe: 1536 at width 8 degrades away from "1.5 KiB". The Size column is also right-aligned (poseview.c:3234) and drawn with TRUNC_END (app.c:497-505), so end-ellipsis, not middle, reaches the screen.

#### MA-024 · core · open
**Folders, queries, query templates, symlinks and virtual directories show no size at all; volumes show their capacity, and a zero-capacity volume shows "-".**  
ReadValue: volume -> BVolume::Capacity() with fValueIsDefined = (capacity != 0); directory/query/query-template/symlink/virtual-directory -> fValueIsDefined = false and returns kUnknownSize; anything else -> st_size, defined. FitValue prints "-" whenever fValueIsDefined is false, and PreferredWidth returns the width of "-" in that case.  
`WidgetAttributeText.cpp` → `SizeAttributeText::ReadValue`  
**TIKU:** Device nodes have no size; they should report undefined so the column shows "-", or report a descriptor-declared payload length.  
**CHECKED:** Nothing suppresses the Size cell by kind: folders, symlinks, queries and volumes all print facts.size like a file, and there is no capacity or undefined/"-" path (only the Value column is blanked for containers, which is a different column).  
*none found*

#### MA-025 · core · done
**Dates pick the longest format that fits the column, degrading from long date + medium time down to short date + short time, then to a bare short date, and only then to an ellipsis.**  
Tries in order {B_LONG_DATE_FORMAT,B_MEDIUM_TIME_FORMAT}, {B_LONG_DATE,B_SHORT_TIME}, {B_MEDIUM_DATE,B_SHORT_TIME}, {B_SHORT_DATE,B_SHORT_TIME}; stops at the first that fits. If none fit, formats with BDateFormat B_SHORT_DATE_FORMAT alone. If that still does not fit, TruncStringBase with the default middle-ellipsis. PreferredWidth measures the format at a 100000px width, i.e. the longest form.  
`WidgetAttributeText.cpp` → `TruncTimeBase`  
**TIKU:** Requires a wall clock; on a board without one the Modified/Created columns must degrade to uptime-relative text or be hidden. Note the Ambiq RTC-persists-across-reflash trap when testing.  
**PORT:** The four pairs are the C locale's, not ICU's; the ladder and its fallbacks are the rule.  A store with no wall clock reads '-' rather than a date in 1970.

#### MA-026 · important · partial
**Text that does not fit is ellipsized in the middle for names and generic strings, and the resulting string is measured so the pose knows its real drawn width.**  
TruncStringBase measures first and copies verbatim if it fits; otherwise it uses BFont::GetTruncatedStrings with the requested mode into a buffer of length+3. Default mode is B_TRUNCATE_MIDDLE; size and Open-With-relation override to B_TRUNCATE_END. The return value is stored as fTruncatedWidth and is what CurrentWidth()/CalcRect use.  
`WidgetAttributeText.cpp` → `TruncStringBase`  
**MISSING:** Names elide in the middle and the fitted string is measured at draw time for the underline/label highlight, but every non-name column elides at the END rather than the middle, so generic strings do not get the middle ellipsis the row states.  
*src/tiku_trk_app.c:470 (draw list cells) / src/tiku_trk_attr.c:391 tiku_trk_text_fit*

#### MA-027 · important · done
**In icon and mini-icon view a pose's name is limited to about 30 characters' worth of width regardless of column settings; in list view it is limited to the column width.**  
NameAttributeText::FitValue uses view->StringWidth("M") * 30 when ViewMode() != kListMode, else fColumn->Width(); always middle-ellipsis. RealNameAttributeText::FitValue always uses the column width (it is a list-only column). The same 30-em rule is repeated in BTextWidget::StartEdit and BTextWidget::CalcRectCommon.  
`WidgetAttributeText.cpp` → `NameAttributeText::FitValue`  
**PORT:** The icon/mini-icon clause now matches the row. draw_pose_icon computes budget = 30 * tiku_desk_text_width(f, "n") once, BEFORE the mini/icon branch, so both non-list modes get the same ~30-character allowance regardless of art size, and the fit is TIKU_TRK_TRUNC_MIDDLE. Measured with the plain font that is 240 px against 300 px for Haiku's 30-em rule — an ordinary name like "documentation.txt" (113 px) now shows whole where the old size+size/2 budget (48 px at a 32 px icon, 24 px in mini) chopped it. List view is unchanged and still fits to cols[j].width.

#### MA-028 · core · differs
**The Kind column shows the MIME type's short human description, falling back to the raw type string, and "Unknown" if the type is unparseable.**  
BMimeType::SetTo(fModel->MimeType()) failing -> B_TRANSLATE("Unknown"); GetShortDescription succeeding -> that description; otherwise the raw MIME string. fValueIsDefined is always set true.  
`WidgetAttributeText.cpp` → `KindAttributeText::ReadValue`  
**TIKU:** Needs a per-node-type description table since there is no MIME database; e.g. "LED device", "Sensor", "Folder".  
**PORT:** There is a real substitute mechanism, not an absence: tiku_trk_model_kind_string consults a type table first (exact then supertype, case-folded) for files and otherwise returns a per-kind description word, with "Unknown" for an unknown kind — exactly the per-node-type description table the row's TIKU note calls for in place of a MIME database.  
*src/tiku_trk_model.c:143-171 tiku_trk_model_kind_string (+ type_row table above it)*

#### MA-029 · important · open
**The Path column shows the containing directory (with its trailing slash), not the file's own path, and "-" when the path cannot be resolved.**  
BEntry::GetPath then TruncateLeaf(), which truncates just past the last '/'. Failure sets "-" and fValueIsDefined=false. The same logic is reachable from the Info window through WidgetAttributeText::AttrAsString with kAttrPath.  
`WidgetAttributeText.cpp` → `PathAttributeText::ReadValue`  
**CHECKED:** There is no Path column at all (fields are Name/Kind/Value/Size/Cap/Modified) and nothing renders a containing directory with a trailing slash; query rows show the entry's own full path instead.  
*none found*

#### MA-030 · polish · open
**The "Original path" column only shows something for items inside the Trash, and finds the location even when only an ancestor folder recorded it.**  
FSGetOriginalPath returns B_ERROR immediately if the entry is not in the Trash. It reads the _trk/original_path attribute string; if absent it walks up parents reading the same attribute, stopping (B_ENTRY_NOT_FOUND) at the Trash root. Failure renders "-".  
`WidgetAttributeText.cpp` → `OriginalPathAttributeText::ReadValue`  
**TIKU:** Only meaningful if TikuOS gains a Trash; otherwise drop the column.  

#### MA-031 · polish · differs
**The Permissions column renders a ten-character Unix-style mode string.**  
First character: 'd' directory, 'l' symlink, 'b' block device, 'c' character device, else '-'. Then nine rwx characters generated by walking a mask from 00400 downward, printing "rwx"[index % 3] or '-'.  
`WidgetAttributeText.cpp` → `ModeAttributeText::ReadValue`  
**TIKU:** Capability bits should render in this slot instead (e.g. r/w/watchable), keeping the fixed-width column feel.  
**PORT:** The permissions slot is occupied by a working alternative rather than left empty: a "Cap" column carrying each node's manifest capability token (with "-" when absent and "?" when the manifest was cut), plumbed through the backend, sortable via field_cmp and toggleable from the Attributes menu — the capability-in-this-slot adaptation the row's TIKU note asks for, though not an rwx string.  
*src/tiku_trk_poseview.c:190 cell_text_at COL_CAP; src/tiku_trk_backend_tiku.c:126 req_cap; src/tiku_trk_poseview.c:337 field_cmp COL_CAP*

#### MA-032 · polish · open
**The app and system version columns read the binary's version resource and show "-" when there is none.**  
Opens the model, dynamic_casts its node to BFile, builds a BAppFileInfo and reads B_APP_VERSION_KIND or B_SYSTEM_VERSION_KIND, showing version.short_info. Model::GetVersionString formats major.middle.minor and GetLongVersionString returns long_info for the Info window.  
`WidgetAttributeText.cpp` → `VersionAttributeText::ReadValue`  
**TIKU:** No app version resources; either drop or repoint at a firmware/module version exposed through the namespace.  

#### MA-033 · core · differs
**An arbitrary attribute column reads at most 1023 bytes of string data, and reads numeric attributes only when they are eight bytes or smaller; anything else renders as "-".**  
B_STRING_TYPE: ReadAttr into a 1024-byte buffer of size-1 and force a NUL terminator. Numeric types: GetAttrInfo first, then read only if info.size != 0 and info.size <= sizeof(int64); the value is accepted only if the read length equals info.size, and is dispatched by size (1/2/4/8 bytes, plus float vs double for the float types). Any type not in the switch leaves fValueIsDefined false. The Model is opened through BModelOpener for the duration and closed after.  
`WidgetAttributeText.cpp` → `GenericAttributeText::ReadValue`  
**TIKU:** Typed VFS descriptors already carry a type and length, so the size-sniffing dance disappears; keep the 1023-byte display cap and the "unreadable renders as -" rule.  
**PORT:** The display cap is 255, not the source's 1023: poses are a fixed array and a kilobyte each costs eight megabytes a window, which a browser that opens sixteen cannot spend.  The editor's ceiling is the same number, so a value never opens already truncated.

#### MA-034 · core · differs
**Generic attribute values are formatted per type: booleans as true/false, times as dates, off_t as file sizes, chars only when printable, floats with %g.**  
B_SIZE_T_TYPE and positive B_SSIZE_T_TYPE render as file sizes; negative B_SSIZE_T_TYPE renders strerror() of the value. B_OFF_T_TYPE renders as a file size and also back-fills fFullValueText. B_TIME_TYPE renders as a date and back-fills fFullValueText. B_CHAR_TYPE with a non-printable byte renders "-". Integers use the obvious printf conversions; note uint32 is printed with B_PRId32 and uint64 with B_PRId64, so large unsigned values display as negative. Unknown types render "-".  
`WidgetAttributeText.cpp` → `GenericAttributeText::FitValue`  
**PORT:** tiku_trk_attr_format is an actual per-type dispatch adapted to the device descriptor namespace — bool on/off, bytes with binary prefixes, seconds as an interval, Hz scaled, counts space-grouped, unit-named fallback, "-" for an unread value — so the row's "format by type rather than print the raw number" behaviour exists by another route.  
*src/tiku_trk_attr.c:199-243 tiku_trk_attr_format (fmt_bytes/fmt_hz/fmt_seconds/fmt_count above)*

#### MA-035 · polish · differs
**A column declared "display as: duration" renders its numeric value as H:MM:SS (or M:SS under an hour), with a leading minus for negatives.**  
B_TIME_TYPE / int8 / int16 / int32 are multiplied by 1000000 to microseconds; B_INT64_TYPE is taken as microseconds directly. Microseconds are then discarded. Undefined values render "-".  
`WidgetAttributeText.cpp` → `DurationAttributeText::FitValue`  
**VERIFIER CORRECTION:** BContainerWindow::ResizeToFit (ContainerWindow.cpp:1326-1336) calls ResizeTo() and MoveTo() FIRST, then DisableScrollBars(), then ScrollBy(), UpdateScrollRange(), EnableScrollBars(). The scroll bars are quiesced around the scroll, not around the resize.  
**PORT:** A duration renderer really is there and really is selected for duration-typed columns — unit == SECONDS routes to fmt_seconds, which splits the magnitude, carries the sign separately (so -3600 is not "0s") and sheds the smallest part as the column narrows; only the notation ("15h 57m 51s" vs H:MM:SS) is adapted.  
*src/tiku_trk_attr.c:130-159 fmt_seconds, dispatched at src/tiku_trk_attr.c:229*

#### MA-036 · polish · partial
**A column declared "display as: checkbox" renders a mark for non-zero and a dash for zero, with the two characters overridable in the column spec.**  
Defaults are U+2716 heavy multiplication X and '-'. The spec form is "checkbox:<on><off>" — the constructor takes the text after ':', uses CharAt(0) as the on character and CharAt(1) as the off character when present. Truth is read from bool / int8 / int16 / int32 (and their unsigned twins) being non-zero; an undefined value renders the off character.  
`WidgetAttributeText.cpp` → `CheckboxAttributeText::FitValue`  
**TIKU:** This is the natural rendering for a boolean VFS node such as /dev/led0; the on/off glyph override should survive.  
**MISSING:** A boolean node does render two-state from non-zero/zero, but as the words "on"/"off"; the per-column overridable on/off characters (the "checkbox:<on><off>" spec the TIKU note asks to survive) do not exist, and an undefined value shows "-" rather than the off character.  
*src/tiku_trk_attr.c:219 tiku_trk_attr_format (TIKU_TRK_UNIT_BOOL)*

#### MA-037 · polish · differs
**A column declared "display as: rating" renders five stars out of a maximum value of ten, with half stars.**  
fCount=5, fMax=10, steps = fMax/fCount = 2. The value is clamped to 0..10 and read from int8/int16/int32 (other types and undefined values give 0). For each of the five positions: full star if rating > n + steps/2, half star if rating > n, else empty star.  
`WidgetAttributeText.cpp` → `RatingAttributeText::FitValue`  
**TIKU:** A general "gauge" rendering of a bounded numeric VFS node (battery, signal) is the same mechanism; consider generalizing rather than dropping.  
**PORT:** The cell arithmetic is exact (five cells, max ten, half above the start and full above the midpoint) and generalised to any node with a declared range, but the marks are ASCII because the drawing floor's font is single-byte, and no column can yet be declared 'display as rating'.

#### MA-038 · important · done
**Sorting on an attribute column puts entries that lack the attribute last, compares strings case-insensitively, and compares everything else numerically in its own width.**  
Both sides are read lazily first. !fValueIsDefined -> returns 1 (or 0 if neither is defined); the other side undefined -> -1. B_STRING_TYPE uses ICompare; B_CHAR_TYPE builds one-character strings and ICompares them; float/double/bool/int8..int64/uint8..uint32 compare their own union member; B_TIME_TYPE shares the int32 case, B_OFF_T_TYPE shares the int64 case, and the default falls through to uint64.  
`WidgetAttributeText.cpp` → `GenericAttributeText::Compare`  
**PORT:** Both failing clauses are fixed inside tiku_trk_attr_compare, which is what field_cmp calls for COL_VALUE. An absent value is now handled before anything else — both empty compares equal, one empty returns 1 (sorts last), the other -1 — and the string fallback is strcasecmp rather than strcmp. Numeric-in-its-own-width comparison is unchanged and still keyed off the descriptor's vtype. Probe against the built engine: compare("","apple")=1, compare("apple","")=-1, compare("Zebra","apple")=25, i.e. undefined last and case folded.

#### MA-039 · core · partial
**The Name column sorts naturally (so file9 precedes file10) unless "sort folder names first" is on, in which case the folder bucketing rule wins.**  
NameAttributeText::Compare and RealNameAttributeText::Compare each check their own static sSortFolderNamesFirst flag (two independent statics, both set from TrackerSettings::SetSortFolderNamesFirst) and delegate to Model::CompareFolderNamesFirst; otherwise NaturalCompare of the two full value strings.  
`WidgetAttributeText.cpp` → `NameAttributeText::Compare`  
**MISSING:** Natural, case-folded name comparison is implemented (file9 before file10) and folder bucketing does win over it, but the "unless the setting is on" clause is missing: there is no sort-folder-names-first setting anywhere, the container bucket is unconditional.  
*src/tiku_trk_poseview.c:283 natural_cmp / src/tiku_trk_poseview.c:355 pose_cmp*

#### MA-040 · important · open
**Column text is only recomputed when something that affects it changed — the value, the column width (list view only), or a display setting.**  
FittingText recomputes if fDirty, or if ViewMode()==kListMode and fColumn->Width() != fOldWidth, or if CheckSettingsChanged() (overridable; TimeAttributeText's override currently returns false with a TODO). CheckViewChanged re-fits and returns whether the fitted string actually differs. StringAttributeText::CheckAttributeChanged re-reads the raw value on every call and sets fDirty on a difference; GenericAttributeText::CheckAttributeChanged additionally never clears an already-set fDirty.  
`WidgetAttributeText.cpp` → `WidgetAttributeText::FittingText`  
**TIKU:** With watch notifications firing on every touch (no coalescing, same-value writes still ring), this re-read-and-compare gate is what prevents redraw storms; it must be kept.  
**CHECKED:** The row's rule IS the recompute gate, and there is none: cell_text_at is a pure recompute called from the draw loop for every visible cell on every frame, with no cached fitted string, no dirty flag and no old-width comparison; refresh_values' value compare is a different gate (which rows changed) and does not make column text conditional.  
*src/tiku_trk_poseview.c:169-201 cell_text_at (no cache) called at src/tiku_trk_app.c:447 and re-fitted at :470 each draw*

#### MA-041 · polish · open
**The Info window renders Created / Modified / Path through the same formatting code as the columns, so they read identically.**  
AttrAsString handles B_TIME_TYPE for _stat/modified (st_mtime) and _stat/created (st_crtime) via TruncTimeBase, and B_STRING_TYPE for _trk/path via GetPath + TruncateLeaf (untruncated when width <= 0). kSizeType returns B_OK doing nothing (the call is commented out). Any other attribute/type combination TRESPASSes and returns B_ERROR.  
`WidgetAttributeText.cpp` → `WidgetAttributeText::AttrAsString`  

#### MA-042 · core · done
**Clicking a selected pose's name a second time, slowly, begins renaming it in place — but a click that merely completed the selection does not, and a fast second click (a double-click) does not either.**  
MouseUp only arms if the widget is editable and the pose is selected. On the first arming click it records system_time(); if that time minus the double-click speed is still before pose->SelectionTime(), the arm is cancelled (the click was part of selecting). If already armed, the second MouseUp disarms — so a genuine double-click opens instead of renaming. The armed widget is registered with the pose view via SetTextWidgetToCheck.  
`TextWidget.cpp` → `BTextWidget::MouseUp`  
**PORT:** All three stated conditions are implemented: name_clicked refuses unless the pose is already selected, refuses when now - sel_time is inside the click window (sel_time is stamped only on the transition into selection, so the selecting click really is excluded), and clears the arm on the second click of a pair; the arm is only offered for clicks in the name column and fires from edit_due once the click window has passed.  
*src/tiku_trk_poseview.c:2008-2038 name_clicked, :2071 edit_due window, :584-591 set_selected; arm gated by on_name at src/tiku_trk_app.c:1367*

#### MA-043 · core · done
**The in-place rename actually starts on a timer, once the double-click window has elapsed with no further click.**  
Driven from BPoseView::Pulse via fTextWidgetToCheck. Requires the widget to still be editable and the pose still selected; when system_time() - fLastClickedTime exceeds get_click_speed() it clears the timestamp and calls StartEdit with the stored bounds/pose/view. If the conditions no longer hold it disarms. CancelWait() disarms explicitly.  
`TextWidget.cpp` → `BTextWidget::CheckExpiration`  

#### MA-044 · core · done
**Editing refuses to start on a non-editable column, on a widget that is already being edited, or while a drag is in progress.**  
StartEdit returns early if !IsEditable() || IsActive(), and again if view->IsDragging(). It sets the pose view's active text widget and active pose, marks the widget active, and hides the widget so the text view is not drawn over the old text.  
`TextWidget.cpp` → `BTextWidget::StartEdit`  
**TIKU:** Add a capability check: a VFS node without a write capability bit must not offer editing at all, the same way a read-only column does not.  
**PORT:** Each refusal has an implementation in the tracker front-end — only the name column arms an edit (and begin_rename additionally refuses device/namespace nodes), the pulse starts a rename only when !a.editing while the editing branch swallows every key, and a press that moves past the 4 px threshold calls cancel_edit_wait so a drag cannot become an edit; the desktop front-end's pulse guards on the value-editor flag rather than its own renaming flag, which is a defect inside the built feature rather than a missing clause.  
*src/tiku_trk_app.c:648-670 begin_rename, :800 editing key swallow, :1418-1424 drag cancels arm, :1553 !a.editing guard*

#### MA-045 · core · done
**The editor opens containing the full untruncated value with everything selected, so typing replaces it outright.**  
SetupEditing is per-attribute: name widgets call DisallowFilenameKeys, SetMaxBytes(B_FILE_NAME_LENGTH) and set the full value; generic attribute widgets call SetMaxBytes(kGenericReadBufferSize - 1) = 1023. StartEdit then calls SelectAll() and ScrollToSelection() so the beginning of long text is visible, and MakeFocus().  
`WidgetAttributeText.cpp` → `NameAttributeText::SetupEditing`  
**UNCONFIRMED:** Nothing after that point could be verified. The truncated row itself IS correct as far as it goes (WidgetAttributeText.cpp:301 FittingText recomputes when fDirty || (list mode && column width != fOldWidth) || CheckSettingsChanged()). No app-windows rows were reviewed, so treat that area as UNVERIFIED, not as verified-empty.  
**PORT:** The separate plain append buffer is gone. desktop.c begin_edit now calls tiku_trk_edit_start(&app->ed, p->value_valid ? p->value : ""), the same primitive the rename editor uses, and edit_start sets sel_a=0, sel_b=len; the keystroke path feeds app->ed through tiku_trk_edit_key, whose CHAR case calls delete_selection() before inserting, so the first keystroke replaces the whole reading. It is seeded from the STORED value (p->value), not the fitted cell text, which is the untruncated half of the rule. The rename editor already satisfied it (app.c:693, desktop.c:632).

#### MA-046 · core · done
**While renaming, control characters and '/' cannot be typed at all — the keystroke is swallowed rather than rejected at commit.**  
DisallowChar for every code below 0x20 plus '/'. Separately DisallowMetaKeys blocks Tab, Escape, Insert, Delete, Home, End, PageUp, PageDown and function keys from reaching the text view (they are handled by the key filter instead).  
`Utilities.cpp` → `DisallowFilenameKeys`  
**TIKU:** Namespace paths have their own illegal characters; the disallow set should be derived from the VFS path rules rather than hardcoded to '/'.  
**PORT:** '/' is now swallowed at the keystroke. tiku_trk_edit_t carries a `deny` set consulted at the top of the CHAR case, before delete_selection, so a denied character neither appears nor clobbers the selection; both rename entry points arm it with tiku_trk_fs_name_deny(). The control-character and meta-key halves were already there (ch < 0x20 || ch == 0x7f is dropped; Tab/Escape/Delete/Home/End are consumed by the editor's own switch). The deny set is sourced from the store as the TIKU note asks — NAME_DENY lives once in tiku_trk_fs.c next to tiku_trk_fs_name_ok, so the keystroke filter and the commit check cannot drift apart. test_edit.c:89-104 covers both the armed and unarmed cases.

#### MA-047 · core · partial
**Return commits the rename, Escape abandons it, Tab moves to the next editable column committing the current one, and Shift-Tab moves to the previous.**  
TextViewKeyDownFilter intercepts B_RETURN and B_ESCAPE and calls view->CommitActivePose(key == B_RETURN) — so Escape commits with saveChanges=false, i.e. discards. B_TAB dispatches to BPose::EditNextWidget / EditPreviousWidget, which walk the column list in the chosen direction, commit the currently active widget when they reach it, and StartEdit on the next column whose Editable() is set.  
`TextWidget.cpp` → `TextViewKeyDownFilter`  
**MISSING:** Return commits and Escape abandons the rename, but Tab/Shift-Tab column traversal is missing entirely — Tab reaches the editor as 0x09, is rejected by the <0x20 guard, and no key path walks to the next editable column.  
*src/tiku_trk_app.c:808 (editing key switch) / src/tiku_trk_edit.c:153 (COMMIT/CANCEL)*

#### MA-048 · important · open
**On a read-only volume the rename editor still opens but the text cannot be changed — it is selectable only, and paste handling is not installed.**  
StartEdit calls textView->MakeEditable(false) and MakeSelectable(true) when view->SelectedVolumeIsReadOnly(); the B_PASTE filter is added only when the volume is writable. On the Desktop the text view colors are forced to the inverse of the Desktop text color and tinted with ReadOnlyTint when read-only.  
`TextWidget.cpp` → `BTextWidget::StartEdit`  
**TIKU:** Map directly to the node's write capability bit: no write capability -> selectable, not editable.  
**CHECKED:** The row's rule is that the editor still opens and is selectable-but-not-editable; the port refuses to open it at all for non-writable nodes, and on the POSIX rename path never consults read-only, so the described affordance exists nowhere.  
*src/tiku_trk_desktop.c:923-937 begin_edit (refusal message, no editor), src/tiku_trk_app.c:648-670 begin_rename (only DEVICE_NODE/VIRTUAL_DIR refused); no editable/selectable split exists in src/tiku_trk_edit.c*

#### MA-049 · polish · partial
**The rename editor grows as you type but never past the column width (list view) or ~30 characters (icon views), scrolling instead.**  
fMaxWidth is the column width in list mode, else StringWidth("M")*30. MakeResizable(!tooWide, tooWide ? NULL : scrollView) toggles between grow-with-text and fixed-width-with-scrolling; the key-down filter re-evaluates on every keystroke. TextViewPasteFilter computes the post-paste width (text width, minus the selected region being replaced, plus the clipboard text width) and, when too wide, resizes the scroll view to exactly maxWidth, shifting it left by half the delta for centered text and the full delta for right-aligned text.  
`TextWidget.cpp` → `TextViewPasteFilter`  
**MISSING:** The cap clause holds by construction — the field is drawn at exactly the name column's width (260 px in the desktop shell) and clipped — but it never grows with the text, has no ~30-character icon-mode form, and does not scroll to keep the caret visible when the text overruns.  
*src/tiku_trk_app.c:533 (editor box = cols[0].width); src/tiku_trk_desktop.c:299*

#### MA-050 · core · done
**Committing a name that was not changed, or that is empty, does nothing at all — no write, no error.**  
StringAttributeText::CommitEditedText returns false when the new text equals fFullValueText and, separately, when TextLength() == 0. GenericAttributeText::CommitEditedText overrides this and only checks for no-change — so an arbitrary attribute CAN be set to an empty value, unlike a name.  
`WidgetAttributeText.cpp` → `StringAttributeText::CommitEditedText`  
**PORT:** Both apps now short-circuit an unchanged name before the store is touched: the leaf of the edit path is compared with the edited text and, on equality, commit is forced to 0, so tiku_trk_fs_rename is never called and no status is set — no write, no error. The empty case was already gated by `commit && len > 0`. Nothing else runs on that path (no after_op, no reload), so a Return on an untouched name is genuinely silent.

#### MA-051 · core · partial
**Some entries can never be renamed: anything showing a localized name, the Desktop, the filesystem root, the Trash, and virtual directories.**  
EditModelName returns B_BAD_VALUE for a NULL/empty name, then B_NOT_ALLOWED when model->HasLocalizedName() || IsDesktop() || IsRoot() || IsTrash() || IsVirtualDirectory(). Because RealNameAttributeText inherits NameAttributeText::CommitEditedTextFlavor, the Real name column is bound by exactly the same refusals.  
`FSUtils.cpp` → `EditModelName`  
**TIKU:** The equivalent list is "nodes without a rename capability" — most of /sys and /dev. Express it as a capability bit rather than a hardcoded kind list.  
**MISSING:** Virtual directories and device nodes are refused up front, but Trash, Desktop and the filesystem root have no refusal at all, and the refusal is a hardcoded kind test rather than the capability bit the row's TIKU note asks for.  
*src/tiku_trk_app.c:657 / src/tiku_trk_desktop.c:615 (begin_rename kind check)*

#### MA-052 · important · open
**Renaming a volume renames the volume itself rather than a directory entry, and fails cleanly on a read-only volume.**  
EditModelName's IsVolume() branch checks BVolume::InitCheck and IsReadOnly (returning B_READ_ONLY_DEVICE), then calls BVolume::SetName under a RenameVolumeUndo which is removed if the rename fails. Model::UpdateEntryRef then refreshes the cached fVolumeName so the pose relabels.  
`FSUtils.cpp` → `EditModelName`  
**TIKU:** Renaming a mount is a namespace operation; decide whether durable mounts carry a user-settable label.  

#### MA-053 · polish · open
**Renaming a query also clears its "temporary" flag, so a query the user bothered to name is kept instead of being reaped.**  
The IsQuery() branch opens the model for writing and calls MoreOptionsStruct::SetQueryTemporary(model->Node(), false) before doing the entry rename under a RenameUndo.  
`FSUtils.cpp` → `EditModelName`  
**TIKU:** Only relevant if saved queries survive into TikuOS.  

#### MA-054 · core · partial
**A rename is validated before it happens: over-length names, names that already exist in the folder, and renames inside system directories each get their own alert and are refused.**  
ShouldEditRefName: length >= B_FILE_NAME_LENGTH -> alert ("The entered folder name is too long." / "...file name...") and B_NAME_TOO_LONG; identical name -> B_OK with no work; ConfirmChangeIfWellKnownDirectory(&entry, kRename) declining -> B_CANCELED; no parent -> B_ERROR; parent.Contains(name) -> alert "An item named '%filename%' already exists." and B_NAME_IN_USE. Every alert sets B_CLOSE_ON_ESCAPE. A successful path registers a RenameUndo which is removed if BEntry::Rename fails.  
`FSUtils.cpp` → `ShouldEditRefName`  
**TIKU:** Name-in-use and length checks become VFS-level; the well-known-directory confirmation maps to protected subtrees such as /sys.  
**MISSING:** The name-in-use refusal and the protected-namespace refusal exist, but there is no length check with its own message — an over-long name only fails inside join(), which returns -1 without filling err, so the status line shows an uninitialised buffer.  
*src/tiku_trk_fs.c:593 (tiku_trk_fs_rename → name_ok/exists/join)*

#### MA-055 · important · done
**After a successful rename or attribute edit the pose immediately moves to its new sorted position rather than waiting for a refresh.**  
StopEdit calls view->CheckPoseSortOrder(pose, poseIndex) only when saveChanges && fText->CommitEditedText(textView) returned true. It then re-shows the widget, invalidates its ColumnRect, removes and deletes the "BorderView" scroll view, forces an immediate window update so the text view disappears at once, returns focus to the pose view, and clears the active flag.  
`TextWidget.cpp` → `BTextWidget::StopEdit`  
**PORT:** tiku_trk_poseview_recheck_order() now re-places a pose whose sort key moved, and refresh_values() calls it whenever a re-read value changes and COL_VALUE is the primary or tie-break sort; commit_edit() reaches it via refresh_values(). I proved it on a live view with numeric descriptors: with rows sorted by value, dropping n4 50->1 moved it to the head and raising n0 10->999 moved it to the tail, both on the same refresh, and the rename path (EV_MOVED remove+insert) still moves immediately. Guard slot==at||slot==at+1 correctly no-ops when nothing moved.

#### MA-056 · important · differs
**Attributes whose type Tracker cannot write are refused with an explicit "Sorry, you cannot edit that attribute" alert instead of silently doing nothing.**  
CommitEditedTextFlavor allows only string, uint8/16/32/64, int8/16/32/64, off_t, time, float, double, char and bool; anything else raises a B_STOP_ALERT with a Cancel button and returns false. A write returning a negative size raises "There was an error writing the attribute." as a B_WARNING_ALERT and marks the value undefined.  
`WidgetAttributeText.cpp` → `GenericAttributeText::CommitEditedTextFlavor`  
**TIKU:** Typed VFS descriptors give the same guard for free; keep the two distinct alerts (type not editable vs write failed) because they mean different things to the user.  
**PORT:** Both distinct alerts genuinely exist — an explicit refusal before the editor opens and a separate device write-error at commit, plus type-directed value refusals in validate — with the un-writable class keyed on capability because every vtype in the port is text-writable.  
*src/tiku_trk_desktop.c:923-937 (refusal) and :362-393 commit_edit (validate error vs device write error); src/tiku_trk_attr.c:251-260 tiku_trk_attr_editable*

#### MA-057 · important · differs
**Typed attribute values are parsed leniently: sizes accept k/MB/GB suffixes, dates accept free-form text, booleans accept off/no/false, and a character attribute refuses multi-byte glyphs.**  
B_BOOL_TYPE is false only for a prefix match against "0" (1 char), "off" (2 chars), "no" (3 chars), "false" (4 chars), or an empty string — note the mismatched lengths, so "of" also reads as false. B_CHAR_TYPE sscanf's one byte and raises an alert if it is not printable. B_TIME_TYPE uses parsedate(text, time(0)) so "tomorrow" works. Integer and off_t types go through StringToScalar, which multiplies by 1024 for k/K, 1048576 for mb/MB, 1073741824 for gb/GB — and also 1073741824 for "byte"/"BYTE", which is almost certainly wrong but is the observed behavior. float/double use sscanf and, on a parse failure, leave the stored value untouched and return whether a value was already on disk.  
`WidgetAttributeText.cpp` → `GenericAttributeText::CommitEditedTextFlavor`  
**TIKU:** Writing to a VFS node is string-in/typed-out anyway; keep the suffix parsing for size-like nodes and the parsedate leniency only if a clock exists.  
**PORT:** The two clauses that have a target are implemented and tested — k/K, m/M, g/G with optional b, and 0/off/no/false — while Time and Character have no counterpart in the port's five-value vtype set, which the row's own TIKU note anticipates.  
*src/tiku_trk_attr.c:264-274 as_bool, :287-315 parse_scalar; src/tiku_trk_attr.h:32-36 (VT_NONE/BOOL/U32/I32/STR only); tests/test_attr.c:129-154, :199-210*

#### MA-058 · core · open
**A column's persisted description records its title, width, alignment, attribute name and type, whether it is a stat field, whether it is editable, and its display-as spec.**  
ArchiveToStream writes AttrHashString("BColumn", B_OBJECT_TYPE) then kColumnStateArchiveVersion (22), then title, offset, width, alignment, attribute name, attribute hash, attribute type, statField, editable, displayAs. InstantiateFromStream accepts any version >= kColumnStateMinArchiveVersion (21) — version 21 lacks the trailing displayAs string, which is why the reader makes that field conditional. The BMessage flavor requires an exact version match.  
`ViewState.cpp` → `BColumn::ArchiveToStream`  
**TIKU:** BFS attributes are gone; per-folder column state needs a decided home (persist cell keyed by path hash, an NVM-region file, or a sidecar). Note the 4 KB durable budget on the smallest targets — one blob per folder will not fit there.  

#### MA-059 · important · open
**Column widths follow the system font size across sessions: a saved layout re-opened at a larger font comes back proportionally wider.**  
Offsets and widths are divided by _Scale() = be_plain_font->Size() / 12.0f on write (floorf) and multiplied back on read (ceilf), in both the stream and message flavors. _Init also scales the width passed to the constructor.  
`ViewState.cpp` → `BColumn::_Scale`  

#### MA-060 · important · open
**A corrupt or hostile saved column is discarded rather than producing a broken title bar.**  
_Sanitize rejects the column when the title exceeds 500 characters, the offset is negative or above 10000, the width is negative or above 10000, the alignment is outside B_ALIGN_LEFT..B_ALIGN_CENTER, or the attribute name exceeds 500 characters. The caller simply gets NULL and stops reading further columns.  
`ViewState.cpp` → `BColumn::_Sanitize`  

#### MA-061 · core · partial
**A folder's saved view state records view mode, last icon mode, both scroll origins, primary and secondary sort attribute and type, reverse-sort, and both icon sizes.**  
_ArchiveToStream writes AttrHashString("BViewState", B_OBJECT_TYPE) then kViewStateArchiveVersion (11), then the eleven fields in that fixed order. Reads require an exact version match (ValidateStream / an explicit compare for the message flavor), so a version bump silently discards every stored folder layout.  
`ViewState.cpp` → `BViewState::_ArchiveToStream`  
**TIKU:** Same storage decision as columns. Since TikuOS is single-architecture the endian-swap paths and the paired _le/_be attribute names can go away — deliberately, not by accident.  
**MISSING:** Only the view mode is written ("list"/"icon"/"mini"); last icon mode, both scroll origins, primary and secondary sort attribute and type, reverse-sort and both icon sizes are never stored, so a folder comes back sorted and scrolled by the defaults.  
*src/tiku_trk_poseview.c:2195 (save_state writes only the mode string)*

#### MA-062 · core · differs
**A newly opened folder with no saved state shows Name / Size / Modified in list mode, sorted by name ascending, with large icons.**  
SetupDefaultColumnsIfNeeded (only when CountColumns()==0) adds Name (145pt, left, _stat/name, B_STRING_TYPE, statField=true, editable=true), Size (80pt, right, _stat/size, B_OFF_T_TYPE, statField=true, editable=false) and Modified (150pt, left, _stat/modified, B_TIME_TYPE, statField=true, editable=false). BViewState::_Init defaults to kListMode, B_LARGE_ICON, origins at (0,0), primary sort = AttrHashString(_stat/name, B_STRING_TYPE), no secondary sort, reverseSort false.  
`PoseView.cpp` → `BPoseView::SetupDefaultColumnsIfNeeded`  
**TIKU:** Note that only the Name column is editable by default — the same should hold for a namespace listing, where the value column becomes editable only for writable nodes.  
**PORT:** Mode, sort key, sort direction and icon size match exactly (list, name, ascending, 32 px) and the column set is the device-namespace analogue the row's own TIKU note anticipates, with COL_SIZE/COL_DATE still available as columns.  
*src/tiku_trk_poseview.c:494-513 (mode LIST, sort_field COL_NAME, icon_size 32, columns Name/Kind/Value/Cap); src/tiku_trk_poseview.h:118-123*

#### MA-063 · important · differs
**Saved state that is out of range is repaired rather than obeyed: icon sizes are clamped, negative list scroll origins are zeroed, and an unrecognizable view mode throws the whole state away.**  
_Sanitize clamps fIconSize and fLastIconSize into 16..128 and zeroes negative fListOrigin components in list mode. With fixOnly=false (the InstantiateFrom* path) it additionally deletes the state when fViewMode or fLastIconMode is not one of kListMode, kIconMode, kMiniIconMode or 0. Note the constructors call _Sanitize(this, true) first, so the destructive check runs only once, at the outer call.  
`ViewState.cpp` → `BViewState::_Sanitize`  
**PORT:** Out-of-range saved state is not obeyed by an actual sanitizer — width clamped up to COL_MIN_WIDTH, insane pose locations ignored and auto-placed, unknown mode falling back, whole record discarded on bad key/version — and icon size and scroll origin are simply never persisted, so there is no obeyed-garbage case to guard.  
*src/tiku_trk_poseview.c:3311-3325 (range rejection), :3352-3357 (width floored to COL_MIN_WIDTH), :2130-2135 (mode fallback), :2162-2166 (SANE_LOCATION); tests/test_title.c:696-733*

#### MA-064 · core · partial
**View state and column layout are stored on the folder itself, with separate slots for the Disks window and the Desktop, so those three never collide.**  
Normal folders use kAttrColumns/_trk/columns and kAttrViewState/_trk/viewstate; the Disks (root) window uses kAttrDisksColumns/_trk/d_columns and kAttrDisksViewState/_trk/d_viewstate stored on the Desktop directory node (WindowStateNodeOpener redirects root windows to FSGetDeskDir); the Desktop uses kAttrDesktopViewState/_trk/desk_viewstate. On a little-endian host each of these actually resolves to the "_le"-suffixed name, with the unsuffixed name treated as the foreign-endian twin (Attributes.h).  
`PoseView.cpp` → `BPoseView::ViewStateAttributeName`  
**TIKU:** Three distinct storage slots must survive even though the attribute mechanism does not; the Desktop/Disks split is user-visible (rearranging the Desktop must not disturb a folder window).  
**MISSING:** State really is stored on the folder itself keyed by path under fixed attribute names, but the three-slot split does not exist in any live path: TIKU_TRK_KEY_DEV_ROOT/TIKU_TRK_KEY_DESKTOP are declared and referenced only by a test, never by the poseview.  
*src/tiku_trk_state.h:26-35 (slot keys declared); src/tiku_trk_poseview.c:2198 and :3271 (always keyed on v->path); only user is tests/test_model.c:113*

#### MA-065 · important · open
**A saved layout written on the other byte order is still honored: the reader falls back to the foreign attribute name and swaps every field.**  
RestoreColumnState / RestoreState first ask node->Contains(nativeName, B_RAW_TYPE); if that returns 0 they retry the foreign name and set wrongEndianness, which is threaded into BColumn/BViewState constructors that B_SWAP_INT32 / B_SWAP_FLOAT / swap_data(B_POINT_TYPE) every field. AttributeStreamFileNode::Read implements the same native-then-foreign-with-swapFunc fallback generically.  
`PoseView.cpp` → `BPoseView::RestoreState`  
**TIKU:** Single-architecture: delete the twin names, but keep the general shape — a stored blob whose layout does not match must be rejected, not misread.  

#### MA-066 · important · done
**A missing, empty or absurdly large state attribute is ignored and the folder falls back to defaults instead of showing garbage.**  
Both RestoreColumnState and RestoreState only proceed when 0 < size < 10000, explicitly to guard against munged attributes. Column instantiation loops until InstantiateFromStream returns NULL, so a truncated stream simply yields fewer columns.  
`PoseView.cpp` → `BPoseView::RestoreColumnState`  
**TIKU:** Same class of guard as a persist-cell magic gate: validate before trusting. A torn write must read as "no saved state".  
**PORT:** A missing or failed read returns before touching the defaults, and a column blob is discarded wholesale on a non-multiple size, an over-large count, a wrong key/version, a bad width/alignment/field or a missing Name column, with defaults left standing.  
*src/tiku_trk_poseview.c:3269-3345 restore_columns guards; tests/test_title.c:696-733 (six forged-record cases assert width_of unchanged)*

#### MA-067 · important · differs
**If the folder was sorted by a column that no longer exists, sorting silently falls back to the first column; a secondary sort equal to the primary is dropped.**  
After columns are restored: if !ColumnFor(PrimarySort()) then SetPrimarySort(FirstColumn()->AttrHash()) and SetPrimarySortType(FirstColumn()->AttrType()); then if PrimarySort() == SecondarySort() then SetSecondarySort(0). Present in both the stream and message restore paths.  
`PoseView.cpp` → `BPoseView::RestoreColumnState`  
**PORT:** Both clauses fire after a restore — an orphaned primary sort silently falls back and a secondary equal to the primary is dropped — with the fallback aimed at the Name column the validation guarantees rather than at whichever column sorted first.  
*src/tiku_trk_poseview.c:3362-3376 (sort_field fallback to COL_NAME, sort2_field cleared when orphaned or equal)*

#### MA-068 · important · open
**Restored columns keep their saved widths and order but always get fresh left-to-right offsets, so a layout can never come back with overlapping or gapped columns.**  
AddColumnList sorts by CompareColumns, then walks assigning column->SetOffset(nextLeftEdge) starting at StartOffset(), advancing by width - kRoomForLine/2 + kTitleColumnExtraMargin. It also starts watching for date-format changes the first time it sees a B_TIME_TYPE column. Resizing (ResizeColumn) and reordering (MoveColumnTo) recompute every offset the same way and set fStateNeedsSaving.  
`PoseView.cpp` → `BPoseView::AddColumnList`  

#### MA-069 · polish · open
**The Desktop can never end up in list mode, even if a saved state says so.**  
Both RestoreState overloads end with: if (IsDesktopView() && ViewMode() == kListMode) fViewState->SetViewMode(kIconMode), described in the source as recovering from a wrongly-set desktop window view state.  
`PoseView.cpp` → `BPoseView::RestoreState`  

#### MA-070 · important · open
**A folder opened for the first time inherits its window size and column layout from its parent folder (staggered so it does not land exactly on top) or from a template folder, but never inherits the parent's scroll position.**  
SetupDefaultState prefers the parent directory when NodeHasSavedState(parent) (i.e. it has a _trk/windframe attribute) and the parent is not the Desktop, staggering in that case; otherwise it falls back to the kDefaultFolderTemplate node in the Tracker settings folder, without staggering. The Desktop copies nothing. The copy passes through a NamesToAcceptAttrFilter allowing exactly _trk/windframe, _trk/windwkspc, _trk/viewstate (+foreign), _trk/columns (+foreign), a SelectiveAttributeTransformer that offsets the frame by sWindowStaggerBy, and another that rewrites the view state blob with both origins set to B_ORIGIN (ClearViewOriginOne in PoseView.cpp).  
`ContainerWindow.cpp` → `BContainerWindow::SetupDefaultState`  
**TIKU:** The allow-list is the important part: inheritance must be an explicit whitelist of state, never a blind copy of everything attached to the node.  

#### MA-071 · important · done
**View state is written back only when something actually changed, and a folder on a read-only volume simply keeps its layout in memory instead of erroring.**  
BPoseView::StateNeedsSaving() = fStateNeedsSaving (set by column resize/move/add) || fViewState->StateNeedsSaving(), the latter being a field-by-field diff against a snapshot taken by _StorePreviousState() at construction and after every archive. BContainerWindow::SaveState opens the node for writing through WindowStateNodeOpener and does nothing at all when StreamNode() is NULL. BPoseView::SaveState clears fStateNeedsSaving after writing.  
`ViewState.cpp` → `BViewState::StateNeedsSaving`  
**TIKU:** Directly relevant to NVM wear: a diff-before-write gate is mandatory, and the Ambiq "persistent is really SRAM mirrored to MRAM" flush-amplification trap makes a per-scroll write unacceptable.  
**PORT:** save_state returns immediately on a clear state_dirty flag that only the thirteen real mutation sites set, and returns 0 rather than an error when the store reports itself not writable, leaving the in-memory layout untouched.  
*src/tiku_trk_poseview.c:2181-2192 (dirty gate then tiku_trk_store_writable gate), state_dirty=1 at :1458,:1517,:3439,:3475 etc.; src/tiku_trk_state.c:317-320*

#### MA-072 · important · partial
**When a file changes on disk, only the affected columns refresh: stat-derived columns on a stat change, and the matching attribute column on an attribute change — including when the attribute was deleted.**  
With an attribute name: look up the widget by AttrHashString(attrName, attrType); if found, CheckAndUpdate it. If not found and attrType is 0 (the attribute was removed, so its type is unknown), scan every widget for a column whose AttrName() matches the string and update that one. With no attribute name: call Model::StatChanged, then CheckAndUpdate every widget whose column has StatField() set. BTextWidget::CheckAndUpdate only invalidates when the value changed AND the fitted text changed AND the widget is visible, unioning the old rect in icon modes.  
`Pose.cpp` → `BPose::UpdateWidgetAndModel`  
**TIKU:** A TIKU_EVENT_VFS notification names the node, not the field, so the rewrite lands on the "no attribute name" branch — meaning every watched node change re-reads all of that pose's columns. The value-changed/text-changed gate is what keeps that affordable.  
**MISSING:** The notice carries no field identity at all (tiku_trk_event_t has op/path/from/dir/model and nothing else), so EV_CHANGED and EV_ATTR share one branch that swaps the whole model, drops value_valid and flags a blanket REDRAW — the data does refresh, but the row's actual rule (only the affected columns; the by-name widget lookup; the type-0 scan for a deleted attribute; the value-AND-fitted-text-AND-visible invalidate gate on this path) has no implementation, and the auditor's own sentence concedes the field routing is absent.  
*src/tiku_trk_poseview.c:2641-2653 (EV_CHANGED/EV_ATTR share one branch: model replaced, value_valid=0, unconditional REDRAW); src/tiku_trk_notify.h:46-53 (event struct has no attribute name/type); gate that does exist is row-level in refresh_values at src/tiku_trk_poseview.c:3018-3052*

#### MA-073 · important · open
**Attributes are copied between nodes through a pull pipeline that can filter by name and rewrite values in flight, and a partial write aborts the copy silently.**  
Nodes are chained with operator<<, which wires fReadFrom/fWriteTo and auto-starts the chain from its head via Start()/Drive(). AttributeStreamFileNode::Drive pumps Next()/Get() pairs writing each attribute to the destination node and stops (returning true) the moment a write is short. NamesToAcceptAttrFilter::Reject passes only names present in its NULL-terminated list. SelectiveAttributeTransformer::Get copies the buffer and applies its callback for the one attribute name it targets, keeping the copies alive until Rewind. AttributeStreamMemoryNode buffers whole attributes in RAM for reuse.  
`AttributeStream.cpp` → `AttributeStreamFileNode::Drive`  
**TIKU:** There is no per-node attribute set to enumerate in a VFS namespace, so this whole machinery is replaced by explicit named state; keep the filter-and-transform-on-copy idea for the inherit-default-state path.  

#### MA-074 · polish · differs
**Narrow column text is still comfortably clickable: a text widget's click area is widened to at least 20 pixels (or the column width, whichever is smaller).**  
CalcClickRect takes CalcRect and, when narrower than kWidthMargin (20), extends the right edge to left + column->Width() if the column itself is narrower than 20, else to left + 20.  
`TextWidget.cpp` → `BTextWidget::CalcClickRect`  
**PORT:** An actual alternative mechanism is pointable and delivers the row's user-visible guarantee: hit-testing uses the whole column cell (x in [cx, cx+width)) while drawing insets the text by the 11/5 margins, and every path that sets a width floors it at TIKU_TRK_COL_MIN_WIDTH 20 (interactive resize, reflow, restore), so a text hit area narrower than 20 px cannot occur.  
*src/tiku_trk_app.c:1330-1341 (full column-cell hit test); src/tiku_trk_poseview.h:95 with floors applied at src/tiku_trk_poseview.c:3356-3357, :3431-3432 and src/tiku_trk_titleview.c:216-217*

#### MA-075 · polish · done
**A symlink's name is drawn underlined, in the first column only.**  
Draw checks fSymLink (cached from model->IsSymLink() at construction) and fAttrHash == view->FirstColumn()->AttrHash(). The underline is stroked with B_MIXED_COLORS two pixels below the baseline, alpha 180 over the text color, and is inset so it spans only the text, not the full column rect.  
`TextWidget.cpp` → `BTextWidget::Draw`  
**PORT:** Stroked solid two pixels below the baseline and inset to the text, in the first column only; not the alpha-180 mixed-colours stroke, which this drawing floor has no blending mode for.

#### MA-076 · polish · done ⟨found by verification⟩
**A symlink's name is drawn underlined in the first column.**  
BTextWidget::Draw, after the text: `if (fSymLink && fAttrHash == view->FirstColumn()->AttrHash())` it re-strokes an alpha-180 underline in the current high colour (skipped for offscreen draws and for cut items).  
`TextWidget.cpp` → `BTextWidget::Draw (fSymLink tail)`  
**TIKU:** An indirection/alias node in the namespace would want the same cheap textual marker.  
**PORT:** Same stroke as MA-075: placement and text-only span are exact, the alpha blend is not.

#### MA-077 · polish · differs ⟨found by verification⟩
**A very narrow text column still has a comfortably clickable hit area.**  
BTextWidget::CalcClickRect widens the computed text rect to kWidthMargin when it is narrower, clamped to the column width if the column itself is narrower than kWidthMargin. BPose::PointInPose(list mode) tests CalcClickRect, while drawing uses CalcRect.  
`TextWidget.cpp` → `BTextWidget::CalcClickRect; BPose::PointInPose (list mode)`  
**PORT:** Same real mechanism as MA-074 and it does satisfy this row: the name-widget test is the whole Name cell rather than the drawn text rect (drawing insets by LEFT/RIGHT margins and fits the string), and the 20 px column floor means that cell is never narrower than kWidthMargin, so hit area is always a superset of the drawn text.  
*src/tiku_trk_app.c:1330-1341 (on_name = cell containing the press); src/tiku_trk_poseview.h:89-95 (margins + MIN_WIDTH 20)*

#### MA-078 · core · done ⟨found by verification⟩
**Editing an attribute column in place enforces the type: unsupported types are refused outright, a bool accepts "0"/"off"/"no"/"false"/empty as false, a Character column refuses a multi-byte glyph, a Time column parses free-form dates, and a failed write is reported.**  
GenericAttributeText::CommitEditedTextFlavor: types outside the string/scalar/float/bool/char set alert "Sorry, you cannot edit that attribute."; B_CHAR alerts "Sorry, the 'Character' attribute cannot store a multi-byte glyph." when !isprint; B_TIME_TYPE uses parsedate(text, time(0)); scalars go through StringToScalar and are written at their declared width; float/double that fail sscanf leave the old on-disk value alone; size < 0 alerts "There was an error writing the attribute." and clears fValueIsDefined. Writes go through Model::WriteAttr (the transient-write-open row).  
`WidgetAttributeText.cpp` → `GenericAttributeText::CommitEditedTextFlavor`  
**TIKU:** This is the write path for a typed VFS node rendered as a column — the type-directed parse/validate/refuse ladder is exactly what a /dev value editor needs.  
**PORT:** The empty-text clause named in the MISSING line is fixed: validate() now checks vtype==VT_BOOL BEFORE the "nothing to write" refusal, so an empty edit on a boolean is accepted, and canonical() sends it as "0" (as_bool("")==-1 -> not >0 -> 0); an empty edit on a number is still refused. The rest of the ladder holds (non-numeric, negative-for-u32, out-of-range naming the range, bool spellings canonicalised) and a device refusal is surfaced verbatim by commit_edit. Caveat, not a failing clause: tiku_trk_attr_editable() still ignores the descriptor (`(void)d;`) so there is no type-based refusal gate - but all five port vtypes (NONE/BOOL/U32/I32/STR) are text-editable, so there is no unsupported type for it to refuse, and the Character/Time clauses have no counterpart type in the port at all.

#### MA-079 · important · partial ⟨found by verification⟩
**Committing an edited name widget renames the file through the normal rename path, so every rename refusal and the well-known-directory confirmation apply; the field itself is capped at the file-name length and rejects illegal filename characters as you type.**  
NameAttributeText::SetupEditing calls DisallowFilenameKeys(textView) and SetMaxBytes(B_FILE_NAME_LENGTH); CommitEditedTextFlavor calls EditModelName(fModel, name, length) and reports success only on B_OK. RealNameAttributeText::SetupEditing does the same for the "Real name" column.  
`WidgetAttributeText.cpp` → `NameAttributeText::SetupEditing / NameAttributeText::CommitEditedTextFlavor`  
**MISSING:** The commit does go through the shared rename path with its refusals and the field is length-capped at TIKU_TRK_EDIT_MAX, but there is no as-you-type illegal-character filter ('/' is typable and only rejected at commit) and no well-known-directory confirmation exists anywhere in the port.  
*src/tiku_trk_app.c:699 (end_rename → tiku_trk_fs_rename) and src/tiku_trk_fs.c:86 (name_ok, commit-time only)*

#### MA-080 · polish · open ⟨found by verification⟩
**An application's whole set of supported types is matched against the whole set of types being dragged, so a drop is allowed if any dragged type is handled.**  
Model::IsDropTargetForList walks the BStringList of dragged types and returns true as soon as SupportsMimeType(NULL, list) != kDoesNotSupportType; Model::IsSuperHandler reports whether the app declares application/octet-stream (it accepts everything). This is the plumbing behind the spring-loaded-folder enable/disable row.  
`Model.cpp` → `Model::IsDropTargetForList / Model::IsSuperHandler`  

#### MA-081 · polish · open ⟨found by verification⟩
**The "Open with relation" column (only ever present in the Open-with window) shows why each application matched, and is truncated at the end rather than the middle.**  
OpenWithRelationAttributeText::ReadValue asks the OpenWithPoseView for OpenWithRelation(model); FitValue asks it for OpenWithRelationDescription() and truncates with B_TRUNCATE_END; PreferredWidth measures at a fixed 500px rather than the usual 100000.  
`WidgetAttributeText.cpp` → `OpenWithRelationAttributeText::FitValue`  

#### MA-082 · polish · done ⟨found by verification⟩
**Owner and Group columns exist in the code but are compiled out, so they never appear in the Attributes menu or as columns.**  
OwnerAttributeText/GroupAttributeText, their NewWidgetText dispatch entries, and the two Attributes-menu items are all inside #ifdef OWNER_GROUP_ATTRIBUTES, which is not defined in the build. A reimplementation that ships them is adding a feature, not porting one.  
`WidgetAttributeText.cpp` → `OwnerAttributeText::ReadValue / GroupAttributeText::ReadValue`  


---

## Queries and the Find window

Architecture notes: [`spec/ARCH-queries.md`](spec/ARCH-queries.md)

#### Q-001 · core · partial
**Alt+F (or the Deskbar's Find… item) opens a Find window; it is a non-resizable, non-zoomable window titled "Find" that closes on Escape.**  
TTracker and BContainerWindow both handle kFindButton by `(new FindWindow())->Show()`; window flags B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE | B_AUTO_UPDATE_SIZE_LIMITS, centered on screen.  
`FindPanel.cpp` → `FindWindow::FindWindow (and Tracker.cpp case kFindButton, ContainerWindow.cpp case kFindButton)`  
**TIKU:** TikuOS has a shell, not windows: the equivalent entry point is a `find` verb/mode. Escape-to-cancel maps to the existing Ctrl+C shell-mode cancel used by `watch`.  
**MISSING:** Ctrl+F and the Deskbar's Find item open a window titled "Find", Escape closes it and the resize grip is explicitly refused for it, but the non-zoomable clause is missing — the tab's zoom widget acts on the panel (zooms it to 544 x find_height) — and the frame is placed at 90,300 rather than centred.  
*src/tiku_trk_desktop.c:open_panel (816), key handler 1972, resize refusal 1714, zoom branch 1679*

#### Q-002 · core · done
**A popup switches the panel between three search modes: by name, by attribute, by formula.**  
fSearchModeMenu items post kByNameItem / kByAttributeItem / kByFormulaItem, handled in FindPanel::MessageReceived which calls SwitchMode(); the initially marked item comes from InitialMode(node).  
`FindPanel.cpp` → `FindPanel::FindPanel / FindPanel::MessageReceived (kByNameItem, kByAttributeItem, kByFormulaItem) / FindPanel::SwitchMode`  

#### Q-003 · core · done
**By-name search matches file names case-insensitively as a substring — unless the text contains '*', in which case the whole text is treated as a pattern and matched for equality.**  
GetByNamePredicate: PushAttr("name"); PushString(text, true) (true = case-insensitive); `strstr(text, "*") != NULL` selects B_EQ (glob/pattern semantics in the FS query engine) else B_CONTAINS.  
`FindPanel.cpp` → `FindPanel::GetByNamePredicate`  
**TIKU:** TikuOS VFS has no query engine or attribute index; the '*' → B_EQ trick must be reimplemented as an explicit glob matcher over node names walked from the namespace.  

#### Q-004 · core · open
**The selected MIME type is ANDed onto every by-name and by-attribute query; picking a supertype searches all of its subtypes.**  
PushMimeType: skips the constraint when type == "mime/ALLTYPES" (the "All files and folders" item); if the type has no '/', appends "/*"; then PushAttr("BEOS:TYPE"), PushString(type), PushOp(B_EQ), PushOp(B_AND).  
`FindPanel.cpp` → `FindPanel::PushMimeType`  
**TIKU:** Replace MIME type with the VFS node's typed descriptor / capability bits; "supertype" becomes a descriptor class, and the "/*" widening becomes a mask test.  

#### Q-005 · core · done
**By-attribute mode shows rows of [attribute menu → operator submenu] + value field; rows are combined with an And/Or menu placed on the preceding row.**  
Each row: BMenuField "MenuField" (col 0), BStringView showing the chosen operator (col 1), BTextControl "TextEntry<row>" (col 2), BMenuField "Logic" (col 3) added on all but the last row. BuildAttrQuery pushes attr, value, operator per row, then pushes the previous row's "combine" operator (defaulting to B_AND when the Logic field is missing).  
`FindPanel.cpp` → `FindPanel::AddAttributeControls / FindPanel::AddLogicMenu / FindPanel::BuildAttrQuery`  

#### Q-006 · core · done
**Every by-attribute row offers three built-in fields — Name, Size, Modified — each with only the operators that make sense for its type.**  
Name (attr "name", B_STRING_TYPE): contains / is / is not / starts with / ends with (operators[0..4]). Size ("size", B_OFF_T_TYPE): greater than / less than / is. Modified ("last_modified", B_TIME_TYPE): before (B_LT) / after (B_GT). First item and its first operator are pre-marked.  
`FindPanel.cpp` → `FindPanel::AddAttributeControls`  
**TIKU:** Size/Modified assume a stat-like model; TikuOS VFS nodes expose typed descriptors instead, so the built-in row set becomes name + descriptor-type + whatever scalar the node reports.  
**UNCONFIRMED:** AddAttributes (FindPanel.cpp) also has an `if (!attributeMessage.FindBool("attr:viewable")) continue;` gate that the row omits — and that call passes no index, so it always tests entry 0's viewable flag rather than the current attribute's. The row's stated requirement set is incomplete and the real gate is index-buggy; I could not confirm the rule as written.  

#### Q-007 · important · open
**The attribute menu is extended with the attributes declared by the currently selected MIME type and its supertype, each with type-appropriate operators.**  
AddMimeTypeAttrs adds supertype attributes first (skipped when the type is supertype-only), then the type's own; AddAttributes reads BMimeType::GetAttrInfo and requires attr:public_name, attr:name and attr:type; strings get contains/is/is not/starts with/ends with, numerics get is/greater than/less than, B_TIME_TYPE gets before/after.  
`FindPanel.cpp` → `FindPanel::AddMimeTypeAttrs / FindPanel::AddAttributes`  
**TIKU:** The MIME attribute registry has no TikuOS analogue; the natural replacement is the VFS node's typed descriptor advertising which named fields are searchable.  

#### Q-008 · important · open
**Typed values are parsed per attribute type before being pushed into the query.**  
B_STRING_TYPE -> PushString(text, true) i.e. case-insensitive; B_TIME_TYPE -> PushDate(text); B_BOOL_TYPE -> "true"=1, "false"=0 (case-insensitive), otherwise atoi, then value %= 2; uint8/16/32 -> PushUInt32(StringToScalar); int8/16/32 -> PushInt32; uint64 -> PushUInt64; int64/off_t -> PushInt64; float -> sscanf %f; double -> sscanf %lf.  
`FindPanel.cpp` → `FindPanel::BuildAttrQuery`  
**TIKU:** StringToScalar accepts K/M/G suffixes; TikuOS should keep that for size fields. 64-bit pushes need care on 16-bit MSP430 targets.  

#### Q-009 · important · open
**A date field holding a relative date such as "today" marks the query dynamic, so its results are re-evaluated later instead of frozen at save time.**  
BuildAttrQuery runs parsedate_etc(text, -1, &flags) and sets dynamicDate when (flags & PARSEDATE_RELATIVE_TIME); SaveQueryAsAttributes then writes kAttrDynamicDateQuery (B_BOOL_TYPE) on the query file.  
`FindPanel.cpp` → `FindPanel::BuildAttrQuery / FindWindow::SaveQueryAsAttributes`  
**TIKU:** Requires a wall clock and a natural-language date parser; on TikuOS the flag is only meaningful where an RTC exists.  

#### Q-010 · core · partial
**By-formula mode lets the user type a raw query predicate, which is used verbatim.**  
FindWindow::GetPredicateString for kByFormulaItem copies the "TextControl" text with `predicate.SetTo(text, 1023)` — silently truncated at 1023 characters.  
`FindPanel.cpp` → `FindWindow::GetPredicateString (case kByFormulaItem)`  
**TIKU:** The predicate grammar is the BFS query language; TikuOS must either define its own predicate grammar over VFS paths/attributes or drop formula mode.  
**MISSING:** Formula mode exists, is pre-filled from the controls and is editable (tiku_trk_find_key writes into f->formula), but the typed text is never used: tiku_trk_find_build's FORMULA case rebuilds the query from the attribute rows and the string is never parsed, so "used verbatim" does not happen.  
*src/tiku_trk_find.c:tiku_trk_find_build (default/FORMULA case, 215-224)*

#### Q-011 · important · done
**Switching from by-name or by-attribute into by-formula pre-fills the formula field with the predicate those controls would have produced.**  
SwitchMode's kByFormulaItem case builds a BQuery via GetByAttrPredicate/GetByNamePredicate, calls GetPredicate() and then sets that string into the newly created TextControl; the conversion is one-way (there is no formula → attribute rows path).  
`FindPanel.cpp` → `FindPanel::SwitchMode`  

#### Q-012 · polish · open
**The MIME type menu is hidden in by-formula mode and shown again in the other two modes, without the panel jumping in width.**  
ShowOrHideMimeTypeMenu hides "MimeTypeMenu" and gives the hidden "MimeTypeMenuSpacer" box its min/max size so the layout keeps the same footprint.  
`FindPanel.cpp` → `FindPanel::ShowOrHideMimeTypeMenu`  

#### Q-013 · important · open
**Changing the MIME type while in by-attribute mode throws away the existing attribute rows and starts over with one fresh row.**  
MessageReceived case kMIMETypeItem: after SetCurrentMimeType, if fMode == kByAttributeItem it calls RemoveAttrViewItems(false) then AddAttrRow() — deliberate, because the available attributes differ per type; entered criteria are lost.  
`FindPanel.cpp` → `FindPanel::MessageReceived (case kMIMETypeItem)`  

#### Q-014 · important · done
**Add and Remove buttons grow and shrink the attribute row list; Remove is disabled while only one row remains.**  
AddAttrRow appends a row and adds the And/Or menu to the previous row, then enables "remove button" when CountRows() > 1; RemoveAttrRow returns early below 2 rows, deletes the last row's views, moves focus to the previous row's TextEntry, and removes the now-dangling Logic field.  
`FindPanel.cpp` → `FindPanel::AddAttrRow / FindPanel::RemoveAttrRow / FindPanel::SetupAddRemoveButtons`  

#### Q-015 · important · open
**The MIME type menu lists "All files and folders" first, then the user's most-used types, then a tree of supertypes with their subtypes; empty supertypes are omitted.**  
AddMimeTypesToMenu: first item + separator, then up to 5 names from MostUsedNames("MostUsedMimeTypes","Tracker") + separator, then GetInstalledSupertypes() submenus filled by AddOneMimeTypeToMenu; supertype submenus with 0 items are removed. Two subtypes sharing a short description get " (subtype)" appended to both labels.  
`FindPanel.cpp` → `FindPanel::AddMimeTypesToMenu / FindPanel::AddOneMimeTypeToMenu`  

#### Q-016 · polish · open
**Picking a MIME type promotes it in a persistent most-used list, so frequently searched types stay near the top of the menu across reboots.**  
kMIMETypeItem calls gMostUsedMimeTypes.AddName(label) for every item except index 0; the list lives in ~/config/settings/Tracker/MostUsedMimeTypes as "<count> <name>" lines, sorted by count then case-insensitive name, counts are decremented on write, clamped to 20, and entries below -10 are dropped (never the first).  
`FindPanel.cpp` → `MostUsedNames::AddName / MostUsedNames::LoadList / MostUsedNames::~MostUsedNames / MostUsedNames::CompareNames`  
**TIKU:** A decaying most-used list is a persist-cell-sized amount of state; on TikuOS it would be a small durable record rather than a settings file.  

#### Q-017 · core · done
**A Target menu scopes the search: "All disks", or any subset of the mounted, query-capable volumes.**  
AddVolumes puts "All disks" (device -1) first, a separator, then a ModelMenuItem per volume where volume.IsPersistent() && volume.KnowsQuery() && GetRootDirectory() succeeds; "All disks" starts marked. The menu is built once — the code notes it is not rebuilt when a volume is mounted later.  
`FindPanel.cpp` → `FindPanel::AddVolumes`  
**TIKU:** There are no volumes; the scope selector becomes a namespace subtree (e.g. /dev vs /sys vs a filestore mount), and mount/unmount maps to a device appearing/disappearing in the VFS.  

#### Q-018 · important · done
**Choosing "All disks" clears every individual volume mark and also drops all folder filters; choosing a single volume toggles it and unmarks "All disks".**  
MessageReceived case kVolumeItem: for device == -1 it unmarks items 2..count+1 and marks item 0; otherwise it unmarks item 0 and toggles the invoked item. In both paths it then walks items [3+fVolumeItemsCount, count-2) removing each folder filter and unmarking it, and finally calls ShowVolumeMenuLabel().  
`FindPanel.cpp` → `FindPanel::MessageReceived (case kVolumeItem)`  

#### Q-019 · important · partial
**"Select folders…" adds specific folders as search scopes; results are then restricted to those subtrees, and selecting any folder unmarks the disks.**  
kSelectDirectoryFilter opens a B_OPEN_PANEL with FolderFilter (directories, or symlinks resolving to directories) and multi-select; kAddDirectoryFilters adds each ref via AddDirectoryFilter, which refuses duplicates (B_CANCELED), inserts a marked ModelMenuItem before the trailing separator + "Select folders…" item, and calls UnmarkDisks(). Clicking an added folder item toggles it off/on (kRemoveDirectoryFilter).  
`FindPanel.cpp` → `FindPanel::MessageReceived (kSelectDirectoryFilter, kAddDirectoryFilters, kRemoveDirectoryFilter) / FindPanel::AddDirectoryFilter / FolderFilter::Filter`  
**TIKU:** Maps cleanly onto "scope this search to a VFS subtree"; the prefix-match implementation below carries over directly.  
**MISSING:** Scoping works — the Search-in popup toggles places, choosing one clears "Everywhere" and choosing Everywhere clears the places, duplicates are refused, and results really are restricted (the walk starts at the scopes and in_scope does a trailing-slash prefix test) — but there is no "Select folders…" chooser: the scope list is fixed at startup to the containers found directly under "/", so an arbitrary deeper folder cannot be added.  
*src/tiku_trk_find.c:open_popup/tiku_trk_find_click (683-696, 743-755), src/tiku_trk_query.c:tiku_trk_query_add_scope; places seeded at src/tiku_trk_desktop.c:1293-1305*

#### Q-020 · important · done
**Folder filters are enforced as path-prefix tests on every result, including results that arrive later from the live query.**  
QueryEntryListCollection stores each filter as its absolute path + "/" in fPathFilters; PathFilter() accepts a ref when any filter is a strncmp prefix of its path. Applied in GetNextEntry/GetNextRef/GetNextDirents (so no pose is ever created) AND in QueryRefFilter::Filter (so live arrivals are filtered too). With filters active GetNextDirents reads one dirent at a time.  
`QueryPoseView.cpp` → `QueryEntryListCollection::PathFilter / QueryEntryListCollection::GetNextEntry / QueryRefFilter::Filter`  

#### Q-021 · polish · done
**The Target menu's label summarises the scope: the single volume or folder name, "N selected", or "All disks".**  
ShowVolumeMenuLabel: 0 volumes + exactly 1 folder filter → that folder's name; 1 volume + 0 folders → the volume label; >1 folders or (not-all-volumes and >1 volumes) → MultipleSelectionsTitle(volumes+folders) i.e. the plural-aware "# selected"; otherwise mark and show "All disks".  
`FindPanel.cpp` → `FindPanel::ShowVolumeMenuLabel / FindPanel::MultipleSelectionsTitle`  

#### Q-022 · core · partial
**The Search button is the default button, so pressing Enter anywhere in the panel runs the search; in template-editing mode the button says Save and only saves.**  
FindPanel::FindPanel creates either a "find" button posting kFindButton or, when editTemplateOnly, a "save" button posting kSaveButton, and calls MakeDefault(true). FindWindow handles kFindButton→Find() and kSaveButton→Save(); both end with PostMessage(B_QUIT_REQUESTED), so the Find window always closes.  
`FindPanel.cpp` → `FindPanel::FindPanel / FindWindow::MessageReceived (kFindButton, kSaveButton)`  
**MISSING:** The Search button is drawn in the default-button state and Return anywhere in the panel runs the search regardless of which field has the caret, but the template-editing clause is absent (Save is a separate always-present button, there is no editTemplateOnly mode) and neither action closes the Find window.  
*src/tiku_trk_find.c:tiku_trk_find_draw (533-534); src/tiku_trk_desktop.c key handler 1970-1971*

#### Q-023 · core · partial
**Running a search writes a query file into ~/queries and asks Tracker to open it; the results appear in a separate window, not in the Find panel.**  
FindSaveCommon creates/overwrites the file (GetQueriesDirectory = B_USER_DIRECTORY/queries, mkdir 0777 if absent), then Find() posts B_REFS_RECEIVED with fRef to be_app; TTracker::OpenRef sees Model::IsContainer() (true for queries) and opens a BQueryContainerWindow.  
`FindPanel.cpp` → `FindWindow::Find / FindWindow::FindSaveCommon / FindWindow::GetQueriesDirectory (Tracker.cpp TTracker::OpenRef)`  
**TIKU:** The "query is a file you open" indirection needs a decision: TikuOS could keep it (query record in the NVM filestore, opened by the shell) or collapse it to a direct command that prints results.  
**MISSING:** Results genuinely appear in a separate window, but the row's stated mechanism — writing a query file into ~/queries and asking Tracker to open it — is simply gone: panel_search runs the walk inline and hands the live run to a new window, and no query record is written or reopened on the run path (saving is an unrelated explicit action), so a user never gets the reopenable query object the row describes.  
*src/tiku_trk_desktop.c:837-865 (panel_search: query_start/step loop then open_results) and :525-550 (open_results); save is a separate button at :1754-1770*

#### Q-024 · important · done
**A new query file gets an auto-generated name describing the criteria plus a timestamp.**  
GetDefaultName: by name → "Name = <text>"; by formula → "Formula <text>"; by attribute → "<MIME label>: <attr> <op> <value>, …" per row; then strftime " - %b %d, %I:%M:%S %p" is appended. MakeValidFilename truncates to B_FILE_NAME_LENGTH-4 + ellipsis and replaces '/' with '_'.  
`FindPanel.cpp` → `FindWindow::GetDefaultName / FindPanel::GetDefaultName / FindPanel::GetDefaultAttrName / MakeValidFilename`  
**TIKU:** Name-length limit and slash escaping depend on the target filestore; TFS name limits differ from B_FILE_NAME_LENGTH.  

#### Q-025 · important · open
**Re-running an already-saved query replaces the same file, which closes the old results window first, and the new window reopens at the old position with the old column layout.**  
FindSaveCommon: replaceOriginal = fFile && (!fFromTemplate || fEditTemplateOnly); it Unsets the file and Removes the entry "to quit the running query and close the corresponding window", captures BContainerWindow::GetLayoutState + FSGetPoseLocation beforehand and restores them in SaveQueryAsAttributes. Find() then polls TTracker::EntryHasWindowOpen(&fRef) with snooze(1000) up to 5000 times before posting refs.  
`FindPanel.cpp` → `FindWindow::FindSaveCommon / FindWindow::Find / FindWindow::SaveQueryAsAttributes`  
**TIKU:** The delete-to-close idiom relies on node monitoring of the query file. With VFS watch notifications the same trick works, but the 5 s bounded spin should become an event wait.  

#### Q-026 · core · open
**Opening a saved query for editing (Edit query, Alt+G) restores the whole panel: mode, MIME type, text or attribute rows, target volumes, the Trash option, keyboard focus and even the text selection.**  
RestoreWindowState reads kAttrQueryInitialMode, kAttrQueryInitialMime, kAttrQueryMoreOptions, kAttrQueryVolume, _trk/directories, then per mode either kAttrQueryInitialString or kAttrQueryInitialNumAttrs + kAttrQueryInitialAttrs (RestoreAttrState per row: menuSelection, subMenuSelection, attrViewText, logicalRelation index), and finally _trk/focusedView plus _trk/focusedSelStart/_trk/focusedSelEnd (selection only re-applied in formula mode).  
`FindPanel.cpp` → `FindPanel::RestoreWindowState / FindPanel::RestoreAttrState / FindPanel::InitialMode / FindPanel::InitialAttrCount`  
**TIKU:** Attributes become fields of one serialized query record; TikuOS has no per-file attribute namespace, so the split between the executable predicate and the UI state must be an explicit record layout.  
**CHECKED:** Nothing opens a saved query for editing: tiku_trk_query_load/deserialise restores mode, terms and scopes into a query record but has no caller outside tests, there is no path back into tiku_trk_find_t, and no focus or text-selection state is stored at all.  
*none found (tiku_trk_query_load unwired — only tests/test_query.c:269 calls it)*

#### Q-027 · important · open
**Target volumes are remembered by volume identity, not by device number, so a query reopened after a remount still targets the right disk — and a volume that is not mounted this time is skipped rather than dropped.**  
SaveQueryAsAttributes embeds creationDate, capacity, deviceName, volumeName, fshName per selected volume (EmbedUniqueVolumeInfo) into kAttrQueryVolume. MatchArchivedVolume scores candidates 5/4/3/2/1 for those five fields and accepts the best with score >= 9; B_DEV_BAD_DRIVE_NUM means "not mounted, keep looking", any other error stops the scan. If a query names every mounted volume it reopens showing "All disks".  
`FindPanel.cpp` → `FindWindow::SaveQueryAsAttributes / FindPanel::RestoreWindowState (Utilities.cpp EmbedUniqueVolumeInfo, MatchArchivedVolume)`  
**TIKU:** Needs a stable device identity in the VFS (serial/UID) to survive re-enumeration — the direct analogue of the score-based match.  

#### Q-028 · core · open
**A query saved as a template opens the Find window (pre-filled) instead of running, while a plain saved query opens its results.**  
The distinction is the file's MIME type: B_QUERY_TEMPLATE_MIMETYPE vs B_QUERY_MIMETYPE (FindWindow::IsQueryTemplate uses strcasecmp on BNodeInfo::GetType). TTracker::OpenRef opens a container window for Model::IsQuery() and a `new FindWindow(ref)` for Model::IsQueryTemplate().  
`FindPanel.cpp` → `FindWindow::IsQueryTemplate / FindPanel::SaveAsQueryOrTemplate (Tracker.cpp TTracker::OpenRef)`  

#### Q-029 · core · open
**Running a search from a template creates a NEW query file and leaves the template untouched; editing a template in place (Edit Query template) saves back over it.**  
FindSaveCommon: newFile = !fFile || (fFromTemplate && !fEditTemplateOnly); replaceOriginal = fFile && (!fFromTemplate || fEditTemplateOnly). The window title becomes "Edit Query template" and fEditTemplateOnly is set only when constructed with editIfTemplateOnly and the file's type is the template type.  
`FindPanel.cpp` → `FindWindow::FindSaveCommon / FindWindow::FindWindow`  

#### Q-030 · important · open
**Opening Find with no query falls back to a "default" template in ~/queries, creating it on first use; with no query loaded the Save item is disabled.**  
FindWindow::FindWindow: if TryOpening(newRef) fails it looks for ~/queries/default, else creates it with O_RDWR|O_CREAT and calls SaveQueryAttributes(fFile, true) (template type + kAttrQueryLastChange + _trk/recentQuery=1); fSaveQueryOrTemplateItem->SetEnabled(false) in that branch.  
`FindPanel.cpp` → `FindWindow::FindWindow / FindWindow::SaveQueryAttributes`  

#### Q-031 · important · open
**A Templates menu lists every query template on the system; picking one loads it into the open Find window and enables Save.**  
PopulateTemplatesMenu runs `_trk/recentQuery == 1` on every persistent volume that KnowsQuery && KnowsAttr, skips refs in Trash, keeps only B_QUERY_TEMPLATE_MIMETYPE entries that still Exists(), de-duplicates by BPath (BQuery can return the same file twice), and targets each item at the FindPanel with kSwitchToQueryTemplate; the handler calls SwitchToTemplate + UpdateFileReferences + LoadDirectoryFiltersFromFile and enables the Save item.  
`FindPanel.cpp` → `FindWindow::PopulateTemplatesMenu / FindWindow::MessageReceived (case kSwitchToQueryTemplate) / FindWindow::SwitchToTemplate`  
**TIKU:** Discovering templates by running a query over an attribute index has no VFS analogue; TikuOS needs an explicit template directory/registry.  

#### Q-032 · important · open
**A Recent queries menu offers the last 10 queries you ran, most recent first, with all query templates listed above them.**  
AddRecentQueries collects `_trk/recentQuery == 1` hits from all volumes, skips Trash and non-existent entries, splits by MIME type, sorts queries by kAttrQueryLastChange descending (SortByDatePredicate), caps at 10, de-duplicates by (ref, changeTime), and separates templates from queries with a separator; addSaveAsItem optionally appends "Save query as template…". RecentFindItemsMenu rebuilds itself on every AttachedToWindow.  
`FindPanel.cpp` → `FindPanel::AddRecentQueries / RecentFindItemsMenu::AttachedToWindow / TrackerBuildRecentFindItemsMenu`  
**VERIFIER CORRECTION:** Templates are conditional on the `includeTemplates` argument (default true). The Find window's own Query > Recent queries submenu is built with `AddRecentQueries(fHistoryMenu, false, &messenger, kSwitchToQueryTemplate, false)` — includeTemplates = **false**, so that menu lists queries only. Templates appear above the queries only in the default-argument callers (RecentFindItemsMenu::AttachedToWindow / TrackerBuildRecentFindItemsMenu).  
**CHECKED:** There is no recent-queries menu anywhere in the panel, the menu bar, the context menus or the Deskbar; the newest-first name index that tiku_trk_query_save maintains records only explicitly saved queries, is capped by the caller not at 10, has no template notion and is never surfaced in any UI.  
*none found (tiku_trk_query_list has no non-test caller)*

#### Q-033 · important · partial
**Every query or template written stamps a change time and a marker attribute, which is what makes it show up in the recent/templates menus.**  
kAttrQueryLastChange (B_INT32_TYPE, time(0)) and "_trk/recentQuery" = 1 are written in SaveQueryAttributes, FindSaveCommon and SaveQueryAsAttributes; TTracker::InstallIndices creates fs indices for both on every writable, query-capable volume at boot so the menus' own queries can find them.  
`FindPanel.cpp` → `FindWindow::SaveQueryAttributes / FindWindow::FindSaveCommon (TrackerInitialState.cpp TTracker::InstallIndices)`  
**MISSING:** query_save does register each written query in a newest-first index attribute, but no change time is stamped and the index has no consumer anywhere in the application — tiku_trk_query_list is called only from tests, and there is no recent/templates menu — so the clause the row hangs the behaviour on ('what makes it show up in the menus') does not reach a user.  
*src/tiku_trk_query.c:791-830 (query_save index merge, no timestamp) and :851 (query_list, whose only callers are tests/test_query.c:280-288)*

#### Q-034 · polish · open
**Dragging the Find window's icon into a folder saves the current search there; the drop menu lets you choose "Create a Query" or "Create a Query template".**  
A DraggableQueryIcon carries B_SIMPLE_DATA with be:actions=B_COPY_TARGET, two be:filetypes (query, query template) and two be:actionspecifier strings. The B_COPY_TARGET reply is resolved in FindPanel::MessageReceived by matching either the action specifier or the file type, then SaveAsQueryOrTemplate creates the file and posts kAttachFile so the window re-attaches to it. DragStarted sets be:clip_name (always "New Query" in practice). The icon is absent in template-edit-only mode.  
`FindPanel.cpp` → `FindPanel::MessageReceived (case B_COPY_TARGET) / FindPanel::SaveAsQueryOrTemplate / DraggableQueryIcon::DragStarted / FindWindow::MessageReceived (case kAttachFile)`  
**TIKU:** No drag and drop; the equivalent is `find … > /path/name.query` — i.e. an explicit save-to-path argument.  

#### Q-035 · important · partial
**Query menu commands: Open… (Alt+O) loads an existing query into the panel, Save (Alt+S) writes it back in place and makes it permanent, Save as query… / Save as template… write a new file via a save panel.**  
BuildMenuBar wires kOpenLoadQueryPanel, fSaveQueryOrTemplateItem (kSaveQueryOrTemplate, 'S'), and two kOpenSaveAsPanel items differing by the bool "saveastemplate"; the save panel's B_SAVE_REQUESTED is completed by FindPanel (accepting either a direct ref or directory+name). kSaveQueryOrTemplate calls SaveQueryAsAttributes(..., temporary=false), which is what un-marks a query as transient.  
`FindPanel.cpp` → `FindWindow::BuildMenuBar / FindWindow::MessageReceived (kOpenLoadQueryPanel, kOpenSaveAsPanel, kSaveQueryOrTemplate) / FindPanel::MessageReceived (case B_SAVE_REQUESTED)`  
**MISSING:** Only Save exists, and as a button that always writes a NEW record under an auto-generated name; Open… (load an existing query into the panel), the in-place/permanent distinction, Save as query… and Save as template… with a save panel are all absent, and the Find window has no menu bar.  
*src/tiku_trk_desktop.c:1755-1772 (HIT_SAVE → tiku_trk_query_save with tiku_trk_find_default_name)*

#### Q-036 · important · open
**An "Include Trash" option makes the query also return items in the Trash; it is off by default and is stored with the query.**  
fSearchInTrash is a checkable item in the Options menu (kSearchInTrashOptionClicked toggles it); its state goes into MoreOptionsStruct::searchTrash in kAttrQueryMoreOptions and is read back by both RestoreWindowState (via FindWindow::SetOptions) and the results view. Enforcement is in QueryRefFilter::Filter: results whose ref is in a Trash node are dropped unless ShowResultsFromTrash().  
`FindPanel.cpp` → `FindWindow::MessageReceived (case kSearchInTrashOptionClicked) / FindWindow::SetOptions / QueryPoseView.cpp QueryRefFilter::Filter`  
**TIKU:** Only meaningful if TikuOS has a trash concept; otherwise drop, but keep the generic "exclude this subtree" filter hook it rides on.  

#### Q-037 · important · open
**Queries created by pressing Search are transient: if they are older than about a week and no window is showing them, Tracker deletes them from ~/queries automatically.**  
FindSaveCommon passes temporary=newFile into SaveQueryAsAttributes, so template-derived/new queries are temporary and explicitly Saved ones are not (MoreOptionsStruct::temporary; MoreOptionsStruct::SetQueryTemporary/QueryTemporary). DeleteTransientQueriesTask walks ~/queries in batches of 100, starting 30 min after boot when the machine has been idle 5 min, and removes an entry only when it is a query, temporary, old enough (kDaysToExpire = 7, compared via tm_mday), and not open in a window (TTracker::EntryHasWindowOpen). Not started in safe mode.  
`FindPanel.cpp` → `DeleteTransientQueriesTask::ProcessOneRef / DeleteTransientQueriesTask::QueryOldEnough / DeleteTransientQueriesTask::StartUpTransientQueryCleaner / MoreOptionsStruct::QueryTemporary`  
**TIKU:** Auto-GC of saved searches needs a wall clock and idle detection; on TikuOS the idle hook exists (tickless idle) but a 7-day timer implies an RTC.  

#### Q-038 · core · done
**Query results are live: files that start or stop matching while the window is open appear and disappear without re-running the search.**  
FetchOneQuery sets query->SetTarget(BMessenger(poseView)) and query->SetFlags(B_QUERY_WATCH_ALL) before Fetch(); the kernel then posts B_QUERY_UPDATE, which BPoseView::MessageReceived routes into FSNotification alongside node-monitor opcodes, and query windows explicitly accept B_ENTRY_CREATED notices whose parent dir is not the window's own model. BQueryPoseView::WatchNewNodeMask() returns 0 because B_QUERY_WATCH_ALL already covers the results.  
`QueryPoseView.cpp` → `QueryEntryListCollection::FetchOneQuery / BQueryPoseView::WatchNewNodeMask (PoseView.cpp BPoseView::FSNotification case B_ENTRY_CREATED)`  
**TIKU:** TikuOS VFS watch says only "node touched, re-read" and the table is 8 per-boot slots — a live query must re-evaluate its own predicate on each notification and cannot rely on kernel-side match/unmatch events. Watch-slot budget caps the number of concurrent live queries.  

#### Q-039 · core · done
**A query spanning several volumes runs one search per volume and presents the union as a single result list.**  
QueryEntryListCollection's constructor either replays kAttrQueryVolume (one FetchOneQuery per matched volume) or, if no volume info is stored or none matched, falls back to searching every persistent KnowsQuery volume; iteration walks fQueryList in order via fQueryListIndex, and Rewind() rewinds all of them. CountEntries() returns 0 — the total is unknown in advance.  
`QueryPoseView.cpp` → `QueryEntryListCollection::QueryEntryListCollection / QueryEntryListCollection::Rewind / QueryEntryListCollection::CountEntries`  

#### Q-040 · important · open
**A query containing a relative date refreshes itself automatically — at the next midnight, or the next hour/minute if the predicate mentions those units — and results that no longer match disappear.**  
InitDirentIterator schedules BQueryPoseView::Refresh via TTracker::MainTaskLoop()->RunLater at the next minute/hour/midnight boundary; the unit is chosen by case-insensitively searching the predicate for "second"/"minute" (minute refresh, DEBUG builds only) and "hour". Refresh clears fAddPosesThreads, drops the query list, sets fCreateOldPoseList so the current poses are snapshotted, re-adds poses, and AddPosesCompleted deletes every pose still in the old list.  
`QueryPoseView.cpp` → `BQueryPoseView::InitDirentIterator / BQueryPoseView::Refresh / BQueryPoseView::AddPosesCompleted`  

#### Q-041 · important · open
**Query result windows are always list view: the view-mode commands do nothing, icon positions are never saved, and "Open parent" is disabled.**  
BQueryPoseView overrides SetViewMode, SavePoseLocations and OpenParent as empty, and both RestoreState overloads force fViewState->SetViewMode(kListMode) after the base call.  
`QueryPoseView.cpp` → `BQueryPoseView::SetViewMode / BQueryPoseView::SavePoseLocations / BQueryPoseView::OpenParent / BQueryPoseView::RestoreState`  
**CHECKED:** Nothing treats result views as list-only: tiku_trk_poseview_set_results does not force the mode, menu_invoke applies Icon/Mini/List view to a query view like any other, save_state writes pose positions with no is_query gate, and Open parent stays enabled (can_open_parent only tests path != "/").  
*none found — no is_query check in src/tiku_trk_poseview.c:tiku_trk_poseview_set_mode (1592) or _save_state (2182), nor in src/tiku_trk_desktop.c:menu_invoke (981-989)*

#### Q-042 · important · partial
**A results window shows Name, Location, Size and Modified by default, and remembers a per-searched-type column layout so e.g. e-mail queries come up with Subject/From/When/Status.**  
SetupDefaultColumnsIfNeeded adds Name(145)/Location(225, kAttrPath)/Size(80)/Modified(150) only when no columns were restored. BQueryContainerWindow::SetupDefaultState copies kAttrWindowFrame/kAttrViewState/kAttrColumns from settings/Tracker/DefaultQueryTemplates/<sanitized type> where the type comes from PoseView()->SearchForType() (kAttrQueryInitialMime resolved through MimeTypeList, defaulting to B_FILE_MIMETYPE) with '/' replaced by '_'; TTracker::InstallDefaultTemplates seeds those files for octet-stream, bookmark, person and e-mail.  
`QueryPoseView.cpp` → `BQueryPoseView::SetupDefaultColumnsIfNeeded / BQueryPoseView::SearchForType (QueryContainerWindow.cpp BQueryContainerWindow::SetupDefaultState, TrackerInitialState.cpp TTracker::InstallDefaultTemplates)`  
**TIKU:** The Location column is what makes results usable; in the TikuOS namespace this is the node's full VFS path.  
**MISSING:** The Location clause is honoured — a query view's rows draw the node's full VFS path instead of its name — but the columns are the generic per-view defaults (Name/Kind/Value/Cap, no Size and no Modified) set in poseview_new, and there is no per-searched-type remembered layout of any kind.  
*src/tiku_trk_desktop.c:folder_draw (267-269) and folder_fit_w (464-475); defaults at src/tiku_trk_poseview.c:506-514*

#### Q-043 · polish · differs
**In a results window, changes to a file's size or modification time refresh its row immediately, like an ordinary folder window.**  
BQueryPoseView::AttributeChanged intercepts attr names "last_modified" and "size", strips the "attr" field from a copy of the message so BPoseView treats it as a B_STAT_CHANGED-style update, and forwards it.  
`QueryPoseView.cpp` → `BQueryPoseView::AttributeChanged`  
**PORT:** An actual refresh path exists and is the folder window's own path: refresh_results re-walks and pushes the set through poseview_apply_results -> notify_diff (facts_differ covers size and mtime) -> the same EV_CHANGED handler, so result rows do update on size/mtime; only the cadence is adapted (3 s re-walk vs the 400 ms folder pump), a tuning constant rather than a missing mechanism.  
*src/tiku_trk_desktop.c:554-590 (refresh_results -> apply_results) with :2310-2320 (3 s requery beat vs :2262 400 us*1e3 pump) and src/tiku_trk_notify.c:78-90 (facts_differ size/mtime)*

#### Q-044 · important · open
**Query windows are treated as read-only containers: no New folder, no paste into them, and the results window's own menu is trimmed to Resize/Select/Select all/Invert selection/Close.**  
BPoseView::TargetVolumeIsReadOnly() returns true whenever the target model IsQuery/IsQueryTemplate; BContainerWindow::ShouldHaveNewFolderItem() excludes queries; BQueryContainerWindow::AddWindowMenu and AddWindowContextMenus build the reduced menus. Read-only-ness of a multi-item selection in a query is computed per item's own volume rather than from the window.  
`QueryContainerWindow.cpp` → `BQueryContainerWindow::AddWindowMenu / BQueryContainerWindow::AddWindowContextMenus (PoseView.cpp BPoseView::TargetVolumeIsReadOnly, BPoseView::SelectedVolumeIsReadOnly)`  
**VERIFIER CORRECTION:** Two different menus, and both are misstated. BQueryContainerWindow::AddWindowMenu builds Resize to fit / Select… / Select all / Invert selection / Close / **Close all in workspace** (six items). BQueryContainerWindow::AddWindowContextMenus builds only Resize to fit / Select… / Select all / Close — it has **no Invert selection**.  
**CHECKED:** Query windows are ordinary folder windows: tiku_trk_sc_ctx_t declares is_query but menu_ctx never sets it and tiku_trk_sc_enabled never reads it, target_ro is decided by a /sys,/dev,/proc path prefix (a results window's pseudo-path "find #N" matches none), so New folder and Paste stay enabled and no trimmed window/context menu variant exists.  
*none found — is_query declared at src/tiku_trk_shortcuts.h:100, unused in src/tiku_trk_shortcuts.c and src/tiku_trk_desktop.c:menu_ctx (149-196)*

#### Q-045 · important · open
**"Edit query" (Alt+G) reopens the Find window on the query — either the query the window is showing, or the query/template files selected in a folder.**  
BQueryPoseView::EditQueries sends kEditQuery with the window's own target ref to the Tracker signature; BPoseView::EditQueries sends the selection with onlyQueries=true (SendSelectionAsRefs filters non-query models) and "editQueryOnPose"; TTracker::EditQueries opens a FindWindow per existing ref. The item is enabled when the window is a query or something is selected, and lives between Open and Open with in the menu; a plain folder window only shows it for query/template poses (or inside ~/queries).  
`QueryPoseView.cpp` → `BQueryPoseView::EditQueries (PoseView.cpp BPoseView::EditQueries, Tracker.cpp TTracker::EditQueries, ContainerWindow.cpp BContainerWindow::SetupEditQueryItem / ShouldHaveEditQueryItem, Shortcuts.cpp TShortcuts::UpdateEditQueryItem)`  

#### Q-046 · important · open
**Unmounting a volume force-closes any query window that is searching it, so the unmount can proceed.**  
TTracker::CloseActiveQueryWindows posts B_QUIT_REQUESTED to every BQueryContainerWindow whose ActiveOnDevice(device) is true, then waits for them to close; ActiveOnDevice asks the pose view whether any BQuery in fQueryList has TargetDevice() == device. TTracker::QueryActiveForDevice answers the same question without closing.  
`QueryPoseView.cpp` → `BQueryPoseView::ActiveOnDevice (Tracker.cpp TTracker::CloseActiveQueryWindows, TTracker::QueryActiveForDevice)`  
**TIKU:** Direct analogue: a device disappearing from the VFS must tear down live searches bound to it, or their watch slots leak.  

#### Q-047 · important · done
**The Select window matches poses by name with five rules — starts with, ends with, contains, matches wildcard expression, matches regular expression — plus Invert and Ignore case; wildcard and Ignore case are the defaults.**  
SelectionWindow builds the popup in that order, marks item 3 (wildcard) and sets fIgnoreCaseCheckBox to 1; ExpressionType maps the menu index through {kStartsWith,kEndsWith,kContains,kGlobMatch,kRegexpMatch}. kSelectMatchingEntries carries ExpressionType/Expression/InvertSelection/IgnoreCase; BPoseView::SelectMatchingEntries clears the selection, enables multiple selection, and adds each pose where `name.Matches(expr, !ignoreCase, type) ^ invertSelection`. TrackerString::Matches dispatches, and kNone matches nothing.  
`SelectionWindow.cpp` → `SelectionWindow::SelectionWindow / SelectionWindow::ExpressionType / SelectionWindow::MessageReceived (PoseView.cpp BPoseView::SelectMatchingEntries, TrackerString.cpp TrackerString::Matches)`  
**TIKU:** This is the one place with a real string-matching engine; a TikuOS shell `select`/`filter` verb should reuse the same five rules so behavior is consistent with find.  

#### Q-048 · polish · done
**A malformed regular expression is reported in an alert naming the specific problem instead of silently matching nothing.**  
SelectMatchingEntries pre-compiles the pattern when expressionType == kRegexpMatch and, on InitCheck() != B_OK, shows a B_STOP_ALERT containing RegExp::ErrorString() — one of 15 messages such as "Unmatched parenthesis.", "Nested *?+.", "Invalid bracket range.", "Trailing \\.", "?+* follows nothing." — and selects nothing.  
`PoseView.cpp` → `BPoseView::SelectMatchingEntries (RegExp.cpp kRegExpErrorStringArray, RegExp::ErrorString)`  

#### Q-049 · important · partial
**Wildcard matching must consume the whole name and supports *, ?, and bracket classes with ranges and negation, UTF-8 aware.**  
StringMatchesPattern: '?' skips one glyph; '*' collapses runs of */? and, when trailing, matches everything; backtracking is stacked up to kWildCardMaximum = 100 levels; success requires both string and pattern to be exhausted. MatchesBracketExpression accepts '^' or '!' as the negator, requires a closing ']' (unmatched bracket = no match), and only allows ranges whose endpoints are both lower, both upper, or both digits — anything else is a syntax error returning false. A leading '[' inside the class is literal.  
`TrackerString.cpp` → `TrackerString::StringMatchesPattern / TrackerString::MatchesBracketExpression / TrackerString::MatchesGlob`  
**TIKU:** Recursion-free but uses two 100-entry pointer arrays on the stack (~800 B on 32-bit); on MSP430-class targets that stack cost needs a smaller bound.  
**MISSING:** glob_match is a faithful port — '?' eats one character, '*' collapses */? runs, trailing '*' matches the rest, a 100-level pattern/string stack backtracks, success needs both exhausted, and bracket_match takes ^ or ! as negator, fails on an unmatched ']', rejects mixed-kind ranges and treats a leading '[' literally — but the UTF-8 clause is missing: glyph handling was deliberately dropped, so '?' and classes step one BYTE (the header states this).  
*src/tiku_trk_match.c:glob_match (88) / bracket_match (46); header note lines 8-11*

#### Q-050 · important · done
**Regular expressions are Spencer-style basic regexps searched anywhere in the name unless anchored, and case-insensitive matching is done by lowercasing both pattern and text.**  
RegExp::Atom implements ^ $ . [ ] ( ) | ? + * and backslash escaping (kMeta = "^$.[()|?+*\\"); there are no \d/\w classes, no {n,m}, and sub-expressions are capped (kSubExpressionMax). RunMatcher tries every start offset unless prog->reganch, using regmust/regstart prefilters. TrackerString::MatchesRegExp lowercases BOTH the pattern and the text when caseSensitivity is false — so character classes written in uppercase stop working in that mode — and returns false if the expression fails to compile. Patterns compiling to more than 32767 bytes are rejected ("Expression too long.").  
`RegExp.cpp` → `RegExp::Atom / RegExp::RunMatcher / RegExp::Compile (TrackerString.cpp TrackerString::MatchesRegExp)`  
**TIKU:** ~1500 lines of regex engine is a real code-size decision on a microwatt target; glob-only is the cheap fallback, and the case-insensitivity-by-lowercasing quirk should be fixed rather than reproduced.  
**PORT:** Alternation, groups, classes, quantifiers and anchors with unanchored search and case folding by lowercasing; no back-references or interval quantifiers.

#### Q-051 · polish · done
**Starts with / ends with / contains use plain substring positions, with a case-insensitive variant chosen per call.**  
StartsWith = FindFirst/IFindFirst == 0; EndsWith computes position = Length() - strlen(needle), returns false if negative, then FindLast/IFindLast == position; Contains = FindFirst/IFindFirst > -1.  
`TrackerString.cpp` → `TrackerString::StartsWith / TrackerString::EndsWith / TrackerString::Contains`  
**PORT:** tiku_trk_match implements STARTS/ENDS/CONTAINS over plain positions with a per-call case_sensitive flag, ENDS anchored at text+tl-el behind the el>tl guard that stands in for the negative-position check, and CONTAINS treating an empty needle as a hit — the same results as FindFirst/IFindFirst/FindLast, and the tests exercise the case flag and the short-string guard.  
*src/tiku_trk_match.c:675-706 (tiku_trk_match STARTS/ENDS/CONTAINS) with tests/test_match.c:38-52*

#### Q-052 · important · open ⟨found by verification⟩
**A brand-new query window inherits its layout from a per-search-type template, so all windows searching the same MIME type look alike.**  
BQueryContainerWindow::SetupDefaultState builds the path "query templates/<search type with '/' replaced by '_'>" under the Tracker settings dir, and (without creating it) copies only kAttrWindowFrame, kAttrViewState(+Foreign) and kAttrColumns(+Foreign) from it — note kAttrWindowWorkspace is deliberately NOT in the allow-list, unlike the folder case.  
`QueryContainerWindow.cpp` → `BQueryContainerWindow::SetupDefaultState`  

#### Q-053 · important · open ⟨found by verification⟩
**Choosing Query > "Open…" silently flips the "Include Trash" option as a side effect of opening the panel.**  
The `case kOpenLoadQueryPanel:` block in FindWindow::MessageReceived has no `break;` — it falls through into `case kSearchInTrashOptionClicked:` which does `fSearchInTrash->SetMarked(!fSearchInTrash->IsMarked())`. Every use of Open… toggles the Trash option of the query being edited.  
`FindPanel.cpp` → `FindWindow::MessageReceived (case kOpenLoadQueryPanel falling into kSearchInTrashOptionClicked)`  
**TIKU:** A genuine defect in the original. The rewrite should NOT reproduce it; record the deviation explicitly so a fidelity diff does not read as a regression.  

#### Q-054 · important · done ⟨found by verification⟩
**Picking an attribute name without opening its operator submenu auto-selects that attribute's first operator; picking an operator marks its parent attribute name.**  
kAttributeItemMain: `if (item->Submenu()->ItemAt(0) != NULL) item->Submenu()->ItemAt(0)->SetMarked(true)` then Invalidate(). kAttributeItem: `item->Menu()->Superitem()->SetMarked(true)` then Invalidate(). FindPanel::Draw then refreshes the row's operator BStringView from the marked submenu item.  
`FindPanel.cpp` → `FindPanel::MessageReceived (cases kAttributeItem, kAttributeItemMain) / FindPanel::Draw`  
**TIKU:** A verb-based UI has no two-level menu, but the rule survives as "an attribute chosen without an operator defaults to that attribute's first legal operator".  

#### Q-055 · polish · open ⟨found by verification⟩
**The "Recent queries" entry in the Query menu is itself clickable and opens the ~/queries folder in Tracker.**  
BuildMenuBar wraps fHistoryMenu in an IconMenuItem carrying `new BMessage(kOpenDir)` with B_DIR_MIMETYPE; FindWindow::MessageReceived's kOpenDir case turns GetQueriesDirectory() into a ref and posts B_REFS_RECEIVED to be_app.  
`FindPanel.cpp` → `FindWindow::BuildMenuBar / FindWindow::MessageReceived (case kOpenDir)`  
**TIKU:** Maps to a `find --list` / "open the query store" shell affordance.  

#### Q-056 · important · open ⟨found by verification⟩
**The query file itself is node-monitored, which is what makes re-running a query close the old results window and what tracks renames of the query.**  
InitDirentIterator calls `TTracker::WatchNode(sourceModel.NodeRef(), B_WATCH_NAME | B_WATCH_STAT | B_WATCH_ATTR, this)` on the query file after building the query list. FindWindow::FindSaveCommon's `entry.Remove()` relies on this notice arriving to quit the window before Find() posts the new refs.  
`QueryPoseView.cpp` → `BQueryPoseView::InitDirentIterator`  
**TIKU:** The delete-to-close idiom is only safe because of this watch; on TikuOS the watch subscription must exist before the record is rewritten, or the 5 s spin in Find() never terminates.  

#### Q-057 · important · partial ⟨found by verification⟩
**Folder filters are stored on the query and reloaded when it is reopened; a filter whose folder has since become a file is dropped, and duplicates are refused.**  
SaveDirectoryFiltersToFile flattens the refs into `_trk/directories` (B_MESSAGE_TYPE) and REMOVES the attribute entirely when no filters remain. LoadDirectoryFiltersFromFile skips refs whose entry exists but is not a directory, then calls AddDirectoryFilter (which returns B_CANCELED on duplicates). QueryEntryListCollection re-reads the same attribute and additionally de-duplicates the resolved path strings.  
`FindPanel.cpp` → `FindPanel::SaveDirectoryFiltersToFile / FindPanel::LoadDirectoryFiltersFromFile`  
**TIKU:** Subtree scoping is the part of the Target menu that ports cleanly; the store-and-revalidate cycle (paths can rot between saves) is the behavior worth keeping.  
**MISSING:** Scopes are stored with the query ("s <path>" lines) and restored on load through tiku_trk_query_add_scope, which normalises and refuses duplicates, but the revalidation clause is missing — nothing stats a stored scope on load, so a scope whose path has become a leaf node (or vanished) is kept rather than dropped.  
*src/tiku_trk_query.c:serialise (741-743) / deserialise (779-781) / tiku_trk_query_add_scope (68)*

#### Q-058 · polish · done ⟨found by verification⟩
**Saving a query auto-marks the volume that each folder filter lives on, so a folder-scoped query still records a target volume.**  
Before embedding volume info, SaveQueryAsAttributes walks fDirectoryFilters and, for each filter's ref->device, marks the matching (currently unmarked) volume item in fVolMenu. Those volumes then get EmbedUniqueVolumeInfo'd into kAttrQueryVolume.  
`FindPanel.cpp` → `FindWindow::SaveQueryAsAttributes`  

#### Q-059 · polish · open ⟨found by verification⟩
**Query result windows are drawn with the read-only background tint, and clipboard cut/copy marks in them are always refreshed.**  
BQueryPoseView::AdoptSystemColors sets B_DOCUMENT_BACKGROUND_COLOR at ReadOnlyTint(); HasSystemColors checks for exactly that tint. BQueryPoseView::MessageReceived intercepts kFSClipboardChanges and always calls UpdatePosesClipboardModeFromClipboard ("poses have always to be updated for the query view") instead of the base class's conditional path.  
`QueryPoseView.cpp` → `BQueryPoseView::AdoptSystemColors / BQueryPoseView::MessageReceived (case kFSClipboardChanges)`  
**TIKU:** The read-only tint is the only visual cue that a results window cannot be written to; worth keeping as a rendering rule for read-only VFS subtrees.  

#### Q-060 · polish · done ⟨found by verification⟩
**Opening the Find panel places the keyboard focus itself when the saved state named no focus view: the by-name/formula field, else the LAST attribute row's text field.**  
FindPanel::AttachedToWindow, after RestoreWindowState: if `!findWindow->CurrentFocus()`, look up "TextControl"; if absent, build the name "TextEntry<CountRows()-1>" and focus that. AddByNameOrFormulaItems and AddAttributeControls also MakeFocus() their new text control as rows are created.  
`FindPanel.cpp` → `FindPanel::AttachedToWindow`  


---

## Icons, thumbnails, pose visuals

Architecture notes: [`spec/ARCH-icons-visuals.md`](spec/ARCH-icons-visuals.md)

#### IV-001 · core · open
**An icon defined by the file itself always wins over any type-based icon.**  
GetNodeIcon opens the node and tries, in order: BAppFileInfo::GetIcon for executables (so app icons come from resources too), else the thumbnail, else BNodeInfo::GetIcon. Success sets source = kNode.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetNodeIcon`  
**TIKU:** Device-namespace nodes have no BFS attributes; decide whether a typed descriptor carries icon bits, or whether device nodes are type-icon-only and the node tier simply never fires.  

#### IV-002 · core · differs
**When no node icon exists the icon is found by walking a fixed fallback chain and the first hit wins.**  
Order (file header comment, implemented in Preload/GetIconFromFileTypes): node icon -> preferred app named by the node -> the MIME type's own icon -> icon supplied by the MIME type's preferred app -> the supertype's MIME icon -> generic 'application/octet-stream' icon -> hard-coded R_FileIcon resource.  
`src/kits/tracker/IconCache.cpp` → `IconCache::Preload / IconCache::GetIconFromFileTypes`  
**TIKU:** MIME lookups become typed-descriptor lookups; the 'preferred app' rungs may collapse if TikuOS has no app-supplied per-type icons, but the supertype and generic rungs must survive.  
**PORT:** tiku_trk_model_icon_name is a fixed first-hit-wins fallback chain (kind specials -> exec bit -> exact type row -> supertype row -> generic "file"); the node-icon and preferred-app rungs are absent as the TIKU note permits, and the two rungs it required (supertype, generic) both survive.  
*src/tiku_trk_model.c:184-250 (icon_name chain) with type_row's exact-then-supertype walk at 111-140; src/tiku_trk_model.h:119-127 (header states only the last two Tracker steps exist)*

#### IV-003 · core · done
**A file whose type resolves to nothing still gets a visible icon, never a blank.**  
GetGenericIcon fetches B_FILE_MIMETYPE from the MIME db; if even that fails GetFallbackIcon loads R_FileIcon out of Tracker's own resources. Preload asserts an icon exists on exit.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetFallbackIcon`  
**TIKU:** The last-resort icon must be linked into the image (no filesystem read), which suits a microcontroller build.  

#### IV-004 · important · partial
**Once an icon has been located, later redraws of the same file skip the search.**  
Model stores IconSource (kNode/kVolume/kMetaMime/kPreferredAppForNode/kTrackerSupplied/...); Preload's else-branch switches straight to that source. A total miss resets it to kUnknownSource so the next draw re-searches.  
`src/kits/tracker/IconCache.cpp` → `IconCache::Preload`  
**MISSING:** The stated clause fails — there is no per-model icon-source memo and icon_name() re-runs the whole fallback walk inside every draw call — but a real memo does exist one stage later (the rasterised bitmap keyed on icon name and size), so redraws skip the render though not the search.  
*src/tiku_trk_app.c:266-269 and 435-440, src/tiku_trk_desktop.c:252-264 (icon_name called per draw); src/tiku_trk_model.h:92-101 (model carries no icon-source field); src/tiku_trk_icons.c:1755-1799 (cache keyed on icon index + size)*

#### IV-005 · important · open
**Tracker avoids touching the disk for a node icon on files that obviously have none.**  
Model::CheckAppIconHint requires either a vector icon attribute or BOTH mini and large icon attributes; failing that, icon source is pre-set to kUnknownNotFromNode and GetNodeIcon is skipped entirely.  
`src/kits/tracker/Model.cpp` → `Model::CheckAppIconHint / Model::FinishSettingUpType`  
**TIKU:** The equivalent hint is a capability bit on the VFS node ('has own icon'), which is cheaper than the two attribute probes.  

#### IV-006 · important · partial
**Volumes get the icon the filesystem itself supplies; network shares get a distinct share icon.**  
GetVolumeIcon: BVolume::IsShared() -> R_ShareIcon resource; else BVolume::GetIcon() sets source = kVolume; else fall back to the B_VOLUME_MIMETYPE icon. A volume with its own node icon still beats all of this (Preload tries GetNodeIcon first).  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetVolumeIcon`  
**TIKU:** Mounted durable stores / remote mounts are the natural analogue; decide whether a mount descriptor can publish its own icon.  
**MISSING:** Volumes do get distinct art ("root" for path "/", otherwise "volume"), but nothing lets a mount/store descriptor publish its own icon and the share clause is missing — the "share" HVIF blob is in the set yet no code path ever selects it.  
*src/tiku_trk_model.c:tiku_trk_model_icon_name (case TIKU_TRK_KIND_VOLUME)*

#### IV-007 · important · differs
**Well-known system folders show special icons instead of the plain folder icon.**  
GetWellKnownIcon matches the node_ref against WellKnowEntryList and maps directory_which to resources: boot disk, BeOS folder, home, fonts, apps, preferences, mail, queries, develop, config, people, downloads. Anything unmatched returns NULL and falls through.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetWellKnownIcon`  
**TIKU:** The list becomes TikuOS's own well-known roots (/sys, /dev, /proc, /data); the mechanism (node identity -> resource id) transfers unchanged.  
**PORT:** Well-known folders genuinely do get their own art instead of the plain folder icon; the port matches on the leaf name (Desktop, Downloads, Mail, People, Fonts, queries, develop, config, apps, preferences, system/boot, plus $HOME) rather than on node identity, and TikuOS namespace dirs get folder_virtual by kind.  
*src/tiku_trk_model.c:216-236 (leaf_is chain, HOME check, default "folder"); leaf_is at 176-181; VIRTUAL_DIR -> "folder_virtual" at 210-212*

#### IV-008 · polish · done
**Well-known folder icons are shared across all folders of the same kind rather than cached per folder.**  
The entry is stored in the shared cache under a synthetic type string "tracker/active_" + wellKnownEntry->name, so e.g. both apps directories share one cached bitmap.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetWellKnownIcon`  

#### IV-009 · polish · partial
**The filesystem root and the printers folder get Tracker-supplied icons.**  
GetRootIcon uses B_ROOT_MIMETYPE, GetPrinterIcon uses B_PRINTER_MIMETYPE, both setting source = kTrackerSupplied; they are checked before the generic path in Preload.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetRootIcon / IconCache::GetPrinterIcon`  
**MISSING:** The root clause holds; a printer kind and its art now exist but no backend classifies a node as one yet.

#### IV-010 · polish · differs
**Asking an application for a type's icon lowercases the type first.**  
GetIconForPreferredApp copies fileTypeSignature into a BString and calls ToLower() before BMimeType::GetIconForType, so mixed-case types still match.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetIconForPreferredApp`  
**PORT:** The port never asks an application for a type's icon — that rung does not exist — so the row's rule is not implemented as stated; what it has is the same case-insensitivity reached another way, by folding both sides during the type-table compare instead of lowercasing the type first.  
*src/tiku_trk_model.c:93-107 (fold_eq) used by type_row at 111-140; no preferred-app icon path anywhere in src/tiku_trk_model.c or src/tiku_trk_icons.c*

#### IV-011 · important · open
**A type that has no icon of its own is remembered as such, so the miss is paid only once.**  
After falling back, an alias entry keyed on (mimeType, preferredApp) is inserted pointing at the entry actually used (generic, or the preferred app's icon), so the next lookup hits on the first FindItem.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetGenericIcon / IconCache::GetIconFromMetaMime`  
**CHECKED:** Nothing remembers a miss: a type with no row falls off the end of the 12-entry table walk to "file" on every single lookup, and the only cache present is the generic bitmap under its icon name, which is the same entry a genuine hit uses — there is no negative or alias entry to point at.  
*src/tiku_trk_model.c:111-140 (type_row returns -1, icon_name falls to "file" at 249); src/tiku_trk_icons.c:1756-1773 (cache is keyed on a found icon; a NULL find is not recorded)*

#### IV-012 · important · differs
**An entry caches exactly one non-mini icon size; switching the window's icon size re-renders every icon.**  
HaveIconBitmap requires fLargeIcon->Bounds().Size() == requested size; a mismatch is treated as a miss and SetIcon deletes and replaces. The mini slot is matched by 'size == sMiniIconSize' only, never by bounds.  
`src/kits/tracker/IconCache.cpp` → `IconCacheEntry::HaveIconBitmap / IconCacheEntry::SetIcon`  
**TIKU:** On a microcontroller a 128x128 RGBA icon is 64 KB; the rewrite needs an explicit size policy and cache budget rather than 'one of each, whatever was asked last'.  
**PORT:** A size-keyed cache with mismatch-is-a-miss really is implemented, just with four slots per icon and FIFO eviction instead of one non-mini slot, so a first switch to a new size re-renders but switching back does not; there is a slot count and no byte budget.  
*src/tiku_trk_icons.c:1745-1753 (TIKU_TRK_ICONS_SLOTS 4, per-icon slot row), 1769-1773 (hit requires row[i].size == size), 1785-1798 (FIFO shift-and-evict)*

#### IV-013 · important · partial
**Icon sizes scale with the UI font, and are always square.**  
IconCache::sMiniIconSize = be_control_look->ComposeIconSize(B_MINI_ICON); BPoseView::IconSize() composes the view-state size the same way and caches it; icon_size_for() asserts width == height and returns width + 1.  
`src/kits/tracker/IconCache.cpp` → `icon_size_for / IconCache::IconCache`  
**MISSING:** Icons are always square (render and paint are size x size), but nothing scales with the UI font: the ladder is a fixed pixel list, the mini size is the constant TIKU_TRK_MINI_ICON 16, and the list row is the constant LIST_ROW_H 20 with only a comment recalling the font metric it was derived from.  
*src/tiku_trk_poseview.c:icon_ladder / src/tiku_trk_app.c:48 LIST_ROW_H*

#### IV-014 · important · done
**Icon view offers six icon sizes and zoom in/out steps through them.**  
kIconMode message with "scale" 0/1 steps the unscaled size through 32, 40, 48, 64, 96, 128 (clamped at both ends); with no size given the last used size is restored, defaulting to 32 if outside 32..128.  
`src/kits/tracker/PoseView.cpp` → `BPoseView::MessageReceived, case kIconMode`  
**PORT:** The missing clause is now built: save_state writes "<mode> <px>" and restore_state parses the number back with sscanf(rec, "%*s %d"), accepting it only in 16..128 so a truncated or pre-size record leaves the constructor's 32 standing. Both size setters mark state_dirty, so the dirty gate does not swallow a size-only change. Verified live: set icon mode + 64 px, save, free the view, reopen (reports mode=0 size=32), restore -> mode=ICON size=64. Old "icon"-only records still restore the mode with no size, which is what tests/test_model.c:114 writes. Six-rung ladder and no-wrap clamping unchanged.

#### IV-015 · important · done
**List mode and mini-icon mode always use the mini icon size regardless of the icon-mode setting.**  
SetIconPoseHeight forces fViewState->SetIconSize(B_MINI_ICON) for kMiniIconMode and kListMode; pose height = icon + font height + 1 in icon mode, max(icon, font+1) in mini mode, and the list row height in list mode.  
`src/kits/tracker/PoseView.cpp` → `BPoseView::SetIconPoseHeight`  
**PORT:** art_size() returns TIKU_TRK_MINI_ICON for anything that is not icon mode, so mini-icon mode and list mode use the mini size whatever icon_size holds, and the list-row path independently clamps its icon to the same 16 px.  
*src/tiku_trk_poseview.c:1753-1763 (art_size: icon_size only in MODE_ICON); src/tiku_trk_poseview.h:39 (TIKU_TRK_MINI_ICON 16); src/tiku_trk_app.c:249-256 and 429-440 (draw clamps, MINI_ICON_MAX 16 at :45); src/tiku_trk_desktop.c:45,241-246*

#### IV-016 · important · done
**Icon positions are preserved across icon-size changes: a pose keeps its relative place when icons grow or shrink.**  
fLocation is stored unscaled against a 32 px reference; BPose::Location multiplies by IconSizeInt()/32 in kIconMode and SetLocation/MoveTo divide by it. SetViewMode also scrolls to keep the view centre stable when the size changes.  
`src/kits/tracker/Pose.cpp` → `BPose::Location / BPose::SetLocation / BPose::MoveTo`  

#### IV-017 · polish · open
**Legacy bitmap icons are centred, not stretched, when the requested icon is bigger.**  
GetIconResource tries B_VECTOR_ICON_TYPE first; falling back to 'ICON'/'MICN' it fills the destination with transparent, then either ConvertFromCMAP8-scales (RGBA32 dest) or ImportBits into the middle at roundf((destWidth - size)/2).  
`src/kits/tracker/Bitmaps.cpp` → `BImageResources::GetIconResource`  
**CHECKED:** There is no legacy raster icon path and no centring-into-a-larger-destination code at all — every icon is one HVIF blob rasterised at exactly the requested edge — so the port does not do this behaviour; the case is designed away rather than achieved by another route.  
*src/tiku_trk_icons.c:1774-1784 (tiku_trk_hvif_parse then tiku_trk_hvif_render(parsed, bmp, size, size); no ICON/MICN or transparent-fill-and-import branch anywhere in the file)*

#### IV-018 · core · differs
**A selected icon is the normal icon dimmed to 66% brightness with its transparency intact.**  
MakeTransformedIcon on B_RGB32/B_RGBA32 multiplies R,G,B by 168>>8 and copies alpha unchanged; on B_CMAP8 it maps through fHighlightTable, built once as screen.IndexForColor(tint_color(colorForIndex, 1.3f)) with B_TRANSPARENT_8_BIT mapped to itself.  
`src/kits/tracker/IconCache.cpp` → `IconCache::MakeTransformedIcon / IconCache::InitHighlightTable`  
**PORT:** The port does derive a distinct selected look from the same normal art per pixel, by a real transform — a 35% wash toward the selection colour applied after the alpha composite and skipped entirely on zero-alpha pixels — rather than Haiku's 168>>8 brightness multiply with alpha copied through; there is no CMAP8 highlight table.  
*src/tiku_trk_iconpaint.c:33-79 (paint(): over() composite then the m-weighted wash, with `((src >> 24) & 0xFFu) == 0u` pixels skipped) and 96-107 (paint_selected passes 0.35f / TIKU_DESK_C_SELECT)*

#### IV-019 · core · partial
**The selected icon is derived from the normal icon on demand and then cached, never fetched separately.**  
AlternateModeForIconConstructing maps any kSelected request back to kNormalIcon; every Get*Icon path ends with 'if (mode != kNormalIcon && HaveIconBitmap(NORMAL_ICON_ONLY,size)) ConstructBitmap(); SetIcon(Adopt(), mode, size)'.  
`src/kits/tracker/IconCache.cpp` → `IconCacheEntry::ConstructBitmap / IconCacheEntry::AlternateModeForIconConstructing`  
**MISSING:** Two clauses hold — the selected look is derived from the normal art and is never fetched as separate artwork — but the stated "and then cached" clause does not: the wash is recomputed pixel-by-pixel on every composite and no second bitmap is ever stored, as the header itself concedes.  
*src/tiku_trk_iconpaint.c:68-77 (wash recomputed inside the per-pixel loop of every paint call); src/tiku_trk_iconpaint.h:41-49 ("Haiku keeps a second bitmap per icon for this; one wash ... without a second cache"); src/tiku_trk_icons.c:1755-1799 caches only the normal render*

#### IV-020 · polish · open
**An icon in an unrecognised colour space renders as a solid black block rather than garbage.**  
MakeTransformedIcon's default case memsets the destination to 0 with the comment that black should make the problem stand out.  
`src/kits/tracker/IconCache.cpp` → `IconCache::MakeTransformedIcon`  

#### IV-021 · core · differs
**A selected pose's label is drawn as reverse video, not as a coloured highlight.**  
BTextWidget::Draw fills the text rect with the low colour and sets high = InvertColorSmart(lowColor); BPose::DrawTextWidget then InvertRect()s the pixel-snapped (ceil left/top, floor right/bottom) text rect.  
`src/kits/tracker/Pose.cpp` → `BPose::DrawTextWidget`  
**PORT:** There is a real alternative selected-label renderer to point at — the label rect is filled with C_SELECT and the ink switched to C_SELTEXT, the same fill-then-contrast pair Haiku reaches via InvertRect — so the pose's label does read as selected, by colour rather than by inversion.  
*src/tiku_trk_app.c:287-293 (icon mode hl fill + C_SELTEXT), :424 and :482 (list row fill + per-column ink); src/tiku_trk_desktop.c:239,270*

#### IV-022 · important · open
**When the window is not active, selection is shown as a translucent grey overlay, or not at all.**  
If !windowActive && ShowSelectionWhenInactive: InvertRect then B_OP_BLEND fill with 128,128,128,255 (192 or 64 grey for cut items). If ShowSelectionWhenInactive is off, drawIconUnselected is true and BPose::DrawIcon requests kNormalIcon even for selected poses.  
`src/kits/tracker/Pose.cpp` → `BPose::Draw / BPose::DrawTextWidget`  

#### IV-023 · important · open
**Files cut to the clipboard are drawn semi-transparent.**  
fClipboardMode == kMoveSelectionTo sets B_OP_ALPHA / B_CONSTANT_ALPHA / B_ALPHA_COMPOSITE with alpha 128, high colour black on a dark background and white on a light one; a selected cut item additionally gets a grey B_OP_BLEND over its inverted label rect.  
`src/kits/tracker/Pose.cpp` → `BPose::Draw / BPose::DrawTextWidget`  
**VERIFIER CORRECTION:** Polarity is inverted. BPose::Draw tests `poseView->HighColor().IsDark()` and BTextWidget::Draw tests `drawView->LowColor().IsLight()` — both pick BLACK when the text colour is dark / the background is light, and WHITE otherwise. So: black on a LIGHT background, white on a DARK one. The same inverted wording appears in the MakeDragBitmap row (`if (HighColor().IsDark()) SetHighColor(0,0,0,alpha)`), which should be corrected the same way.  

#### IV-024 · important · open
**In list mode only the name column is drawn selected; the other columns stay normal.**  
BPose::Draw sets 'if (index > 0) selected = false' inside the column loop. Also, a non-full draw redraws only column 0 (columnsToDraw = 1 unless fullDraw).  
`src/kits/tracker/Pose.cpp` → `BPose::Draw`  
**CHECKED:** The row's discriminating outcome is that columns other than the name stay normal, and the port does the opposite everywhere — the whole row rect is filled with C_SELECT before the column loop and every column's text is drawn in C_SELTEXT — with no column-0-only redraw path either, so there is no alternative mechanism, only the negation.  
*src/tiku_trk_app.c:424-425 (row fill) and :482-483 (every column drawn in C_SELTEXT); src/tiku_trk_desktop.c:239-240,270,287*

#### IV-025 · important · open
**Clicking the transparent corner of an icon does not select the pose.**  
IconHitTest returns true unconditionally inside the icon rect inset by (width+1)/8 on each side; outside that it samples the bitmap - B_RGBA32 requires alpha > 20, B_CMAP8 requires != B_TRANSPARENT_8_BIT, any other colour space returns true.  
`src/kits/tracker/IconCache.cpp` → `IconCacheEntry::IconHitTest`  
**TIKU:** Alpha-precise hit testing needs the decoded bitmap resident; if TikuOS keeps icons compressed, decide between a coarse rect test and a stored alpha mask.  
**CHECKED:** The stated behaviour — a click on a transparent icon corner not selecting — simply does not happen: pose_at accepts any point inside the pose rect (which is the full icon-width box when the label is narrower), and there is no alpha sample or mask anywhere in the tree, so the TIKU note's coarse-rect option was taken and the behaviour dropped rather than re-routed.  
*src/tiku_trk_poseview.c:1899-1918 (pose_at rect test) with :1862-1894 (pose_rect, w = min(cw, art)); no alpha test exists in src/*

#### IV-026 · core · partial
**In icon mode a click counts as hitting the pose if it lands on opaque icon pixels or anywhere in the label rect.**  
BPose::PointInPose (icon-mode overload) tests _IconRect first and defers to IconCache::IconHitTest(where - location); otherwise it tests the first column widget's CalcRect and only if the widget IsVisible().  
`src/kits/tracker/Pose.cpp` → `BPose::PointInPose`  
**MISSING:** The label half of the rule is there — the pose rect is widened (re-centred both ways in icon mode) to the measured label width so the name is clickable where it is drawn — but the "opaque icon pixels" clause is missing: the whole icon+label bounding box hits, including the gap between them.  
*src/tiku_trk_poseview.c:tiku_trk_poseview_pose_rect (PVL-065 branch)*

#### IV-027 · important · done
**In list mode the whole icon rect is clickable - no alpha test.**  
The list-mode PointInPose overload returns true for any point inside _ListIconRect, then scans columns using CalcClickRect and reports which widget was hit.  
`src/kits/tracker/Pose.cpp` → `BPose::PointInPose`  
**PORT:** List-mode hit testing maps y to a row across the full row width including the icon strip with no alpha test, and a separate column scan then reports whether the name widget was hit, which is what arms the rename — both clauses present (the scan's origin is BORDER rather than BORDER+start_offset, so its boundaries sit ~21 px left of the drawn columns, a fidelity slip inside an implemented rule).  
*src/tiku_trk_app.c:758-772 (row_at) and :1326-1341 (column scan) feeding :1364 on_name -> name_clicked*

#### IV-028 · core · open
**Image files show a thumbnail of their contents instead of a type icon.**  
GetNodeIcon calls GetThumbnailFromAttr before GetFileIconFromAttr for non-executables; ShouldGenerateThumbnail gates on the 'Generate image thumbnails' setting (default true) AND the MIME type case-insensitively starting with "image".  
`src/kits/tracker/Thumbnails.cpp` → `GetThumbnailFromAttr / ShouldGenerateThumbnail`  
**TIKU:** Translator-based decode does not exist on TikuOS; decide which formats (if any) generate thumbnails and whether generation is even affordable.  

#### IV-029 · core · open
**A cached thumbnail is reused only while the file has not been modified since the thumbnail was made.**  
GetThumbnailFromAttr compares the Media:Thumbnail:CreationTime attribute (B_TIME_TYPE, int64) against the node's modification time; it must be strictly greater. Otherwise every attribute whose name starts with "Media:Thumbnail" is removed and the thumbnail is regenerated.  
`src/kits/tracker/Thumbnails.cpp` → `GetThumbnailFromAttr`  
**TIKU:** Attribute-carried thumbnails have no equivalent for read-only/device nodes; a side cache keyed by node identity plus mtime is the likely replacement.  

#### IV-030 · important · open
**Thumbnails are stored on the file itself as a 128x128 WebP image.**  
GenerateThumbnailJob::Execute scales to B_XXL_ICON (redefined to 128 in this file), translates to B_WEBP_FORMAT and writes it to the kAttrThumbnail ("Media:Thumbnail") attribute as B_RAW_TYPE, plus the creation time. Reading back at exactly 128 imports without rescaling; any other size is down-scaled.  
`src/kits/tracker/Thumbnails.cpp` → `GenerateThumbnailJob::Execute`  

#### IV-031 · core · open
**Generating a thumbnail never blocks the window; the pose shows a type icon and switches to the thumbnail when it is ready.**  
GenerateThumbnail returns B_BUSY after queueing; GetNodeIcon treats B_BUSY specially (it does not fall through to GetFileIconFromAttr), so the lookup proceeds to type-based icons for now. Workers run as spawned threads over a JobQueue.  
`src/kits/tracker/IconCache.cpp` → `IconCache::GetNodeIcon`  

#### IV-032 · important · open
**Big images are thumbnailed on a separate queue from small ones so one huge file cannot stall the rest.**  
GenerateThumbnail picks sThumbnailWorkers[LARGER_FILES_WORKER] when st_size >= 128 * 1024, else SMALLER_FILES_WORKER; each queue's worker thread is spawned lazily at B_NORMAL_PRIORITY.  
`src/kits/tracker/Thumbnails.cpp` → `GenerateThumbnail`  

#### IV-033 · important · open
**The same file is never thumbnailed twice concurrently.**  
GenerateThumbnail scans the sActiveJobs list (guarded by sActiveJobsLock) for a job with the same node_ref and returns B_BUSY if found; jobs add/remove themselves in their ctor/dtor.  
`src/kits/tracker/Thumbnails.cpp` → `GenerateThumbnail`  

#### IV-034 · important · open
**A thumbnail preserves the image's aspect ratio, centred inside the square icon with transparent margins.**  
ThumbBounds computes a wide or tall sub-rect of the icon bounds and offsets it by half the leftover; ScaleBitmap fills with B_TRANSPARENT_COLOR then draws with B_OP_ALPHA / B_ALPHA_COMPOSITE and B_FILTER_BITMAP_BILINEAR.  
`src/kits/tracker/Thumbnails.cpp` → `ThumbBounds / ScaleBitmap`  

#### IV-035 · polish · open
**Thumbnailing an image also records its pixel dimensions on the file, and gives up quietly on volumes that reject attributes.**  
Execute writes Media:Width (B_INT32_TYPE) first; only if exactly sizeof(int32) bytes were written does it write Media:Height and the thumbnail itself.  
`src/kits/tracker/Thumbnails.cpp` → `GenerateThumbnailJob::Execute`  

#### IV-036 · important · open
**When the thumbnail cannot be persisted the icon still refreshes on screen.**  
If the attribute write failed, Execute posts kUpdateThumbnail with the node_ref to be_app; TTracker walks every open container window, finds the pose by node_ref and calls BPoseView::UpdateIcon, stopping at the first match. When the write succeeded, node monitoring does it instead.  
`src/kits/tracker/Tracker.cpp` → `TTracker::MessageReceived, case kUpdateThumbnail`  
**TIKU:** The VFS watch notification (TIKU_EVENT_VFS) replaces node monitoring here; the explicit message path is still needed for nodes that cannot be watched.  

#### IV-037 · important · open
**A file that already carries a thumbnail is known up-front to have its own icon.**  
Model::FinishSettingUpType sets fIconFrom = kNode when GetAttrInfo(kAttrThumbnail) succeeds or ShouldGenerateThumbnail(type) is true, so the node path is taken on the very first draw.  
`src/kits/tracker/Model.cpp` → `Model::FinishSettingUpType`  

#### IV-038 · core · open
**Editing a file's icon attribute updates its icon everywhere immediately.**  
Model::IconAttrChanged returns true for kAttrIcon, kAttrLargeIcon, kAttrMiniIcon and kAttrThumbnail; BPose::UpdateWidgetAndModel then calls UpdateIcon, which calls IconCache::IconChanged(model) - dropping the node-cache entry (for kNode or kVolume sources) and calling Model::ResetIconFrom - and invalidates just the icon rect.  
`src/kits/tracker/Pose.cpp` → `BPose::UpdateIcon / Model::IconAttrChanged`  
**TIKU:** Maps onto VFS watch events; note the rewrite must distinguish 'icon data changed' from 'type changed', which drive different cache evictions.  

#### IV-039 · important · open
**Changing a MIME type's icon in the FileTypes preference repaints every affected file, batched rather than one at a time.**  
NoticeMetaMimeChanged reads be:which (B_ICON_CHANGED / B_ICON_FOR_TYPE_CHANGED / B_APP_HINT_CHANGED / B_PREFERRED_APP_CHANGED) and defers through a MetaMimeChangedAccumulator with a 200 ms delay and 5 s cap; MetaMimeChanged then evicts the shared entry, sleeps 10 ms, and re-icons poses whose IconFrom is not kNode/kUnknown* and whose MimeType matches case-insensitively.  
`src/kits/tracker/PoseView.cpp` → `BPoseView::NoticeMetaMimeChanged / BPoseView::MetaMimeChanged / OneMetaMimeChanged`  

#### IV-040 · polish · differs
**Icons that may still be drawing are never freed underneath the app server.**  
IconChanged retires an entry's four bitmaps into SharedIconCache::fRetiredBitmaps instead of deleting them; only when that list passes 10*1024 entries are the oldest 512 deleted.  
`src/kits/tracker/IconCache.cpp` → `IconCacheEntry::RetireIcons`  
**TIKU:** If TikuOS drawing is synchronous the retirement list can be dropped - but only if async blits are genuinely impossible.  
**PORT:** The invariant the row states does hold: compositing is fully synchronous inside paint(), which fetches the bitmap and finishes blending into the app's own surface within one call and nothing else retains the pointer, so the evicting free() in icons_bitmap can never land under a draw in flight — the condition the TIKU note put on dropping the retirement list.  
*src/tiku_trk_iconpaint.c:33-81 (bitmap fetched at :44, consumed in the same call); src/tiku_trk_icons.c:1786-1795 (evict + free); only caller of icons_bitmap outside tests*

#### IV-041 · polish · open
**Deleting a file drops its cached icon, unless the entry was preloaded as permanent.**  
IconCache::Deleting(Model*) only touches the node cache and only when IconFrom() == kNode; NodeIconCache::Deleting refuses to remove entries flagged Permanent(). NodePreloader preloads /boot/system/apps and /boot/system/preferences icons with permanent = true and calls IconCache::Removing (which ignores the flag) when such an entry disappears.  
`src/kits/tracker/IconCache.cpp` → `IconCache::Deleting / NodeIconCache::Deleting / NodePreloader::PreloadOne`  

#### IV-042 · core · partial
**A symlink shows its target's icon; a broken symlink shows a broken-link icon.**  
BPose::ResolvedModel returns LinkTo() when the link resolves and the symlink Model itself when it does not; the unresolved Model's MIME type is B_LINK_MIMETYPE ("application/x-vnd.Be-symlink"), which Tracker installs with R_BrokenLinkIcon. Attributes and label always come from the symlink, only the icon from the target.  
`src/kits/tracker/Pose.h` → `BPose::ResolvedModel`  
**MISSING:** Broken-ness is signalled (the backend sets facts.link_broken, the Kind column says "Broken link", and desktop.c paints the row's art dimmed), but the "a symlink shows its target's icon" clause is entirely missing — every SYMLINK model returns the "symlink" art regardless of what it resolves to.  
*src/tiku_trk_model.c:tiku_trk_model_icon_name (case TIKU_TRK_KIND_SYMLINK)*

#### IV-043 · important · done
**A symlink's name is underlined in the name column.**  
BTextWidget::Draw draws a B_MIXED_COLORS line under the text when fSymLink and the widget is the first column's; the line sits 2 px below the baseline (offset by -(descender-2) when the descender exceeds 2) and is inset to cover only the text width, drawn at alpha 180 unless the item is cut.  
`src/kits/tracker/TextWidget.cpp` → `BTextWidget::Draw`  
**TIKU:** The only badge-like overlay in this Tracker; TikuOS capability bits (read-only, device, remote) are a natural place to add more, and the rewrite should generalise this to a badge hook.  
**PORT:** Same stroke as MA-075.

#### IV-044 · important · differs
**A link whose target disappears or reappears repaints with the right icon without a refresh.**  
UpdateWasBrokenSymlink stops watching the old target, clears LinkTo and re-icons when the entry no longer resolves; otherwise it calls CreateSymlinkPoseTarget, re-icons and closes the target node. UpdateBrokenSymLink asserts the model is an unresolved symlink before re-iconing.  
`src/kits/tracker/Pose.cpp` → `BPose::UpdateWasBrokenSymlink / BPose::UpdateBrokenSymLink`  
**PORT:** A link that loses or regains its target re-draws from the change notice, which carries link_broken; the target itself is not watched, so the repaint waits for the next notice rather than arriving with it.

#### IV-045 · important · open
**Writable volume poses carry a free-space bar down the right edge of their icon, red when nearly full.**  
BPose ctor registers a periodic update for non-root volumes that are not "cdda" and not read-only. DrawBar: bar width = 7/32 * (iconSize+1), floored at 4 (yOffset 0, else 2), a 32,32,32,92 shadow line, a 76,76,76,192 frame, free part in FreeSpaceColor, used part in UsedSpaceColor - or WarningSpaceColor when free space is under 5% (CalcFreeSpace encodes that as -2 - percent). Capacity 0 reports 100%.  
`src/kits/tracker/Pose.cpp` → `BPose::DrawBar / CalcFreeSpace / BPose::UpdateVolumeSpaceBar`  
**TIKU:** Directly reusable for durable-store fill level; the -2-percent warning encoding is a wart worth replacing with an explicit flag.  

#### IV-046 · important · open
**The Trash icon changes between empty and full as items are moved in and out.**  
BTrashWatcher watches every writable persistent volume's trash dir and, on transitions, writes R_TrashIcon or R_TrashFullIcon into the boot volume's trash directory as kAttrIcon (vector) or kAttrLargeIcon/kAttrMiniIcon; the ordinary attribute-changed path then re-icons the pose.  
`src/kits/tracker/TrashWatcher.cpp` → `BTrashWatcher::UpdateTrashIcon`  
**TIKU:** State-dependent icons via a write-back to the node's own icon attribute is a hack; with typed VFS descriptors the state should be a descriptor field the icon resolver reads.  

#### IV-047 · core · differs
**Pose labels are a single line, ellipsised in the middle when too long.**  
NameAttributeText::FitValue truncates with B_TRUNCATE_MIDDLE; the width budget is the column width in list mode and StringWidth("M") * 30 in icon and mini-icon modes. TruncStringBase only truncates when the string actually exceeds the width, using BFont::GetTruncatedStrings. There is no wrapping anywhere - the label rect is exactly one ActualFontHeight tall.  
`src/kits/tracker/WidgetAttributeText.cpp` → `NameAttributeText::FitValue / TruncStringBase`  
**PORT:** The single-line, truncate-only-when-over-width, middle-ellipsis behaviour is there and list mode uses the column width as the row states, but the icon/mini budget is a cell-relative size + size/2 (48 px at a 32 px cell) instead of StringWidth("M")*30, which visibly moves the threshold at which a label ellipsises, so this is another route rather than the stated rule implemented.  
*src/tiku_trk_attr.c:391-408 (untouched when it measures within width) and :423-438 (middle); src/tiku_trk_app.c:279-284 (budget = size + size/2) vs :470-475 (list = column width)*

#### IV-048 · important · done
**In icon mode the label is centred under the icon and may be wider than it; in mini-icon mode it sits to the right of the icon.**  
CalcRectCommon: icon mode left = poseLoc.x + roundf((iconSize - viewWidth)/2), bottom = poseLoc.y + IconPoseHeight(); mini mode left = poseLoc.x + iconSize + kMiniIconSeparator (3). BPose::CalcRect widens the pose rect symmetrically in icon mode when the text is wider than the icon, and by kMiniIconSeparator + text width in mini mode.  
`src/kits/tracker/TextWidget.cpp` → `BTextWidget::CalcRectCommon / BPose::CalcRect`  
**PORT:** The mini-icon clause is now built and agrees with the hit rect. draw_pose_icon takes a `mini` branch that puts the label to the RIGHT of the art (lx = box.x + art + 4) and vertically centres it on the icon instead of the old centred-underneath placement; pose_rect's MINI branch grows the rect the same way (*w = art + 4 + tw), so drawn text and selectable rectangle now use the identical separator. The icon-mode clause is untouched and still holds: label centred at x + (size - tw)/2 under the icon, and the rect re-centres and widens both ways when tw > art.

#### IV-049 · polish · open
**Labels over a desktop background picture get a contrast halo so they stay readable.**  
When WidgetTextOutline() is set (turned on by BackgroundImage) and the pose is not selected and the draw is direct, BTextWidget::Draw renders the string twice with B_FONT_FALSE_BOLD_WIDTH 2.0 then 1.0 using B_SHINE_COLOR at alpha 30/65 for dark text, or once at false-bold 1.0 in black alpha 30 plus a 1,1-offset black alpha 200 pass for light text, before drawing the real string.  
`src/kits/tracker/TextWidget.cpp` → `BTextWidget::Draw`  

#### IV-050 · polish · differs
**A very short label still has a comfortably clickable area.**  
CalcClickRect widens a rect narrower than kWidthMargin (20) to 20 px, or to the column width if that column is itself narrower than 20.  
`src/kits/tracker/TextWidget.cpp` → `BTextWidget::CalcClickRect`  
**PORT:** There is a pointable floor doing the row's job: in icon mode pose_rect never returns a width below the icon art (min(cw, art) with cw >= art), so a two-character label still owns a ~32 px box, and in list mode row_at takes the whole row width while the name widget's click span is the entire name column — both at or above the 20 px comfort floor CalcClickRect enforces.  
*src/tiku_trk_poseview.c:1886-1894 (pose_rect width floor) ; src/tiku_trk_app.c:758-772 (row_at) and :1329-1341 (column-wide name span)*

#### IV-051 · important · open
**Poses over a textured background are never drawn onto a dirty background; the region is invalidated instead.**  
BPose::Draw returns early with poseView->Invalidate(rect) when !fBackgroundClean && !fullDraw, flipping fBackgroundClean so the follow-up full draw proceeds. Deselecting over a texture draws the icon directly with B_OP_OVER and merely invalidates the label rect - unless the pose has a space bar, in which case the icon is invalidated too.  
`src/kits/tracker/Pose.cpp` → `BPose::Draw / BPose::DeselectWithoutErasingBackground`  

#### IV-052 · polish · open
**Volume poses are invalidated rather than drawn in place while the space bar setting is on.**  
BPoseView::DrawPose calls Invalidate(rect) instead of pose->Draw() when TrackerSettings().ShowVolumeSpaceBar() and the pose is a volume, so the bar and icon repaint together.  
`src/kits/tracker/PoseView.cpp` → `BPoseView::DrawPose`  

#### IV-053 · important · open
**Dragging a selection shows a translucent picture of the dragged poses that fades out at the edges when the selection is large.**  
MakeDragBitmap builds an RGBA32 bitmap clipped to a ComposeIconSize(256)-sized box around the cursor, re-draws each selected pose into it via BPose::Draw with a foreign view and offset, at constant alpha 192 (164 when fading), black on dark backgrounds and white on light, then applies FadeRGBA32Horizontal/Vertical over a ComposeIconSize(32)-wide band on each clipped side.  
`src/kits/tracker/PoseView.cpp` → `BPoseView::MakeDragBitmap`  
**CHECKED:** Nothing builds a drag picture: an icon-mode drag moves the real poses live via move_pose on MotionNotify, and there is no bitmap, no constant-alpha pose re-draw into a foreign view, and no edge fade anywhere in the tree.  
*none found*

#### IV-054 · important · open
**Menu items that name a file show that file's icon, dimmed when the item is disabled.**  
ModelMenuItem::DrawIcon draws at BSize(ListIconSize()-1, ListIconSize()-1) through IconCache::Draw when enabled, and through SyncDraw with DimmedIconBlitter when not - which blits B_OP_ALPHA/B_CONSTANT_ALPHA at high colour (0,0,0,128) for RGBA32 icons and B_OP_BLEND otherwise. Label is offset by iconSize + iconSize/4 and the item height grows to the icon height.  
`src/kits/tracker/IconMenuItem.cpp` → `ModelMenuItem::DrawIcon / DimmedIconBlitter / ModelMenuItem::GetContentSize`  

#### IV-055 · polish · open
**Marking an icon menu item inside a menu field copies its icon up to the field's visible item.**  
IconMenuItem::SetMarked walks up Supermenu() to the topmost menu, checks its parent is a BMenuField, and calls SetIcon(fDeviceIcon) on the first item, then invalidates the menu. SetIcon re-creates the bitmap at ComposeIconSize(fWhich) and ImportBits into it.  
`src/kits/tracker/IconMenuItem.cpp` → `IconMenuItem::SetMarked / IconMenuItem::SetIcon`  

#### IV-056 · polish · open
**An icon menu item built from a MIME type falls back to the supertype's icon.**  
The (label, message, iconType, which) and (submenu, message, iconType, which) constructors try BMimeType::GetIcon, then GetSupertype()->GetIcon, and drop the bitmap entirely if both fail so the item draws without an icon.  
`src/kits/tracker/IconMenuItem.cpp` → `IconMenuItem::IconMenuItem`  

#### IV-057 · polish · open
**The window's folder icon is itself an icon-cache draw and is drag-startable only from opaque pixels.**  
DraggableContainerIcon::Draw blits the target model's icon centred in the view with B_OP_ALPHA; MouseDown only arms a drag when IconCache::IconHitTest passes, and the drag bitmap is icon plus middle-truncated name at text alpha 192. The icon repaints when the folder's own attributes change (AttributeChanged calls IconCache::IconChanged and invalidates the view).  
`src/kits/tracker/DraggableContainerIcon.cpp` → `DraggableContainerIcon::Draw / DraggableContainerIcon::MouseDown`  
**UNCONFIRMED:** DraggableContainerIcon.cpp has no AttributeChanged at all (only Draw/MouseDown/MouseMoved/MessageReceived). The repaint is driven from BPoseView::AttributeChanged in PoseView.cpp, which finds the "DraggableContainerIcon" view by name; the sub-claim cannot be confirmed in the cited file.  

#### IV-058 · polish · open
**The Get Info window's icon highlights as a drop target by switching to the selected icon variant.**  
HeaderView::MouseMoved redraws with kSelectedIcon while the drag is over fIconRect and kNormalIcon when it leaves, tracking the transition in fIsDropTarget; the icon is also alpha hit-tested for drag start and double-click.  
`src/kits/tracker/infowindow/HeaderView.cpp` → `HeaderView::MouseMoved / HeaderView::MouseDown`  

#### IV-059 · important · differs ⟨found by verification⟩
**The legacy bitmap-icon fallback exists at only two sizes: any requested size other than mini is loaded as a large icon, and the resource must be exactly size*size bytes or the load fails outright.**  
BImageResources::GetIconResource collapses the request with `size = (size == B_MINI_ICON ? B_MINI_ICON : B_LARGE_ICON)` before LoadResource('MICN'/'ICON'), then returns B_ERROR unless `length == size*size`. Only after that does the transparent-fill + centre/ConvertFromCMAP8 path run.  
`src/kits/tracker/Bitmaps.cpp` → `BImageResources::GetIconResource`  
**TIKU:** Sets the floor for the icon store: a vector icon or two fixed raster sizes. A TikuOS icon resource format should decide this explicitly rather than inheriting the 2-size accident.  
**PORT:** The icon store is vector-only and icons_bitmap renders any requested edge from 1 to 1024 px straight from the HVIF blob into a per-size cache slot, which is the vector option the row's TIKU note offered in place of inheriting the MICN/ICON two-size collapse and the length == size*size gate.  
*src/tiku_trk_icons.c:1755-1798 (size bound 1..1024, hvif_parse + hvif_render at requested size); icon table entries are all HVIF blobs (:24 onwards)*

#### IV-060 · important · open ⟨found by verification⟩
**A finished thumbnail is inserted directly into the node icon cache at the requested size before any attribute is written, so the pose redraws from memory even on volumes that reject attributes.**  
GenerateThumbnailJob::Execute scales into a BBitmap, locks IconCache::sIconCache->fNodeCache, FindItem/AddItem by node_ref and calls entry->SetIcon(cacheThumb, kNormalIcon, fRequestedSize) — all before the Media:Width / Media:Height / Media:Thumbnail writes.  
`src/kits/tracker/Thumbnails.cpp` → `GenerateThumbnailJob::Execute`  
**TIKU:** The cache insert, not the attribute, is what makes the icon appear; the attribute is only the persistence tier. Worth keeping that split explicit on a target where NVM writes are expensive.  

#### IV-061 · polish · differs ⟨found by verification⟩
**Only the selected variant can ever be auto-constructed; no other icon mode is derivable.**  
IconCacheEntry::CanConstructBitmap returns true only for kSelected and false otherwise, and both ConstructBitmap overloads ASSERT(requestedMode == kSelected && constructFromMode == kNormalIcon). Any future badge/overlay mode would have to be fetched, not derived.  
`src/kits/tracker/IconCache.cpp` → `IconCacheEntry::CanConstructBitmap / IconCacheEntry::ConstructBitmap`  
**TIKU:** Directly relevant if TikuOS wants capability badges (read-only, device, remote) — the Tracker cache has no room for them without widening this.  


---

## List columns, count view, settings

Architecture notes: [`spec/ARCH-titleview-settings.md`](spec/ARCH-titleview-settings.md)  
**PORT:** Derivation is generalised rather than absent: one paint() mixes the normal art toward an arbitrary wash, and both the selected variant (0.35 toward C_SELECT) and a dim variant (0.55 toward C_DOC, used for broken links) are constructed from it, so the row's positive core is implemented and its "only selected is derivable" restriction is what the port declines.  
*src/tiku_trk_iconpaint.c:33-105 (shared paint; paint_dim :90, paint_selected :97); src/tiku_trk_desktop.c:249-256 (dim used for link_broken)*

#### TS-001 · core · done
**Clicking a column header makes that column the primary sort and forces ascending order.**  
Click = mouse released within one double-click-speed of press with no movement; sets primary sort hash, SetReverseSort(false), then SortPoses() + Invalidate(). If the new primary equals the current secondary, secondary is cleared to 0.  
`TitleView.cpp` → `ColumnDragState::Clicked`  
**TIKU:** Sort key is an attribute-name+type hash; with typed VFS descriptors the key should be the descriptor id, and non-comparable descriptor types (e.g. write-only actuators) must be refused as sort keys.  
**PORT:** The non-primary, non-shift branch sets sort_field and forces sort_reverse=0, the secondary is collapsed to -1 when it equals the new primary, resort()+filter rebuild follow, and the caller only reaches it when the release was within TIKU_TRK_CLICK_SPEED_US of the press with no movement.  
*src/tiku_trk_poseview.c:1447-1456 (else-branch, sort2 collapse, resort); src/tiku_trk_titleview.c:380-384 with the deadline set at :195*

#### TS-002 · core · done
**Clicking the header of the column that is already the primary sort reverses the sort direction.**  
Only when Shift is not held, or when there is no secondary sort. Reverse is implemented by swapping the two poses before comparing, not by negating the comparison, so the ordering stays stable.  
`TitleView.cpp` → `ColumnDragState::Clicked`  
**PORT:** Clicking the already-primary header toggles sort_reverse and is taken exactly when Shift is absent or there is no secondary (Shift+secondary swaps the pair instead); the port negates the comparator rather than swapping the pair, which is order-identical here because field_cmp is antisymmetric and a natural-name tie-break makes the order total, so the user sees the described reversal.  
*src/tiku_trk_poseview.c:1430-1442 (reverse toggle under the shift/secondary condition); src/tiku_trk_poseview.c:372 (negation in pose_cmp)*

#### TS-003 · important · done
**Shift+clicking the primary sort column swaps the primary and secondary sort columns.**  
Only when a secondary sort exists; otherwise Shift+click on primary falls through to reversing the sort.  
`TitleView.cpp` → `ColumnDragState::Clicked`  

#### TS-004 · important · done
**Shift+clicking a non-primary column sets it as the secondary sort; Shift+clicking the current secondary column clears the secondary sort.**  
Secondary sort is only honoured if a column for that hash still exists; BPoseView::SetSecondarySort resets both secondary hash and type to 0 when no matching column is found.  
`TitleView.cpp` → `ColumnDragState::Clicked`  

#### TS-005 · important · done
**Poses that tie on the primary sort attribute are ordered by the secondary sort attribute.**  
PoseCompareAddWidget compares the primary widget; only if the result is 0 and this is the first iteration does it retry with SecondarySort(); at most two levels, never three. Reverse sort applies to both levels because it swaps the operands up front.  
`PoseView.cpp` → `PoseCompareAddWidget`  
**PORT:** Two levels, never three, and the reverse applies to both because the result is negated once at the end rather than the operands swapped up front -- the same ordering by a different route.

#### TS-006 · important · done
**The primary and secondary sort columns are marked in the header with a small triangle whose direction shows the sort direction.**  
Triangle drawn 6px left of the title text, centred vertically; apex down for ascending, apex up when ReverseSort(). The primary marker is drawn with panel-background tint 1.6, the secondary dimmer at tint 1.3, so the two are distinguishable.  
`TitleView.cpp` → `BColumnTitle::Draw`  
**PORT:** Six pixels left of where the title text starts, so it follows the alignment; apex down ascending, up reversed; primary at tint 1.6 and the tie-break at 1.3.  Three stroked rows rather than a subpixel triangle, which is what this drawing floor has.

#### TS-007 · important · done
**Dragging a column header sideways over a neighbouring header reorders the columns live.**  
A swap happens only when the pointer is over a different title AND either that title starts at or after the dragged title's right edge (moving right), or the pointer has passed the neighbour's left edge plus the dragged column's width (moving left) — the latter prevents an immediate snap-back oscillation. Offsets of all columns are recomputed from StartOffset() after the move.  
`TitleView.cpp` → `ColumnDragState::Moved`  

#### TS-008 · important · differs
**Dragging a column header out of the title bar (vertically) removes that column, which then follows the mouse as a dotted outline rectangle.**  
Hysteresis: removal only fires once the pointer leaves the title bounds inset by kRemoveTitleMargin (10px) vertically. The column is archived to a BMallocIO first so it can be restored; if RemoveColumn refuses (last column) nothing is removed.  
`TitleView.cpp` → `ColumnDragState::Moved`  
**PORT:** The ten-pixel vertical hysteresis, the removal and the release-stands rule are exact.  A torn-out column simply leaves the strip; there is no dotted rectangle following the pointer around the screen, because nothing here draws outside its own window.

#### TS-009 · important · done
**Dragging a removed column back into the title bar re-inserts it, after whichever header the mouse is over.**  
The column is re-instantiated from the archived stream (BColumn::InstantiateFromStream), added with AddColumn(column, after), the rect tracking ends and the drag continues on the restored title.  
`TitleView.cpp` → `ColumnDragState::Moved`  

#### TS-010 · core · done
**Dragging the right edge of a column header resizes that column live, with the rest of the row re-flowing as you drag.**  
The grab zone is kEdgeSize (6px) either side of the column's right border. New width = mouse.x + initial grab offset - column offset, clamped to kMinColumnWidth (20). Resizing scrolls the untouched right-hand part with CopyBits and only repaints the resized column, then does a synchronous update of the tail.  
`TitleView.cpp` → `ColumnResizeState::Moved`  

#### TS-011 · polish · done
**While resizing, a vertical guide line is drawn down the pose view at the column's new right edge and erased when the mouse is released.**  
Line is drawn in the low colour darkened by B_DARKEN_1_TINT; the erase path re-tints for read-only target volumes (ReadOnlyTint) so the erased strip matches the read-only background. Never used on the Desktop view (asserted).  
`TitleView.cpp` → `ColumnResizeState::DrawLine`  

#### TS-012 · important · done
**Double-clicking a column's resize edge sizes that column to fit its widest visible content.**  
Requires a previous click on the same edge within get_click_speed(); middle (tertiary) mouse button does it on a single click with no timing requirement. Width is max over the current pose list of widget->PreferredWidth(), floored at kMinColumnWidth (20); if nothing would change, ResizeColumnToWidest returns false and no redraw happens.  
`TitleView.cpp` → `BTitleView::MouseDown`  
**PORT:** A fit consumes the press even when the width does not move, so a second double-click cannot arm a resize on the column just fitted; and any previous divider click inside the window counts, as in the source, not only one on the same divider.

#### TS-013 · polish · done
**The pointer becomes an east-west resize cursor when hovering a column's resize edge, and only while the window is active.**  
Reverts to the system default cursor elsewhere and on B_EXITED_VIEW.  
`TitleView.cpp` → `BTitleView::MouseMoved`  

#### TS-014 · important · open
**Right-clicking the column title bar opens the Attributes menu as a pop-up, including the per-MIME-type submenus.**  
Built fresh each time via NewAttributesMenu + AddMimeTypesToMenu + MarkAttributesMenu, targeted at the pose view; the popup is not opened when the window was inactive (the click just activates).  
`TitleView.cpp` → `BTitleView::MouseDown`  
**TIKU:** MIME-type-derived attribute lists become descriptor enumeration over the device namespace; capability bits decide which descriptors are offerable as columns.  

#### TS-015 · polish · done
**A click in the title bar of an inactive window only activates the window; it does not sort or resize.**  
Also: any in-progress inline rename is committed (CommitActivePose) before title tracking starts, and the mouse is captured with B_NO_POINTER_HISTORY | B_LOCK_WINDOW_FOCUS for the duration of the drag.  
`TitleView.cpp` → `BTitleView::MouseDown`  

#### TS-016 · important · done
**A press-and-release counts as a click (sort) only if it is short and stationary; anything longer or moved is a drag.**  
Click iff system_time() <= press time + double-click-speed and the pointer never moved. Within the click window a 1px margin around the press point is ignored so a shaky click still sorts.  
`TitleView.cpp` → `ColumnTrackState::MouseUp`  

#### TS-017 · polish · done
**The pressed column header is drawn depressed while the mouse is down, and the dragged header is drawn with a highlight outline at the drag offset.**  
Pressed background is panel colour darkened B_DARKEN_1_TINT with top/bottom borders; the drag outline is a B_CONTROL_HIGHLIGHT_COLOR rect blitted through the same offscreen pass as the titles.  
`TitleView.cpp` → `ColumnDragState::DrawOutline`  
**PORT:** Pressed is a darkened title rather than an inset bevel, which is what Haiku's control look actually draws.  The dragged title is painted last so its neighbour cannot erase the edge it is sliding over; Tracker gets the same result by blitting the outline after the strip.

#### TS-018 · polish · done
**Column titles are truncated with an ellipsis when too narrow, and honour the column's left/center/right alignment.**  
Truncation is B_TRUNCATE_END against (width - kTitleColumnExtraMargin = 16). Left-aligned titles start at left+1+11, right-aligned end 5px from the right edge, centered are centred on the measured (post-truncation) width.  
`TitleView.cpp` → `BColumnTitle::Draw`  

#### TS-019 · polish · partial
**The title bar uses a smaller font than the list body and has a fixed height.**  
Font = plain font size * 0.75 floored, minimum 8pt; height = max(13, ceil(fontSize * 1.4)). MinSize width is 16, MaxSize width unlimited, height fixed at that value in both.  
`TitleView.cpp` → `BTitleView::BTitleView`  
**MISSING:** The fixed height is there (TITLE_H 18, used for the strip rect, the tracker's height and the hit test) but the smaller font clause is missing: draw_title renders titles in tiku_desk_font_plain(), the same font the rows use, and the height is a constant rather than derived from a 0.75-scaled font.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:40 (TITLE_H) and :141 (draw_title font)*

#### TS-020 · important · done
**The column titles scroll horizontally in lock-step with the list body.**  
The horizontal scroll bar holds a pointer to the title view and calls ScrollTo(value) on it before its own ValueChanged; the title view is never scrolled vertically.  
`PoseView.cpp` → `TScrollBar::ValueChanged`  
**PORT:** Titles and rows read ONE horizontal offset, so they cannot drift; Tracker keeps two views in step by scrolling the title first and the body second inside one message.  A row's cell clip is intersected with the list, not substituted for it, so a scrolled cell cannot paint over the border.

#### TS-021 · core · done
**The column title bar exists only in list mode; switching to an icon mode hides it and also hides the Attributes menu.**  
Switching back shows both. On a directory switch that restores a different view mode, the same show/hide happens; staying in list mode just invalidates the title view. Leaving list mode also clears any active type-ahead filtering.  
`PoseView.cpp` → `BPoseView::SetViewMode`  
**PORT:** list_rect adds TITLE_H only in list mode and the icon path's sunken fill starts at TAB_H+BORDER so it repaints over the strip, title_hit returns 0 for every non-list mode, the Attributes menu is inserted at index 2 / removed on the live view_mode at each rebuild, and leaving list mode clears type-ahead and stops filtering.  
*src/tiku_trk_app.c:96-101 and :780-782 (title_hit); src/tiku_trk_app.c:305-320 (icon fill covers the band); src/tiku_trk_cwmenu.c:280-289; src/tiku_trk_poseview.c:1599-1603*

#### TS-022 · core · done
**Ticking an attribute in the Attributes menu adds it as a column; unticking removes it.**  
Menu items carry attr_name, attr_type, attr_hash, attr_width, attr_align, attr_editable, attr_statfield and optional attr_display_as; the handler removes the column if one already exists for that hash, otherwise builds a BColumn from the message fields and appends it. Menu marks are recomputed from ColumnFor(hash) after every add/remove.  
`PoseView.cpp` → `BPoseView::HandleAttrMenuItemSelected`  
**TIKU:** Attribute descriptors come from the VFS node's typed descriptor table; the editable flag maps to a write capability bit rather than a BFS attribute flag.  
**PORT:** Each Attributes item calls tiku_trk_poseview_toggle_column, which removes the matching column when present and otherwise appends one built from column_def (title/width/alignment) and reflows, and every rebuild recomputes the marks from tiku_trk_poseview_has_column rather than from remembered menu state; the fixed field table in place of message-carried attr descriptors is the namespace adaptation, not a missing clause.  
*src/tiku_trk_desktop.c:1019-1030 (items → toggle_column) and :166-177 (columns mask from has_column); src/tiku_trk_poseview.c:3110-3148 (remove/append); src/tiku_trk_cwmenu.c:186-199 (marks)*

#### TS-023 · important · partial
**Removing the last remaining column is refused with the alert "You must have at least one attribute showing."**  
Only when runAlert is true (menu path). Internal callers such as Paste layout pass false and just get a false return, which is how paste keeps one old column alive until the new set is installed.  
`PoseView.cpp` → `BPoseView::RemoveColumn`  
**MISSING:** The port refuses the removal that would empty the view — but by a different rule (the Name column is hard-refused in toggle_column and the menu item is marked+disabled) and silently: there is no last-column check, no runAlert parameter and no alert string anywhere in the tree, so the user never sees the message the row states.  
*src/tiku_trk_poseview.c:3108-3109 (Name refusal); src/tiku_trk_cwmenu.c:186-188 (Name marked and disabled); grep for "at least one" / "alert" across src returns nothing*

#### TS-024 · important · partial
**Adding or removing a column shifts every column to its right by the column's width plus margin, and only the affected region is repainted.**  
Add inserts after the given column (default: last), creates widgets for the currently visible poses only, and invalidates from the new column's offset rightwards; remove drops each pose's widget for that column first. Both set fStateNeedsSaving and re-mark the Attributes menu. While filtering, add rebuilds the filtered list and remove re-tests every filtered pose (the removed column may have been what matched).  
`PoseView.cpp` → `BPoseView::AddColumn`  
**MISSING:** Add/remove reflow every column to its right by width+MARGIN, set state_dirty, leave the Attributes marks recomputed on next rebuild, and thin the filtered list in place on remove while rebuilding it on add; the missing clause is the scoped repaint -- there is no invalidation region, the whole window is redrawn on the next dirty frame.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:3104-3148 (toggle_column, filter_changed calls) and :3391 (reflow_columns); full redraw at /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:1565*

#### TS-025 · important · open
**The Attributes menu offers per-MIME-type submenus listing that type's user-visible attributes, built from the types actually present in the folder.**  
Only installed types with attr:public_name entries and attr:viewable true are listed; each contributes width/alignment/editable/display_as from the MIME database. Supertype menus with no direct items get their sub-menus promoted to the top level and are then deleted; a trailing separator is removed if it would be the only item.  
`ContainerWindow.cpp` → `BContainerWindow::AddMimeMenu`  
**TIKU:** Replace with enumeration of a node's typed descriptors; the "viewable" flag becomes a capability/visibility bit on the descriptor.  
**VERIFIER CORRECTION:** BContainerWindow::AddMimeTypesToMenu removes ANY trailing separator unconditionally (`separator = menu->ItemAt(CountItems()-1); if (dynamic_cast<BSeparatorItem*>(separator) != NULL) remove`). The "only item" qualifier comes from the source comment, not the code.  

#### TS-026 · important · open
**"Copy layout" and "Paste layout" transfer a folder's whole column set and sort order through the clipboard.**  
Copy flattens SaveState(BMessage) under clipboard type "application/tracker-columns"; paste removes all old columns (keeping one alive until the new ones are added, then removing it), instantiates the archived columns in order, then applies primary/secondary/reverse sort and re-sorts. These are the first two items of the Attributes menu, before a separator.  
`PoseView.cpp` → `BPoseView::MessageReceived (kCopyAttributes)`  

#### TS-027 · important · done
**The "Arrange by" menu mirrors the attribute list and selects the primary sort without necessarily showing the column.**  
Built by copying the Attributes menu items from index 3 onward (skipping Copy layout, Paste layout and the separator) with the message what changed to kArrangeBy. Choosing an attribute that has no column adds the column first; choosing the current primary sort sets the sort hash to 0; secondary sort is always cleared and Cleanup(true) runs. The submenu marks the current primary sort and the reverse-order item.  
`ContainerWindow.cpp` → `BContainerWindow::NewArrangeByMenu`  

#### TS-028 · polish · open
**Time-valued columns repaint themselves when the system date/time format changes.**  
Adding the first B_TIME_TYPE column starts watching kDateFormatChanged; removing the last one stops. On notice, only the rectangles of the time columns are invalidated.  
`PoseView.cpp` → `BPoseView::UpdateDateColumns`  
**TIKU:** Becomes a VFS watch subscription on a format/locale node; same start-on-first-user, stop-on-last-user reference discipline.  

#### TS-029 · core · partial
**Column layout and view state are remembered per folder and restored when the folder is reopened.**  
Columns go to the folder attribute _trk/columns (root/Disks window: _trk/d_columns) and view state to _trk/viewstate (Desktop: _trk/desk_viewstate) as raw streams; attributes larger than 10000 bytes or of the wrong type are ignored. Restored columns are sorted by stored offset then re-laid out contiguously from StartOffset(), so overlapping saved offsets self-heal.  
`PoseView.cpp` → `BPoseView::RestoreColumnState`  
**TIKU:** BFS attributes do not exist: per-folder view state needs an explicit store (sidecar keyed by path/node id, or a namespace metadata node). Device-namespace directories may be read-only, so the store must tolerate "cannot persist here".  
**MISSING:** Columns (order/width/alignment) and view mode plus per-entry positions are stored per path through the state store, which degrades on a non-writable store -- but restore_columns is only called once at startup, so navigating into another folder restores its mode and icon positions and keeps the previous folder's columns, and sort keys and scroll origin are never persisted at all.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:3256 (restore_columns) and :2182 (save_state, writes mode only); sole restore call at /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:1124*

#### TS-030 · polish · done
**Column layouts written on a machine of the opposite endianness are still read correctly.**  
Each state has a second attribute name (_trk/columns_le vs _trk/columns_be, etc.); if the native-order one is absent the foreign one is read with byte swapping of offset, width, alignment, hash and type. Writes always emit both names via AttributeStreamNode::Write.  
`ViewState.cpp` → `BColumn::BColumn(BMallocIO*, int32, bool)`  
**TIKU:** Single-target namespace: the foreign-endian variant can be dropped, but the decision must be recorded, since it changes the on-disk attribute set.  
**VERIFIER CORRECTION:** The big-endian names carry no suffix. Attributes.h defines kAttrColumns_be = "_trk/columns" / kAttrColumns_le = "_trk/columns_le" (likewise "_trk/viewstate" vs "_trk/viewstate_le", "_trk/d_columns" vs "_trk/d_columns_le"). So the on-disk pair is "_trk/columns" and "_trk/columns_le", not "..._be"/"..._le".  
**PORT:** Which byte order a record is in is stamped IN the record rather than encoded in which attribute name it was stored under, and every record in a blob must agree.  The version must match exactly where Tracker accepts a range, there being one format so far.

#### TS-031 · polish · done
**Column widths and positions survive a change of system font size proportionally.**  
Offsets and widths are divided by (plain font size / 12) when archived and multiplied back when restored, so a layout authored at 12pt reopens correctly at other sizes.  
`ViewState.cpp` → `BColumn::_Scale`  
**PORT:** Widths are stored in 12-point units, rounded up on load and down on save, so a width survives a round trip; a stored value below the minimum column width is raised on load and written back raised, which is the clamp showing rather than drift.  Offsets are not scaled because they are not stored as positions at all (PVL-033).

#### TS-032 · important · done
**Corrupt or hostile saved column data is discarded rather than applied.**  
A restored column is rejected if title length > 500, offset < 0 or > 10000, width < 0 or > 10000, alignment outside B_ALIGN_LEFT..B_ALIGN_CENTER, or attribute name length > 500. Stream reads also require the class key hash to match and version >= 21 (current 22); message reads require the version to match exactly.  
`ViewState.cpp` → `BColumn::_Sanitize`  
**PORT:** Bounds are checked on the SCALED value, as Tracker checks them after its own scaling, so the ceiling does not move with the font; a record set naming one column twice is refused outright, because the name column cannot be removed and such a layout could never be undone.

#### TS-033 · important · partial
**Restored view state is clamped to sane values: icon sizes to 16..128 and list scroll origin to non-negative.**  
Applied on both stream and message restore. A full sanity pass (not the fix-only one) also rejects view modes that are not list/icon/mini-icon/0. A Desktop window that restores list mode is forced back to icon mode.  
`ViewState.cpp` → `BViewState::_Sanitize`  
**MISSING:** Restore does sanity-check what it actually stores (an unknown view-mode string falls back to list, pose locations beyond +/-50000 are dropped, column widths/offsets/alignments/fields out of range reject the whole record), but the two clamps this row names do not happen on restore because icon size and scroll origin are never persisted -- 16..128 is enforced only in set_icon_size and the non-negative scroll only in scroll_to -- and there is no Desktop-forced-back-to-icon rule.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:2125-2166 (restore_state) and :1480 (set_icon_size), :3659 (scroll_to)*

#### TS-034 · core · differs
**A folder with no saved column state opens with Name, Size and Modified columns.**  
Name 145px left-aligned (editable, stat field), Size 80px right-aligned, Modified 150px left-aligned; also applied as a repair when a restore produced zero columns. Adding the Modified column starts date-format watching.  
`PoseView.cpp` → `BPoseView::SetupDefaultColumnsIfNeeded`  
**PORT:** A default column set really is installed at view construction for a folder with no saved state, and it survives a rejected restore because restore_columns returns 0 without touching v->columns on every rejection path — the set is Name 240 / Kind 140 / Value 190 / Cap 60 because those are the device-namespace analogues of Size/Modified, and date-format watching is vacuous since the port carries no date-format setting.  
*src/tiku_trk_poseview.c:505-515 (constructor defaults + reflow); :3271-3341 (every restore rejection returns 0, leaving the defaults); no date-format setting exists anywhere in src/*

#### TS-035 · core · done
**A folder with no saved state sorts by Name, ascending, in list mode with no secondary sort.**  
BViewState defaults: view mode kListMode, icon size and last icon size B_LARGE_ICON, both origins (0,0), primary sort = hash of the name attribute with B_STRING_TYPE, secondary 0, reverse false. On restore, if no column matches the saved primary sort the first column becomes the primary sort, and a primary equal to the secondary clears the secondary.  
`ViewState.cpp` → `BViewState::_Init`  

#### TS-036 · important · partial
**Column and sort changes are written back to the folder only when something actually changed.**  
Resize, reorder, add and remove set fStateNeedsSaving; sort/mode/origin changes are detected by BViewState comparing every field against a shadow copy taken at construction and at each archive. The window checks both before saving on close.  
`ViewState.h` → `BViewState::StateNeedsSaving`  
**TIKU:** With a namespace store, the same dirty-bit discipline avoids a write per drag frame — important if the backing store is NVM.  
**MISSING:** The dirty-bit discipline exists and works for view state -- resize, move, add, remove, sort_click, set_mode and set_icon_size all set state_dirty and save_state early-returns when it is clear -- but the column write is ungated: save_columns never consults state_dirty and the window calls it on every title-bar release, so a click that only re-sorted rewrites the layout record.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:2187 (save_state gate) vs :3217 (save_columns, no gate); caller /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:1472*

#### TS-037 · core · partial
**The bottom-left status area shows the item count: "no items", "1 item"/"N items", or "K/N selected" when there is a selection.**  
Plural forms and number grouping are locale-formatted (BStringFormat + BNumberFormat on the total). Text is truncated at the end if it does not fit.  
`CountView.cpp` → `BCountView::Draw`  
**MISSING:** An empty folder says so rather than counting to zero; the source's other count-view states are not all there.

#### TS-038 · important · open
**During a long directory read a barber pole spins in the status area, but only if the read takes more than half a second.**  
StartBarberPole only arms it (fStartSpinningAfter = now + 500000us); the first paint of the pole invalidates the whole text+pole rect, later frames only the pole. Animation is driven by Pulse(); the stripe bitmap offset decrements 5..0 and wraps each frame. Ended by AddPosesCompleted and by every early-out in the add-poses path (bad directory, rewind failure, superseded thread).  
`CountView.cpp` → `BCountView::TrySpinningBarberPole`  
**TIKU:** The add-poses worker runs on its own thread and locks the window to touch the count view; a Tiku port must keep an equivalent boundary (or post an event) since VFS enumeration and watch callbacks may arrive off the UI context.  

#### TS-039 · polish · open
**While the barber pole is visible the count text gets 10px less room, so text and pole never overlap.**  
TextInvalRect subtracts 10 from the right when fShowingBarberPole; the pole itself is a 7x7 inner rect inset 3,4 from the right end of the view with a 1px bevel frame drawn light/shadow.  
`CountView.cpp` → `BCountView::TextInvalRect`  

#### TS-040 · polish · open
**The status area only repaints when the item count or the selection count actually changes.**  
BCountView caches fLastCount/fLastCountSelected (initialised to -1 so the first check always paints) and invalidates just the text sub-rectangle; the barber pole area is checked separately in the same call.  
`CountView.cpp` → `BCountView::CheckCount`  

#### TS-041 · important · partial
**Typing to jump to an item replaces the count with the typed string, on a document-coloured background, and the string disappears after a pause.**  
The count view background switches to B_DOCUMENT_BACKGROUND_COLOR and the text to B_DOCUMENT_TEXT_COLOR while typing ahead, and the string is truncated from the beginning so the most recent characters stay visible. A BMessageRunner ticking at the double-click speed clears it once 2x double-click-speed has elapsed since the last key; the same 2x window decides whether a new keystroke extends or restarts the match string.  
`CountView.cpp` → `BCountView::SetTypeAhead`  
**MISSING:** The typed string replaces the count on a sunken document ground and is cleared by type_tick once the quiet period (1 s = 2x click speed) has passed since the last key, with backspace restarting it; the missing clause is truncation -- the string is printed whole, not truncated from the beginning to keep the newest characters visible.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:226 (draw_count type-ahead branch) and :1557 (type_tick); /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1307*

#### TS-042 · important · partial
**With type-ahead filtering on, typed characters accumulate in the status area as a live filter and the list shrinks to matching items.**  
Count view shows "K/N filter" when something is selected, else "N filter", truncated in the middle. Backspace removes one character (RemoveFilterCharacter); Shift+Space starts a second, independent filter term shown as "|" (all terms must match, each against any column's text, case-insensitive substring); Escape or Shift+Return cancels filtering and CancelFilter clears the string. Filtering is list-mode only, and Tab type-select is disabled while the preference is on.  
`CountView.cpp` → `BCountView::AddFilterCharacter`  
**TIKU:** Filter matches the rendered text of every column, so it depends on descriptor formatting; decide whether device nodes with expensive reads participate in filtering.  
**MISSING:** Characters accumulate into a live filter that shrinks the list, backspace removes one character and then a whole term, Shift+Space starts an independent term shown joined by '|', every term must match and each may match any column's rendered text case-insensitively, and Shift+Return cancels; missing is the count-view label form -- it shows the raw filter text, never "N filter"/"K/N filter" middle-truncated -- and Escape only clears the string (type_clear) without leaving filter mode.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:1211 (type_ahead filtering branch), :1277 (type_string '|' join), :filter_pose; Escape at /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:904*

#### TS-043 · important · open
**Clicking the count area opens a menu of the folder's parent hierarchy for navigating upwards.**  
Activates and updates the window first; suppressed in file panels, when there is no target model, and at the volumes root. The menu is popped at the view's bottom-left + 3px with the count view rect as the click-to-open rect.  
`CountView.cpp` → `BCountView::MouseDown`  

#### TS-044 · polish · partial
**The count view is laid out at the bottom-left, sharing the row with the horizontal scroll bar and matching its height.**  
Added to the count container with weight 0.25 and given the h-scrollbar's MinSize as its explicit minimum (1px less in file panels, which have no upper border). The title view is inserted at index 0 of the bordered pose container.  
`ContainerWindow.cpp` → `BContainerWindow::InitLayout`  
**MISSING:** The count view is at the bottom-left of the pose container at a fixed height, but it spans the full width and there is no horizontal scroll bar in the window at all (only the vertical one), so nothing shares its row, its min-size or its height.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:214 (draw_count rect) and :505 (only scrollbar drawn)*

#### TS-045 · core · open
**Preferences live in a plain-text file at ~/config/settings/Tracker/TrackerSettings and are loaded lazily on first use.**  
Each line is "Name value", booleans written as on/off, colours as 0x%08x; the file is deleted and rewritten wholesale on save. Normal saves write only settings that differ from their default, but every save triggered from the preferences window passes onlyIfNonDefault=false and therefore writes all of them. Unknown names in the file are ignored with an "unknown command" error. Settings are also saved on Tracker quit.  
`TrackerSettings.cpp` → `TTrackerState::LoadSettingsIfNeeded`  
**TIKU:** Needs a TikuOS-side settings store (namespace node or persist cells); the text argv format and the write-only-if-non-default rule are the parts users can observe.  

#### TS-046 · important · open
**"Show Disks icon" and "Show volumes on Desktop" are mutually exclusive, and "Show shared volumes on Desktop" is only available with the latter.**  
Defaults: ShowDisksIcon off, MountVolumesOntoDesktop on, MountSharedVolumesOntoDesktop on. Setting either one through the API forces the other to its complement (SetShowDisksIcon also sets MountVolumesOntoDesktop to !enabled). Changing them broadcasts kVolumesOnDesktopChanged (and kDesktopIntegrationChanged on Defaults/Revert) with all three booleans so open windows adapt live.  
`SettingsViews.cpp` → `DesktopSettingsView::MessageReceived (kShowDisksIconChanged)`  
**TIKU:** "Volumes" become namespace mounts; the same one-of-two policy should apply to whether mounts appear on the Desktop or under a Disks node.  

#### TS-047 · polish · partial
**"Show folder location in title tab" puts the full path in the window title.**  
Default off (kDefaultShowFullPathInTitleBar). Notifies kWindowsShowFullPathChanged; also enables path watching so the title tracks renames of ancestors.  
`SettingsViews.cpp` → `WindowsSettingsView::MessageReceived (kWindowsShowFullPathChanged)`  
**MISSING:** The full path does reach the title and follows the folder's own rename through the MOVED/RETITLE notice, but the row's rule is a preference: there is no setting, no default-off, no kWindowsShowFullPathChanged-style propagation, and no ancestor path watching — the port hard-codes the on state, which is absence of the mechanism, not another route to it.  
*src/tiku_trk_app.c:559-570 (retitle builds the path unconditionally); src/tiku_trk_desktop.c:2302-2306 (RETITLE copies poseview path); src/tiku_trk_poseview.c:2460-2462 (only the folder's own MOVED)*

#### TS-048 · important · open
**"Single window navigation" reuses one window when opening folders, and "Show navigator" (only selectable with it on) adds the back/forward/up bar.**  
Defaults: both off. Turning single-window off forces ShowNavigator to 0 and disables its checkbox; both notices (kShowNavigatorChanged, kSingleWindowBrowseChanged) are sent so open windows add or remove the navigator and re-bind their shortcuts immediately.  
`SettingsViews.cpp` → `WindowsSettingsView::MessageReceived (kSingleWindowBrowseChanged)`  

#### TS-049 · important · partial
**"Outline selection rectangle only" switches rubber-band selection from a translucent filled rectangle to an outline.**  
The checkbox is the inverse of the stored TransparentSelection setting (default on, i.e. checkbox off). Pose views cache the value at construction and update it from the kTransparentSelectionChanged notice; the translucent rectangle is drawn in B_NAVIGATION_BASE_COLOR at alpha 128 with a B_CONTROL_HIGHLIGHT_COLOR interior at alpha 90.  
`SettingsViews.cpp` → `WindowsSettingsView::MessageReceived (kTransparentSelectionChanged)`  
**MISSING:** The band is always drawn as an outline (the setting's ON state), but there is no setting, no cached value, no change notice and no translucent alternative -- the graphics layer has no alpha compositing at all, only opaque fills and tints.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:331-344 (draw band, tiku_desk_frame); no alpha in /home/ambuj/tikuOS/applications/desktop/src/tiku_desk_gfx.h*

#### TS-050 · important · partial
**"List folders first" sorts directories ahead of files regardless of name.**  
Default on. The value is pushed into NameAttributeText and RealNameAttributeText (static, process-wide) at load and on every change; on notice, list-mode views re-sort and repaint immediately.  
`TrackerSettings.cpp` → `TrackerSettings::SetSortFolderNamesFirst`  
**TIKU:** Namespace entries are not just files/folders; decide where device nodes rank in the folders-first partition.  
**MISSING:** Containers are ordered ahead of non-containers before the sort field is consulted, which is the default-on behaviour a user sees, but it cannot be turned off: there is no setting, no process-wide value pushed into the name comparison and no change notice, and the partition is applied for every sort field rather than living inside the name attribute.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_poseview.c:355-363 (pose_cmp)*

#### TS-051 · important · open
**"Hide dotfiles" hides entries whose name starts with a dot.**  
Default off. Read once per directory-read thread; when it is off, "." and ".." are explicitly skipped instead. Changing it triggers a full Refresh() of open pose views.  
`SettingsViews.cpp` → `WindowsSettingsView::MessageReceived (kHideDotFilesChanged)`  

#### TS-052 · important · partial
**"Enable type-ahead filtering" swaps the type-to-select behaviour for type-to-filter in list mode.**  
Default off. While on, Tab-based type-select is disabled in list mode. If the preference is switched off while a filter is active, filtering is stopped immediately on every open view via the kTypeAheadFilteringChanged notice.  
`SettingsViews.cpp` → `WindowsSettingsView::MessageReceived (kTypeAheadFilteringChanged)`  
**MISSING:** Type-to-filter genuinely swaps for type-to-select (tiku_trk_poseview_type_ahead branches on v->filtering), it is off by default, and turning it off drops the terms and rebuilds the listing — but it is a per-view runtime mode toggled by Tab in one window, so the stated clauses "Tab-based type-select is disabled" and "filtering is stopped on every open view via the notice" have no counterpart at all.  
*src/tiku_trk_poseview.c:1205-1228 (filter-vs-select branch) and :1318-1333 (set_filtering drops terms); src/tiku_trk_app.c:908-912 (Tab is the per-view toggle, no cross-view propagation)*

#### TS-053 · polish · open
**"Generate image thumbnails" controls whether image files show a thumbnail instead of a type icon.**  
Default on; consulted per file when deciding whether a thumbnail should be produced.  
`SettingsViews.cpp` → `WindowsSettingsView::MessageReceived (kGenerateImageThumbnailsChanged)`  
**TIKU:** Thumbnail generation is a per-node capability question in a device namespace; nodes without readable bulk content must not be probed.  

#### TS-054 · important · open
**"Show space bars on volumes" draws a used/free capacity bar on volume icons, with three user-chosen colours.**  
Default on; colours default to used 0,203,0 / free 255,255,255 / warning 203,0,0, all with alpha 192 (the alpha is forced to 192 on every pick because BColorControl ignores alpha but equality checks do not). The bar is drawn along the right edge of the icon, 7/32 of icon width (min 4px); the warning colour replaces the used colour when free space is below 5%. Toggling the setting or a colour makes Tracker force a periodic update of all volume poses; with the setting off, Tracker's Pulse skips that update entirely.  
`SettingsViews.cpp` → `SpaceBarSettingsView::MessageReceived (kSpaceBarColorChanged)`  
**TIKU:** Capacity/free reporting for a namespace mount needs a typed descriptor; nodes that cannot report capacity must simply have no bar rather than a zero bar.  

#### TS-055 · polish · open
**The space-bar colour editor edits one of the three colours at a time, chosen from a pop-up above the colour control.**  
Menu items Used/Free/Warning space color; index 0 (Used) is marked at start-up and the control is preloaded with the used colour. Switching the menu item reloads the control from the corresponding setting; every colour control change writes straight through to the setting and posts kSettingsContentsModified.  
`SettingsViews.cpp` → `SpaceBarSettingsView::MessageReceived (kSpaceBarSwitchColor)`  

#### TS-056 · polish · open
**The number of remembered recent applications, documents and folders is 10 each.**  
Stored as RecentApplications / RecentDocuments / RecentFolders scalars; read together via RecentCounts() when building the favorites/recent menus. No preferences-window control exposes them in this build — they are file-only settings.  
`TrackerSettings.cpp` → `TrackerSettings::RecentCounts`  

#### TS-057 · polish · open
**"Eject when unmounting" is offered on the Disk mount page together with the automount and boot-mount policies.**  
Default on. This page talks to the mount server (kSetAutomounterParams / kGetAutomounterParams with a 2.5s timeout and a "mount server could not be contacted" alert), not to the Tracker settings file, and it declares itself never defaultable; Revert replays the settings captured when the window was shown. Automount choices additionally request a rescan.  
`AutoMounterSettings.cpp` → `AutomountSettingsPanel::_SendSettings`  

#### TS-058 · core · open
**The preferences window has four pages (Desktop, Windows, Volume icons, Disk mount) and applies every change immediately, with no OK button.**  
Each control writes the setting, sends its notice, and posts kSettingsContentsModified, which re-saves the whole settings file and refreshes the buttons. Closing the window hides it instead of quitting, so state and the recorded revert baseline survive; opening it re-records the revert baseline and re-reads current values into every page.  
`TrackerSettingsWindow.cpp` → `TrackerSettingsWindow::_HandleChangedContents`  

#### TS-059 · important · open
**The Defaults and Revert buttons are enabled only when at least one page has something to reset, and pressing one applies it to every page that needs it.**  
Defaults enabled if any page's IsDefaultable() is true (i.e. any of its settings differs from the compiled default); Revert enabled if any page's IsRevertable() is true (differs from the values captured when the window was shown). Both start disabled and are recomputed after every change.  
`TrackerSettingsWindow.cpp` → `TrackerSettingsWindow::_UpdateButtons`  

#### TS-060 · polish · open
**A page whose settings have been changed since the window opened is shown in bold in the page list.**  
SettingsItem::DrawItem picks be_bold_font when the page's IsRevertable() is true, plain otherwise; the list is invalidated on every settings change so the bolding is live.  
`TrackerSettingsWindow.cpp` → `SettingsItem::DrawItem`  

#### TS-061 · polish · open
**Reverting the Windows page fails to restore "Hide dotfiles" and corrupts "List folders first" instead.**  
WindowsSettingsView::Revert calls settings.SetSortFolderNamesFirst(fHideDotFiles) in the fHideDotFiles branch — a genuine bug in the original; the notice sent is still kHideDotFilesChanged. The rewrite should restore HideDotFiles here, and this row exists so the deviation is deliberate rather than accidental.  
`SettingsViews.cpp` → `WindowsSettingsView::Revert`  

#### TS-062 · polish · open
**Two obsolete preferences are still read from and written to the settings file so that old files round-trip.**  
DesktopFilePanelRoot (default true) and ShowSelectionWhenInactive (default true) are registered and exposed through the API but have no control in the preferences window; ShowSelectionWhenInactive is still honoured by pose views (cached at construction, updated on kShowSelectionWhenInactiveChanged).  
`TrackerSettings.cpp` → `TTrackerState::LoadSettingsIfNeeded`  

#### TS-063 · polish · partial ⟨found by verification⟩
**Right-clicking the column title bar opens the Attributes menu directly.**  
BTitleView::MouseDown: if SecondaryMouseButtonDown(modifiers(), buttons) it builds a BPopUpMenu, fills it via window->NewAttributesMenu() + AddMimeTypesToMenu() + MarkAttributesMenu(), targets the pose view and Go()es at the click point, then returns without starting any column tracking.  
`TitleView.cpp` → `BTitleView::MouseDown (secondary-button branch)`  
**MISSING:** The secondary-button branch is implemented in the tracker -- a right-click anywhere on the strip returns TIKU_TRK_TT_MENU and starts no resize or drag -- but no caller acts on it: the single-window app discards the flag and has no menu system, and the shell that owns the Attributes menu has no title bar, so no popup ever appears.  
*/home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_titleview.c:136-140 (tiku_trk_titleview_mouse_down); flag discarded at /home/ambuj/tikuOS/applications/tracker-new/src/tiku_trk_app.c:1242*

#### TS-064 · polish · differs ⟨found by verification⟩
**The column title bar renders through a shared offscreen bitmap whenever a column is pressed, dragged or resized, so the tracked column does not flicker.**  
BTitleView::Draw(BRect, bool useOffscreen, bool updateOnly, const BColumnTitle* pressedColumn, void (*trackRectBlitter)(BView*, BRect), BRect passThru) begins on sOffscreen (a file-static OffscreenBitmap), redraws every title into it, lets the blitter stamp the drag outline or resize line, then DrawBitmap()s the result. All three ColumnTrackState paths call it with useOffscreen = true.  
`TitleView.cpp` → `BTitleView::Draw (offscreen overload) / BTitleView::sOffscreen`  
**PORT:** A real alternative mechanism achieves the row's outcome: every frame — titles, the dragged title drawn last and on top, and the resize guide line — is composed into the app's own pixel surface and pushed with a single memcpy+XPutImage, so a pressed, dragged or resized column cannot flicker; the title-strip-only OffscreenBitmap is simply subsumed by whole-window double buffering.  
*src/tiku_trk_app.c:550-557 (blit: memcpy into the XImage + one XPutImage); :367-388 (titles and the dragged title composed into the surface); :509-518 (resize guide line drawn into the same surface)*

#### TS-065 · important · open ⟨found by verification⟩
**The Disk mount page also carries the automount and initial-mount policy radio groups, and the Defaults button can never touch that page.**  
AutomountSettingsPanel builds autoMountAll / autoMountAllBFS / scanningDisabled and initialMountAll / initialMountAllBFS / initialMountRestore / initialDontMount controls alongside fEjectWhenUnmountingCheckBox, all sent in one kSetAutomounterParams message; IsDefaultable() is hard-coded to return false, and IsRevertable() compares the whole reply message against the one captured when the window was shown (BMessage::HasSameData).  
`AutoMounterSettings.cpp` → `AutomountSettingsPanel::IsDefaultable / AutomountSettingsPanel::IsRevertable`  
**TIKU:** The only settings page whose state does not live in Tracker — the whole-message diff for revertability is the pattern to copy if TikuOS settings are owned by another service.  


---

## Application level: Tracker app, desktop, open-with, file panels, task loop

Architecture notes: [`spec/ARCH-app-windows.md`](spec/ARCH-app-windows.md)

#### AW-001 · core · differs
**Tracker always has a Desktop window; it is created during application construction, before ReadyToRun, and locked while it initializes.**  
Constructor takes AutoLock<WindowList> then AutoLock<BWindow> on the new BDeskWindow, calls Init(), then creates BTrashWatcher "so the Trash icon gets set".  
`Tracker.cpp` → `TTracker::TTracker`  
**TIKU:** TikuOS has no windowing desktop; the 'desktop root container' becomes the top-level namespace view. Decide whether a root view exists at boot before any user request.  
**PORT:** A desktop object really is constructed at startup, before the X display and the event loop, and it outlives every folder window (close_window states the backdrop and Deskbar ARE the application); the TIKU note's open question — whether a root view exists at boot — is answered by opening default folder windows (/sys/mem and /dev on a device, $HOME otherwise) before any user request.  
*src/tiku_trk_desktop.c:1271-1274 (surface + tiku_trk_desk_new) and :1307-1311 boot-time open_folder, both before XOpenDisplay at :1314; :403-407 close_window comment*

#### AW-002 · polish · open
**Anything launched from Tracker inherits /boot/home as its working directory, and Tracker raises its own open-file limit to 512 so nested copies work.**  
chdir(find_directory(B_USER_DIRECTORY)); setrlimit(RLIMIT_NOFILE, 512).  
`Tracker.cpp` → `TTracker::TTracker`  
**TIKU:** cwd inheritance is a POSIX-process notion; in TikuOS the equivalent is the default namespace root handed to a launched task.  

#### AW-003 · important · open
**When the node-monitor allocation is exhausted, watching retries once after raising the per-team node-monitor limit by 512, instead of silently failing to track a folder.**  
watch_node returning B_NO_MEMORY -> NeedMoreNodeMonitors() bumps RLIMIT_NOVMON by kNodeMonitorBumpValue (512) from DEFAULT_MON_NUM (4096), then retries. In a file panel (no TTracker) it just fails.  
`Tracker.cpp` → `TTracker::WatchNode / TTracker::NeedMoreNodeMonitors`  
**TIKU:** VFS watch slots are a fixed table (TIKU_VFS_WATCH_MAX, 8 by default) with no growth path; the rewrite must either grow it or define an explicit eviction/refusal rule, and the 'silently stops updating' failure mode must not be inherited.  

#### AW-004 · core · open
**On quit, the set of open folder windows is written to an attribute on the Desktop directory so the same windows reopen next boot; if no windows were open the attribute is deleted.**  
Flattened BMessage with "paths" strings (deduped), a per-path int8 flag (kOpenWindowNoFlags/kOpenWindowMinimized/kOpenWindowHasState), an "open_disks_window" bool for the root window, and optional "window state" sub-messages; written to kAttrOpenWindows on FSGetDeskDir(). Skipped entirely in safe mode.  
`Tracker.cpp` → `TTracker::QuitRequested`  
**TIKU:** Attribute-on-a-directory becomes a persist cell / durable node in the device namespace; the message must be replaced with an explicit serialized record, and the write must sit inside an NVM unlock window.  

#### AW-005 · important · partial
**Per-window state is embedded in the saved session only for windows that cannot save state to their own folder — a duplicate window, or a folder on a volume without writable attributes / read-only; such windows are then barred from writing state on close.**  
Condition: window != FindContainerWindow(ref) || (deviceFlags & (B_FS_HAS_ATTR|B_FS_IS_READONLY)) != B_FS_HAS_ATTR; then SaveState(stateMessage) + SetSaveStateEnabled(false) + kOpenWindowHasState.  
`Tracker.cpp` → `TTracker::QuitRequested / GetVolumeFlags`  
**TIKU:** Read-only or attribute-less backing store is the normal case for device nodes; the fallback path (state stored centrally instead of beside the object) becomes the default, not the exception.  
**MISSING:** The device backend does put state in a central per-device sidecar rather than on the object, which is the TIKU note's fallback-as-default, but there is no per-window condition and no fallback at all on the local backend: an unwritable store makes save_state return 0 and the arrangement is simply lost, and no window is ever flagged to stop writing state on close.  
*src/tiku_trk_backend_tiku.c:457-465 tk_store (sidecar by devid); src/tiku_trk_poseview.c:2187-2194 save_state (store unwritable -> return 0, silent loss); no session-embedding or save-state-enabled flag exists in src/*

#### AW-006 · core · open
**At startup the previously open windows reopen with their workspace, minimized state and window decor restored, and the Disks window reopens only if the Disks icon setting is on.**  
kRestoreWorkspace | (minimized ? kIsHidden : 0) | kRestoreDecor; state messages consumed in order via stateMessageCounter; only models that still InitCheck OK and IsContainer() are opened. A pathFilter prefix can restrict which paths are reopened.  
`Tracker.cpp` → `TTracker::_OpenPreviouslyOpenedWindows`  

#### AW-007 · important · open
**Booting in safe mode suppresses session restore, the desktop replicant shelf, the transient-query cleaner and the open-windows attribute write.**  
BootedInSafeMode() guards in ReadyToRun, QuitRequested and BDeskWindow::Init.  
`Tracker.cpp` → `TTracker::ReadyToRun / TTracker::QuitRequested`  
**TIKU:** Equivalent is a recovery/minimal boot flag; worth keeping so a corrupt persisted namespace view cannot brick the shell.  

#### AW-008 · important · open
**The user cannot quit Tracker with the quit shortcut; pressing it while the settings window is active hides that window instead.**  
If CurrentMessage() has bool "shortcut", walk WindowAt() for fSettingsWindow, hide it if visible and active, and return false.  
`Tracker.cpp` → `TTracker::QuitRequested`  
**CHECKED:** There is no settings/preferences window at all (the Preferences menu item falls through menu_invoke's default), and the quit shortcut is not intercepted — Ctrl+Q sets running = 0 and the process exits in both binaries.  
*none found; contradicted by src/tiku_trk_desktop.c:main (XK_q + ControlMask -> app.running = 0) and src/tiku_trk_app.c:handle_key (XK_q)*

#### AW-009 · important · open
**Quitting waits for in-flight copy/move/empty-trash work to stop, up to five seconds, before the application exits.**  
gStatusWindow->AttemptToQuit() once up front, then 50 iterations of AttemptToQuit()+snooze(100000).  
`Tracker.cpp` → `TTracker::QuitRequested`  
**TIKU:** Long-running namespace operations must have the same 'ask to stop, bounded wait' contract; a device write that cannot be aborted needs an explicit rule.  

#### AW-010 · important · done
**A quit request from the Deskbar means 'close all your windows', not 'exit': every window except the Desktop and the status window is asked to quit and Tracker keeps running.**  
kCloseAllWindows; BDeskWindow and BStatusWindow are skipped in both the WindowAt() pass and the fWindowList removal pass.  
`Tracker.cpp` → `TTracker::CloseAllWindows`  
**PORT:** The Deskbar's per-team window menu maps Close All to TIKU_TRK_DB_CLOSE_ALL, which closes every desk window in a loop and leaves app.running alone (only the separate leaf-menu Quit clears it), and the backdrop and Deskbar are not desk windows so there is nothing that needs skipping.  
*src/tiku_trk_desktop.c:1645-1655 (DB_CLOSE_ALL loop) vs :1609-1611 (DB_QUIT sets running = 0); src/tiku_trk_deskbar.c:670-672 window-menu idx 3; src/tiku_trk_win.c:181-199 desk_close*

#### AW-011 · polish · open
**Close-all-in-workspace closes only windows present in the current workspace, again never the Desktop or the status window.**  
currentWorkspace = 1 << current_workspace(); iterates the window list backwards so removal is safe.  
`Tracker.cpp` → `TTracker::CloseAllInWorkspace`  

#### AW-012 · polish · open
**Closing a folder window can close every window showing that folder or anything inside it.**  
kCloseWindowAndChildren with a node_ref in B_RAW_TYPE data; matches window->TargetModel()->NodeRef() == node or BDirectory(node).Contains(window entry).  
`Tracker.cpp` → `TTracker::CloseWindowAndChildren`  

#### AW-013 · important · open
**On first run Tracker installs the system metamime types with icons, short/long descriptions and preferred apps, and installs extra viewable/editable attribute columns for bookmarks, People files and print spool files.**  
InstallMimeIfNeeded for B_APP/B_FILE/B_VOLUME/B_QUERY/B_QUERY_TEMPLATE/B_LINK/B_ROOT/B_BOOKMARK/B_PERSON/B_PRINTER_SPOOL/B_PRINTER; ExtraAttributeLazyInstaller adds e.g. META:url, META:name..META:nickname with per-column width and alignment. Only rewrites what is missing unless a force mask is set.  
`TrackerInitialState.cpp` → `TTracker::InitMimeTypes / TTracker::InstallMimeIfNeeded / ExtraAttributeLazyInstaller::AddExtraAttribute`  
**TIKU:** The type database becomes typed descriptors on VFS nodes; 'install once at first boot, don't clobber user edits' is the rule to keep.  

#### AW-014 · polish · open
**Tracker creates the query indices it depends on, on every writable persistent volume that supports attributes and queries.**  
fs_create_index(device, kAttrQueryLastChange, B_INT32_TYPE) and "_trk/recentQuery"; volumes that are read-only, non-persistent, !KnowsAttr or !KnowsQuery are skipped.  
`TrackerInitialState.cpp` → `TTracker::InstallIndices`  

#### AW-015 · polish · open
**Default column layouts for query result windows (generic, bookmark, person, e-mail) are written once into template nodes if absent.**  
Per-type AttributeTemplate with kAttrWindowFrame (kDefaultFrame 40,40,695,350), kAttrViewState_be and generated kAttrColumns bits; written only when DefaultStateSourceNode(query,...,false) finds nothing.  
`TrackerInitialState.cpp` → `TTracker::InstallDefaultTemplates / mkColumnsBits`  

#### AW-016 · polish · open
**A default desktop background image is installed only when the boot desktop folder has no background attribute yet.**  
Checks GetAttrInfo(kBackgroundImageInfo); places the logo at a golden-ratio x and 0.9 height y of the main screen, mode kAtOffset, all workspaces.  
`TrackerInitialState.cpp` → `TTracker::InstallTemporaryBackgroundImages`  

#### AW-017 · core · open
**The Desktop window is not movable, closable, resizable, zoomable or minimizable, lives on all workspaces, and accepts the first click without needing activation.**  
kDesktopWindowLook/kDesktopWindowFeel plus B_NOT_MOVABLE|B_WILL_ACCEPT_FIRST_CLICK|B_NOT_ZOOMABLE|B_NOT_CLOSABLE|B_NOT_MINIMIZABLE|B_NOT_RESIZABLE|B_ASYNCHRONOUS_CONTROLS, B_ALL_WORKSPACES.  
`DeskWindow.cpp` → `BDeskWindow::BDeskWindow`  
**VERIFIER CORRECTION:** It is disabled for EVERY container window — BContainerWindow::CreatePoseView (ContainerWindow.cpp:621) calls EnableBorderHighlight(false) unconditionally, and QueryContainerWindow.cpp:87 / VirtualDirectoryWindow.cpp:83 repeat it. The only place it is turned ON is FilePanelPriv.cpp:792. In practice the B_KEYBOARD_NAVIGATION_COLOR border appears only in file panels.  

#### AW-018 · core · open
**The Desktop has no menu bar, no scroll bars and no container/border view — it is the bare pose area sized to the screen.**  
ShouldAddMenus/ShouldAddScrollBars/ShouldAddContainerView all return false; Init() resizes to BScreen().Frame() before the inherited Init so volume poses are not clipped.  
`DeskWindow.cpp` → `BDeskWindow::ShouldAddMenus / ShouldAddScrollBars / ShouldAddContainerView / BDeskWindow::Init`  
**CHECKED:** The backdrop is chrome-less only because it is not a Desktop window at all: desk_draw fills the surface and composites folder windows over it, there is no pose view, no volume poses and no screen-sized pose area, and root is opened in an ordinary cascaded window like any other folder.  
*src/tiku_trk_win.c:493-510 tiku_trk_desk_draw (fill C_BACKDROP then draw_window per window); src/tiku_trk_desktop.c:1073-1084 open_folder used for "/" and every other path*

#### AW-019 · important · open
**Desktop icons never auto-scroll, are never remapped into a grid, and are always forced to stay on-screen.**  
SetIconMapping(false), SetEnsurePosesVisible(true), SetAutoScroll(false); pose view adopts the screen's desktop color as view+low color (forcing alpha to 255).  
`DeskWindow.cpp` → `BDeskWindow::CreatePoseView`  

#### AW-020 · important · open
**Changing screen resolution saves the icon positions against the old frame first, then re-lays icons that would fall off the new screen.**  
SaveDesktopPoseLocations() with fOldFrame, ResizeTo(new), then CheckPoseVisibility(frameChanged ? &frame : 0) so old-frame pose info can drive rearrangement.  
`DeskWindow.cpp` → `BDeskWindow::ScreenChanged / BDeskWindow::SaveDesktopPoseLocations`  

#### AW-021 · polish · open
**Dropping a color onto the desktop changes the desktop background color and tells the Backgrounds preferences app about it.**  
message->FindData("RGBColor", 'RGBC') on a dropped message; BScreen::SetDesktopColor plus SetViewColor/SetLowColor, then forwards the message to application/x-vnd.Haiku-Backgrounds.  
`DeskWindow.cpp` → `BDeskWindow::MessageReceived`  

#### AW-022 · important · open
**Tracker add-ons get keyboard shortcuts: each add-on's built-in default key is bound as Cmd+Opt+<key>, and the user's shortcuts settings file can rebind them; edits to that file are applied live.**  
BEOS:default_shortcut string resource (must be exactly 1 char) -> AddOneShortcut with kDefaultModifiers (B_OPTION_KEY|B_COMMAND_KEY). ApplyShortcutPreferences reads ~/config/settings/shortcuts_settings, accepts only "command" paths under a Tracker add-ons dir, maps mcidx 0/1/3 to Shift/Control/Option (Command always forced), and RevertToDefault first. BPathMonitor on the settings file re-runs it.  
`DeskWindow.cpp` → `BDeskWindow::InitAddOnsList / BDeskWindow::ApplyShortcutPreferences / AddOneShortcut / RevertToDefault`  
**TIKU:** Add-ons are dynamically loaded executables; in TikuOS the analogue is statically registered commands, so the 'rescan directory and rebind' half disappears while the user-rebinding half should stay.  

#### AW-023 · important · open
**Installing or removing a Tracker add-on updates the shortcuts and rebuilds the add-ons menu in every open window without a restart.**  
B_NODE_MONITOR on each add-ons directory (watched in LoadAddOnDir) -> InitAddOnsList(true) + ApplyShortcutPreferences(true) + PostMessageToAllContainerWindows(kRebuildAddOnMenus).  
`DeskWindow.cpp` → `BDeskWindow::MessageReceived / LoadAddOnDir`  

#### AW-024 · important · partial
**The Desktop hosts replicants, persisted in a shelf file, and shows placeholders for replicants whose code is missing.**  
tracker_shelf file under the Tracker settings dir, created if absent; BShelf(&ref, PoseView()) with SetDisplaysZombies(true). Skipped in safe mode.  
`DeskWindow.cpp` → `BDeskWindow::Init`  
**TIKU:** No replicant mechanism; if dropped, say so explicitly rather than silently.  
**MISSING:** A replicant-shaped mechanism exists (named tray items bound to namespace nodes with a live value and a once-resolved format), but the Desktop hosts none of it, there is no shelf file at all (three entries hardcoded at startup, only in device mode), and an unreadable node draws its label with an empty value rather than a placeholder — two of the row's three clauses have no implementation to point at.  
*src/tiku_trk_deskbar.c:190 tiku_trk_deskbar_add_replicant, :213 tiku_trk_deskbar_refresh (n<0 -> continue, valid stays 0), :540 draw uses r->valid ? r->value : ""; src/tiku_trk_desktop.c:1286-1290 three hardcoded tray replicants*

#### AW-025 · polish · done
**Desktop view-mode shortcuts: Cmd+1 icon view, Cmd+2 mini-icon view, Cmd+plus / Cmd+minus step the icon size.**  
kIconMode / kMiniIconMode messages targeted at the pose view; the +/- variants carry int32 "scale" 1 and 0. Shown in the context menu even though they don't fire from the menu items.  
`DeskWindow.cpp` → `BDeskWindow::Init`  
**PORT:** The desktop shell's key handler now binds the chords it was missing: ControlMask + XK_1/2/3 dispatches menu_invoke() with CMD_ICON_VIEW / CMD_MINI_ICON_VIEW / CMD_LIST_VIEW (which call tiku_trk_poseview_set_mode), and ControlMask + XK_plus/XK_equal/XK_KP_Add/XK_minus/XK_KP_Subtract calls tiku_trk_poseview_step_icon_size() followed by a cleanup relayout. Accepting both XK_plus and XK_equal matters — with Ctrl held, an unshifted '+' key reports XK_equal. The target is the focused folder window (fl != NULL) rather than a desktop pose view, but this port's desktop background has no poses at all (desktop_t holds only folder windows; there is no root view), so there is nothing else these could act on. No keysym collision: XK_1/2/3 and the +/- set appear nowhere else in the handler, and the blocks sit after the editing/renaming/select modal branches so they cannot eat typed characters.

#### AW-026 · polish · open
**With the Disks setting on, a root ("/") icon appears on the Desktop; it is added by synthesizing an entry-created notification rather than by a special code path.**  
Hand-built B_NODE_MONITOR message with opcode B_ENTRY_CREATED for the root model, posted to the pose view. The same trick removes it (B_ENTRY_REMOVED) when the setting changes.  
`DeskWindow.cpp` → `BDeskWindow::Init / DesktopPoseView::AdaptToVolumeChange`  
**TIKU:** Synthesized watch events are a useful idiom for TIKU_EVENT_VFS: mounting a device can be announced as a create on the parent directory node.  

#### AW-027 · core · open
**The Trash pose is created after all other desktop poses so it lands in the next free slot, and its icon is watched so it redraws when it changes.**  
CreateTrashPose() called from AddPosesCompleted; WatchNewNode(&nref, B_WATCH_ATTR) on the trash dir.  
`DesktopPoseView.cpp` → `DesktopPoseView::AddPosesCompleted / DesktopPoseView::CreateTrashPose`  

#### AW-028 · polish · open
**Desktop label color flips between black and white based on the desktop background brightness, independently of the color used in folder windows.**  
SetHighColor(LowColor().Brightness() <= ui_color(B_DESKTOP_COLOR).Brightness() ? kWhite : kBlack).  
`DesktopPoseView.cpp` → `DesktopPoseView::AdaptToBackgroundColorChange`  

#### AW-029 · important · open
**The Trash icon shows full vs empty, updated live as items enter or leave the trash on any volume, including volumes mounted later.**  
BTrashWatcher watches every persistent writable volume's trash dir plus B_WATCH_MOUNT; ENTRY_CREATED sets full, MOVED (only across directories) / REMOVED / DEVICE_UNMOUNTED re-check all trash dirs; UpdateTrashIcon writes R_TrashIcon/R_TrashFullIcon into the boot trash dir's icon attributes.  
`TrashWatcher.cpp` → `BTrashWatcher::MessageReceived / BTrashWatcher::UpdateTrashIcon / BTrashWatcher::WatchTrashDirs`  
**TIKU:** Writing the icon into the object's own attributes is how state is published; with typed VFS descriptors the fullness would be a readable node value plus a notify, not a stored icon.  

#### AW-030 · core · partial
**Double-clicking an item opens it as a window if it is a container, opens a Find window if it is a query template, and otherwise launches it — but a folder or query with a non-Tracker preferred app is handed to that app instead.**  
OpenRef: openAsContainer = model->IsContainer(); reset to false when BNodeInfo::GetPreferredApp() != kTrackerSignature (skipped when the selector is kOpenWith). Query templates spawn FindWindow(model->EntryRef()).  
`Tracker.cpp` → `TTracker::OpenRef`  
**TIKU:** A device node's 'preferred handler' is the analogue of a typed descriptor's owning driver; the rule 'container-ness can be overridden by a declared handler' is worth preserving.  
**MISSING:** Only the container clause is implemented — a double-click on a container opens its own window (and in a query view opens the folder holding the result); there is no query-template-opens-Find path (tiku_trk_query_load is never called from a view), nothing launches a non-container, and no preferred-handler override of container-ness exists anywhere in the tree.  
*src/tiku_trk_desktop.c:main, ButtonPress double-click branch (open_folder on is_container)*

#### AW-031 · important · open
**Opening an entry whose link target cannot be resolved offers a Get Info panel instead of a bare error.**  
BEntry(ref, true).InitCheck() failure -> warning alert with buttons Get info / Cancel (Escape closes); choosing Get info sends kGetInfo with the ref to the Tracker.  
`Tracker.cpp` → `TTracker::OpenRef`  

#### AW-032 · important · open
**Option-opening a document closes the parent window about a second after the launch succeeds; option-opening a folder closes the parent only once the child window has actually appeared, and gives up after five seconds.**  
Documents: Thread::Launch of LaunchAndCloseParentIfOK -> synchronous TrackerLaunch, then fTaskLoop->RunLater(CloseParent, 1000000). Containers: CloseParentWaitingForChildSoon -> RunLater(CloseParentWaitingForChild, delay 200ms, period 100ms, timeout 5s), which resolves symlinks and requires the child window to be non-hidden. The Desktop is never closed as a parent.  
`Tracker.cpp` → `TTracker::LaunchAndCloseParentIfOK / TTracker::CloseParentWaitingForChild / TTracker::CloseParentWindowCommon`  

#### AW-033 · important · open
**After an operation that creates an entry, Tracker selects that entry in its parent window once it shows up, retrying for up to five seconds.**  
SelectChildInParentSoon -> RunLater(SelectChildInParent, 100ms delay, 200ms period, 5s timeout); returns true (task done) only when FindPose succeeds.  
`Tracker.cpp` → `TTracker::SelectChildInParentSoon / TTracker::SelectChildInParent`  
**TIKU:** Polling for 'has the watcher delivered the create yet' maps directly onto TIKU_EVENT_VFS; the rewrite can make this event-driven, but must keep a timeout.  

#### AW-034 · important · open
**Refs forwarded to a handler keep the originating pose view's messenger and any be:* fields (e.g. be:line / be:column from /bin/open), so line-jumping and scripting-back still work.**  
If "TrackerViewToken" messenger present it is copied alone; otherwise every field whose name starts with "be:" is copied into the bundled message. "nodeRefsToClose" / "nodeRefToSelect" are read per-index as B_RAW_TYPE data.  
`Tracker.cpp` → `TTracker::RefsReceived`  

#### AW-035 · important · open
**Choosing the Tracker itself as the Open With handler is redirected to the running Tracker instead of launching a second copy.**  
Reads the handler's signature via BAppFileInfo; if strcasecmp(signature, kTrackerSignature) == 0 it falls through to the normal kOpen path, otherwise TrackerLaunch(&handlingApp, message, true).  
`Tracker.cpp` → `TTracker::RefsReceived (case kOpenWith)`  

#### AW-036 · core · open
**Launching is never done on a window thread: launch requests are posted to a dedicated looper that calls the launch function, so the UI cannot block on a slow app start.**  
gLaunchLooper (LaunchLooper) is created and Run() in the TTracker constructor; AsynchLaunchBinder packs the function pointer, refs, appRef and openWithOK flag into a BMessage. Synchronous TrackerLaunch is only used from an already-spawned thread.  
`Tracker.cpp` → `LaunchLooper::MessageReceived / AsynchLaunchBinder (FSUtils.cpp)`  
**TIKU:** Same invariant with protothreads: an open/launch must not run inside the shell process's tick; it must be queued to a worker or split across yields.  

#### AW-037 · important · open
**A launch that fails is retried once after force-mime-setting files that have no type or a generic one.**  
SniffIfGeneric removes kAttrMIMEType and calls update_mime_info when the node has no type or B_FILE_MIME_TYPE; the launch loop runs at most twice (mimesetIt guard).  
`FSUtils.cpp` → `_TrackerLaunchDocuments / _TrackerLaunchAppWithDocuments / SniffIfGeneric`  
**TIKU:** Type sniffing has no analogue for device nodes, which carry a declared descriptor type; decide whether unknown files get sniffed at all.  

#### AW-038 · polish · open
**When launching documents, the application itself is removed from the list of refs handed to it so it never opens itself, and a lone double-clicked app is launched with no refs.**  
Compares BEntry(appRef, true) with BEntry(ref, true) for each ref (symlink aware) and removes matches; refsToPass is NULL when nothing is left, which also suppresses the 'find another app' offer.  
`FSUtils.cpp` → `_TrackerLaunchDocuments`  

#### AW-039 · important · open
**Launch failures produce a specific diagnosis rather than a generic error, and most offer a Find button that opens the Open With window.**  
Distinct alerts for B_UNKNOWN_EXECUTABLE, B_LEGACY_EXECUTABLE, B_LAUNCH_FAILED_EXECUTABLE (offers to clear the x bits and retry), B_LAUNCH_FAILED_APP_IN_TRASH, B_LAUNCH_FAILED_APP_NOT_FOUND, and B_MISSING_SYMBOL / B_MISSING_LIBRARY which name the symbols/libraries via LoaderErrorDetails. Read-only volume + B_LAUNCH_FAILED_EXECUTABLE retries with a guessed MIME type.  
`FSUtils.cpp` → `_TrackerLaunchDocuments / LoaderErrorDetails`  

#### AW-040 · core · open
**The Open With list is built from the union of every selected document's supporting apps, its preferred app, and superhandlers; if nothing but superhandlers was found the list widens to every application on the system.**  
AddOneRefSignatures pushes each doc's preferred-app signature and its type's supporting apps and calls NonGenericFileFound for non-generic types; AddSupportingAppForTypeToQuery(B_FILE_MIMETYPE) adds superhandlers. Rewind builds an OR-ed query over kAttrAppSignature and appends ConditionalAllAppsIterator, which only iterates while ShowAllApplications() (canAddAllApps && !fFoundOneNonSuperHandler) is true — and must come last for filtering to be correct.  
`OpenWithWindow.cpp` → `OpenWithPoseView::InitDirentIterator / SearchForSignatureEntryList::Rewind / ConditionalAllAppsIterator::Iterate`  
**TIKU:** Query-driven discovery over an attribute index has no direct analogue; the rewrite needs an explicit handler registry keyed by descriptor type.  

#### AW-041 · important · open
**The Open With list filters out non-executables, non-application files, apps in the Trash, and every copy of the Tracker except the running one; in the widened all-apps mode it also drops background-only and argv-only apps and apps with no signature.**  
CanOpenWithFilter: IsExecutable + MimeType == B_APP_MIME_TYPE + !FSInTrashDir; Tracker special case compares against the active app_info ref; ShowAllApplications() branch checks B_BACKGROUND_APP / B_ARGV_ONLY and empty signature. Apps with kNoRelation are dropped unless in all-apps mode.  
`OpenWithWindow.cpp` → `SearchForSignatureEntryList::CanOpenWithFilter / OpenWithRefFilter::Filter`  

#### AW-042 · important · open
**Open With rows are sorted by how well the app matches (then by name) and a Relation column explains each row in words.**  
Relation order kNoRelation < kSuperhandler < kSupportsSupertype < kSupportsType < kPreferredForType < kPreferredForFile; columns Name/Relation/Location/Version with primary sort = relation, secondary = name. Descriptions: "Preferred for file", "Preferred for %type", "Handles %type" (short description of the type), "Handles any %type" (supertype), "Handles any file", "Does not handle file".  
`OpenWithWindow.cpp` → `OpenWithPoseView::SetupDefaultColumnsIfNeeded / SearchForSignatureEntryList::Relation / SearchForSignatureEntryList::RelationDescription`  

#### AW-043 · important · open
**The preferred application is preselected in the Open With window, but only when all selected documents agree on one.**  
TrySettingPreferredApp/TrySettingPreferredAppForFile increment a counter; GetPreferredApp returns true only when the count is exactly 1. CreatePoses selects the pose whose entry_ref equals fPreferredRef.  
`OpenWithWindow.cpp` → `SearchForSignatureEntryList::GetPreferredApp / OpenWithPoseView::CreatePoses`  

#### AW-044 · important · open
**"Open and make preferred" sets the chosen app as the preferred handler for every distinct type among the opened documents, and records the app hint for its signature.**  
Collects unique types with AddOneUniqueDocumentType, then for each installed type SetPreferredApp(appSignature) and mime.SetTo(appSignature).SetAppHint(appRef); then performs the normal open.  
`OpenWithWindow.cpp` → `OpenWithContainerWindow::MakeDefaultAndOpen / SetDefaultAppForOneType`  

#### AW-045 · important · open
**The Open With buttons live-update: Open is disabled with no selection, and "Open and make preferred" is disabled for an app that does not handle the type or that is already the preferred app.**  
Evaluated in Pulse(); SetCanOpen / SetCanSetAppAsDefault; kNoRelation -> can open but cannot make preferred; when a common preferred app exists the button enables only if the selected ref differs from it.  
`OpenWithWindow.cpp` → `OpenWithPoseView::Pulse / OpenWithContainerWindow::SetCanOpen / SetCanSetAppAsDefault`  

#### AW-046 · important · open
**Choosing an app that does not claim the document's type warns and requires confirmation before opening; the warning is skipped when all documents are of generic type.**  
OpenWithRelation(model) == kNoRelation && !fIterator->GenericFilesOnly() -> alert with Cancel (Escape) / Open, default Cancel. A missing application file gives a "Could not find application" alert instead.  
`OpenWithWindow.cpp` → `OpenWithPoseView::OpenSelection`  

#### AW-047 · important · open
**The Open With window is single-selection, list-mode only, non-editable, non-draggable, has no menu bar and no context menu, and Escape cancels it.**  
fMultipleSelection=false, fDragEnabled=false, fSavePoseLocations=false, SetPoseEditing(false); RestoreState forces kListMode; ShowContextMenu is a no-op; ShouldAddMenus false; KeyDownFilter turns bare Escape into kCancelButton. Its state is saved to a shared "OpenWithSettings" template node, not per-folder. Tab moves focus instead of moving between poses.  
`OpenWithWindow.cpp` → `OpenWithPoseView::OpenWithPoseView / OpenWithContainerWindow::KeyDownFilter / OpenWithContainerWindow::SaveState / OpenWithPoseView::KeyDown`  

#### AW-048 · important · open
**The Open With submenu separates apps by relation, disambiguates identically named apps by [volume name] or full path, marks the preferred app, and shows a disabled "no supporting apps" item when empty.**  
Built lazily as a BSlowMenu; sorted first by name+volume to detect repeats, labels computed, then re-sorted by relation+name; a separator is inserted at each relation change; label truncated at kMaxMenuWidthFactor (33) * default label spacing; each item's message carries the handler ref and the parent window's node_ref as "nodeRefsToClose".  
`OpenWithWindow.cpp` → `OpenWithMenu::DoneBuildingItemList / SortByRelationAndName`  

#### AW-049 · core · open
**Tracker serves open/save panels to other applications; the panel window runs inside the requesting app's team, not in Tracker.**  
BFilePanel constructs a TFilePanel (a BContainerWindow subclass) and raises that app's file-descriptor limit to 512; SetClientObject/SetIsFilePanel. Because be_app is not a TTracker there, Tracker-only paths (node-monitor bumping, icon preloader, option-close-parent) degrade.  
`FilePanel.cpp` → `BFilePanel::BFilePanel`  
**TIKU:** In TikuOS the picker is a shell mode/service in one process; the 'runs in the caller' property disappears, but the degradation rules it forced (no privileged helpers available) should be re-checked.  

#### AW-050 · important · open
**The panel's start directory falls back from the requested directory to the user's home to the boot volume root.**  
Requested entry must SetTo OK and IsDirectory; otherwise B_USER_DIRECTORY; otherwise BVolumeRoster boot volume root.  
`FilePanelPriv.cpp` → `TFilePanel::TFilePanel`  

#### AW-051 · core · open
**Open panels reply B_REFS_RECEIVED with the chosen refs; save panels reply B_SAVE_REQUESTED with a "directory" ref plus a "name" string; cancelling a client-owned panel sends B_CANCEL carrying the original what and the panel pointer.**  
Default fMessage is chosen by mode; QuitRequested with fClientObject hides the window, calls WasHidden(), and sends B_CANCEL with int32 "old_what" and pointer "source". Without a client object the window really quits.  
`FilePanelPriv.cpp` → `TFilePanel::TFilePanel / TFilePanel::QuitRequested / TFilePanel::HandleSaveButton`  

#### AW-052 · core · open
**Saving refuses illegal or conflicting destinations: the system root, "." and "..", an unopenable folder, and a name already used by a folder; an existing file prompts Replace / Cancel with Cancel first.**  
Each refusal shows a centered alert and re-selects the text; the replace prompt returning 0 (Cancel) aborts, otherwise the app is left to overwrite. Success adds "directory" and "name" and, if fHideWhenDone, quits the window.  
`FilePanelPriv.cpp` → `TFilePanel::HandleSaveButton / TFilePanel::ShowCenteredAlert`  

#### AW-053 · important · open
**The default button's label and enabled state track the selection against the requested node flavors, and a single selected folder always enables it (relabelled "Open" in a save panel).**  
AdjustButton runs after every B_KEY_DOWN/B_MOUSE_DOWN via DispatchMessage; in a save panel the button follows the text field being non-empty; with no selection the button is enabled only if B_DIRECTORY_NODE is requested; both the unresolved and link-resolved flavor are tested per selected item.  
`FilePanelPriv.cpp` → `TFilePanel::AdjustButton / TFilePanel::DispatchMessage / GetLinkFlavor`  

#### AW-054 · polish · open
**In a save panel, selecting a single non-directory file copies its name into the save text field without stealing focus.**  
Only when the pose view has focus and exactly one pose is selected; a selected directory instead enables the button and relabels it Open.  
`FilePanelPriv.cpp` → `TFilePanel::AdjustButton`  

#### AW-055 · core · open
**Double-clicking a folder (or a link to one) in a panel always navigates into it rather than choosing it; if several are selected the first is entered.**  
B_REFS_RECEIVED handler resolves the link, and if IsDirectory() calls SwitchDirectory. For files, a message carrying "TrackerViewToken" (i.e. from the pose view) is ignored when the default button is disabled. Attempting to save more than one item shows "saving more than one item is not allowed".  
`FilePanelPriv.cpp` → `TFilePanel::MessageReceived (case B_REFS_RECEIVED)`  

#### AW-056 · important · open
**Everything chosen through a panel, plus the folder it was chosen from, is added to the recent documents / recent folders lists.**  
OpenSelectionCommon adds each ref via BRoster::AddToRecentFolders or AddToRecentDocuments, then always AddToRecentFolders(TargetModel()->EntryRef()).  
`FilePanelPriv.cpp` → `TFilePanel::OpenSelectionCommon`  

#### AW-057 · important · open
**Panel keyboard rules: Cmd+W or Escape cancels, Cmd+Up goes to the parent, Cmd+Down opens the selected folder, Cmd+D Desktop, Cmd+H home, Cmd+Shift+A select-by-expression, Shift+Delete deletes; Escape first commits an in-progress rename or clears type-ahead filtering.**  
key_down_filter handles Cmd+Up and Cmd+w itself and, for bare Escape, prefers CommitActivePose(false), then cancelling type-ahead filtering, then cancelling the panel; Return commits an active pose. The shortcut set is registered in Init and re-added after directory switches and AddPosesCompleted because the kit drops them.  
`FilePanelPriv.cpp` → `key_down_filter / TFilePanel::Init / TFilePanel::SwitchDirectory / BFilePanelPoseView::AddPosesCompleted`  

#### AW-058 · polish · open
**Cmd+D duplicates the selection instead of jumping to the Desktop when the pose view has focus and the selection can be duplicated.**  
kSwitchToDesktop handler checks PoseView()->IsFocus() && CanMoveToTrashOrDuplicate() and re-posts the message as kDuplicateSelection.  
`FilePanelPriv.cpp` → `TFilePanel::MessageReceived (case kSwitchToDesktop)`  

#### AW-059 · important · open
**Dropping one entry on a panel navigates to it; dropping a file navigates to its parent and selects the file, and in a save panel also fills in the file's name.**  
MessageDropFilter accepts exactly one ref, resolves symlinks to directories, and for non-directories schedules SelectChildInParent (delay 0 if already in that directory, else 100ms; period 200ms, timeout 5s) before switching. Drops of other counts are swallowed.  
`FilePanelPriv.cpp` → `TFilePanel::MessageDropFilter`  

#### AW-060 · important · open
**If the folder a panel is showing gets moved it follows it; if that folder is deleted the panel falls back to the boot volume root.**  
FSFilter on B_NODE_MONITOR: B_ENTRY_MOVED updates the model's entry ref and re-switches (deliberately not retitling); B_ENTRY_REMOVED switches to the boot root.  
`FilePanelPriv.cpp` → `TFilePanel::FSFilter`  
**TIKU:** Same rule needed for a device node that disappears while a picker is open — the picker must not be left pointing at a dead node.  

#### AW-061 · important · open
**With "Desktop is root" enabled, navigating up from the root lands on the Desktop and the panel's Desktop shows the integrated volumes; the setting change is picked up live from Tracker.**  
SwitchDirToDesktopIfNeeded maps the desktop dir, and (when the Disks icon is off) the root dir, onto the Desktop; OpenParent uses the desktop dir as the parent of the root; SwitchDirectory calls AddVolumePoses when the target is the Desktop. kDesktopFilePanelRootChanged arrives via BHandler::StartWatching on the Tracker messenger.  
`FilePanelPriv.cpp` → `TFilePanel::SwitchDirToDesktopIfNeeded / TFilePanel::OpenParent / TFilePanel::MessageReceived (B_OBSERVER_NOTICE_CHANGE)`  
**TIKU:** Cross-process settings observation becomes a watch on a settings node; the 'panel re-reads and re-navigates immediately' behavior should be kept.  

#### AW-062 · polish · open
**The panel's Favorites menu can add the current folder as a favorite and open the favorites folder for editing.**  
kAddCurrentDir creates a symlink named after the current folder inside <settings>/kGoDirectory; kEditFavorites sends that folder to the Tracker as B_REFS_RECEIVED. The Favorites submenu is also re-filtered whenever the panel's ref filter changes.  
`FilePanelPriv.cpp` → `TFilePanel::MessageReceived (kAddCurrentDir / kEditFavorites) / TFilePanel::SetRefFilter`  
**VERIFIER CORRECTION:** ContainerWindow.cpp:796-809 calls keymap.GetModifiedCharacters("+", B_SHIFT_KEY, 0, unmodified) — i.e. it collects the UNMODIFIED characters whose SHIFTED form is '+' (on a US layout, '='), and binds Cmd+<that unmodified key>. It also skips any key that already has a shortcut (if (!HasShortcut(key, 0))).  

#### AW-063 · important · open
**Panel geometry and columns persist across panels and applications in one shared settings template, and a panel with nothing restored is positioned like an alert relative to its parent window or the screen.**  
RestoreState/SaveState use DefaultStateSourceNode(kDefaultFilePanelTemplate); only kAttrWindowFrame is applied on restore, with SetSizeLimits(360,10000,200,10000). BFilePanel::Show positions via AlertPosition unless DefaultStateRestored(); a panel already open in another workspace is pulled to the current workspace rather than switching workspaces. State is saved once at construction if setup dirtied it, so one panel cannot clobber another's saved state.  
`FilePanelPriv.cpp` → `TFilePanel::RestoreState / TFilePanel::SaveState / BFilePanel::Show (FilePanel.cpp)`  

#### AW-064 · polish · open
**File panels are list mode only, never save icon positions, have no Window menu, no navigation submenu and no drop context menu, and their title reads "<Application>: Open" / "<Application>: Save".**  
BFilePanelPoseView::RestoreState forces kListMode, SavePoseLocations is a no-op, DisableSaveLocation() is called in Init; AddWindowMenu / SetupNavigationMenu / AddDropContextMenu are empty; title built from be_app's app_info (localized file name if preferred) plus the button label.  
`FilePanelPriv.cpp` → `TFilePanel::Init / TFilePanel::AddWindowMenu / TFilePanel::SetupNavigationMenu / BFilePanelPoseView::RestoreState`  

#### AW-065 · important · open
**Get Info is one window per node: asking again for an already-open node just activates the existing window, and several info windows cascade rather than stacking.**  
FindInfoWindow(model->NodeRef()) under the window-list lock; new windows get an index and offset by (index*8, index*8), with odd indices first shifted right by 320, and are pulled to (50,50) if they would land off-screen.  
`Tracker.cpp` → `TTracker::OpenInfoWindows / BInfoWindow::Show (infowindow/InfoWindow.cpp)`  

#### AW-066 · core · open
**The Get Info window has three tabs — Information, Permissions, Attributes — above a header showing the item's icon and name.**  
HeaderView + BTabView with GeneralInfoView (SetName "Information"), FilePermissionsView ("Permissions"), AttributesView (SetName "Attributes"). Shortcuts: Cmd+E edit name, Cmd+O open, Cmd+U unmount, Cmd+P jump to the Permissions tab.  
`infowindow/InfoWindow.cpp` → `BInfoWindow::BInfoWindow / BInfoWindow::MessageReceived (kPermissionsSelected)`  
**TIKU:** For a device node the 'Attributes' tab becomes the typed descriptor + capability bits view; Permissions maps onto capability bits rather than POSIX mode.  

#### AW-067 · core · open
**The Information tab shows Size (or Capacity for volumes/root), Created, Modified, Kind and Location, plus Link to for symlinks, Version and Description for executables, and Filesystem for volumes.**  
Field set and layout in Draw(); strings built in InitStrings — created/modified via WidgetAttributeText, Kind from the MIME short description falling back to the raw type, symlink target read raw from the link with " (broken)" appended when the target is missing, executable description from the long version string with newlines/tabs flattened, volume filesystem string as "<fsh_name> (block size: N[, indexed])".  
`infowindow/GeneralInfoView.cpp` → `GeneralInfoView::Draw / GeneralInfoView::InitStrings`  

#### AW-068 · important · open
**Folder sizes are computed in the background: the field reads "calculating…" until done, and clicking the size field (or the context-menu item) recalculates it.**  
CalcSize thread spawned in Show for directories (skipped for volumes/root, which use stat/volume data); fStopCalc plus wait_for_thread joins it before the window quits or a recalculation restarts. Clicking is only armed for directories that are not volumes or root. Get Info on the Trash sums every persistent volume's trash directory.  
`infowindow/InfoWindow.cpp` → `BInfoWindow::CalcSize / BInfoWindow::MessageReceived (kRecalculateSize) / GeneralInfoView::MouseDown / GeneralInfoView::MouseUp`  
**TIKU:** A recursive size walk over a device namespace must be interruptible the same way; the fStopCalc + join discipline is the part to keep.  

#### AW-069 · important · open
**Volume and root info shows "capacity (used used -- free free)" refreshed once a second, and file sizes are re-polled because they are not reported by node monitoring until the file is closed.**  
SetPulseRate(1000000) in the window; CheckAndSetSize returns early when free bytes are unchanged, prints "-" when capacity is 0, and for root sums FreeBytes/Capacity over all volumes.  
`infowindow/GeneralInfoView.cpp` → `GeneralInfoView::CheckAndSetSize / GeneralInfoView::Pulse`  

#### AW-070 · core · open
**The item's name is renamed in place from the info window: click the title or press Cmd+E, Return commits, Escape cancels, and deactivating the window commits; a rejected name (too long / already in use) reopens the editor.**  
BeginEditingTitle builds a BTextView with DisallowFilenameKeys and a key filter; FinishEditingTitle calls EditModelName and reopens on B_NAME_TOO_LONG / B_NAME_IN_USE; MakeFocus(false) and WindowActivated(false) both commit.  
`infowindow/HeaderView.cpp` → `HeaderView::BeginEditingTitle / HeaderView::FinishEditingTitle / HeaderView::TextViewFilter`  
**TIKU:** Renaming a device node may be forbidden; the rewrite needs an explicit 'name is read-only' state instead of a silently failing editor.  

#### AW-071 · important · open
**Non-executable files get an "Opens with:" pop-up listing the type's supporting apps plus "Default application"; choosing one writes that file's preferred app immediately.**  
Menu built from BMimeType::GetSupportingApps, items labelled with the resolved application's file name (falling back to the signature), current preference pre-marked, "Default application" marked by default; kSetPreferredApp writes via BNodeInfo::SetPreferredApp and updates the model. Not offered for executables, directories or volumes.  
`infowindow/GeneralInfoView.cpp` → `GeneralInfoView::GeneralInfoView / GeneralInfoView::MessageReceived (kSetPreferredApp)`  

#### AW-072 · important · open
**Location and "Link to" are clickable: Location opens the item's parent folder with the item selected, Link to does the same for the target, and a broken link instead opens a file panel to pick a new target.**  
OpenParentAndSelectOriginal posts B_REFS_RECEIVED with the parent ref and "nodeRefToSelect" raw data; a missing target routes to BInfoWindow::OpenFilePanel. Fields invert while pressed and only fire if the mouse is still inside on release.  
`infowindow/GeneralInfoView.cpp` → `GeneralInfoView::OpenLinkSource / GeneralInfoView::OpenLinkTarget / GeneralInfoView::MouseUp`  

#### AW-073 · polish · open
**A truncated Location, Link to or Description reveals its full text in a floating pop-up when hovered, and that pop-up is itself clickable to follow the link; it disappears when the mouse leaves or the window deactivates.**  
OpenToolTipWindow creates a borderless floating window clamped to the screen containing a TrackingView; only opened when the string is wider than the available width and the info window is active with no button held. WindowActivated(false) quits all three pop-ups.  
`infowindow/GeneralInfoView.cpp` → `GeneralInfoView::MouseMoved / OpenToolTipWindow / TrackingView::MouseUp`  

#### AW-074 · important · open
**Re-pointing a broken symlink from the info window preserves the link's name and all of its attributes.**  
Node watching is stopped first (so the deletion does not close the window), the old attributes are copied into an AttributeStreamMemoryNode, the old link is removed, a new link with the same name is created via BDirectory::CreateSymLink, attributes are copied back, watching restarts and both views are re-linked. Triggered by kSetLinkTarget, by the file panel's reply, or by dropping an item on the window when the model is a symlink.  
`infowindow/InfoWindow.cpp` → `BInfoWindow::MessageReceived (case kNewTargetSelected / B_SIMPLE_DATA)`  

#### AW-075 · important · open
**The icon in the info window is draggable (Option makes it a copy), is a drop target, and double-clicking it opens the item; the Trash icon can only be opened, never dragged.**  
Drag starts after kDragSlop (3px) with a composed translucent icon+label bitmap, be:actions = B_COPY_TARGET when Option is held else B_MOVE_TARGET; drops are accepted through BPoseView::CanHandleDragSelection / HandleDropCommon and highlight the icon; fTrackingState is open_only_track for the Trash. Double-click posts B_REFS_RECEIVED with a TrackerViewToken back to be_app.  
`infowindow/HeaderView.cpp` → `HeaderView::MouseDown / HeaderView::MouseMoved / HeaderView::MouseUp / HeaderView::MessageReceived`  

#### AW-076 · important · open
**Right-clicking the info window icon gives a context menu with a navigation submenu for folders/volumes, Open, Edit name, Unmount (disabled for the boot volume), Identify (Option forces a re-identify), Empty Trash, Recalculate folder size, Set new link target and Permissions.**  
Items are conditional on the model: no Edit name/Unmount for desktop/root/trash, no Identify for root/volume/trash, Empty Trash only for the trash, size item only for non-volume non-root directories, link target only for symlinks. Identify calls update_mime_info with force level 2 when Option is down, else 1.  
`infowindow/HeaderView.cpp` → `HeaderView::BuildContextMenu / BInfoWindow::MessageReceived (kIdentifyEntry)`  
**VERIFIER CORRECTION:** Only half true. ActivateWindowFilter (ContainerWindow.cpp:143) does skip BPoseView and DraggableContainerIcon, but DraggableContainerIcon activates the window itself: MouseDown calls Window()->Activate(true) when the click misses the icon's opaque pixels, and MouseUp activates whenever no drag was started. So a plain click on the folder icon still activates on mouse-up; only a click that becomes a drag does not.  

#### AW-077 · important · open
**An info window closes itself when its item is deleted or its volume is unmounted, and follows renames and moves by updating the title and the Location field.**  
Watches B_WATCH_ALL | B_WATCH_MOUNT on the node; B_ENTRY_REMOVED for our node and B_DEVICE_UNMOUNTED for our device call Close(); B_ENTRY_MOVED/B_STAT_CHANGED/B_ATTR_CHANGED are forwarded to the general view first, then the header, then the permissions view (order matters, the general view mutates the model). Icon attribute changes invalidate the cached icon; a MIME type change re-reads the strings.  
`infowindow/InfoWindow.cpp` → `BInfoWindow::MessageReceived (B_NODE_MONITOR) / GeneralInfoView::ModelChanged`  
**TIKU:** Maps cleanly onto TIKU_EVENT_VFS, but note the ordering dependency between the three sub-views — that must be reproduced or removed deliberately.  

#### AW-078 · important · open
**The Permissions tab is a 3x3 read/write/execute grid applied the moment a box is toggled, with owner and group fields that only accept numeric ids and are disabled unless you are root or the owner.**  
Checkboxes map to S_IRUSR..S_IXOTH and call BNode::SetPermissions with the recomposed mode; failures re-read the node and beep. Owner/group parsed with sscanf("%d"); a non-numeric entry re-reads and beeps. Text controls disabled when teamInfo.uid != 0 && nodeOwner != teamInfo.uid; the whole grid is hidden if permissions cannot be read.  
`infowindow/FilePermissionsView.cpp` → `FilePermissionsView::ModelChanged / FilePermissionsView::MessageReceived`  
**TIKU:** Direct analogue is the VFS node's capability bits; the immediate-apply-and-beep-on-failure interaction is the part to preserve.  

#### AW-079 · important · open
**The Attributes tab lists every attribute with its name, a decoded value and a human-readable type name; BEOS:TYPE is hidden, long strings are truncated and multi-valued blobs collapse to "<N values>".**  
Iterates GetNextAttrName; strings read into a 64-byte buffer; per-type decoding for bool/int8..int64/uint8..uint64/float/double/rect/time (32- and 64-bit time both handled, formatted with BDateTimeFormat); unknown types show "<N bytes of data>"; ~50 type names mapped, unknown -> "(unknown)". Column order shown is Name, Value, Type.  
`infowindow/AttributesView.cpp` → `AttributesView::AttributesView`  
**TIKU:** This is the natural home for a device node's typed descriptor dump (type, unit, range, capability bits) — the row set is richer than BFS attributes, not poorer.  

#### AW-080 · important · done
**The Select window matches names by starts with / ends with / contains / wildcard / regular expression, defaults to wildcard with case ignored and invert off, and applies to the window it was opened from.**  
Popup index maps to kStartsWith/kEndsWith/kContains/kGlobMatch/kRegexpMatch (item 3, wildcard, marked by default); Ignore case checkbox starts checked; posts kSelectMatchingEntries with ExpressionType, Expression, InvertSelection, IgnoreCase to fParentWindow.  
`SelectionWindow.cpp` → `SelectionWindow::SelectionWindow / SelectionWindow::MessageReceived / SelectionWindow::ExpressionType`  

#### AW-081 · important · differs
**The Select window hides rather than closes, so the expression and options survive for the next use, and it opens centred on the mouse but clamped 20px inside the screen.**  
QuitRequested hides and returns false; MoveCloseToMouse centres on the pointer, clamps to the screen and pins to the current workspace. It is a subset window of its parent, switched to normal feel when the parent is the Desktop (a floating subset window would never come to front over the desktop). Hide() must happen before PostMessage so the target window is active when the selection message arrives.  
`SelectionWindow.cpp` → `SelectionWindow::QuitRequested / SelectionWindow::MoveCloseToMouse / SelectionWindow::MessageReceived`  
**PORT:** The expression, rule and checkboxes survive closing and the placement clamp is Tracker's 20 px.  The window is closed and remade rather than hidden, and this window system has no subset/floating feel to switch.

#### AW-082 · polish · open
**Other applications can ask Tracker to select named files: Tracker finds the open parent window, activates it, and selects each entry by an anchored regular expression on its name.**  
kSelect builds "^name$" with kRegexpMatch, InvertSelection false, IgnoreCase false; the window must be activated to populate the pose list and the code snoozes 100 ms before posting.  
`Tracker.cpp` → `TTracker::SelectRefs`  
**TIKU:** The 100ms sleep is a race workaround — the rewrite should wait on the pose-list-populated event instead of sleeping.  

#### AW-083 · core · partial
**Deferred and periodic work runs on a separate low-priority background thread, never on a window thread; window threads only ever queue tasks.**  
StandAloneTaskLoop spawns "TrackerTaskLoop" at B_LOW_PRIORITY lazily on the first AddTask; the Tracker's fTaskLoop is created with keepThread=true so the thread persists when the queue empties, while other loops let the thread exit. fLock (BLocker) guards the task list and Pulse() asserts it is held.  
`TaskLoop.cpp` → `StandAloneTaskLoop::StartPulsingIfNeeded / StandAloneTaskLoop::Run / TaskLoop::Pulse`  
**TIKU:** With cooperative protothreads there is no preemptive worker; deferred work becomes a timer-driven protothread, and every task body must be yield-safe and short.  
**MISSING:** The timer-driven deferred/periodic loop the TIKU note asks for genuinely exists and is tested, but no shipped binary uses it — nothing outside tests includes tiku_trk_task.h — and the apps' actual deferred work (the 400 ms backend pump, the rename pulse) runs inline in the window event loop, which is exactly what the row forbids.  
*src/tiku_trk_task.c:299 tiku_trk_loop_run (ordered due list); grep shows tiku_trk_task.h included by no other src file, only tests/test_task.c; src/tiku_trk_desktop.c:2262 inline `if (tiku_trk_now() - last_pump > 400000)` pump in the X event loop*

#### AW-084 · important · done
**Four task shapes exist: run once after a delay, run periodically until the task reports itself done, run periodically with a hard timeout, and run only when the machine has been idle.**  
OneShotDelayedTask, PeriodicDelayedTask (removed when the functor result is true), PeriodicDelayedTaskWithTimeout (also removed once currentTime > fTimeoutAfter), RunWhenIdleTask. RunIfNeeded returning true means 'remove me'.  
`TaskLoop.cpp` → `TaskLoop::RunLater / TaskLoop::RunWhenIdle / PeriodicDelayedTaskWithTimeout::RunIfNeeded`  
**TIKU:** Maps onto tiku_timer EVENT/CALLBACK modes; the 'functor returns done' contract has no timer equivalent today and must be added.  

#### AW-085 · important · partial
**Idle-only work waits for CPU load under 15% and for the system to have been idle at least as long as requested; any burst of activity restarts the wait.**  
ActivityLevel() averages per-CPU active_time; load = delta/elapsed minus a task-overhead allowance (kIdleTreshold subtracted when checking whether an already-running task is still idle); also requires idle_time() >= fIdleFor. State machine kInitialDelay -> kInitialIdleWait -> kInIdleState.  
`TaskLoop.cpp` → `RunWhenIdleTask::RunIfNeeded / RunWhenIdleTask::IsIdle`  
**TIKU:** Idle detection on TikuOS is 'no process ready and no timer armed' — the power-managed idle hook, not CPU load; the semantics differ enough to need a decision.  
**MISSING:** The idle-duration half is implemented (a TASK_IDLE body is skipped while now - last_activity < idle_for and note_activity restarts the wait), but the row's other stated gate — CPU load under 15% — has no counterpart or substitute signal of any kind, and no app calls note_activity, so the activity-restart clause is inert outside tests.  
*src/tiku_trk_task.c:319-324 (TASK_IDLE gate), :177-183 tiku_trk_loop_note_activity; only caller is tests/test_task.c:161; no load/utilisation metric anywhere in src*

#### AW-086 · important · partial
**The task thread sleeps exactly until the next task is due (or one heartbeat, whichever is sooner) and is woken immediately when a new task is added.**  
snoozeTill = min(LatestRunTime() - 1000, now + fHeartBeat), snooze_until on B_SYSTEM_TIMEBASE, or snooze(1000) if already past; AddTask suspends/snoozes 1ms/resumes the thread when get_thread_info reports B_THREAD_ASLEEP.  
`TaskLoop.cpp` → `StandAloneTaskLoop::Run / StandAloneTaskLoop::AddTask`  
**TIKU:** This is exactly the tickless-idle discipline: compute the next deadline rather than polling. Should reuse the existing tickless machinery instead of a heartbeat.  
**MISSING:** The next-deadline computation exists and is tested (tiku_trk_loop_next_due returns microseconds to the soonest task, -1 when nothing is scheduled, and there is no heartbeat cap), but no caller sleeps on it — both event loops usleep(10000) unconditionally every cycle — and there is no wake-on-add because there is no sleeping thread to wake.  
*src/tiku_trk_task.c:tiku_trk_loop_next_due; src/tiku_trk_desktop.c:2343 and src/tiku_trk_app.c:1581 (usleep(10000))*

#### AW-087 · important · done
**Repeated similar requests collapse into a single pending task instead of queueing up, bounded by both a maximum accumulation count and a maximum accumulation window.**  
AccumulatedRunLater scans for an AccumulatedOneShotDelayedTask whose functor CanAccumulate the new one; accumulating resets fRunAfter to now+delay, so a stream of updates keeps deferring until the count or the time bound is hit.  
`TaskLoop.cpp` → `TaskLoop::AccumulatedRunLater / AccumulatedOneShotDelayedTask::CanAccumulate / AccumulatedOneShotDelayedTask::Accumulate`  
**TIKU:** Directly relevant to VFS watch events, which explicitly do not coalesce today; if the rewrite adds coalescing it must keep the max-delay bound so updates cannot starve.  
**PORT:** run_accumulating scans the pending list for a live task under the same key, consults can_accumulate, folds via acc and slides due to now+delay, and both stated bounds are enforced — accumulated >= max_count and now > first_queued + max_us each break out and queue a fresh task instead.  
*src/tiku_trk_task.c:204-239 (count bound :208, window bound :211, deadline slide :225, first_queued stamped :238); tests/test_task.c:94-133*

#### AW-088 · important · open
**File panels do not spawn a task thread; their deferred work is pumped from the window's own pulse.**  
TFilePanel creates a PiggybackTaskLoop; PulseMe runs Pulse() at most once per heartbeat and stops pulsing when the queue empties (KeepPulsingWhenEmpty false).  
`TaskLoop.cpp` → `PiggybackTaskLoop::PulseMe / TFilePanel::TFilePanel (FilePanelPriv.cpp)`  
**TIKU:** This is the model TikuOS should follow everywhere — deferred work driven from an existing tick rather than a new thread.  

#### AW-089 · polish · open
**Destroying a task loop asks the worker to quit and waits up to ten seconds before giving up on it.**  
fNeedToQuit set under the lock, then up to 10000 iterations of snooze(1000) waiting for fScanThread to become -1; the worker clears fScanThread and returns when it sees fNeedToQuit or when Pulse() reports the queue is empty and it should not keep pulsing.  
`TaskLoop.cpp` → `StandAloneTaskLoop::~StandAloneTaskLoop / StandAloneTaskLoop::Run`  
**CHECKED:** loop_free walks the task list, frees each task and frees the loop, and returns — there is no quit request, no handshake and no bounded wait of any duration, so the row's rule has no implementation rather than an alternative one.  
*src/tiku_trk_task.c:87-102 tiku_trk_loop_free*

#### AW-090 · important · open
**Temporary queries older than a week that are not open in any window are deleted automatically, but only while the machine is idle and never in the first half hour after boot.**  
RunWhenIdle(worker, 30*60s initial delay, 5*60s idle requirement, 10s heartbeat); a query must be MoreOptionsStruct::QueryTemporary, older than kDaysToExpire (7) by ctime, and !EntryHasWindowOpen before entry.Remove(). Batched kBatchCount refs per invocation.  
`FindPanel.cpp` → `DeleteTransientQueriesTask::StartUpTransientQueryCleaner / DeleteTransientQueriesTask::ProcessOneRef / DeleteTransientQueriesTask::QueryOldEnough`  
**TIKU:** Background deletion of user data on an idle timer is a policy decision worth re-confirming; on a durable-NVM device the write cost also matters.  

#### AW-091 · important · open
**Icons for the apps and preferences folders are pre-cached in the background about ten seconds after startup, and only for Tracker and the Deskbar — never for a file panel hosted in someone else's application.**  
InitIconPreloader checks be_app is a TTracker or has the Deskbar signature; NodePreloader::Preload snoozes 100x100ms first, then watches each directory and preloads mini icons for files whose icon source is unknown, keeping the models and updating the cache on attribute/stat changes and dropping them on removal.  
`NodePreloader.cpp` → `NodePreloader::Preload / NodePreloader::PreloadOne / InitIconPreloader (Tracker.cpp)`  

#### AW-092 · important · partial
**Single-window browsing: opening a folder from a folder window replaces that window's contents instead of opening a new window; holding Option overrides it, and the Desktop and file panels never behave this way.**  
Requires SingleWindowBrowse, !IsDesktopView, !IsFilePanel, no Option key, the current target is a directory and the opened pose resolves to a directory; then posts kSwitchDirectory instead of opening. In single-window mode Option-open also stops adding "nodeRefsToClose", so the parent is not closed.  
`PoseView.cpp` → `BPoseView::OpenSelection / BPoseView::OpenSelectionCommon`  
**MISSING:** Replace-in-place browsing exists in the single-window binary — opening a container saves state, switches the view's directory and retitles the same window — but there is no SingleWindowBrowse setting, no Option-key override to open a new window instead (open_selection ignores modifiers), and no desktop/file-panel exemption; the spatial shell is a separate binary that always opens a new window.  
*src/tiku_trk_app.c:open_selection (tiku_trk_poseview_switch_dir)*

#### AW-093 · core · done
**Opening something that already has a window open in the current workspace activates that window instead of opening a second one; windows on other workspaces are left alone so the workspace does not jump.**  
OpenContainerWindow loops FindContainerWindow(nodeRef, n) and activates every match whose Workspaces() includes the current workspace, then deletes the model and returns. The Desktop model in single-window mode is exempt from this rule. checkAlreadyOpen is skipped for the Open With window.  
`Tracker.cpp` → `TTracker::OpenContainerWindow`  
**PORT:** open_folder's first loop matches an existing folder by path with a live open window, activates it and returns that folder instead of creating a second one; the shell has no workspaces, so the workspace-scoping clause cannot be violated.  
*src/tiku_trk_desktop.c:1079-1087 (path match + win->open -> tiku_trk_desk_activate, early return)*

#### AW-094 · important · done
**A newly opened container window adopts its model, and if the pose view fails to construct the window quits itself rather than showing an empty frame.**  
Window subclass chosen by model kind (query -> BQueryContainerWindow, virtual directory -> VirtualDirectoryWindow, Open With -> OpenWithContainerWindow which adopts a cloned refs message, else BContainerWindow); CreatePoseView under LockLooper, then a kRestoreState message optionally carrying a "state" sub-message.  
`Tracker.cpp` → `TTracker::OpenContainerWindow`  
**PORT:** The folder record owns path, pose view and window and is handed to the window as its draw context, and the ordering makes the empty frame impossible: the pose view is built first and NULL returns before any window exists, and if desk_open then fails the view is freed and no window is left up; saved state is restored immediately after construction.  
*src/tiku_trk_desktop.c:1095-1128 (poseview_new -> NULL return; restore_state; desk_open -> poseview_free + NULL return; folder_count++ only on success)*

#### AW-095 · important · partial
**Moving items to the Trash from outside a window goes through the open window when there is one, so the next item gets selected; the rest are trashed asynchronously in one batch.**  
kMoveSelectionToTrash: refs whose parent window is open use PoseView()->MoveEntryToTrash(&ref); all others accumulate into a BObjectList passed to FSMoveToTrash.  
`Tracker.cpp` → `TTracker::MoveRefsToTrash`  
**MISSING:** Trashing a multi-item selection does go through one batched filesystem job, but the routing rule is missing: there is no entry point for refs from outside a window, nothing routes a trash through the window that holds the item, and no next item is selected — remove_pose explicitly clears the active pose to -1 when the removed row was active.  
*src/tiku_trk_app.c:handle_key XK_Delete -> src/tiku_trk_fs.c:tiku_trk_fs_trash; src/tiku_trk_poseview.c:remove_pose*

#### AW-096 · important · open
**Unmounting from the mount menu saves every window's icon positions before the volume goes away, and active query windows on a device can be closed first (waiting up to three seconds) so they do not block the unmount.**  
kUnmountVolume calls SaveAllPoseLocations() then falls through to forwarding the message to the mount server; CloseActiveQueryWindows posts B_QUIT_REQUESTED to each query window active on the device then polls QueryActiveForDevice 30 times at 100 ms. SaveAllPoseLocations uses BDeskWindow::SaveDesktopPoseLocations for the desktop.  
`Tracker.cpp` → `TTracker::MessageReceived (kUnmountVolume) / TTracker::CloseActiveQueryWindows / TTracker::SaveAllPoseLocations`  
**TIKU:** Device removal is the direct analogue: pending state must be flushed to NVM and dependent views torn down before the node disappears.  

#### AW-097 · polish · open
**Volume free-space bars refresh once per second, only while the setting is enabled, and refresh immediately when the setting or its color changes.**  
SetPulseRate(1000000) in the constructor; Pulse() returns early unless ShowVolumeSpaceBar(), then gPeriodicUpdatePoses.DoPeriodicUpdate(false); kShowVolumeSpaceBar / kSpaceBarColorChanged force DoPeriodicUpdate(true).  
`Tracker.cpp` → `TTracker::Pulse / TTracker::MessageReceived`  
**TIKU:** A once-per-second poll is a power cost on a microwatt target; prefer a watch notification on the volume node.  

#### AW-098 · polish · open
**A generated thumbnail replaces the placeholder icon in whichever open window is showing that file.**  
kUpdateThumbnail carries a node_ref; Tracker walks every container window under the window-list lock, locks each, and calls UpdateIcon on the first matching pose, then stops.  
`Tracker.cpp` → `TTracker::MessageReceived (case kUpdateThumbnail)`  

#### AW-099 · important · open
**Tracker exposes a scripting interface: empty the Trash, create a folder, get the window for a folder, open a window on a folder, and show the preferences window.**  
kTrackerPropertyList: Trash{B_DELETE_PROPERTY}, Folder{B_CREATE_PROPERTY,B_GET_PROPERTY,B_EXECUTE_PROPERTY} with B_REF_TYPE data, Preferences{B_EXECUTE_PROPERTY}; all B_DIRECT_SPECIFIER only. get/execute Folder reply with a "window" messenger or an "error" int32; scripting messages are dispatched before the normal switch in MessageReceived.  
`TrackerScripting.cpp` → `TTracker::HandleScriptingMessage / TTracker::ExecuteProperty / TTracker::GetProperty / kTrackerPropertyList`  
**TIKU:** This is the seam that matches the TikuOS shell: the same operations should be reachable as shell verbs and as namespace writes, not through a separate scripting suite.  

#### AW-100 · polish · done
**Command-line arguments handed to Tracker are resolved against the sender's working directory first, then as absolute paths, and each is opened.**  
Requires a "cwd" string in the current message; BEntry(&workingDirectory, argv[i]) then get_ref_for_path fallback; index starts at 1 (argv[0] skipped).  
`Tracker.cpp` → `TTracker::ArgvReceived`  
**PORT:** The multi-window shell no longer ignores paths: every non-flag argument from i = 1 is collected into openme[] (bounded by TIKU_TRK_WIN_MAX) and each one gets its own open_folder() call, cascaded, with the HOME default only when none was given. argv[0] is still skipped and relative paths still resolve through the process cwd in the backend with absolute paths passing through unchanged, which is the two-stage resolution collapsed onto a CLI launcher. The flag chain is not broken by the new branch — "-seconds" and the other flags start with '-' so they fall past the path branch, and their values are consumed by argv[++i]. Residual, not the row's clause: the single-window tracker binary still does start = argv[i] last-wins (src/tiku_trk_app.c:1103), so with two paths it shows the last rather than the first; it has exactly one view, so it cannot open a window per path either way.

#### AW-101 · polish · open
**The preferences window is a singleton that is hidden rather than destroyed, and is re-shown or merely activated on subsequent requests; a request can jump straight to a specific page.**  
ShowSettingsWindow creates it once; if hidden -> Show, else Activate. kRunAutomounterSettings shows it and then selects TrackerSettingsWindow::kAutomountSettings.  
`Tracker.cpp` → `TTracker::ShowSettingsWindow / TTracker::MessageReceived (kRunAutomounterSettings)`  

#### AW-102 · polish · open ⟨found by verification⟩
**Tracker add-ons get user-assignable Command shortcuts that run the add-on against the current selection.**  
AddOneShortcut posts kLoadAddOn with the add-on's ref via window->AddShortcut(key, modifiers, ...); AddOnShortcuts/ApplyShortcutPreferences maintain the key per add-on and RemoveShortcut/re-add on change, with kDefaultModifiers as the default chord.  
`DeskWindow.cpp` → `AddOneShortcut / RevertToDefault / BDeskWindow::ApplyShortcutPreferences`  
**TIKU:** The analogue is binding a shell verb to a key against the selected namespace node.  

#### AW-103 · polish · open ⟨found by verification⟩
**In a file panel Cmd-Up opens the parent folder and Cmd-W cancels the panel, regardless of what the pose view would do with those keys.**  
key_down_filter is a window-level common filter: with B_COMMAND_KEY it maps B_UP_ARROW to kOpenParentDir and 'w' to kCancelButton and returns B_SKIP_MESSAGE, so the pose view never sees them; it also swallows Escape (see the corrections).  
`FilePanelPriv.cpp` → `key_down_filter`  

#### AW-104 · polish · open ⟨found by verification⟩
**The Desktop window creates the replicant shelf and its own view-mode shortcuts, and skips the shelf entirely in safe mode.**  
BDeskWindow::Init: unless BootedInSafeMode(), it creates/open()s the shelf file under the Tracker settings dir and builds a BShelf on the pose view with SetDisplaysZombies(true); then adds Cmd+1 (icon), Cmd+2 (mini icon), Cmd++ / Cmd+- (scale) shortcuts — which show up in the Desktop context menu even though those menu items are not what invokes them; then injects the root "/" pose when "show Disks icon" is on.  
`DeskWindow.cpp` → `BDeskWindow::Init`  

#### AW-105 · polish · done ⟨found by verification⟩
**The Select window is reused rather than destroyed: closing it hides it, so the previous expression, rule and checkboxes are still there next time; it opens centred on the mouse but clamped 20 px inside the screen.**  
SelectionWindow::QuitRequested calls Hide() and returns false; the kSelectButtonPressed path also Hides before posting (order matters so the target window activates). MoveCloseToMouse centres on the pointer then clamps x/y with MAX(20, MIN(screen - 20 - size, pos)) and pins the window to the current workspace.  
`SelectionWindow.cpp` → `SelectionWindow::QuitRequested / SelectionWindow::MoveCloseToMouse`  
**TIKU:** A shell `select` mode keeps the last expression as its default — the same "sticky panel" behavior with no window.  


---

## Deskbar: the whole application

Architecture notes: [`spec/ARCH-deskbar.md`](spec/ARCH-deskbar.md)

#### DB-001 · core · partial
**The leaf (Deskbar) menu always opens with the same fixed head: About this system / Find… / Show replicants / [Mount] / Deskbar preferences… / separator / Shutdown…**  
Built in one shot as the first build state (kStart); 'About Haiku' vs 'About this system' chosen by HAIKU_DISTRO_COMPATIBILITY_OFFICIAL; Mount submenu only when MOUNT_MENU_IN_DESKBAR is defined  
`DeskbarMenu.cpp` → `TDeskbarMenu::AddStandardDeskbarMenuItems`  
**TIKU:** 'Mount' has no analogue; a TikuOS build would expose mounted namespaces/volumes from the VFS root instead  
**MISSING:** The leaf menu is a fixed one-shot head (About this system / Find… / sep / Deskbar preferences… / sep / /sys /dev /data / sep / Shut down…) with Mount correctly adapted to the VFS roots, but 'Show replicants' is absent entirely and 'Deskbar preferences…' is a dead label (index 3 falls to `default: break` in the click switch).  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_draw (items[] at line 570) and tiku_trk_deskbar_click (switch at line 684)*

#### DB-002 · core · open
**Below the fixed head the leaf menu shows Recent documents / Recent folders / Recent applications, then a separator, then the live contents of the user's Deskbar folder as a navigable file menu**  
Three-phase state machine kStart -> kAddingRecents -> kAddingDeskbarMenu -> kDone; the whole recents block (both separators) is omitted when all three recent types are disabled; each enabled type is added individually  
`DeskbarMenu.cpp` → `TDeskbarMenu::AddNextItem`  
**TIKU:** The folder-backed part becomes a VFS directory listing; recents need a kernel-side MRU list since there is no BRoster  
**VERIFIER CORRECTION:** Only the LEADING separator is skipped. In TDeskbarMenu::AddNextItem the `AddSeparatorItem()` before the recents submenus is inside the `if (enabled)` branch, but the trailing `AddSeparatorItem()` is unconditional — a separator is always emitted before the Deskbar-folder contents even when all three recent types are disabled.  

#### DB-003 · important · open
**The Deskbar folder shown in the leaf menu is ~/config/settings/deskbar/menu, falling back to the system data directory copy if the user one is missing**  
Resolved fresh on every menu open in MenusBeginning; kDeskbarMenuEntriesFileName == "menu_entries"; if neither exists the menu is not populated at all  
`BarWindow.cpp` → `TBarWindow::MenusBeginning`  
**TIKU:** Path becomes a VFS path; the two-tier user/system fallback is worth keeping  
**VERIFIER CORRECTION:** TBarWindow::MenusBeginning appends kDeskbarMenuEntriesFileName ("menu_entries") to GetDeskbarSettingsDirectory() → ~/config/settings/deskbar/**menu_entries**, with the system fallback /system/data/deskbar/menu_entries; the entry is opened with symlink traversal. The path "~/config/settings/deskbar/menu" is B_USER_DESKBAR_DIRECTORY, which is the DROP target used by AddRefsToDeskbarMenu — a different constant from the one that supplies the menu contents.  

#### DB-004 · polish · open
**If the Deskbar folder is empty the menu shows a single disabled '<Deskbar folder is empty>' item rather than an empty menu**  
Only when the built item list is empty; otherwise the normal BNavMenu sort/finish runs  
`DeskbarMenu.cpp` → `TDeskbarMenu::DoneBuildingItemList`  

#### DB-005 · important · open
**The leaf menu is rebuilt from disk every time it is opened, so files added to the Deskbar folder appear without restarting Deskbar**  
ClearMenuBuildingState sets fMenuBuilt=false; MenusEnded additionally calls sDeskbarMenu->ForceRebuild() under the menu's looper lock  
`DeskbarMenu.cpp` → `TDeskbarMenu::ClearMenuBuildingState`  
**TIKU:** With VFS watch notifications this could become event-driven instead of rebuild-on-open  

#### DB-006 · important · partial
**Choosing the 'Shutdown…' parent item asks for confirmation; choosing 'Power off' or 'Restart system' inside its submenu acts immediately**  
The parent item's message is kShutdownSystem with "confirm"=true; submenu items carry bare kShutdownSystem/kRebootSystem; both route to BRoster::Private::ShutDown(reboot, confirm, false)  
`DeskbarMenu.cpp` → `TDeskbarMenu::AddStandardDeskbarMenuItems`  
**TIKU:** Maps to a TikuOS power/reset request; the confirm flag distinction should survive  
**MISSING:** A single 'Shut down…' item exists and acts immediately (case 9 -> TIKU_TRK_DB_QUIT -> app.running = 0), but there is no confirmation on the parent item and no Power off / Restart submenu, so the confirm-vs-immediate distinction the row is about does not exist.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_click case 9; src/tiku_trk_desktop.c:main TIKU_TRK_DB_QUIT arm*

#### DB-007 · important · open
**'Show replicants' is a checkable item that toggles the dragger handles of every replicant system-wide, and its checkmark reflects the current global state when the menu opens**  
kToggleDraggers -> BDragger::ShowAllDraggers()/HideAllDraggers(); mark set from BDragger::AreDraggersDrawn()  
`BarApp.cpp` → `TBarApp::MessageReceived case kToggleDraggers`  
**TIKU:** Needs an equivalent global 'show handles' flag on the namespace-hosted widgets  

#### DB-008 · important · open
**While files are being dragged, every fixed leaf-menu item (About, Find, Show replicants, Power off, Restart) is disabled, leaving only folder entries and recents as drop targets**  
item->SetEnabled(!dragging) on each, where dragging == fBarView->Dragging()  
`DeskbarMenu.cpp` → `TDeskbarMenu::AddStandardDeskbarMenuItems`  

#### DB-009 · important · open
**Dropping files on the leaf logo creates symlinks to them in the Deskbar folder; dropping on a folder entry inside the menu creates the symlinks in that subfolder; dropping on a file entry launches it with the dropped refs**  
TBarMenuTitle::Invoke calls HandleDeskbarMenu(NULL) (top level); a B_REFS_RECEIVED retargeted to the bar view carries the destination ref — directory => AddRefsToDeskbarMenu, file => SendDragMessage(NULL,&ref) which FSLaunchItem's it. In vertical mini-mode a drop landing in the team title is ignored  
`BarView.cpp` → `TBarView::HandleDeskbarMenu`  
**TIKU:** Symlink creation needs a VFS link primitive; if absent, record the target path in a menu manifest instead  

#### DB-010 · polish · done
**In vertical expanded mode the leaf menu pops out to the side of the bar (right of it when docked left, left of it when docked right) instead of below the title**  
point = supermenu rect.RightTop()+ (0,3) when left, rect.LeftTop()-(menuWidth,0)+(0,3) when right; all other modes use BMenu::ScreenLocation()  
`DeskbarMenu.cpp` → `TDeskbarMenu::ScreenLocation`  
**PORT:** The hardcoded x is gone. A new shared helper leaf_menu_rect() computes the panel from the dock: LEFT_EXPANDED puts it at frame.x + frame.w + 2 (right of the bar, which sits at x=0), otherwise frame.x - m.w - 2 (left of the bar for RIGHT_EXPANDED and RIGHT_MINI), with a clamp at 0 for a screen too narrow to hold it. Crucially the draw path and the click hit-test now call the SAME helper with the same rowh/rows, so the popup and its hit rectangle can no longer disagree. It still pops to the side of the bar rather than below the title, as the row requires; y = LEAF_H + 2 sits just under the leaf rather than the original's top+3, a 20-odd-pixel difference in the same direction, not a placement change. Not fixed, but not part of this row: the window menu (frame.x - 130) and the clock calendar (frame.x - 190) are still hardcoded left of the bar and go off-screen at negative x when docked left.

#### DB-011 · important · open
**Pressing the keyboard Menu key with no other key held opens the leaf menu; Deskbar can also be told to open the running-apps menu the same way**  
input_server sends 'BeMn' (kShowDeskbarMenu) to the Deskbar signature; TBarApp locks the bar window and calls ShowDeskbarMenu(), which StartMenuBar(0, sticky, showMenu). kShowTeamMenu ('TmMn') opens index 0 of the key menu bar, or index 2 when there is no bar menu bar  
`BarApp.cpp` → `TBarApp::MessageReceived case kShowDeskbarMenu`  

#### DB-012 · polish · open
**Cmd+F anywhere in Deskbar opens Tracker's Find window, and the leaf menu's Find… item does the same**  
AddShortcut('F', B_COMMAND_KEY, kFindButton) in the window constructor; the kFindButton case forwards the message to the Tracker signature; ResetTargets re-points the Find item at Tracker every open  
`BarWindow.cpp` → `TBarWindow::TBarWindow`  
**TIKU:** Retarget at whatever TikuOS provides for search, or drop  

#### DB-013 · important · open
**Leaf-menu file/folder items are targeted at Tracker when it is running and at Deskbar itself when it is not, and every settings-toggle item is re-targeted at the application on each open**  
DefaultTarget() returns BMessenger(kTrackerSignature) if valid else BMessenger(be_app); ResetTargets walks the top-level items and re-points kShowSplash/kToggleDraggers/kConfigShow/kAlwaysTop/kExpandNewTeams/kHideLabels/kResizeTeamIcons/kSortRunningApps/kTrackerFirst/kRebootSystem/kSuspendSystem/kShutdownSystem/kRealignReplicants/kShowHideTime/kShowSeconds/kShowDayOfWeek/kShowTimeZone/kGetClockSettings at be_app — it does NOT recurse into submenus  
`DeskbarMenu.cpp` → `TDeskbarMenu::ResetTargets`  

#### DB-014 · core · open
**Every running application appears once in the list; background apps and Deskbar itself never appear**  
Refused when (flags & B_BACKGROUND_APP) != 0 or signature matches kDeskbarSignature case-insensitively; the initial list is seeded from be_roster->GetAppList() at startup and kept live by be_roster->StartWatching(this)  
`BarApp.cpp` → `TBarApp::AddTeam`  
**TIKU:** Replace roster watching with the TikuOS process registry plus watch notifications on a /proc-like subtree  

#### DB-015 · core · partial
**Launching a second instance of an already-running application adds it to the existing entry rather than creating a new one; the entry disappears only when its last instance quits**  
Matched case-insensitively by signature into BarTeamInfo::teams; second and later launches broadcast kAddTeam, and quits broadcast kRemoveTeam until the last one, which broadcasts B_SOME_APP_QUIT and deletes the BarTeamInfo  
`BarApp.cpp` → `TBarApp::RemoveTeam`  
**MISSING:** A repeat launch folds into the existing entry (name scan returns the existing index instead of adding a row), but matching is case-sensitive rather than case-insensitive and there is no instance count and no removal path at all — no remove_team, no team_count decrement anywhere — so the 'disappears when its last instance quits' clause is missing.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_add_team (dedupe loop line 99); no remove_team in src*

#### DB-016 · important · open
**With 'Tracker always first' on, Tracker is pinned to the head of the list; with 'Sort applications by name' on, the rest are inserted in locale-collated name order**  
Insertion sort against existing item labels using BCollator from BLocale::Default(); the sort skips index 0 when Tracker is pinned there; a full re-sort (fTeamList.SortItems(TTeamMenu::CompareByName)) happens on rebuild  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::AddTeam`  
**TIKU:** Pinning the file manager first is a policy that should be a named setting, not a hardcoded signature  

#### DB-017 · important · open
**Application icons are drawn at a user-chosen size from 16 to 96 pixels in steps of 8, and changing the size re-lays out the whole bar**  
kResizeTeamIcons carries "be:value" (slider notches); iconSize = value*8 clamped to [16,96]; unchanged values are a no-op; in horizontal mode the leaf menu and tray are also re-placed. Icons are cached per size in BarTeamInfo::iconCache[11]  
`BarApp.cpp` → `TBarApp::MessageReceived case kResizeTeamIcons`  

#### DB-018 · polish · done
**An application with no icon of its own falls back to the generic application icon, and if even that is unavailable the slot is drawn fully transparent rather than garbage**  
be_roster GetAppInfo -> BAppFileInfo::GetIcon, else BMimeType(B_APP_MIME_TYPE)::GetIcon, else fill bits with B_TRANSPARENT_32_BIT (or B_TRANSPARENT_MAGIC_CMAP8)  
`BarApp.cpp` → `TBarApp::_CacheTeamIcon`  
**TIKU:** Icon comes from the device/app descriptor in the namespace instead of a file's attribute  
**PORT:** The opaque blue placeholder rect + frame is deleted. The row now paints only tiku_trk_icon_paint("folder" for Tracker's own team, "application" for everything else); tiku_trk_icons_bitmap() returns NULL for an unknown name or a failed HVIF parse/render, paint() returns 0 without touching a pixel, so the slot keeps the already-drawn row background — the port's equivalent of B_TRANSPARENT bits showing the parent through — with no garbage and no fake icon. The chain is two-step rather than three (there is no per-app icon lookup; every non-Tracker team gets the generic one), but that is the TIKU note on the row, not the clause that was missing.

#### DB-019 · important · partial
**Double-clicking an application in the list activates that application; a single click just opens its window menu**  
Requires clicks>1, the same item as the previous click, and delta <= get_click_speed(); activates team 0 of the group via be_roster->ActivateApp. fLastClickTime is deliberately NOT refreshed when the double-click fires, so a triple click does not re-trigger  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::MouseDown`  
**MISSING:** The window menu opens on the *secondary* button on a team row (win_menu_team = i), and the double-click-activates-the-application clause is entirely missing — deskbar_click has no click count parameter and no activate-app action exists (the double-click plumbing in desktop.c serves folder rows only).  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_click lines 716-723*

#### DB-020 · important · open
**Cmd+Ctrl+Shift click on an application (the 'Vulcan Death Grip') kills every team behind it immediately, with no quit request**  
kill_team() per team, then B_SOME_APP_QUIT to be_app and B_SOME_APP_QUIT (single team) or kRemoveTeam (multiple) with "itemIndex" to the owning menu so it removes the item without waiting for the roster  
`TeamMenuItem.cpp` → `TTeamMenuItem::HandleMouseDown`  
**TIKU:** Needs a force-terminate capability bit; guard it behind a capability rather than a modifier combo alone  

#### DB-021 · important · partial
**Middle-clicking an application launches a new instance of it; Ctrl+click brings all its windows to front; Ctrl+Shift+click minimizes them all; Shift+middle-click on a window entry closes that window**  
Middle-click requires Shift NOT held; Ctrl paths post kBringTeamToFront/kMinimizeTeam with "itemIndex" to the menu, which resolves the item and calls TeamShowHideCommon with the item's screen frame for the zoom animation. The window-close path builds a BMessenger from client_window_info team/port/token and sends B_QUIT_REQUESTED  
`TeamMenuItem.cpp` → `TTeamMenuItem::HandleMouseDown`  
**MISSING:** Only the close-a-window clause exists, remapped to right-click on a window row; there is no middle-button path at all (desktop.c derives only `secondary = button == 3`), Ctrl on a team row does nothing, and Ctrl+Shift minimize-all is absent (Hide all lives in the window menu, not as a chord).  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_click lines 716-741; src/tiku_trk_desktop.c:main line 1595*

#### DB-022 · important · partial
**In vertical expanded mode with 'Show application expander' on, each app grows a triangle on its right edge; clicking it expands the app's windows inline underneath it, and the triangle animates down on press before committing on release**  
Expander hit area is the rightmost kSwitchWidth (12px) of the item frame; mouse-down sets B_RIGHT_DOWN_ARROW and grabs pointer events, mouse-up calls ToggleExpandState(true) and settles on B_DOWN_ARROW / B_RIGHT_ARROW. Expanding MOVES the window items out of the submenu into the bar (all but the trailing hide/show/close/separator four)  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::MouseUp`  
**MISSING:** The triangle is drawn in the rightmost EXPANDER_W of each team row, the same strip hit-tests as the expander, and toggling it nests that app's windows inline underneath — but the state flips on the single click event, so the press-time down-arrow / commit-on-release animation is missing (and there is no 'Show application expander' setting to turn it off).  
*src/tiku_trk_deskbar.c:draw_expander + tiku_trk_deskbar_click line 717*

#### DB-023 · polish · partial
**With 'Expand new applications' on, applications launched from then on come up already expanded**  
On first build, expandNewTeams expands each item; on later launches the newly added item is toggled. Turning 'Show application expander' off collapses any item that was expanded, on the next rebuild  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::BuildItems`  
**MISSING:** A newly registered application does come up expanded (add_team sets expanded = 1 unconditionally), but there is no 'Expand new applications' setting to turn that off and no collapse-of-expanded-items when the expander is disabled, since no expander setting exists.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_add_team line 110*

#### DB-024 · core · differs
**Expanded window lists refresh themselves about 7 times a second: windows that open, close, get minimized, or move to another workspace update in place without any user action**  
monitor_team_windows loop: snooze(150000); marks every window item RequiresUpdate, SetTo()s the survivors, inserts new ones at InsertIndexFor, deletes the ones still flagged, then Invalidate() and SizeWindow(1) if anything was added/removed. Runs only in vertical mode (StartMonitoringWindows on layout change to column, Stop on row)  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::monitor_team_windows`  
**TIKU:** Polling can be replaced by watch notifications on the window/session namespace; keep the coalescing so a burst of changes causes one relayout  
**PORT:** There is a real refresh mechanism, not an absence: publish_windows re-derives the whole window list from the desktop's z-order on every repaint and set_windows swaps the rows while the expanded flag lives on the team and survives, so opens, closes and minimizes update in place with no user action — the beat is the 100 Hz dirty-repaint loop rather than a 150 ms monitor thread, which is the notification-driven substitution the row's TIKU note invites.  
*src/tiku_trk_desktop.c:1184-1206 publish_windows called first in draw_all :1211; src/tiku_trk_deskbar.c:159-187 set_windows (t->expanded untouched); main loop usleep(10000) at :2343*

#### DB-025 · polish · done
**A window entry's title has the owning application's name stripped off both ends, so 'Foo: Untitled' and 'Untitled - Foo' both show as 'Untitled'**  
RemoveFirst("<AppName>: ") and RemoveLast(" - <AppName>") applied to wInfo->name before SetTo/creation  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::monitor_team_windows`  

#### DB-026 · important · done
**Window entries carry an icon that distinguishes shown/minimized and this-workspace/other-workspace, four states in all**  
FetchWindowIcon(local, minimized) indexes R_WindowShownIcon / R_WindowHiddenIcon / R_WindowShownSwitchIcon / R_WindowHiddenSwitchIcon; 'local' means ((1<<current_workspace()) & wInfo->workspaces) != 0. Icons are vector, rasterized and cached per font size  
`BarApp.cpp` → `TBarApp::FetchWindowIcon`  
**TIKU:** Workspace concept may not exist; the shown/minimized distinction still should  

#### DB-027 · important · open
**Only ordinary, non-hidden windows are listed — panels, floaters and hidden windows never appear in an application's window list**  
feel must be B_NORMAL_WINDOW_FEEL or kWindowScreenFeel, and show_hide_level <= 0  
`WindowMenu.cpp` → `TWindowMenu::WindowShouldBeListed`  

#### DB-028 · important · partial
**An application's window menu ends with Hide all / Show all / Close all; 'Hide all' is disabled when every window is already minimized and 'Show all' when none is**  
miniCount compared against itemCount; if the app has no windows at all the menu instead shows a disabled 'No windows' plus a 'Quit application' item — which is suppressed for Tracker. The three controls are omitted entirely while dragging  
`WindowMenu.cpp` → `TWindowMenu::AttachedToWindow`  
**MISSING:** The menu is exactly Hide all / Show all / sep / Close all and all three invoke correctly, but no item is ever disabled — the panel is drawn with tiku_desk_ui_menu(..., hot = -1), which has no disabled-item concept — so the 'Hide all greyed when all minimized / Show all when none is' clause and the empty-app 'No windows' + 'Quit application' variant are absent.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_draw line 582-588 and tiku_trk_deskbar_click lines 662-674; ../desktop/src/tiku_desk_ui.h:111*

#### DB-029 · polish · done
**Closing all windows of Tracker sends Tracker a private 'close all Tracker windows' command instead of quitting it**  
command switches from B_QUIT_REQUESTED to 'Tall' when the running app's signature matches kTrackerSignature  
`ShowHideMenuItem.cpp` → `TShowHideMenuItem::TeamShowHideCommon`  
**TIKU:** Keep as a per-app override hook rather than a hardcoded signature  

#### DB-030 · important · partial
**Clicking a window entry brings that window to the front (Ctrl+click minimizes it) and zooms the window frame out of / into the menu item's rectangle**  
do_window_action(fID, action, zoomRect, doZoom); zoom only when the state actually changes (minimized->front or shown->minimize); zoomRect is the item frame converted to screen, taken from the parent team item when not expanded. Invoke is a no-op while dragging  
`WindowMenuItem.cpp` → `TWindowMenuItem::Invoke`  
**MISSING:** Clicking a window entry raises it (un-minimize + desk_activate) and Ctrl+click minimizes it, but there is no zoom animation of the window frame out of / into the item rectangle — the only 'zoom' in the port is the tab's maximize button — and no dragging no-op guard.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_click lines 728-739; src/tiku_trk_desktop.c:main DB_ACTIVATE_WINDOW/DB_MINIMIZE_WINDOW arms*

#### DB-031 · polish · done
**Window entries are kept in natural-sort order by title (so 'Doc 2' sorts before 'Doc 10')**  
InsertIndexFor scans from startIndex until NaturalCompare(existing, new) > 0; TWindowMenu::AttachedToWindow uses it for the submenu, the monitor thread for inline items  
`WindowMenuItem.cpp` → `TWindowMenuItem::InsertIndexFor`  

#### DB-032 · polish · partial
**Application and window labels are truncated to the available width, and hovering an item whose label was truncated (or any app item when names are hidden) shows the full name as a tooltip**  
Truncation budget is menu MaxContentWidth minus padding, minus kSwitchWidth when the expander is showing; the tooltip is set/cleared on B_INSIDE_VIEW transitions and only when the hovered item changed  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::MouseMoved`  
**UNCONFIRMED:** MouseMoved contains only the tooltip half of the row. No truncation budget is computed there; the cited symbol does not implement the cited rule (the rule itself is real, but in TTruncatableMenuItem::Label(float) in TruncatableMenuItem.cpp).  
**MISSING:** Truncation with an ellipsis against a real width budget is implemented for both app names and window titles, and the budget correctly subtracts the expander width — but the tooltip half is missing entirely: the deskbar has no hover/MouseMoved path and the string 'tooltip' does not appear anywhere in src.  
*src/tiku_trk_deskbar.c:fit_label, called at lines 488 and 511*

#### DB-033 · important · open
**In horizontal mode the application buttons shrink as more apps launch and grow back as they quit, between a font/icon-derived minimum and 1.25x that; once even the minimum will not fit, scroll arrows appear at the ends of the list**  
CheckItemSizes recomputes on every add/remove (delta +1/-1) using MinHorizontalItemWidth/MaxHorizontalItemWidth against MaxHorizontalWidth (screen width minus drag region and leaf menu); CheckForSizeOverrunHorizontal drives TInlineScrollView::AttachScrollers/DetachScrollers through TBarView::CheckForScrolling  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::CheckItemSizes`  

#### DB-034 · important · open
**In vertical mode scroll arrows appear as soon as the app list runs past the bottom of the screen, and the mouse wheel over the list scrolls it — a modifier key (Option/Cmd/Ctrl) or Shift on the arrows scrolls three times as fast**  
CheckForSizeOverrunVertical compares Window()->Frame().bottom against the screen frame; default small step 19px, large step = 3x small; detaching the scrollers resets the scroll position to the start  
`ExpandoMenuBar.cpp` → `TExpandoMenuBar::MessageReceived case B_MOUSE_WHEEL_CHANGED`  

#### DB-035 · important · open
**Dragging files over the application list highlights the app under the cursor and dropping hands the files to it; apps that cannot open the dragged types are drawn/behaved as unavailable unless Ctrl is held to force acceptance**  
Types are cached once per drag (CacheDragData/SpringLoadedFolderCacheDragData); AppCanHandleTypes checks the app's supported types via BAppFileInfo::IsSupportedType, always true for Tracker, always true with B_CONTROL_KEY. Drop sends B_REFS_RECEIVED to a running app, else launches it with the refs. Leaving the view or a drop-less mouse-up clears the highlight and the cached drag  
`BarView.cpp` → `TBarView::AppCanHandleTypes`  
**TIKU:** Type matching must come from the namespace descriptor's declared types; capability bits could gate whether a target may receive a drop at all  

#### DB-036 · polish · open
**Hovering off an open Deskbar submenu does not close it while the cursor is still within 20px of the menu, or over the leaf menu or any application item**  
MenuTrackingHook returns 'end the menu' only when the point is outside the menu bounds inset by -kMenuTrackMargin (20) AND not inside the deskbar menu frame AND not over a team item  
`BarView.cpp` → `TBarView::MenuTrackingHook`  

#### DB-037 · core · partial
**In mini mode the application list collapses into a second icon next to the leaf logo which pops up the running-apps menu; with no apps running that menu shows a disabled 'no application running'**  
PlaceDeskbarMenu adds the team title in kMiniState and removes it in vertical expando (replacing it with a separator in horizontal); the title is inserted at index 0 when docked left, index 1 when docked right. TTeamMenu rebuilds its whole item list in AttachedToWindow and computes a max content width from the widest app name (capped at gMaximumWindowWidth)  
`BarView.cpp` → `TBarView::PlaceDeskbarMenu`  
**MISSING:** Only the collapse half exists — in TIKU_TRK_DB_RIGHT_MINI the draw path sets rows = 0 and lifts the tray to the leaf — and it is unreachable in practice (no caller of tiku_trk_deskbar_set_dock anywhere); there is no second team-title icon beside the leaf and no running-apps popup, so the 'no application running' disabled item does not exist either.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_draw line 445 and 524; no set_dock caller in src*

#### DB-038 · core · partial
**Replicants can be added to the tray by any application, by archived view or by add-on file, and are addressable afterwards by numeric id or by view name**  
kMsgAddView/kMsgAddAddOn -> TBarView::AddItem -> TReplicantTray::AddIcon/LoadAddOn; add-ons must export instantiate_deskbar_entry(image_id, entry_ref*, w, h) or instantiate_deskbar_item(w, h); duplicate view names are refused; the id is returned in the reply, or an "error" field on failure  
`StatusView.cpp` → `TReplicantTray::LoadAddOn`  
**TIKU:** This is the biggest namespace-shaped change: a tray item becomes a VFS node with a typed descriptor and capability bits instead of a loaded shared object; 'add by ref' becomes 'add by node path'  
**MISSING:** The blessed TikuOS reshape is present — a tray item is a namespace path whose typed descriptor is resolved once at subscribe time and then formatted — but the addressability clause is missing: add_replicant returns bare 0/-1 rather than an id, there is no lookup by id or view name, no duplicate-name refusal, and no runtime add path (the only calls are three fixed device paths at startup).  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_add_replicant; src/tiku_trk_desktop.c:main lines 1286-1291*

#### DB-039 · important · open
**Add-on replicants survive a reboot: their paths are stored in a 'replicants' settings file and reloaded at startup, and any that no longer load are quietly dropped from the file**  
kReplicantSettingsFile = "replicants", field kReplicantPathField = "replicant_path"; InitAddOnSupport walks the stored paths and removes entries whose entry is missing or whose load failed; _SaveSettings rewrites the file on every add/remove and on shutdown  
`StatusView.cpp` → `TReplicantTray::InitAddOnSupport`  
**TIKU:** Persist node paths in a durable settings cell; re-resolve each path at boot and prune dangling ones the same way  

#### DB-040 · important · open
**Deleting or moving an add-on's file on disk updates the tray live — the replicant is removed when its file is deleted (unless the owning app is still running) and merely re-pointed when the file is moved**  
watch_node(B_WATCH_NAME|B_WATCH_ATTR) per add-on item; B_ENTRY_REMOVED -> UnloadAddOn unless be_roster->IsRunning(&item->entryRef); B_ENTRY_MOVED -> MoveItem updates entryRef.directory only  
`StatusView.cpp` → `TReplicantTray::HandleEntryUpdate`  
**TIKU:** Node monitoring maps directly onto VFS watch notifications; the 'don't remove while the owner is running' rule still applies  

#### DB-041 · important · open
**A replicant taller than the tray's maximum replicant height is refused outright, and one that changes its own size after being added forces a full re-layout of the tray**  
AcceptAddon rejects on replicantFrame.Height() > fMaxReplicantHeight; AddIcon snapshots the archived "_frame" rect and calls RealignReplicants when the view's bounds differ afterwards (works around replicants that resize in AttachedToWindow). fMaxReplicantHeight = max(gMinReplicantHeight, team icon size) capped at TabHeight-1; fMaxReplicantWidth is fixed at 129  
`StatusView.cpp` → `TReplicantTray::AcceptAddon`  

#### DB-042 · core · differs
**Tray icons pack left-to-right and, in vertical mode, wrap onto additional rows; the first row leaves room for the clock, and the tray (and therefore the whole Deskbar) grows and shrinks to fit**  
LocationForReplicant scans each row for free space past the right edge of every replicant already on that row (gap sIconGap), reserving fClockMargin + clock width on row 0 when the clock is visible; horizontal mode simply chains off the previous replicant. Removing one re-flows everything from that index and AdjustPlacement re-runs the whole window placement pipeline  
`StatusView.cpp` → `TReplicantTray::LocationForReplicant`  
**PORT:** The tray layout is genuinely re-shaped for label+value namespace readings — one full-width row per replicant with the clock in its own reserved band — and the bar height is recomputed from tray_count plus CLOCK_H on every draw, so the tray and the whole Deskbar size themselves to their contents; left-to-right packing and wrapping have no meaning for full-width rows.  
*src/tiku_trk_deskbar.c:449-450 (frame.h = ... + tray_count * TRAY_ROW + CLOCK_H + PAD) and :530-548 tray rows, :550+ clock band*

#### DB-043 · important · open
**Right-clicking the tray — or pressing and holding on it for twice the double-click time — opens the clock menu, or a lone 'Show clock' item when the clock is hidden**  
MouseDown polls the mouse every 50ms and bails if it moves more than 4px in either axis; B_SECONDARY_MOUSE_BUTTON opens immediately. With the clock visible it delegates to TTimeView::ShowTimeOptions at the same screen point  
`StatusView.cpp` → `TReplicantTray::MouseDown`  

#### DB-044 · core · partial
**The clock shows short date+time and can additionally show seconds, day of week and time zone; its tooltip is the full long-form date, and it repaints once a second**  
BDateTimeFormat with fields HOUR|MINUTE plus SECOND/WEEKDAY/TIMEZONE; window pulse rate is set to 1000000us; the view only redraws when the formatted string actually changed, and re-measures/re-centers itself when it does. A leading zero is stripped from the tooltip date  
`TimeView.cpp` → `TTimeView::Pulse`  
**TIKU:** Needs a wall-clock source and locale formatting; on a microwatt target a 1 Hz repaint is a power decision worth revisiting  
**MISSING:** The clock renders short date + time ("7 Aug  16:26"), but there are no seconds / day-of-week / time-zone options, no long-form-date tooltip, and no 1 Hz repaint — it is redrawn only when something else dirties the surface, so with no tray replicants the displayed minute can go stale.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_draw lines 549-566; src/tiku_trk_desktop.c:main dirty-driven draw loop*

#### DB-045 · important · partial
**Left-clicking the clock pops up a calendar window; clicking it again just re-activates the existing one, and the calendar closes as soon as it loses focus or Esc is pressed**  
ShowCalendar sends B_SET_PROPERTY Active=true to the existing window if its messenger is still valid; the window flips above the clock when it would fall off the bottom of the screen and further flips left/up in Show() by screen quadrant; B_CLOSE_ON_ESCAPE plus Cmd+W; in focus-follows-mouse mode the very first deactivation is swallowed (fSuppressFirstClose)  
`TimeView.cpp` → `TTimeView::ShowCalendar`  
**MISSING:** Clicking the clock band opens the calendar and the next click anywhere dismisses it, but a second click does not re-activate an existing window (it closes it), and there is no Esc or focus-loss close — tiku_trk_deskbar_calendar_open has no caller and no XK_Escape path touches the deskbar.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_click lines 658-661 and 744-746*

#### DB-046 · polish · open
**The calendar has -/+ steppers for month and year and clicking a day moves the selection, with the month name column sized so it never reflows**  
kMonthDown/Up and kYearDown/Up apply BDate::AddMonths/AddYears; invalid dates are ignored; min column width = plain font width of "September"  
`CalendarMenuWindow.cpp` → `CalendarMenuWindow::MessageReceived`  

#### DB-047 · important · open
**Hiding the clock persists the choice and tells the Time preferences application, so the two stay in sync in both directions**  
ShowHideTime flips fTime's hidden state, re-flows the tray, writes Settings()->showClock, and sends kShowHideTime with "showClock" to application/x-vnd.Haiku-Time; conversely kGetClockSettings is answered with showClock/showSeconds/showDayOfWeek/showTimeZone. Clock options live in their own clock_settings file, saved from SaveTimeSettings on quit  
`StatusView.cpp` → `TReplicantTray::ShowHideTime`  

#### DB-048 · core · open
**Dragging the tray's dragger handle re-docks Deskbar into one of ten screen zones: four vertical mini corners, four horizontal mini corners, and left/right vertical expanded or top/bottom horizontal expanded**  
CalculateRegions divides the screen into quarters horizontally and halves vertically; corner zones are menuBarHeight tall (horizontal) or menuBarHeight..2.9x menuBarHeight (vertical); MouseMoved tests the regions in a fixed short-circuit order and calls ChangeState(..., async=true) only when the target differs from the current state. Regions are recomputed on every ScreenChanged  
`StatusView.cpp` → `TDragRegion::MouseMoved`  

#### DB-049 · polish · open
**Holding Ctrl+Cmd and clicking anywhere on Deskbar starts the same dock-drag as grabbing the dragger handle**  
Exact modifier match (Ctrl|Cmd with neither Option nor Shift) and only when the click is inside the window; forwards to fDragRegion->MouseDown at the dragger's top-left. The dragger's own hit area is DragRegion() inset by -2 because that rect is sized for drawing  
`BarView.cpp` → `TBarView::MouseDown`  

#### DB-050 · important · open
**In vertical mode a resize strip along the inner edge lets the user drag the bar wider or narrower, clamped between the minimum and twice the minimum width, and the width is remembered across restarts**  
TResizeControl::MouseMoved resizes (and also moves, when docked right) the window by the cursor delta, refusing to grow past gMaximumWindowWidth or shrink below gMinimumWindowWidth; FrameResized writes Settings()->width, pushes the new max content width into the expando bar and re-runs UpdatePlacement. The control is hidden in horizontal mode  
`StatusView.cpp` → `TResizeControl::MouseMoved`  

#### DB-051 · important · open
**Auto-hide slides Deskbar off to a 1px sliver when the pointer leaves its neighbourhood and brings it back when the pointer touches the screen edge it lives on**  
Hidden size is kHiddenDimension (1), except horizontal expanded which keeps full screen width; re-show requires the pointer to be exactly on a screen edge inside the window frame; hiding requires the pointer to be more than kMaxPreventHidingDist (80px) outside the window. Suppressed while a menu or the calendar is open  
`BarView.cpp` → `TBarView::MouseMoved`  

#### DB-052 · important · open
**Auto-raise floats Deskbar above other windows when the pointer touches its screen edge (and while any of its menus is open) and drops it back afterwards, without changing which window is active**  
RaiseDeskbar(true) sets floating feel then immediately normal feel; RaiseDeskbar(false) does SendBehind(itself). Auto-raise is mutually exclusive with always-on-top (the setter forces it off, and the preference checkbox disables) and is suppressed while a mouse-down that started outside the window is still held  
`BarView.cpp` → `TBarView::RaiseDeskbar`  

#### DB-053 · important · open
**Deskbar refuses to be minimized, refuses to be closed, and Cmd+Q closes its preferences window instead of quitting**  
Minimize(bool) only forwards a false; QuitRequested returns false when the message carries "shortcut"; a genuine system shutdown saves settings first. The window is created with B_NOT_CLOSABLE|B_NOT_MINIMIZABLE|B_NOT_MOVABLE|B_AVOID_FRONT on B_ALL_WORKSPACES  
`BarApp.cpp` → `TBarApp::QuitRequested`  

#### DB-054 · core · open
**Ctrl+Tab from any window opens the application switcher; holding Ctrl keeps it on screen, Shift reverses direction, and releasing Ctrl within ~180ms instead performs an instant switch to the previous application without ever showing the panel**  
BWindow::_Switcher sends 'TASK' with key/modifiers/when/team; MainEntry busy-waits up to 180000us in 20ms steps and calls QuickSwitch if the key or Ctrl is released first. Repeats are filtered by fSkipUntil and by the semaphore. Ctrl+` (rawKey 0x11) cycles windows of the current app instead of applications  
`Switcher.cpp` → `TSwitchManager::MainEntry`  
**TIKU:** Requires a system-wide hotkey path from the window/input layer to Deskbar; the timing constants are load-bearing for the feel  

#### DB-055 · important · open
**The switcher orders applications most-recently-used-first, but only re-sorts when it has been idle for more than 0.4 seconds, so repeated fast switching keeps a stable order**  
_SortApps uses BPrivate::get_application_order(current_workspace()) and appends anything not in that list; triggered only when now > fLastSwitch + 400000, which also resets the quick-switch anchor to the current app/window  
`Switcher.cpp` → `TSwitchManager::_SortApps`  

#### DB-056 · important · open
**While the switcher is up: arrows and Tab move between apps and windows, Esc cancels, Space/Enter commits, Q quits the selected app and H hides it**  
Right/Left(or '1') = CycleApp, Up/Down = CycleWindow (no wrap), Tab = CycleApp honouring Shift, Esc = Stop(false), Space/Enter = Stop(true). Q skips Tracker and pre-steps back one app when quitting the last entry so the selection does not jump to the start; H minimizes every team in the group. Key auto-repeats are ignored until the first real press, then honoured every 6th repeat  
`Switcher.cpp` → `TSwitcherWindow::DoKey`  

#### DB-057 · core · open
**Committing a switch brings the whole application forward, switching workspace if needed and raising every window of a multi-instance app in their existing relative order**  
ActivateApp: single-team case just be_roster->ActivateApp (refusing when the target is minimized and forceShow is off, or when it lives on another workspace and workspace switching is not allowed); multi-team case activates to the workspace of the selected window, collects all visible windows of the group from the token list and brings them to front back-to-front, then raises the selected window last  
`Switcher.cpp` → `TSwitchManager::ActivateApp`  

#### DB-058 · polish · open
**The switcher animates: the outgoing icon shrinks, the strip scrolls to the new slot, the incoming icon grows, and arrow scrollers appear at the ends when there are more apps than the seven visible slots**  
AnimateIcon does two 20ms inset steps each way between the 24px and 48px cached icons via an offscreen bitmap; scroll step is half the slot size and doubles when wrapping more than 4 slots; the selected icon is drawn large only when not auto-scrolling. Clicking the left/right scroller area jumps by SlotCount-1; clicking above/below the window list scrolls one window  
`Switcher.cpp` → `TIconView::AnimateIcon`  
**TIKU:** Animation timing is user-visible feel; on a constrained target consider a static fallback but keep the slot/scroller semantics  

#### DB-059 · polish · open
**Clicking an application icon in the switcher selects it, and pressing a mouse button outside the switcher window dismisses it without switching**  
TIconView::MouseDown maps x to a slot and calls SwitchToApp; Pulse (100ms while shown) commits with Stop(true) as soon as Ctrl is released in hair-trigger mode, or cancels with Stop(false) on an outside click when hair-trigger is off. The window view's Pulse resets to window 0 if the selected window vanished  
`Switcher.cpp` → `TIconView::Pulse`  

#### DB-060 · polish · open
**The switcher panel centers itself on the main screen, slightly above vertical center, and re-centers when the screen resolution or workspace changes**  
CenterWindow aligns in the screen frame then lifts by screenFrame.Height()/16; also re-centered by Process() if the panel would fall outside the screen. Window title strings are 'AppName: WindowTitle' unless the window name already matches the app name under SmartStrcmp (which ignores spaces and underscores), and are middle-truncated to fit  
`Switcher.cpp` → `TSwitcherWindow::CenterWindow`  

#### DB-061 · core · open
**Deskbar preferences offer: recent documents/folders/applications with per-type counts, sort apps by name, Tracker always first, show application expander, expand new applications, hide application names, icon size slider, dock location picker, always on top, auto-raise, auto-hide**  
Recent counts range 0..50; a count of 0 forces that type disabled even if its checkbox is on (enabled && count > 0 in the app's handler). Expander and expand-new are disabled unless the bar is vertical AND expanded; auto-raise is disabled while always-on-top is on; each dependent state is re-evaluated on kStateChanged and kUpdatePreferences  
`PreferencesWindow.cpp` → `PreferencesWindow::_EnableDisableDependentItems`  
**TIKU:** Settings should live in durable cells; the count-0-means-disabled coupling is a real rule, not a bug  

#### DB-062 · important · open
**The Defaults and Revert buttons are enabled only when they would actually change something, Revert restoring the values the window opened with**  
_IsDefaultable/_IsRevertable compare fifteen fields (sort, trackerFirst, superExpando, expandNewTeams, hideLabels, iconSize, the three recent-enabled flags, the three recent counts, alwaysOnTop, autoRaise, autoHide) — note dock location, width and clock options are deliberately NOT part of either comparison. _UpdatePreferences applies only the differing fields and Invoke()s each control so the app sees a normal toggle  
`PreferencesWindow.cpp` → `PreferencesWindow::_IsRevertable`  

#### DB-063 · important · open
**The dock location picker is a little monitor you click or drag inside: the thirds of its screen area map to left/right corners, the middle band to expanded left/right, and the center to top/bottom; arrow and numpad keys pick corners too**  
_ScreenCorner splits at 1/3 and 2/3 horizontally and vertically; the middle vertical band returns LEFT_TOP|kExpandBit / RIGHT_TOP|kExpandBit (expanded from the bottom is not supported); Invoke adds "location" and "expand"; keys 8/2/7/9/3/1 and Up/Down/Home/PageUp/PageDown/End map to the six locations  
`ScreenCornerSelector.cpp` → `ScreenCornerSelector::_ScreenCorner`  

#### DB-064 · polish · open
**The preferences window remembers its screen position across sessions and refuses to reopen off-screen; it hides rather than quits when closed and comes to the current workspace when shown again**  
'prefs_window_settings' file holds "window_position"; restored only if BScreen(this).Frame().Contains(where), else CenterOnScreen. QuitRequested saves and posts kConfigQuit rather than returning true; Show() calls SetWorkspaces(B_CURRENT_WORKSPACE) when it was hidden  
`PreferencesWindow.cpp` → `PreferencesWindow::QuitRequested`  

#### DB-065 · polish · open
**The 'Edit in Tracker…' button in preferences opens the user's Deskbar folder so its menu contents can be edited directly**  
OpenWithTracker(B_USER_DESKBAR_DIRECTORY)  
`PreferencesWindow.cpp` → `PreferencesWindow::MessageReceived case kEditInTracker`  
**TIKU:** Becomes 'open this VFS directory in the file browser'  

#### DB-066 · polish · open
**Dropping files onto the Deskbar icon (or passing refs to it) launches them, but directories are silently ignored**  
Each ref is re-wrapped in its own B_REFS_RECEIVED and passed to TrackerLaunch only when !entry.IsDirectory()  
`BarApp.cpp` → `TBarApp::RefsReceived`  

#### DB-067 · polish · open
**Changing the system locale re-reads localized application names, reformats the clock and re-flows the tray without a restart**  
B_LOCALE_CHANGED refreshes BLocaleRoster, updates gLocalizedNamePreferred from the "filesys" field, then falls through to forward to the tray, which calls UpdateTimeFormat/Update and jumps to the realign path. Team names are taken from BLocaleRoster::GetLocalizedFileName when that preference is on, falling back to ref->name  
`BarApp.cpp` → `TBarApp::MessageReceived case B_LOCALE_CHANGED`  

#### DB-068 · important · open
**Deskbar re-lays itself out when the workspace is activated and when the screen resolution changes, keeping itself docked to the correct edge**  
WorkspaceActivated calls UpdatePlacement unless in vertical expanded mode (where it only re-sizes/re-positions/invalidates, to avoid rebuilding the app list); ScreenChanged re-applies size limits, recalculates the dock regions and re-runs UpdatePlacement  
`BarWindow.cpp` → `TBarWindow::WorkspaceActivated`  

#### DB-069 · important · open ⟨found by verification⟩
**The preloaded app/preferences icons stay live: the preloader node-monitors the directories and each preloaded file, re-icons on change and force-removes permanent entries when a file disappears.**  
NodePreloader::PreloadOne calls TTracker::WatchNode(dir, B_WATCH_DIRECTORY) and TTracker::WatchNode(model, B_WATCH_STAT | B_WATCH_ATTR) per file; its MessageReceived calls IconCache::IconChanged(model->ResolveIfLink()) on change and IconCache::Removing(model) (which ignores the Permanent flag, unlike Deleting) when an entry goes away.  
`src/kits/tracker/NodePreloader.cpp` → `NodePreloader::PreloadOne / NodePreloader::MessageReceived`  
**TIKU:** The Permanent/Removing asymmetry is the only way a pinned cache entry can ever be freed — a rewrite that forgets it leaks the pinned tier.  

#### DB-070 · important · partial ⟨found by verification⟩
**Clicking the tray clock opens a calendar popup; right-clicking it opens Time preferences… / Hide clock / Show calendar….**  
TTimeView::MouseDown routes B_SECONDARY_MOUSE_BUTTON to ShowTimeOptions(screen point) and B_PRIMARY_MOUSE_BUTTON to ShowCalendar(point). ShowCalendar re-activates an existing CalendarMenuWindow if one is open, else places it 4 px below the view, flipping above the view when it would fall past the screen bottom. The clock's tooltip is the current date string, refreshed in Update().  
`TimeView.cpp` → `TTimeView::MouseDown / TTimeView::ShowCalendar / TTimeView::ShowTimeOptions`  
**TIKU:** Requires an RTC; the calendar popup is droppable, but "status area item with its own context menu" is the reusable shape.  
**MISSING:** Clicking the clock opens a calendar popup, but the button is not discriminated — the clock branch ignores `secondary`, so right-click opens the calendar too and the Time preferences… / Hide clock / Show calendar… context menu does not exist; placement is a fixed rect beside the bar with no 4 px-below rule or bottom-of-screen flip, and there is no date tooltip.  
*src/tiku_trk_deskbar.c:tiku_trk_deskbar_click lines 744-746; calendar rect at tiku_trk_deskbar_draw line 595*

#### DB-071 · important · open ⟨found by verification⟩
**Clock display options (show/hide clock, seconds, day of week, time zone) are forwarded to the replicant tray, which re-flows its icons when the clock's width changes.**  
TBarApp::MessageReceived groups kRealignReplicants / kShowHideTime / kShowSeconds / kShowDayOfWeek / kShowTimeZone / kGetClockSettings and forwards each to fStatusViewMessenger "so it should update the time view and reflow the tray icons"; TTimeView::SetShowSeconds/SetShowDayOfWeek/SetShowTimeZone each resize to preferred and invalidate.  
`BarApp.cpp` → `TBarApp::MessageReceived (kShowHideTime / kShowSeconds / kShowDayOfWeek / kShowTimeZone / kRealignReplicants)`  

#### DB-072 · important · open ⟨found by verification⟩
**With auto-raise on (and always-on-top off), opening any Deskbar menu raises the Deskbar and closing the last one lowers it again.**  
TBarWindow::MenusBeginning increments fMenusShown and calls fBarView->RaiseDeskbar(true) when `!alwaysOnTop && autoRaise`; MenusEnded decrements and calls RaiseDeskbar(false) only when `fMenusShown <= 0`, so a submenu opening does not re-lower the bar.  
`BarWindow.cpp` → `TBarWindow::MenusBeginning / TBarWindow::MenusEnded`  

#### DB-073 · important · open ⟨found by verification⟩
**Always-on-top, auto-raise and auto-hide are mutually constrained: turning always-on-top on forces auto-raise off, and always-on-top changes the bar window's feel live.**  
kAlwaysTop toggles fSettings.alwaysOnTop and immediately calls fBarWindow->SetFeel(alwaysOnTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL). kAutoRaise computes `fSettings.autoRaise = fSettings.alwaysOnTop ? false : !fSettings.autoRaise`. All three also poke the preferences window with kUpdatePreferences so its checkboxes track changes made from BDeskbar.  
`BarApp.cpp` → `TBarApp::MessageReceived (kAlwaysTop / kAutoRaise / kAutoHide)`  

#### DB-074 · important · open ⟨found by verification⟩
**A recents type with a count of zero counts as disabled, and its submenu disappears from the leaf menu entirely.**  
kUpdateRecentCounts sets `fSettings.recentAppsEnabled = enabled && count > 0` (same for folders and documents) and then refreshes the preferences window. TRecentsMenu reads recentXxxCount/recentXxxEnabled at construction, and TDeskbarMenu::AddNextItem only adds the submenus whose RecentsEnabled() is true — dropping the whole block when none is.  
`BarApp.cpp` → `TBarApp::MessageReceived (case kUpdateRecentCounts) / TRecentsMenu::TRecentsMenu`  
**TIKU:** "count 0 means off" is a settings-normalisation rule worth reproducing rather than storing two independent fields.  

#### DB-075 · polish · open ⟨found by verification⟩
**Files dropped on the Deskbar application itself (not on the menu) are launched through Tracker, and dropped folders are silently ignored.**  
TBarApp::RefsReceived iterates the refs, skips any whose BEntry IsDirectory(), and for each remaining ref builds a fresh single-ref B_REFS_RECEIVED and calls TrackerLaunch(&refsReceived, false).  
`BarApp.cpp` → `TBarApp::RefsReceived`  
