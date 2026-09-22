# The TikuDesktop Book

A guide to the architecture of TikuDesktop, the desktop for TikuOS
computers. It follows the structure of the Be Book: one chapter per kit,
then the shells built on the kits, the applications that run on them,
and the boards they connect to.

This book describes the tree as of 19 September 2026. Every name in it
is a file, a function, or a type that exists in the repository. The
headers under `kits/` are the reference; this book explains how the
pieces fit together.

---

## Contents

**Part I — Orientation**
1. What TikuDesktop is
2. The layout of the tree
3. The rules that keep the layout

**Part II — The kits**
4. The Support Kit
5. The Device Kit
6. The Storage Kit
7. The Interface Kit
8. The Translation Kit
9. The Application Kit
10. The Media Kit

**Part III — The Tracker and its shells**
11. The Tracker core
12. The Desktop shell
13. The Deskbar
14. The browser shell

**Part IV — Applications**
15. Writing an application
16. The applications that ship

**Part V — Boards**
17. A board on the desktop
18. Two boards on one desktop

**Part VI — Testing and portability**
19. Tests, scripts, and checks
20. Linux and macOS

Appendix A — Glossary. Appendix B — Reading order. Appendix C — File map.

---

# Part I — Orientation

## 1. What TikuDesktop is

TikuOS is an operating system for microcontrollers such as the Ambiq
Apollo510, the Nordic nRF54L15, and the Raspberry Pi RP2350. It gives
these chips a filesystem, processes, a shell, and a device namespace, so
they behave like small computers rather than firmware.

TikuDesktop is the desktop for these computers. It runs on a Linux or
macOS host and draws a desktop in the style of BeOS R5. Each board
connected by cable, network, or radio appears as a volume mounted under
`/devices`. The same file windows that browse the host's files browse
the board's namespace.

Three design decisions shape the whole system.

**One toolkit for both the host and the board.** The interface kit that
draws the desktop's windows is the same code that a TikuOS board uses
to draw its own windows. Each control is written once. Its *behaviour*
(state and input handling) is kept in a separate file from its *paint*
(drawing). A board has no display, so instead of painting it records
the drawing calls as a *display list* and sends the list to the desktop
over a serial line. The desktop replays the list. This is how a board
can show a window on the desktop even though the serial line is far too
slow to carry pixels.

**Applications run in their own processes.** An application is a
*descriptor*: a struct of entry points (`start`, `stop`, `event_in`,
`tick`, `pick`, `closed`). The application draws into surfaces it owns
and hands finished frames to a set of *services*. The services may be
direct function calls inside the desktop process or messages over a
socket to a desktop in another process; the application cannot tell the
difference. Installed applications always run out of process, each in
its own `tiku-run` host, so a crash in an application does not take
down the desktop.

**One file manager, two shells.** The file manager (the pose view, folder
windows, Deskbar, preferences) is one body of code. It is compiled into
two programs: `TikuDesktop`, the spatial desktop, and `TikuTracker`, a
single-window browser. The same driven test scripts run against both,
without a display.

## 2. The layout of the tree

The repository at `tikuOS/applications/` separates the system, applications,
public libraries and development support. See the [layout guide](layout.md)
for the complete source/output map and compatibility paths.

```
apps/            applications: Editor, Terminal, IDE, Notes, Reader, ...
system/          desktop, tracker, deskbar, shared code, runner, supervisor
kits/            seven libraries: support, device, storage, interface,
                 translation, application, media
tests/           unit suites, driven scripts and fixtures
examples/        SDK demos and diagnostic galleries
assets/          artwork, sounds, help and translations
docs/            architecture, guides, contracts, plans and history
mk/              the build, one makefile per concern
```

Sizes on the day of writing: the kits are 104 C files and about 51,000
lines; the Tracker is about 62,000 lines; the applications about 13,500
lines. There are 157 unit-test programs and 264 driven scripts. The
desktop shell alone is 21 files and 17,600 lines.

Development output lives in `build/`; distributable packages live in `dist/`.

| product | location |
|---|---|
| the kits | `build/lib/libtiku_*.a`, plus position-independent copies under `build/lib/pic/` for loadable applications |
| the shells | `build/runtime/system/TikuDesktop`, `build/runtime/system/TikuTracker`; script-capable builds under `build/dev/` |
| the hosts | `build/runtime/system/tiku-run`, `build/runtime/system/tiku-desk` |
| the applications | `build/runtime/apps/<id>/app.manifest` and `<Module>.so` |
| the tests | `build/tests/test_*` |
| the portable runtime | `dist/TikuDesktop/`, made by `make package` |
| the macOS bundle | `dist/TikuDesktop.app`, made by `make app` |

## 3. The rules that keep the layout

Four checks run as part of `make test`. Each fails the build if a single
source line breaks a boundary.

**The kit table** (`kits/tools/check_kits.py`). Each kit may include
headers from itself and from the kits listed in its row, and from
nothing else. A kit may not include from a kit further down the list.

| kit | may include from |
|---|---|
| support | nothing |
| device | support |
| interface | nothing |
| storage | support, device |
| translation | support, storage, interface |
| application | support, device, interface |
| media | nothing |

No kit may include a `tiku_trk_desktop*` or `tiku_trk_app.*` header,
because a kit must not know about the application built on top of it.
The same script refuses a drawing header (`tiku_gfx.h`, `tiku_font.h`,
`tiku_ui.h`, `tiku_dl.h`) inside a behaviour file. This keeps every
behaviour file compilable on a board with no display.

**The architecture check** (`tools/check_architecture.py`).
The Tracker core may not include X11 or drawing headers. Only a short
list of files may use `__linux__`, `__APPLE__`, or open a host `/proc`
path; this list is the *platform seam*: `tiku_sys_linux.c`,
`tiku_sys_darwin.c`, `tiku_volume.c`, `tiku_state.c`,
`tiku_backend_posix.c`, `tiku_link_tcp.c`, `tiku_cocoa.m`, and a few
more. The check also reads the symbol tables of the shipping shells and
fails if they contain application code or a module loader.

**The application boundary** (`tools/check_app_boundaries.py`). No
application includes a desktop-private header, and no shell contains
application code.

