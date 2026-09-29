# TikuDiff: what changed, between two files, folders or releases

*A plan for comparing text files side by side, folders by their
contents, and two archives as the folders they hold.  2026-09-29.*

## 1. What it is for

- Two versions of a source file, a configuration, a log: which lines
  changed, and inside a changed line, which words.
- Two folders or two releases -- an SDK's 1.2 and 1.3 zips: which files
  were added, removed and changed; then, file by file, the lines.
- A bug report: the difference as a unified diff (`diff -u`), on the
  clipboard or in a file, which every reviewer and `patch` reads.

## 2. Shape

A kit and an app, as TikuArchive is.

**`kits/support/tiku_diff`** compares, with no window and no files: two
arrays of lines in, runs of kept, removed and added lines out, and the
unified text of them.  Lines are hashed and numbered first, so the
algorithm compares integers; lines found on one side only cannot match
and are set aside before it runs.  The algorithm is Myers' O(ND) with the
linear-space split at the middle snake: memory O(N+M), the shortest
script when the difference is small.  Past a bounded number of steps a
split takes the furthest point either search reached instead, so a
100,000-line file where everything moved still answers promptly -- with
a script that is correct but may not be the shortest, and says so.
Options: whitespace changes ignored; a CRLF line equal to its LF twin.
The same machine run over the words of a changed line and its
replacement marks what changed inside it.  Small enough for a board.

**`apps/diff`** is TikuDiff:

- *Files*: two panes, rows aligned -- a line with no counterpart faces an
  empty band -- with line numbers; removed lines tinted on the left,
  added on the right, a changed pair on both with its changed words
  darker.  One vertical scrollbar moves both; a strip beside it maps
  where the changes are in the whole file, and a click there goes to
  it.  Next and Previous Change, from the toolbar, the Go menu and keys.
- *Folders*: one list of relative paths -- changed, only left, only
  right, and (when asked for) the same -- sizes beside them.  Opening a
  changed text file shows its lines; Back returns to the list.  Binary
  files are compared by their bytes and said to differ or not.
- *Archives* as sides: a ZIP, tar, tar.gz or gzip is extracted with the
  archive kit, under its safety rules, into a private folder removed on
  close, and compared as the folder it holds.
- *Choosing*: Compare... asks for the left file, then the right; Compare
  Folders... the same for folders (a new folder mode of the shell's
  panel, `TIKU_APP_PICK_LOCAL_FOLDER`); Open Left... and Open Right...
  replace one side; Swap Sides turns them round.
- *Out*: Edit > Copy Unified Diff; File > Save Unified Diff...
- Nothing open: the window is blank, its status line empty.

Limits: bytes are shown in the interface's mono face, tabs as four
columns; a file over 64 MB, or with NUL bytes, compares as binary.

## 3. Tests

- Kit: fixed cases; random edits whose script, applied, turns A into B,
  and whose length equals a dynamic-programming LCS on small inputs; the
  step bound still gives a valid script; unified text byte for byte as
  `diff -u` writes it for the fixtures.
- App: headless, including the .c: two files, the rows and tints, Next
  and Previous, folders, an archive side, the unified diff on the
  clipboard, the blank empty window.
- Lane: TikuDiff from Applications, comparing two fixture files.

## 4. Not in this plan

Merging and editing, three-way comparison, and comparing images or PDFs.
