# TikuBasic editing and device workflow

- The editor shows the execution destination and state. Choose the
  default board in TikuDesktop's Devices menu. No default means **This computer**;
  a disconnected named default never falls back to local execution. An active
  run continues to display its original board if the desktop default changes.
- Run saves the source first. Failed saves block execution. Uploads larger than
  8,191 bytes, embedded NULs, and read failures are rejected without sending a
  partial program. Firmware may impose additional limits and can reject a program.
- **Save on [board]** previews `/data/<name>.bas`, the board and byte count.
  The current desktop board-save interface supports seven-character names.
  Shortening is called out, and saving always requires explicit overwrite
  confirmation because that interface does not expose a slot-existence query.
  Enter/Escape cancel; click **Save / Replace** to proceed. The confirmed upload
  uses the displayed board and the captured program, even if either later changes.

## Find and Replace

Open **Edit > Find and Replace** (the desktop Command modifier + H), or use the
Find toolbar button. Searches are literal and case-sensitive, within one line.
Tab switches fields; Enter finds the next match or replaces the selected match
when the replacement field is focused. Empty replacement text deletes a match.
**All** replaces every match in the current file as one undoable operation.
Oversized result lines are rejected before any change. Escape/Close hides the bar.
The ordinary Find command retains BASIC line-number lookup; replacement uses
literal text, including numbers.

**Edit > Find in project** searches the Find field's text across project files,
using unsaved buffers where open. Results are clickable source lines, capped at
200; the status reports the cap and unreadable files. It does not replace across
files or search while a program is running.

## Recovery

Changed open buffers are checkpointed every two seconds, separately from source
files, in `~/.TikuBasic-recovery` (`~/.TikuC-recovery` for TikuC). Unchanged text
does not rewrite a checkpoint. Writes use private temporary files and atomic
rename; per-file locks prevent concurrent instances from overwriting snapshots.
`TIKU_IDE_RECOVERY_DIR` may select another absolute, user-owned private directory.

On restart, recovery files reopen as **modified**, with a recovery notice. The
original source is unchanged until Save. Only two editor windows fit the current
app session; **Project > Recover unsaved files** opens remaining snapshots after
closing a file. Saving or explicitly discarding removes that file's snapshot.
Closing a modified editor asks Save/Discard/Cancel. Unexpected host closure keeps
a final checkpoint when possible. A crash can lose edits since the last successful
checkpoint; this is not a replacement for backups or a power-loss guarantee.

## Verification

From `applications`:

```sh
make build/tests/test_ide_workflow build/tests/test_ide_default build/tests/test_textview
build/tests/test_ide_workflow
build/tests/test_ide_default
build/tests/test_textview
```

The workflow test uses mock devices and private temporary files; it does not
connect to hardware. It also renders `/tmp/tikubasic-find-replace.png` and
`/tmp/tikubasic-upload.png` for layout inspection.