**The toolkit check** (`make check-toolkit`). The display-list operation
table (`dl_ops.txt`) generates a header for each end; the check fails if
either is stale. Every behaviour file is compiled with the board's
compiler under `TIKU_DISPLAYLESS`. Text measurements from the desktop's
fonts are compared against the board's metric tables.

---

# Part II — The kits

Each kit is a directory under `kits/`, a static library, and a set of
headers. The first comment in each header states what the file is for.
A program links the kits it needs. The link order is application,
translation, interface, storage, device, media, support, because a
library must come before the libraries it depends on.

## 4. The Support Kit

Directory: `kits/support/`. This kit contains what every other kit
depends on. It does not draw and does not talk to boards.

**Messages — `tiku_msg.h`.** A message is a code that says what the
message is, followed by named fields. Each field has a type and one or
more values. A reader asks for fields by name and skips fields it does
not know. This makes messages tolerant of version differences, which
matters when a board runs older firmware than the desktop. The format is
little-endian and every value carries its own length. The design is
close to Be's `BMessage`; `doc/message.md` lists the differences.
Messages carry device replies, launch data, and the conductor's answers.

**Workers — `tiku_work.h`.** The desktop's main loop is the only owner of
desktop state. Slow operations are given to worker threads as *jobs*. A
job may only touch the data it was given, and it returns a result that
the loop applies on its next turn. The kit is two queues (requests in,
results out) and a small pool of threads. Directory listings,
thumbnails, board polls, and serial port opens all run as jobs, so the
window never blocks on them.

**The watchdog — `tiku_watch.h`.** A single-threaded program freezes when
one turn of its loop does not finish. The watchdog runs outside the loop,
counts turns, and when a turn takes too long it records what that turn
was doing.

**The trail and reports — `tiku_trail.h`, `tiku_report.h`.** The trail is
a fixed-size ring buffer of log lines kept in memory. It is always on
and never written to disk during normal operation. When the program
receives a fatal signal, the report code writes a file containing the
signal, the faulting address (from `tiku_sys_fault_pc()`), and the trail.
The program then dies normally so a core dump is still produced.
Sending `SIGUSR2` to a running desktop writes the same report on demand.

**Strings — `tiku_strings.h`.** Every user-visible string is looked up by
key. `tiku_str("menu.about")` returns the string for the current
language from `strings/<lang>.xml`. English strings are compiled in as
the fallback.

**Manifests — `tiku_manifest.h`.** An application manifest is a
version-1 `key=value` file. Reading it executes no code. Unknown,
duplicate, overlong, and malformed fields are rejected. The `module`
field is a file name, never a command.

**System calls — `tiku_sys.h`.** One header, with one implementation
file per host operating system, selected by the makefile from
`uname -s`. It covers the things POSIX leaves to the platform: process
listing, memory size, uptime, the path of the running executable, the
faulting program counter in a signal handler, serial port enumeration,
and serial rates above the standard `termios` set. All other code in the
tree is written to POSIX and compiles unchanged on both hosts.

## 5. The Device Kit

Directory: `kits/device/`. This kit moves bytes to and from one board
and builds conversations on top. It has five layers. Each layer hides
the one below it.

**Transport — `tiku_tx.h`.** Serial, TCP, or a caller-supplied file
descriptor with hooks, behind three functions. Code above this layer
does not know which transport it is using. Opening a serial port may
take seconds, so it is never done on a thread that draws.

**Session — `tiku_session.h`.** A session sends a command to a board's
shell and returns whatever the board printed before its next prompt.
Asynchronous lines that begin with `~<path>` (subscription updates) are
passed to a callback wherever they appear in the stream. A session can
also return raw text, which is needed for a running program's output,
because that output has no prompt to mark its end.

**Frames — `tiku_frame.h`.** A frame is an END byte, a marker byte that
names the channel, the payload with SLIP escaping, and another END byte.
The decoder extracts frames for one marker from a byte stream and
treats everything else as text. This lets a board's window session share
the console line with the board's shell.

**Links — `tiku_link.h`, `tiku_link_serial.h`, `tiku_link_tcp.h`.** A
link carries whole messages to and from one board over any medium. It
mirrors the kernel's link on the board side. A message arrives complete
or not at all. If a link loses a message, it reports the loss and the
layer above sends a new greeting.

**Channels — `tiku_channel.h`.** The desktop's window session (chapter 9)
speaks over a file descriptor. A channel provides one: a socket pair,
where one end is given to the desktop's listener and the other is fed
by the messages a link receives from the board. The channel greets the
board, greets again after a loss, and backs off when the board does not
answer. It can also own a network port for a board that has no shell.

**The namespace mirror — `tiku_ns.h`.** A board's `/sys/vfs/manifest`
describes its whole namespace: each node's path, kind, permissions, a
typed descriptor, and capabilities. The mirror reads the manifest once.
It reads values on demand, or receives them as `~` lines when
subscribed. Every board window renders from this mirror.

## 6. The Storage Kit

Directory: `kits/storage/`. This kit holds the file model, the storage
backends, and the namespace that joins them.

**The Model — `tiku_model.h`.** The Model is the one place that knows
what kind of object a list entry represents. Everything above it asks
questions such as `IsContainer`, `IsWritable`, and `Kind` instead of
checking which backend an object came from. This is what lets one view
browse both host files and a board's namespace. A directory on a board
is of kind `VIRTUAL_DIR`, not `DIRECTORY`. Nothing can be created in,
renamed in, or pasted into a virtual directory. That single rule makes
`/sys`, `/dev`, and `/proc` read-only. A board's `/data` store is a real
`DIRECTORY`.

**Backends — `tiku_backend_posix.c`, `tiku_backend_tiku.c`.** A backend
is a table of operations (`tiku_backend_ops_t`): list, stat, read, write,
rename, remove, and id lookup. The POSIX backend serves the host's
files. The Tiku backend serves a board, through a session and its
namespace mirror.

**The namespace — `tiku_mount.h`.** The namespace joins several backends
and is itself a backend, so the code above it does not know there is
more than one store. Each backend is attached at a path prefix: `/` for
the host, `/devices/board1` for a board. Each operation goes to the
backend with the longest matching prefix. The namespace does three
things a simple forwarder would not: it rewrites paths in both
directions (a board asked about `/sys/mem` answers with a model whose
path is `/devices/board1/sys/mem`); it synthesises parent directories
(`/devices` exists because something is mounted under it); and it keeps
ids distinct (ids from different backends may collide, so ids leaving
the namespace carry the mount number in their top byte). The namespace
holds up to eight backends: the host and seven boards. See
`doc/namespace.md`.

