# TikuBasic editing and device workflow

- The editor shows the execution destination and state. Choose the
  default board in TikuDesktop's Devices menu. No default means **This computer**;
  a disconnected named default never falls back to local execution. An active
  run continues to display its original board if the desktop default changes.
- The editor keeps a file's bytes: line endings, tabs and control characters
  load and save as they are, and Save ends an unended last line with the file's
  own line break. There is no line or line count limit.
- Run saves the source first. Failed saves block execution. A program holding a
  control character other than tab, a line longer than the 159 bytes BASIC
  reads, or more than 143 bytes after a line's number is refused, naming the
  line, before a local run, a board run, a board save or a debug run. Uploads
  larger than 8,191 bytes, embedded NULs, and read failures are rejected without
  sending a partial program. Firmware may impose additional limits and can
  reject a program.
- Device-run errors stay in the console with their line numbers; finishing a run
  does not open a duplicate Messages window. Messages is used as a fallback if
  no console is available.
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
and selects the next one when the replacement field is focused. Empty
replacement text deletes a match. **All** replaces every match in the current
file as one undoable operation. Escape/Close hides the bar.
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

Startup opens an empty Project window; saved drafts do not automatically open
editors or load a previous project. **Project > Recover unsaved files** explicitly
opens available snapshots. Opening a source file also restores its own snapshot,
if present. Recovered buffers are **modified**, with a recovery notice; the original
source is unchanged until Save. Only two editor windows fit the current app
session; close a file and use the recovery command again for remaining snapshots.
Saving or explicitly discarding removes that file's snapshot.
Closing a modified editor asks Save/Discard/Cancel. Unexpected host closure keeps
a final checkpoint when possible. A crash can lose edits since the last successful
checkpoint; this is not a replacement for backups or a power-loss guarantee.

## Keyword assistance

In a BASIC editor, use **Edit > Complete keyword** (Command + Space) on a
prefix. A list of matching keywords opens under the word, with a tip beside it.
Up/Down selects a suggestion; Enter/Tab or clicking inserts it as one undoable
edit. Completion does not insert into comments or quoted strings.
**Edit > Keyword help** (Command + K) shows the same list for the word at the
caret, and the tip gives the chosen keyword's syntax, a description and an
example; clicking a keyword shows its help. Not every keyword has detailed help
yet. Escape closes the list and the tip. Firmware feature availability can
differ by board.

## BASIC debugger

The BASIC editor's **Debug** menu provides:

- **Start debugging** (Command + D), initially paused before the first line.
- **Toggle breakpoint at caret** (Command + B), up to 16 numbered lines. A
  breakpoint marks its line and follows it through edits and Renumber; when
  debugging starts, each marked line must still have a number and be the last
  line with it, and that number is where the run pauses.
- **Step line** (Command + J) and **Continue** (Command + G).
- **Stop** (Command + period), also available in the debugger panel.

Configure breakpoints and the panel's Watch field before starting. Watches accept
up to eight scalar variable names separated by commas or spaces, including string
variables such as `A$`; expressions and array elements are not supported. String
previews are limited to 32 bytes. The green source row is the next line to execute;
breakpoints use red shading. A step executes an entire numbered line, including
colon-separated statements, not one individual statement.

Debugging uses the desktop's default board, or the local BASIC interpreter when
there is no default. A board requires firmware supporting `DEBUG` protocol v1;
older firmware is rejected before RUN. The source must use numbered BASIC lines.
Source editing stays locked during debugging, and the green row is shown only
while the source is the text the run was given. Watches refresh at pauses and at
completion; there is no arbitrary pause command while running. Breakpoints and
watch configuration are currently session-only.

If a board does not acknowledge Stop within five seconds, the editor releases its
source lock and warns that the remote program may still be running. Check the
board before starting another run. This timeout does not prove the board stopped.

## Verification

From `applications`:

```sh
make build/tests/test_ide_workflow build/tests/test_ide_page build/tests/test_ide_default
make build/runtime/apps/basic/tiku-basic build/tests/test_ide_debug build/tests/test_ide_deadboard
build/tests/test_ide_workflow
build/tests/test_ide_page
build/tests/test_ide_default
build/tests/test_ide_debug
build/tests/test_ide_deadboard
python3 apps/basichost/test_debug.py
```

The workflow test uses mock devices and private temporary files; it does not
connect to hardware. It also renders `/tmp/tikubasic-find-replace.png` and
`/tmp/tikubasic-upload.png` for layout inspection.
