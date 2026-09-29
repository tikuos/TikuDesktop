# TikuGrab: a window, or the desk, as a picture

*A plan for screenshots worth attaching to a bug report or putting in a
document.  2026-09-29.*

## 1. What there is

Take Screenshot -- the Tiku menu, Command-Shift-3, the camera on the menu
bar -- saves the whole desk to Desktop/Screenshots.  The PNG is stored
uncompressed, about 2 MB for a 1000 by 680 desk and four times that on a
doubled screen, and it cannot leave anything out.

## 2. What TikuGrab adds

- **One window, alone.**  Drawn by itself, so a window half under
  another comes out whole; its tab and rounded corners stand on
  transparency, as a document wants them.
- **The desk without TikuGrab in it.**
- **A timer** -- 3, 5 or 10 seconds -- to catch an open menu or a tip.
- **A preview**, and Save As... to put it elsewhere; Open (in
  TikuImage) and Show in Tracker.
- **Compressed PNGs**: a desk falls from about 2 MB to about 100 KB.  The
  same writer serves Take Screenshot.

## 3. Shape

An application cannot see the desk: it is a process of its own and only
sends its drawing.  So the capture is the desktop's, a service on the
message road the pick panel already uses:

- `TIKU_MSG_GRAB` asks: the windows there are (title, and an id that is
  a slot and its generation), or a capture of the desk or of one window.
- `TIKU_MSG_GRABBED` answers: the list, or the saved file's path and
  size, or why not.  The desktop writes the file into Desktop/Screenshots
  itself, named by the time; an application never names a path for the
  desktop to write.
- The desk is drawn again with the asker's windows left out, saved, and
  drawn as it was before the frame is shown, so nothing flickers.

Beneath it:

- Interface kit: `tiku_workspace_draw_window()`, one window's chrome and
  content into a surface of its own; a window marked hidden is skipped.
- Translation kit: `tiku_deflate` (LZ77 with hash chains, fixed and
  dynamic Huffman blocks) and `tiku_png_write()` (RGBA or RGB, a filter
  chosen per row).

`apps/grab` is TikuGrab: a small window, a toolbar (Desk, Window, timer),
the preview, and the status line naming the file.  Before the first
capture the window is blank.

## 4. Tests

- Kit: DEFLATE round trips through both inflaters and Python's zlib, on
  fixtures and random data; a written PNG read back by `tiku_image`
  pixel for pixel, alpha too; the size of a desk-like picture.
- Interface: a covered window, drawn alone, has its own pixels.
- System: the grab service lists, captures and refuses as it should.
- App: headless, with a fake desktop answering its messages.
- Lane: TikuGrab from Applications; a desk and a window captured.

## 5. Not in this plan

Choosing a region with the pointer, recording video, and pictures on
the clipboard (the desk's clipboard carries text).