**File operations — `tiku_fs.h`.** Copy, move, duplicate, rename, move
to trash, and undo, with Tracker's rules. A move within one volume is a
rename; across volumes it is a copy followed by a delete. Duplicate
generates a new name instead of asking. Every operation reports
progress and resolves name conflicts through an alert, so a window
never blocks on a long copy.

**UI state — `tiku_state.h`.** Icon positions, column sets, and view
state are stored through one interface, which is a port of Tracker's
`AttributeStreamNode`. For host files the state is stored in the file's
extended attributes. For board nodes it is stored in a *sidecar* file on
the host, keyed by the board's identity. The callers use one code path
for both.

**Attributes — `tiku_attr.h`.** A TikuOS node carries a typed descriptor
in its manifest: `vtype,unit,freshness,cost[,lo..hi]`. The descriptor
tells the desktop how to format a value (for example `1.2 MiB` or
`15h 57m`) and lets it reject an invalid write before sending it to the
board (for example the string `banana` to a `u32` node).

**Identity — `tiku_ident.h`.** A board is identified by a scored match
over the identity nodes it publishes, following Tracker's
`MatchArchivedVolume`. The result is one string. Every persistent store
keys on that string.

**Queries — `tiku_query.h`.** A query is a predicate plus a scope, as in
Tracker. Terms are (attribute, operator, value), joined with and/or. The
folder scope is applied outside the predicate, so it can be added to a
running search. No index is needed: a volume is a subtree of the
namespace, and the type registry is the node's own descriptor. A search
is stepped, sixteen directories per loop turn, so a large disk does not
stall the desktop.

**Change notices — `tiku_notify.h`.** All change notices (node monitor,
path monitor, live query) go through one handler, so a query window and
a folder window update by the same code. On the host the source is a
directory watch. A board cannot yet push folder-level notices, so the
desktop derives them by listing the directory again and comparing.

**Volumes — `tiku_volume.h`.** The mount table, partitions, mount, and
unmount, with one implementation for Linux (`/proc` files) and one for
macOS (`getmntinfo` and DiskArbitration). On macOS the partition scan is
cached and keyed on the set of disk names and the mount table, because
the Mount menu asks for it on every frame a folder window draws.

**VFS over the owner — `tiku_vfs_rpc.h`.** Device utilities do not open a
second serial connection. They send requests to the desktop, which owns
the board's connection. The desktop runs each request on the board's
worker, in order with its own traffic. The target board is pinned by
identity. A write must be preceded by a read, and the write is refused
if the bytes have changed since. Replies carry a session generation so
a late reply cannot reach a replacement application.

## 7. The Interface Kit

Directory: `kits/interface/`. This kit draws every pixel of the desktop
and defines the interface to the windowing system. It has 51 headers and
is the only kit that also runs on the board.

### 7.1 Surfaces and the theme — `tiku_gfx.h`

A surface is a 32-bit pixel buffer (`0x00RRGGBB`) with a clip rectangle
and a scale factor. On a Retina display the surface stores `(w × scale)
by (h × scale)` pixels, and every drawing call still takes logical
coordinates. The drawing functions are `tiku_fill`, `tiku_frame`,
`tiku_hline`, `tiku_vline`, `tiku_blit`, `tiku_copy_bits`, `tiku_veil`
(blend one colour over a region), and, added in September 2026,
`tiku_shadow`, `tiku_fill_round`, `tiku_frame_round`, `tiku_grab`,
`tiku_blend`, and `tiku_blend_scaled`.

The theme is a runtime table of semantic colours: panel, document, text,
tab, selection, focus, backdrop, danger, and the syntax-colouring inks.
All widgets draw through the theme. The default table is the BeOS R5
look; `docs/plans/apps/R5-SPEC.md` lists the measured values (panel 216,216,216;
tab 255,203,0; backdrop 51,102,152). A dark table changes every colour
without changing any layout. Shades of the panel colour are computed
with `tiku_tint()`, which implements BeOS's `tint_color` arithmetic.

### 7.2 Text — `tiku_font.h`, `tiku_ttf.h`, `tiku_glyphpath.h`, `tiku_cff.h`

The UI font is compiled in as 8-bit coverage bitmaps generated by
`genfont.py`, so text is anti-aliased and the runtime needs no font
library. A font loaded from a `.ttf` or `.otf` file is rasterised on
demand by the kit's own TrueType and CFF interpreters, so it can draw
any glyph the file contains. The font size is a user preference (12
pixels by default). Layouts ask the font for row and tab heights rather
than using fixed numbers.

### 7.3 Controls — `tiku_ui.h` and the behaviour/paint split

`tiku_ui.h` draws the R5 controls in immediate mode: each call paints one
control in one state. The controls are `tiku_ui_button`, `checkbox`,
`radio`, `menufield`, `scrollbar`, and `tab_widget`. States are flags:
`TIKU_S_DISABLED`, `TIKU_S_DEFAULT`, `TIKU_S_PRESSED`, `TIKU_S_FOCUS`.
Layout and hit testing are done by the window system, not here.

Controls with state come in pairs of files. `tiku_field.c` holds a text
field's state and handles its input; `tiku_field_event.c` maps events to
it; neither file draws. The same pattern applies to `tiku_slider`,
`tiku_tabs`, `tiku_list`, `tiku_textview` (a multi-line editor with
selection and undo), `tiku_doc` (a laid-out document), `tiku_markdown`,
and `tiku_sensor` (plots, meters, sparklines), each with a
`_paint.c` file. `tiku_press.c` tracks a mouse press by hit id so a
button acts on release and draws as held while pressed. `tiku_dialog.c`
and `tiku_alert.c` build the R5 alert and modal panel from these parts.
`tiku_form.c` and `tiku_layout.c` build a panel from a description.

### 7.4 Windows — `tiku_window.h`

A *workspace* composites up to sixteen windows onto one surface. It owns
each window's frame, the z-order, focus, and a cache of each window's
content pixels, so dragging a window repaints only the area that moved.
A window has a tab with close and zoom buttons, a border, a resize grip,
a title, and a draw callback that paints its content inside a clip
rectangle. A window may publish menus as a `tiku_menuset_t` (plain data)
together with a pick callback. The desktop owns z-order, focus,
invalidation, and chrome. The window's owner owns the content.

Since September 2026 a window casts a soft shadow that extends
`TIKU_WINDOW_SHADOW` (14 pixels) past its frame; its corners are rounded
except under the tab; a new window rises 8 pixels into place over 150 ms;
a closed window leaves a shrinking, fading copy for 140 ms; a minimised
window's copy flies to its Deskbar entry. These animations run on a
workspace clock that the shell advances only when a real display is
present. A scripted run never advances the clock, so its frames are
still.

### 7.5 Menus — `tiku_menu.h`

`tiku_menu_t` is the live menu object the shell draws and tracks the
pointer through: items with check marks, shortcuts, icons, and submenus;
layout; and hot-row tracking. It is distinct from `tiku_menuset_t`, the
flat data an application sends to describe its menus. A menu is rebuilt
each time it opens, so labels and enabled states reflect the current
selection.

### 7.6 The display list — `tiku_dl.h`

A window session normally sends a window's pixels. A 372 × 302 window is
449,376 bytes, which takes 39 seconds at 115,200 baud. The display list
sends the drawing calls instead: for the same window, about 21 calls
and 600 bytes. The calls are the interface kit's own, at the level of
"a button, at this rectangle, disabled, with this label". Text
measurement is done at the sending end, because both ends have the same
font metric tables, so no round trip is needed per label.

The in-memory form is the wire form: `[u16 op][u16 length][payload]` per
command. A reader that meets an unknown op skips it by its length. The
op table is `dl_ops.txt`; a generator produces the header for each end.

A display list can also be *narrated* (turned into descriptive text) and
reduced to *facts* (which buttons and rows it contains). The test
scripts' `expect-narrate` and `expect-control` read those outputs.

### 7.7 The host — `tiku_host.h`

The host is the interface to the windowing system. Its functions are
`open`, `close`, `present`, `poll`, `wait`, `scale`, `set_title`,
`set_fullscreen`, `set_cursor`, `pointer`, `cancel_composition`,
`present_stats`, and `tiku_host_show` (open a window, show one surface,
wait for a key). Two implementations exist: `tiku_x11.c` (with
`tiku_x11_clipboard.c` and a per-host `tiku_x11_command_*.c` that says
which key is the command key), and `tiku_cocoa.m`, a single AppKit file
for macOS. `mk/toolchain.mk` selects `TIKU_HOST=cocoa` on macOS and
`x11` elsewhere. `tiku_present.c`, shared by both, compares the new
frame with the previous one in 64 × 32 tiles and returns the changed
rectangles.

Input crosses the host as `tiku_event_t`: pointer, key, wheel, resize,
activation, close, clipboard, and text input. Modifiers are logical:
`TIKU_MOD_CMD` is Control on Linux and Control or Command on macOS.
Printable keys carry their character. `tiku_text_input.c` assembles
complete UTF-8 strings from an input method so composed characters
arrive whole. `tiku_clipboard.h` is the portable clipboard store; each
host mirrors it to the X11 `CLIPBOARD` selection or the macOS
pasteboard.

## 8. The Translation Kit

Directory: `kits/translation/`. This kit converts images and fonts from
external formats into the desktop's own.

**Icons — `tiku_icons.h`, `tiku_hvif.h`, `tiku_iconpaint.h`.** The
standard Tracker icons are compiled in as HVIF, the Haiku vector icon
format. One vector definition serves every size; the renderer
rasterises at the requested size with analytic anti-aliasing. Icons are
named in Tracker's vocabulary (`folder`, `trash_full`,
`microcontroller`). `tiku_icons_tiku.c` holds icons drawn for this
desktop, and an application bundle may carry its own. Rendered icons
are cached by name and size.

**Images — `tiku_image.h`.** Decodes BMP, PNG (with the kit's own
inflate implementation), and baseline JPEG. Input bytes and output
pixels are bounded. The format is detected from the file's first bytes,
not its name.

**Thumbnails — `tiku_thumb.h`.** Decides when to make a thumbnail, where
to store it, and queues the work so it does not block the window. A
cached thumbnail is used only if it is newer than the file. Large images
go on a separate slow queue. A list entry shows its type icon until the
thumbnail is ready.

**User fonts — `tiku_fontdir.h`.** Scans a folder of `.ttf` files and
groups them by the family name inside each file. The families appear in
the Fonts window.

## 9. The Application Kit

Directory: `kits/application/`. This kit defines how an application is
written and how it runs, in the desktop's process or in its own.

**The descriptor — `tiku_app.h`.** An application is a
`tiku_app_descriptor_t`:

```c
const tiku_app_descriptor_t tiku_notes_app = {
    .id       = "org.tikuos.notes",
    .name     = "Sticky Notes",
    .start    = notes_start,     /* open windows, send first frames */
    .stop     = notes_stop,
    .tick     = notes_tick,      /* called every loop turn with the time */
    .pick     = notes_pick,      /* a menu command for one window */
    .event_in = notes_event_in,  /* an input event and the window it hit */
    .closed   = notes_closed,    /* the desktop closed a window */
};
```

`pick` and `event_in` must return 0 when they have handled the input.
A nonzero return means "the application is finished", and the host
exits.

**The services — `tiku_app_services_t`.** The services are the functions
an application calls to reach the desktop. They are: `open`, `frame`
(send a finished pixel frame), `present` (send a display list instead of
pixels), `menus`, `close`, `configure` (title, size, resizable,
zoomable), `place` (request a window *role*, such as announcement or
panel, never a position, because the application does not know the
screen), `resize`, `sizeable`, `raise_window`, `pick` (open a file
panel), `clip_set` and `clip_get`, `help_page`, `help_open`,
`help_html`, `send_message`, and the board services: `board_names`,
`current_device`, `device_stats`, `board_run`, `board_input`,
`board_stop`, `board_save`, `board_port`, `board_mcu`, `board_release`,
`board_reclaim`, and `board_find`. Through the board services an
application uses a board that the desktop already owns.

**The window session — `tiku_remote.h`.** The window session is the
protocol between an application process and the desktop. A window is a
surface plus a route for events. Menus travel as `tiku_menuset_t`.
Menu picks return as command numbers. The wire format is little-endian
structs, `[u32 type][u32 length][payload]`, over a Unix stream socket at
`$HOME/.config/tracker/desk.sock`. `HELLO` carries a protocol version.
The message types are `HELLO`, `WELCOME`, `OPEN`, `CONFIGURE`, `FRAME`,
`SURFACE` (a shared-memory surface, used when both ends are on the same
machine), `DRAW` (a display list), `MENUS`, `PLACE`, `RAISE`,
`SIZEABLE`, `EVENT`, `PICK`, `CLOSE`, `CLOSED`, `SHELL_IN`,
`SHELL_OUT`, `BASIC_IN`, `BASIC_OUT` (a board's console carried on the
session), `TELL`, `SAY`, `DESCRIBE`, `READY`, and `SHUTDOWN`. A board's
window session uses the same protocol over a channel, so a board window
and an application window are the same kind of object to the desktop.

**The client — `tiku_client.h`.** `tiku_client_run(&descriptor)`
connects to the desktop, calls `start`, and pumps events until the
application is done. When compiled with `TIKU_APP_SO`, the same source
file exports `tiku_app_v1`, which `tiku-run` loads. When compiled as a
program, `main()` calls the client.

**Native controllers — `tiku_window_app.h`.** For applications that have
their own window controllers (Reader, Image): a private workspace whose
windows are published over the session. Only pixels, menus, and events
cross the process boundary.

**The conductor — `tiku_conduct.h`.** A control channel for driving a
running desktop from another process. It has two commands: `INJECT` one
input event and `QUERY` one pixel or one named string. It is off unless
the desktop is started with `-conduct`. Test scripts use it when they
drive a desktop that has a real display.

## 10. The Media Kit

Directory: `kits/media/`.

`tiku_sound.h` is a table of named sound events in the R5 style: the
startup chime, the beep, window open, window minimise, and so on. Each
event has a clip, an on/off setting, and a volume share. A shell raises
an event; it does not know what plays. The kit hands PCM data to a
backend one chunk per loop turn, so the loop never waits on the audio
device. `tiku_score.h` defines a text format for sounds (`wave`,
`attack`, `release`, `note`, `rest`) so a system sound can be reviewed as
text and rendered at any sample rate. `tiku_wav.h` reads WAV files. The
backend is one file per host: PulseAudio on Linux, CoreAudio on macOS.
With no backend, events are silent but still counted, which is what
scripted tests check.

---

# Part III — The Tracker and its shells

## 11. The Tracker core

`system/shared/core` and `src/ui` contain the file manager. They were
written from a reading of Tracker's source; `spec/ARCHITECTURE.md`
cites where each structure was read, and `FEATURES.md` lists 889
behaviours as contract rows, each marked done or differing. The core
includes no drawing headers.

**The pose view — `src/ui/poseview/`.** One engine serves icon,
mini-icon, and list mode from one state, as `BPoseView` did. The state
includes the pose list, the filtered list produced by type-ahead, the
visible order, the selection, the columns, and the grid and drop target
used for icon placement. Twelve files split the engine by concern:
columns, drawing, extent, icon mode, keys, loading, rename, selection,
sort, spatial placement, and type-ahead. Directory reading is
asynchronous: a listing arrives in batches from a worker, and change
notices use the same code path as the initial read.

**State on the objects.** Icon positions, column sets, and view state
are stored as attributes of the folder (`_trk/pinfo`, `_trk/columns`,
`_trk/viewstate`, Tracker's names), through `tiku_state.h`: extended
attributes on the host, a sidecar file for a board.

**Around the view.** `titleview` handles the column header strip and
what a drag on it means: a press starts a resize or a reorder, and
thresholds decide whether the result is a click, a reorder, a resize, or
a column removal. `cwmenu` builds the context menu from the selection.
`navigator` and `navmenu` are the path bar. `find` is the Find window.
`springmenu` is the folder menu that opens when a drag hovers over a
folder. `nodeicon` chooses which icon a node shows.

**Commands and models — `src/core`.** `drag` and `drop_protocol`
implement drag and drop with Tracker's copy/move rules. `launch` chooses
which application opens a document, using a registry in the same way
Tracker used its MIME database: an exact type match ranks above a
supertype match, which ranks above an application that accepts
anything. `types` is the type registry. `select` and `match` implement
Select by pattern. `info` is the Get Info model, `panel` the file panel
model, `frame` window placement, `switcher` the window switcher. The
rest are `associations`, `prefs`, `deskprefs`, `soundprefs`,
`background`, `color`, `alert`, `repaint`, `task` (the load meter), and
`state_pipeline`.

**Windows — `src/ui/windows/`.** The folder window (split into
lifecycle, editing, and rendering files), About, Get Info, Find, Select,
Open With, the file panel, five preference windows (Tracker, desktop,
Deskbar, fonts, sounds), and the switcher. Each is a window whose draw
and event callbacks the shell installs into the workspace.

## 12. The Desktop shell

`system/desktop/` is the spatial desktop: 21 files that
share one `desktop_t` struct through `tiku_trk_desktop_private.h`. No
file outside the directory includes that header.

### 12.1 The main loop

`tracker_run()` in `tiku_trk_desktop.c` is the loop. One iteration is a
*turn*, with three phases.

1. **`desktop_turn_begin`.** Apply results from the workers. Reload the
   settings file if it changed. Pump the window sessions (application
   and board windows) and the conductor. Check for a report request.
2. **Events.** Read every pending event from the host (or from the
   script adapter) and dispatch it: `desktop_pointer_down`, `_move`,
   `_up`, the key handler, resize, close. All routing decisions are in
   `tiku_trk_desktop_events.c`.
3. **`desktop_turn_end`.** Pump the devices (a listing still arriving
   keeps the window redrawing). Pump change notices. If anything is
   dirty and at least 16.7 ms have passed since the last frame, call
   `draw_all` and then `blit`. Advance the splash and shutdown
   animations. Sleep until the next scheduled task or until the host
   has input, whichever is sooner.

`draw_all` draws the whole desktop in a fixed order: the backdrop; the
Deskbar (below or above the windows, according to its setting); the
workspace (every window, then any closing-window copies); then the
overlays: the Get Info hover, the column-strip drag, the drag image, the
menu bars, the open menu (last, because it is above everything), the
status overlay, the splash, and the Force Quit overlay. A partial
repaint limits all of this to a damage rectangle. The workspace's
content cache respects the damage rectangle, so a window being dragged
is repainted from cached pixels. The shadow's extent is included in the
damage.

`blit` presents the surface through the host. The host compares it with
the previous frame and uploads only the changed tiles. The desktop
records draw time per frame, present time, frames over 16 ms, and cache
hits, and includes them in the `SIGUSR2` report.

### 12.2 Where things live

| file | contents |
|---|---|
| `lifecycle` | command-line options, startup, the host, the splash, shutdown |
| `events` | all pointer and key handling; the capture rule (a window that receives a press also receives the release); the column strip; the selection band; drags |
| `draw` | `draw_all`, `blit`, damage tracking, the overlays |
| `commands` | the File, Window, Attributes, and system menu commands |
| `globalbar` | the global menu bar and the Devices menu |
| `deskbar` | handling of Deskbar picks: raise, minimise, close, the leaf menu |
| `mounts` | device mounts, the periodic sweep, the change-notice pump |
| `wire` | polling a board for changes, as one job per board, never on the main thread |
| `boardrun` | running a program on a mounted board on behalf of an application |
| `remote` | windows of out-of-process sessions: frames, menus, configure |
| `embed` | the services implementation for a descriptor hosted in-process (demos only) |
| `apps` | the application registry, launching, Open With |
| `panels` | the file panel and the desktop's own alerts |
| `vfs` | the VFS-over-owner service for device utilities |
| `associations`, `deviceprefs`, `forcequit`, `report`, `startup` | as named |

### 12.3 Three kinds of window

The workspace treats all windows alike. The shell distinguishes three
kinds.

- **Tracker's own windows.** Folder windows, Get Info, Find, the
  preference windows. Their draw callbacks belong to the ui layer.
- **Remote windows.** An application's or a board's.
  `tiku_trk_desktop_remote.c` turns an `OPEN` message into a workspace
  window. The window's draw callback copies the last frame the session
  sent, or plays the session's display list. The shell publishes the
  session's menus and forwards events with window-relative coordinates.
  Closing is initiated by the desktop: Cmd-W or the close box sends
  `CLOSED`. The application is told; it cannot refuse.
- **Embedded windows.** A descriptor hosted inside the desktop process
  through `tiku_trk_desktop_embed.c`, which implements the services
  contract in-process. This is used for demonstrations. Installed
  applications never run this way.

### 12.4 Session and supervisor

`src/shell/session/` saves and restores the desktop: every open folder
window, each view's state and columns, and the Deskbar's position and
width. The state is stored in the sidecar for a board or in the local
store. At startup the shell opens the paths given on the command line;
otherwise the saved session; otherwise the home directory. At shutdown
it finishes any file operation in progress, with a time limit, resolving
conflicts by renaming.

`tiku-desk` (`system/supervisor/`) is a supervisor. It starts `TikuDesktop`,
keeps its stderr, and if the desktop exits abnormally it records the
signal, the exit status, and the last output, then restarts the desktop
once with a pointer to that record so the first thing on screen
explains what happened. If the desktop fails twice in a minute, the
supervisor leaves it stopped.

## 13. The Deskbar

`system/deskbar/` is the Deskbar. It has three sections, in the
original's order: the leaf menu (Find, Force Quit, Settings, the
Applications and Device Tools submenus, recent folders, Shut down); the
list of running applications, with each application's windows nested
under it in expanded mode; and the tray, with replicants and the clock.
It can dock at any corner or along an edge, in mini or expanded form.
Its preferences are a separate window. Its behaviour is documented
against the `DB-*` rows of `FEATURES.md`. The shell sends the Deskbar the
current window list every frame. Deskbar picks (raise, minimise, close)
return to the shell as commands, and the shell carries them out through
the workspace.

## 14. The browser shell

`system/tracker/` is the single-window browser: one
folder at a time, a navigator bar, the same pose view, the same file
operations, and the same preferences, in thirteen files. It exists so
that the file manager is tested as a product independent of the spatial
desktop. Every script that can run in both shells does. One bug, a
queued file operation that never ran, was found only because the second
shell exposed it.

---

# Part IV — Applications

## 15. Writing an application

The smallest application is `examples/apps/hello.c`:

```c
static int
hello_start(void **state, const tiku_app_services_t *services)
{
    hello_state_t *st = calloc(1, sizeof *st);

    st->services = services;
    /* The application owns its pixels. It sends finished frames
     * through the services; nothing else draws into its window. */
    st->surface = tiku_surface_new(HELLO_W, HELLO_H, TIKU_C_PANEL);
    st->id = services->open(services->ctx, "Hello", HELLO_W, HELLO_H);
    tiku_text(st->surface, tiku_font_bold(), 20, 40 + font->ascent,
              "Hello from a descriptor.", TIKU_C_TEXT);
    (void)services->frame(services->ctx, st->id, st->surface->px,
                          HELLO_W, HELLO_H);
    *state = st;
    return 0;
}
```

The file also defines the descriptor, an export symbol for the module
loader, and a `main()` for standalone use:

```c
#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &hello_app
};
#endif
#ifndef TIKU_APP_EMBED
int main(void) { return tiku_client_run(&hello_app); }
#endif
```

A manifest, `hello.app`, describes the bundle. The desktop reads it
without executing anything:

```ini
version=1
id=hello
name=Hello
module=hello.so
category=demos
icon=application
types=
```

`mk/apps.mk` compiles the module against the position-independent kits
into `build/runtime/apps/hello/` and copies the manifest beside it as
`app.manifest`. The Deskbar's Applications or Demos menu lists the
bundle by its `category`. `types` lists the document types the
application reads; this determines whether it appears in Open With.
`help` names a page under the help root. `startup` migrates an old
preference identity and does not start anything automatically.

**What the runtime provides.** Launching runs `tiku-run <module>
[document]` in a new process. The runner loads the module, adds the
bundle directory to the child's environment, and connects to the desk
socket. The desktop places, frames, focuses, and closes windows. Menus
are sent as data; the application sends them again when they change.
The global menu bar shows them while the window is focused. Text input
arrives as complete strings. The clipboard is a service. A crash in the
application does not affect the desktop. This is process isolation, not
a security sandbox: the application runs under the user's account and
links the kits.

**What the application must do.** Return 0 from `pick` and `event_in`
unless the application is finished. Keep each window's surface at the
size given by the last `RESIZE` event. Implement `closed`, or the
application will draw into a window that no longer exists. Run slow
work on a worker (`tiku_work`) and apply the result in `tick`. Never
include a `tiku_trk_*` header; the boundary check rejects it.

**Testing.** Compile the application source with `TIKU_APP_EMBED` into a
unit test, supply a `tiku_app_services_t` of stub functions, and call
`start`, `event_in`, `tick`, and `pick` directly.
`tests/apps/test_notes.c` is an example. Then write a driven
script: a setup script copies the module into the test's application
directory, the script opens the application from the Applications menu,
and `expect-title`, `expect-menu`, `expect-narrate`, and
`expect-file-has` verify the result.

## 16. The applications that ship

| bundle | description | notes |
|---|---|---|
| Editor (`apps/editor`) | a text editor in the style of Pe: multiple documents, find, syntax colouring | see `PE_REFERENCE.md` |
| Terminal (`apps/terminal`) | a terminal over a pseudo-terminal | |
| TikuBasic, TikuC (`apps/ide`) | the IDE: projects, builds for the host or a board, error messages that jump to a line, a BASIC debugger | one code base; the BASIC host interpreter is built from the board's interpreter source in `apps/basichost` |
| Sticky Notes (`apps/notes`) | one note per window and per file; important notes listed first in the Notes menu | double-click selects a word, triple-click a line |
| Reader / Help (`apps/reader`) | documents and help pages, with bookmarks and navigation | Help requests start it in help mode with structured data over a private descriptor |
| Image (`apps/image`) | an image viewer built on `tiku_image` | a native controller through `tiku_window_app` |
| Console (`apps/console`) | a board's console, and a network console | uses `SHELL_IN` and `SHELL_OUT` on the session |
| Bench (`apps/bench`) | runs the TikuBench suites on a board and reports results | |
| Device Monitor, VFS Inspector (`apps/deviceutils`) | live readings and the device namespace, through `tiku_vfs_rpc` | a write requires a read, a comparison, and confirmation |
| Calculator, developer Calculator (`apps/calculator`, `apps/developer-calculator`) | a plain calculator, and one with number bases and bit widths | |
| Hex (`apps/hex`) | a hex editor | |
| examples (`examples/apps`) | hello, clock, counter, sketch, twowin, sensors, and a framework demo | each shows one feature |
| samples (`examples/diagnostics`) | the widget gallery, the sound gallery, the probe, a metrics generator | standalone programs, not bundles |

---

# Part V — Boards

## 17. A board on the desktop

A TikuOS board connects to the desktop by cable, network, or radio. The
desktop uses it in three ways.

**As a volume.** With `-devices`, the desktop scans the serial ports
(`tiku_sys_serial_ports`), asks each candidate for its name, and lists
it in the Devices menu at the right end of the global menu bar, for
example *Connect to nRF54L15 (cu.usbmodem…)*. Connecting opens a
session, reads the namespace manifest, and mounts a Tiku backend at
`/devices/<name>`. A folder window on `/devices/nrf54l15/sys` lists
`mem`, `cpu`, `power`, and `radio`, with typed columns. `/data` is a
real directory; files can be dropped into it. The desktop polls each
board for changes every few hundred milliseconds. The poll is a job on a
worker, one per board, so a board that stops answering does not block
the loop. A board that disconnects is unmounted at once.

**As a window.** A board built with `TIKU_APPL_GUI` speaks the window
session over a channel framed on its console line (`tiku_frame`,
`tiku_channel`). It opens a window and, since it has no pixels, sends
`DRAW` messages carrying a display list. The desktop draws the window
with its own widgets. The result is a Board window of the same kind as
an application window. A board with no shell can own a network port. A
board on a radio or a network dials in through a relay to a desktop
started with `-listen`.

**As a computer.** The About window (`tiku_trk_about_window.c`) asks each
connected board for its memory maps (`/sys/mem/sram_map`, `nvm_map`),
its clock (`/sys/cpu/freq`), its uptime, and its boot count, and draws
the SRAM and NVM bars with their regions (static, stack pool, tier, code,
store, persist). Each board has a page, with previous and next buttons.
Device Monitor plots readings. VFS Inspector edits nodes. The IDE builds
for the board and runs programs on it through `board_run`, receiving
output through `board_said`. The current device, chosen in the Devices
menu, is the default target for an application's board operations.

**Identity and state.** A board is identified by the key `tiku_ident`
derives from its identity nodes. Its icon positions and window state are
stored in a sidecar keyed by that string, so a board's windows return to
their places regardless of which port it appears on.

## 18. Two boards on one desktop

Nothing in the previous chapter is limited to one board. The namespace
holds eight backends. Each board has its own session, channel, worker
job, and sidecar. The About window pages through all connected boards.
The Devices menu lists them and marks the current one. On the day of
writing, an nRF54L15-DK and an Apollo510 EVB were connected at the same
time for the first time: both were discovered and mounted, both appeared
in About with their own memory maps, and the numbers matched what each
board's own shell reported. The `boardboth` test script covers a second
arrangement, one board on a cable and one connecting over IP.

---

# Part VI — Testing and portability

## 19. Tests, scripts, and checks

`make test` runs four layers of tests.

**Unit tests.** 157 programs under `tests/test_*.c`, one
per subject, found by file name. They print `[PASS] ROW` lines using the
contract's row ids (`IV-022`, `DB-030`, `CW-034`), so a failure names a
behaviour.

**Driven scripts.** 264 scripts under `tests/integration/shell/*.script`,
run by `run.sh` against both shells with no display. The window is
drawn into memory exactly as it would be on screen. A script is a list
of gestures and assertions:

```
click 71 8            menu-pick Important     key n ctrl
press 400 300         move 450 315            release 470 320
expect-title Milk     expect-pixel 293 156 fbca0a
expect-menu New note|Important||Yellow|Green|Blue|Pink||Delete note|Quit|
expect-narrate b.txt  expect-file-has /tmp/trk_home/note-003.txt Milk
expect-windows 3      expect-read /devices nrf54l15|apollo510|
shot /tmp/trk_look.png                        quit
```

`wait N` waits N loop turns, not seconds, so a script is deterministic.
A setup script under `setup/<name>.sh` prepares the home directory the
script runs in. Header lines declare requirements: `# board: yes`,
`# board: both`, `# board: discover`, `# prefs: factory`. A script can
check a pixel, a window title, a menu's full text, the window count, a
file's contents, a narration, or a sound event. This is how changes to
the look are verified: the September 2026 shadows changed nine
pixel-checking scripts, and each was updated with its intent unchanged.

**Checks.** The kit table, the architecture check, the application
boundary, the toolkit check, the contract check (`FEATURES.md` has no
duplicate rows), the menu-command check (every menu command reaches a
handler), and the shipping check (the shipping shells contain no
script-driver code).

**The conductor.** For driving a desktop that has a real display:
`-conduct` opens the `INJECT`/`QUERY` channel, and `build/dev/trk-conduct`
drives it from another process.

Two things the suite cannot verify: what a board on the bus changes
(three scripts fail when a board is plugged in, and the plan document
records why), and appearance, which is what the `shot` command is for.

## 20. Linux and macOS

Both hosts build the same sources. The differences are confined to three
places.

- `kits/support/tiku_sys.h` and its two implementations: processes,
  memory, the fault program counter, serial port enumeration and rates.
- `mk/toolchain.mk`: the Darwin section (`-D_DARWIN_C_SOURCE`, the
  `tiku_darwin.h` prelude that supplies names libc spells differently,
  the frameworks, `TIKU_HOST`).
- The per-host files on the platform seam: the volume table, the sound
  backend, the window host, the command key.

On macOS the desktop draws through `tiku_cocoa.m`: an AppKit window
whose view copies only the changed tiles, with the desktop's own loop
pumping AppKit events from inside `wait` and `poll`, and the Retina
scale taken from the host. No X server is needed. `make TIKU_HOST=x11`
still builds the Xlib host and its two X-specific test suites.
`make app` produces `build/TikuDesktop.app` containing the applications
and a launcher that knows which host it was built with. The test scripts
do not know which host drew the frame.

---

# Appendix A — Glossary

**Backend** — a table of file operations over one store: the host's
files or a board. **Behaviour file** — the part of a control that keeps
state and handles input; it compiles on the board; its paint code is in
a separate file. **Channel** — a socket pair that carries a board's
window session over its console line. **Conductor** — the inject/query
channel for driving a desktop with a display. **Descriptor** — an
application's table of entry points. **Display list** — a recording of
drawing calls, sent by a board instead of pixels. **Job** — a unit of
work given to a worker thread. **Lane** — one driven script. **Model** —
the object that knows what kind of thing a list entry is. **Mount** — a
backend attached at a path prefix. **Pose** — one entry in a view.
**Seam** — one declaration with one implementation file per host.
**Services** — the functions an application calls to reach the desktop.
**Session** — a prompt-aware conversation with a board's shell; also the
window session protocol. **Sidecar** — the host-side file holding a
board's UI state. **Tier** — a region of a board's memory, as reported by
its maps. **Turn** — one iteration of the shell's main loop. **Workspace**
— the compositor of in-process windows.

# Appendix B — Reading order

For the architecture: this book, then `docs/architecture/tracker/ARCHITECTURE.md`
(what Tracker was and what the rewrite reproduces),
`spec/ARCH-runtime-boundaries.md` (why applications are separate
processes), `docs/architecture/applications.md` (manifests, the ABI, the runtime),
and `examples/apps/README.md`.

For a kit: its header comments, in the order this book's chapters list
them, and the `doc/` folder where one exists: `message.md`,
`namespace.md`, `display-list.md`, `toolkit.md`, `controls.md`,
`text-input.md`, `sensors.md`.

For a behaviour: its row in `FEATURES.md`, the unit test that prints that
row, and the script that exercises it.

For the macOS port: `docs/plans/PLAN-macos.md`, which records what each milestone
found.

# Appendix C — File map

```
kits/support/      tiku_msg tiku_work tiku_watch tiku_trail tiku_report
                   tiku_strings tiku_manifest tiku_sys{,_linux,_darwin}
kits/device/       tiku_tx tiku_session tiku_frame tiku_link{,_serial,_tcp}
                   tiku_channel tiku_ns
kits/storage/      tiku_model tiku_backend_{posix,tiku} tiku_mount tiku_fs
                   tiku_state tiku_attr tiku_ident tiku_query tiku_notify
                   tiku_volume tiku_vfs_rpc tiku_config{,_outbox} tiku_vdir
kits/interface/    tiku_gfx tiku_theme tiku_present tiku_font tiku_ttf
                   tiku_cff tiku_glyphpath tiku_ui tiku_window tiku_menu
                   tiku_dl tiku_host tiku_x11{,_clipboard,_command_*}
                   tiku_cocoa.m tiku_event tiku_text_input tiku_clipboard
                   tiku_field tiku_textview tiku_list tiku_tabs tiku_slider
                   tiku_doc tiku_markdown tiku_sensor tiku_form tiku_layout
                   tiku_dialog tiku_alert tiku_press tiku_splash tiku_logo
kits/translation/  tiku_icons tiku_icons_tiku tiku_hvif tiku_iconpaint
                   tiku_image tiku_jpeg tiku_inflate tiku_thumb tiku_fontdir
kits/application/  tiku_app tiku_app_load tiku_client tiku_remote
                   tiku_window_app tiku_conduct
kits/media/        tiku_sound tiku_sound_{pulse,coreaudio} tiku_score tiku_wav
system/            desktop/ tracker/ deskbar/ shared/{core,ui,shell}/
                   runner/tiku_run.c supervisor/tiku_desk.c
apps/              editor terminal ide notes reader image console bench
                   deviceutils calculator developer-calculator hex basichost
examples/          apps/ diagnostics/
tests/             system/ kits/ apps/ integration/ fixtures/ tools/
```
