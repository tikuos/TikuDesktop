# The TikuDesktop Book

*The architecture of the desktop for TikuOS computers, written the way the
Be Book was written: one chapter per kit, each saying what the kit is for,
what it holds, and how a program uses it — then the shells built on the
kits, the applications that run on them, and the boards they connect to.*

This book describes the tree as it stands on 19 September 2026. Every
name in it is a file, a function or a type you can open; where a chapter
summarises a reasoning that a source file states at length, it says which
file. It is a guide, not a reference: the headers under `kits/` are the
reference, and they are written to be read.

---

## Contents

**Part I — Orientation**
1. What TikuDesktop is
2. The shape of the tree
3. The rules that keep its shape

**Part II — The Kits**
4. The Support Kit
5. The Device Kit
6. The Storage Kit
7. The Interface Kit
8. The Translation Kit
9. The Application Kit
10. The Media Kit

**Part III — The Tracker and its Shells**
11. The Tracker core
12. The Desktop shell
13. The Deskbar
14. The browser shell

**Part IV — Applications**
15. Writing an application
16. The applications that ship

**Part V — Boards**
17. A board on the desk
18. Two boards on one desk

**Part VI — Proving It**
19. Tests, lanes and gates
20. Two hosts, one tree

Appendix A — Glossary. Appendix B — Reading order. Appendix C — File map.

---

# Part I — Orientation

## 1. What TikuDesktop is

TikuOS is an operating system for microcontroller-class computers — an
Apollo510, an nRF54L15, an RP2350 — that are meant to be *computers*, with
a filesystem, processes, a shell and a namespace, rather than firmware.
TikuDesktop is the desktop for them. It runs on a Linux or macOS host,
draws a spatial desktop in the BeOS R5 manner, and treats each board on a
cable, a socket or a radio as a volume mounted under `/devices`, browsed
with the same windows that browse the host's own files.

Three ideas run through everything below.

**One toolkit, both ends.** The interface kit that draws the desktop's
windows is the same code a TikuOS board draws with. A control is written
once, its *behaviour* (state and input) apart from its *paint*, and the
board — which has no pixels to paint into — records what it would have
drawn as a display list and sends that. The desktop plays the list. So a
board's window on the desk is drawn by the board, with the desktop's own
widgets, over a serial line that could never carry the pixels.

**The process boundary is a deployment property.** An application is a
descriptor: `start`, `stop`, `event_in`, `tick`, `pick`, `closed`. It
draws into surfaces it owns and hands finished frames to *services*. It
never learns whether those services are the desktop's own functions in
one process or a socket to a desktop in another. Installed applications
always run out of process, each in its own `tiku-run`, so an
application's fault is its own; the contract is identical either way.

**The Tracker is the product, built two ways.** The file manager — the
pose view, the folder windows, the Deskbar, the preferences — is one body
of code composed into two shells: the spatial desktop (`TikuDesktop`)
and a single-window browser (`TikuTracker`). Every behaviour is proved in
both by the same driven scripts, run with no display at all.

## 2. The shape of the tree

The repository at `tikuOS/applications/` has five layers, each depending
only on the ones below it.

```
apps/            the applications: Editor, Terminal, IDE, Notes, Reader …
system/          tiku-run (application host), tiku-desk (supervisor)
TikuTracker/     the Tracker: core, ui, deskbar, and both shells
kits/            seven libraries: support, device, storage, interface,
                 translation, application, media
mk/              the build, one file per concern
```

By size, on the day of writing: the kits are 104 C files and 51 000
lines; the Tracker 62 000 lines across core, ui, deskbar and shells; the
applications 13 500 lines; 157 unit-test programs and 264 driven scripts.
The desktop shell alone is 21 files and 17 600 lines — one composition
split over files that share `tiku_trk_desktop_private.h` and nothing
else.

Everything builds into one `build/` directory:

| product | where |
|---|---|
| the kits | `build/libtiku_*.a`, PIC twins under `build/pic/` for loadable apps |
| the shells | `build/system/TikuDesktop`, `build/system/TikuTracker`; script-driven flavours under `build/dev/` |
| the hosts | `build/system/tiku-run`, `build/system/tiku-desk` |
| the applications | `build/apps/<id>/app.manifest` + `<Module>.so` |
| the tests | `build/tests/test_*` |
| the Mac bundle | `build/TikuDesktop.app` from `make app` |

## 3. The rules that keep its shape

A layer is only a folder until something refuses the include that would
undo it. Four checks run under `make test` and each fails the build on a
single wrong line.

**The kit table** (`kits/tools/check_kits.py`). A kit may include from
itself and from the kits its row names, and from nothing else — *including
a kit further down the list*, which is what makes it a table rather than
an order:

| kit | may include from |
|---|---|
| support | — |
| device | support |
| interface | — |
| storage | support, device |
| translation | support, storage, interface |
| application | support, device, interface |
| media | — |

No kit may name a `tiku_trk_desktop*` or `tiku_trk_app.*` header: a kit
that knows about the application on top of it has stopped being a kit.
The same check refuses a paint header (`tiku_gfx.h`, `tiku_font.h`,
`tiku_ui.h`, `tiku_dl.h`) inside a *behaviour* file, which is what keeps
a control's logic compilable on a board with no display.

**The architecture gate** (`TikuTracker/tools/check_architecture.py`).
Tracker core never includes X11 or a drawing header; no source outside a
short list may say `__linux__`, `__APPLE__` or open a host `/proc` path;
the shipping shells link no application code or module loader, proved by
reading their symbol tables. The list of files allowed to name a machine
is the *platform seam*: `tiku_sys_{linux,darwin}.c`, `tiku_volume.c`,
`tiku_state.c`, `tiku_backend_posix.c`, `tiku_link_tcp.c`, `tiku_cocoa.m`
and a handful more. A name that arrives on that list because somebody
reached for `#ifdef` is exactly what the check exists to stop.

**The application boundary** (`tools/check_app_boundaries.py`). No
application imports a desktop-private header; no shell contains
application code.

**The toolkit check** (`make check-toolkit`). The display-list op table
(`dl_ops.txt`) generates both ends' headers and fails when either is
stale; every behaviour file is compiled with the *board's* compiler under
`TIKU_DISPLAYLESS`; text metrics agree between the desktop's faces and
the board's tables.

The rest of this book is what those rules protect.

---

# Part II — The Kits

Each kit is a directory under `kits/`, a static archive, and a set of
headers whose first comment says what the file is for. A program links
the kits it needs; the archives are built in link order (application,
translation, interface, storage, device, media, support) because a kit
must come before the kits it draws on.

## 4. The Support Kit

*What every other kit stands on, and nothing that draws or talks to a
board.* `kits/support/`.

**A message that describes itself — `tiku_msg.h`.** In shape it is Be's
`BMessage`: a code saying what the message is, and under it named fields,
each with a type and one or more values. A reader asks for what it knows
by name and steps over what it does not, which is the version tolerance a
struct compiled at both ends cannot offer once the device on the cable
runs last month's firmware. The flat form is little-endian throughout,
every value carries its length, the flat size can be asked before
writing, and nothing instantiates a class named in the data. It is the
envelope everything else carries: device replies, launch data, the
conductor's answers. `doc/message.md` argues the four differences from
`BMessage`.

**Workers for anything that waits — `tiku_work.h`.** The desktop's loop
is the one owner of the desk's state. A *job* is what it hands to a
worker: self-contained, touching nothing it was not given, returning a
result the loop applies on its next turn. The kit is two rings — requests
in, results out — and a few threads. Directory listings, thumbnails,
board polls and the serial open that can block for thirty seconds on a
Mac all go through it, which is why a window never freezes on them.

**Noticing a turn has not ended — `tiku_watch.h`.** A shell with one
thread freezes by not coming back from something, and nothing inside it
can notice. A watcher outside the loop counts turns; when one stops
turning it writes down what that turn was doing.

**What the program was doing — `tiku_trail.h`, `tiku_report.h`.** The
trail is a fixed ring of formatted lines, always on and never written to
disk, so the last thing a program did is there when it stops doing
anything. A report is what a fatal signal leaves: the signal, where it
happened (through `tiku_sys_fault_pc()`), and the trail up to it — then
the program dies as it would have, so a core still drops. `SIGUSR2` asks
a running desktop for the same report on demand.

**The words — `tiku_strings.h`.** Every line a person reads is named
rather than written in code. `tiku_str("menu.about")` returns the line in
the language in force from `strings/<lang>.xml`; the English is baked in
as the floor.

**Manifests — `tiku_manifest.h`.** A version-1 `key=value` file read
without executing anything. Unknown, duplicate, overlong and malformed
fields fail closed; `module` is a leaf name, never a command.

**What only the machine can answer — `tiku_sys.h`.** One declaration, one
file per host, chosen by the Makefile from `uname -s`: processes, memory,
uptime, the executable's own path, a fault's program counter, serial
port enumeration and rates above what `termios` names. Everything else in
the tree is written to POSIX and says the same thing on both hosts.

## 5. The Device Kit

*Bytes to and from one board, and the conversations built on them.*
`kits/device/`. Five layers, each opaque to the one above.

**Transport — `tiku_tx.h`.** Serial, TCP, or a caller's own descriptor and
hooks, behind three calls. Nothing above knows which. A serial open is
allowed to take seconds, so it is never done on a thread that draws.

**Session — `tiku_session.h`.** A prompt-aware conversation with one
device: send a command, get back what the device printed before its next
prompt. Asynchronous `~<path>` subscription lines are routed to a handler
wherever they land in the stream. A session also hands over *raw* text —
a running program's output has no prompt to end it until it ends.

**Frames — `tiku_frame.h`.** A frame is `END`, a marker byte naming its
channel, the payload escaped the SLIP way, `END`. The decoder cuts one
marker's frames out of the byte stream and calls everything else text.
This is how a board's window session shares the console line with its
shell.

**Links — `tiku_link.h`, `tiku_link_serial.h`, `tiku_link_tcp.h`.** Whole
messages to and from one board on any medium: the desktop's mirror of the
kernel's link. A message arrives whole and intact or not at all; a link
that loses one says so, and the session above re-greets.

**Channels — `tiku_channel.h`.** The desktop's window session speaks over
a file descriptor. A channel gives it one: a socket pair, one end for the
desktop's listener to adopt, the other fed by the messages a link carries
from the board. It greets the board, re-greets after a loss, backs off
when nothing answers, and can hold a port of its own for a board built
without a shell.

**The namespace mirror — `tiku_ns.h`.** A board's `/sys/vfs/manifest`
describes its whole namespace: path, kind, permissions, a typed
descriptor, capabilities. The mirror reads it once, then values on demand
or as `~` lines when subscribed. This is what every board window renders.

## 6. The Storage Kit

*The Model, the backends, and one namespace over all of them.*
`kits/storage/`.

**The Model — `tiku_model.h`.** Tracker's `Model` is the one place that
knows what kind of thing a pose wraps; everything above it asks questions
(`IsContainer`, `IsWritable`, `Kind`) rather than testing a backend. That
is what lets local files and a device namespace be browsed by the same
view. A device directory is `VIRTUAL_DIR`, not `DIRECTORY`: it cannot be
created in, renamed or pasted into, and the whole read-only story for
`/sys`, `/dev` and `/proc` follows from that one answer. A dynamic store
like `/data` *is* a real directory.

**Backends — `tiku_backend_posix.c`, `tiku_backend_tiku.c`.** A backend
is a table of operations (`tiku_backend_ops_t`): list, stat, read, write,
rename, remove, ids. The POSIX backend is the host's files; the Tiku
backend is a board, answered through a session and its namespace mirror.

**One namespace — `tiku_mount.h`.** A namespace over several backends,
itself a backend. Each is attached at a path prefix — `/` for the host,
`/devices/board1` for a board — and every operation goes to the longest
prefix that claims it. Three things it must do that a forwarder would
not: rewrite paths both ways, so a board asked about `/sys/mem` answers
as `/devices/board1/sys/mem`; synthesise the ancestors, so `/devices`
exists because something is mounted under it; and keep identities apart,
so two backends' inode 12345 stay distinct — ids leaving the router carry
the mount in their top byte. Eight backends at most: the local files and
seven devices. `doc/namespace.md` is the reasoning.

**File operations — `tiku_fs.h`.** Copy, move, duplicate, rename, trash,
undo, with Tracker's rules: the same-volume test decides move versus
copy; duplicate invents a name; every operation reports progress and
answers conflicts through an alert, so a window never freezes waiting.

**Where UI state lives — `tiku_state.h`.** Tracker's
`AttributeStreamNode` seam: pose positions, column sets and view state go
through one interface, so "the object holds its own state" (local files,
in extended attributes) and "the viewer holds it for the object" (device
nodes, in a sidecar keyed by the device's identity) are one code path.

**Attributes and descriptors — `tiku_attr.h`.** A TikuOS node carries its
own display hint: the manifest's meta column is a typed descriptor
(`vtype,unit,freshness,cost[,lo..hi]`). It is what shows a reading as
"1.2 MiB" or "15h 57m" rather than a bare integer, and what refuses
"banana" to a `u32` before the device is troubled.

**Identity — `tiku_ident.h`.** A device is recognised by a scored match
over the identity nodes it publishes, modelled on Tracker's
`MatchArchivedVolume`; every durable store keys on the string this
produces, so there is exactly one spelling of "which board is this".

**Queries — `tiku_query.h`.** A predicate plus a scope, as in Tracker:
terms of (attribute, operator, value), joined with and/or, with the
folder filter applied outside the predicate so it can be added to a
running search. No index needs to exist: a volume is a namespace subtree
and the type registry is the node's own descriptor. A search is *stepped*
— sixteen directories a turn — so a real disk never stalls the desk.

**Change notices — `tiku_notify.h`.** Tracker feeds node-monitor,
path-monitor and live-query notices through one handler; that entry point
comes first and every source is made to fit it. The host's source is a
directory watch; the device's, which cannot push folder-level notices
yet, is derived by diffing a re-listing against what the view holds.

**Volumes — `tiku_volume.h`.** The mount table, partitions, mount and
unmount, behind one interface with a Linux and a Darwin body: `/proc`
files on one, `getmntinfo` and DiskArbitration on the other. The
partition scan is cached on the Mac, keyed on the disk names and the
mount table, because the Mount menu asks on every frame a folder window
draws.

**VFS over the owner — `tiku_vfs_rpc.h`.** Device utilities never open a
second serial connection. They ask the desktop, which serialises the
request with its own traffic on the device's worker, pins the device by
identity, and requires a write to be preceded by a read whose bytes still
match. Replies carry a session generation so a late one cannot reach a
replacement application.

## 7. The Interface Kit

*Every pixel the desktop draws, and the seam to the machine that shows
them.* `kits/interface/` — 51 headers, the largest kit, and the one that
also runs on the board.

### 7.1 The surface and the theme — `tiku_gfx.h`

A surface is a plain 32-bit buffer (`0x00RRGGBB`) with a clip rectangle
and a *scale*: a Retina surface keeps its pixels at `(w×scale) ×
(h×scale)` and every call still takes logical coordinates. The
primitives: `tiku_fill`, `tiku_frame`, `tiku_hline`/`vline`, `tiku_blit`,
`tiku_copy_bits`, `tiku_veil` (a blend of one colour over a region), and
since September 2026 `tiku_shadow`, `tiku_fill_round`,
`tiku_frame_round`, `tiku_grab`, `tiku_blend` and `tiku_blend_scaled` —
the soft things a modern desk is made of.

The theme is one runtime table of *semantic* colours — panel, document,
text, tab, selection, focus, backdrop, danger, the code inks — and every
widget paints through it. The default table is the R5 look (`apps/R5-SPEC.md`
holds the measured values: panel 216, tab 255,203,0, backdrop 51,102,152);
a dark table recolours every widget without moving a layout. Every shade
of the panel comes from `tiku_tint()` (BeOS's `tint_color` arithmetic)
rather than a second constant.

### 7.2 Text — `tiku_font.h`, `tiku_ttf.h`, `tiku_glyphpath.h`, `tiku_cff.h`

The UI face is baked: 8-bit coverage maps generated by `genfont.py`, so
text is anti-aliased like R5's and the runtime carries no font library.
A face read from a `.ttf` or `.otf` file rasterises what it is asked for
through the kit's own TrueType and CFF interpreters — Greek, Cyrillic,
kana — and keeps it. Stem darkening is applied to analytic coverage so
small text reads as ink. The face size is a preference (12 px by
default), and every layout in the tree *asks* how tall a row or a tab is
rather than writing 22 down.

### 7.3 Controls — `tiku_ui.h` and the behaviour/paint split

`tiku_ui.h` is the R5 control set as immediate-mode drawing: each call
paints one control in one state — `tiku_ui_button`, `checkbox`, `radio`,
`menufield`, `scrollbar`, `tab_widget`. Layout and hit-testing belong to
the window system, so these stay pure. States combine:
`TIKU_S_DISABLED`, `TIKU_S_DEFAULT`, `TIKU_S_PRESSED`, `TIKU_S_FOCUS`.

Stateful controls come in pairs. `tiku_field.c` keeps a text field's
state and takes its input; `tiku_field_event.c` maps events to it;
nothing in either draws. `tiku_slider.c` / `tiku_slider_paint.c`,
`tiku_tabs.c` / `tiku_tabs_paint.c`, `tiku_list.c` / `tiku_list_paint.c`,
`tiku_textview.c` (a multi-line editor with selection, undo and a caret),
`tiku_doc.c` / `tiku_doc_paint.c` (a laid-out document),
`tiku_markdown.c`, `tiku_sensor.c` / `tiku_sensor_paint.c` (plots,
meters, sparklines) — all the same shape. `tiku_press.c` tracks a press
by a window's own hit ids so a button acts on release and shows itself
held. `tiku_dialog.c` and `tiku_alert.c` compose these into the R5 alert
and the modal panel; `tiku_form.c` and `tiku_layout.c` lay a described
panel out from a specification.

### 7.4 Windows — `tiku_window.h`

*Composited in-process windows.* A workspace holds up to sixteen windows
over one surface: their frames, z-order, focus, and a cache of each
window's content pixels so a drag repaints only what moved. A window has
a tab with a close and a zoom button, a border, a resize grip, a title,
and a *draw* callback that paints its content into a clip; it may publish
menus (a `tiku_menuset_t`, flat data) and a pick callback. The desktop
owns z-order, focus, invalidation and chrome; the window's owner owns
what is inside the content rectangle.

Since September 2026 a window casts a soft shadow that reaches
`TIKU_WINDOW_SHADOW` (14 px) past its frame, its corners are rounded
where no tab stands, a window opening rises eight pixels into place over
150 ms, a closed one leaves a ghost that shrinks and fades, and a
minimised one flies to its Deskbar row. All of it runs on a *workspace
clock* the shell ticks only when it has a real window: a scripted run
tells the workspace no time, and its frames stay still.

### 7.5 Menus — `tiku_menu.h`

The running menu object the shell tracks a pointer through and draws:
items with marks, shortcuts, icons, submenus; layout; hot-row tracking.
Not the publish protocol — `tiku_menuset_t` is what an application hands
across, this is what the shell makes of it, built fresh each time a menu
opens so labels and enabled states are decided from the current
selection.

### 7.6 The display list — `tiku_dl.h`

*What was drawn, rather than what it came out as.* A session hands its
window over as pixels; a 372×302 window is 449 376 bytes, thirty-nine
seconds at 115 200 baud. The display list is the third way to say what a
window looks like: the *calls* — "a button, here, disabled, labelled
this" — around six hundred bytes for that same window. It is deliberately
not an app_server: nothing draws a line on anybody's behalf, the
vocabulary is the interface's own and semantic where the interface is
semantic, and measuring stays at the sending end because both ends carry
the face tables. The recorded form is the wire form, `[u16 op][u16
length][payload]`, so a player steps over an op it does not know. The op
table lives in `dl_ops.txt` and generates both ends' headers.

A list can also be *narrated* — turned into the words a screen reader or
a test would say — and reduced to *facts* (which buttons, which rows).
The driven scripts' `expect-narrate` and `expect-control` read those.

### 7.7 The host — `tiku_host.h`

The one seam to the machine that shows pixels and delivers input:
`open`, `close`, `present`, `poll`, `wait`, `scale`, `set_title`,
`set_fullscreen`, `set_cursor`, `pointer`, `cancel_composition`,
`present_stats`, and `tiku_host_show` for a sample with one picture to
show. Two bodies fulfil it: `tiku_x11.c` (with `tiku_x11_clipboard.c` and
a per-host `tiku_x11_command_*.c` that says which key is Command) and
`tiku_cocoa.m`, one AppKit file for the Mac. `mk/toolchain.mk` picks
`TIKU_HOST=cocoa` on Darwin and `x11` elsewhere. `tiku_present.c` is the
damage diff both share: it compares the new frame against the last in
64×32 tiles and returns the rectangles that changed.

Events cross this seam as `tiku_event_t` — pointer, key, wheel, resize,
activation, close, clipboard, text input — with logical modifiers
(`TIKU_MOD_CMD` is Control on Linux, Control *or* Command on the Mac) and
printable keys as their characters. `tiku_text_input.c` assembles bounded
UTF-8 commits from an input method so an accent or an ideograph reaches a
field whole. `tiku_clipboard.h` is the portable store; the hosts mirror
it to `CLIPBOARD` or the pasteboard.

## 8. The Translation Kit

*Pictures and letters from other formats into the desktop's own.*
`kits/translation/`.

**Icons — `tiku_icons.h`, `tiku_hvif.h`, `tiku_iconpaint.h`.** The
standard Tracker artwork compiled in as HVIF, Haiku's vector icon
format: one definition serves every size and the renderer rasterises at
the size actually asked for, with analytic coverage so edges are smooth.
Names are the Tracker's own vocabulary — `folder`, `trash_full`,
`microcontroller` — and a bundle can carry an icon of its own.
`tiku_icons_tiku.c` holds the icons drawn for this desktop. Rendering is
cached per name and size, so the same icon in every row of a list costs
one rasterisation.

**Images — `tiku_image.h`.** Content-sniffed BMP, PNG (with the kit's own
inflate) and baseline JPEG, bounded in encoded bytes and decoded pixels.
Whether a file is an image is decided by its first bytes, never its name.

**Thumbnails — `tiku_thumb.h`.** When to make one, where it lives, and
the queue that makes it without stopping the window: a cached thumbnail
is trusted only while newer than the file, a big image goes on a slow
queue so it cannot hold up the small ones, and a pose shows its type icon
until the picture is ready.

**Fonts dropped in — `tiku_fontdir.h`.** A folder is the interface: drop
a `.ttf` in it and the family appears in the Fonts window, grouped by the
name the file calls itself.

## 9. The Application Kit

*How an application is written once and run either in the desktop or in
a process of its own.* `kits/application/`.

**The descriptor — `tiku_app.h`.** An application is a
`tiku_app_descriptor_t`:

```c
const tiku_app_descriptor_t tiku_notes_app = {
    .id       = "org.tikuos.notes",
    .name     = "Sticky Notes",
    .start    = notes_start,     /* open windows, paint first frames   */
    .stop     = notes_stop,
    .tick     = notes_tick,      /* every turn, with the clock          */
    .pick     = notes_pick,      /* a menu command for one window       */
    .event_in = notes_event_in,  /* an event, and the window it hit     */
    .closed   = notes_closed,    /* the runtime took a window           */
};
```

`pick` and `event_in` return nonzero *only* to say the application is
done — the runtime reads any other nonzero as a request to quit.

**The services — `tiku_app_services_t`.** What an application draws
*through*, and the reason it never needs the desktop's private state:
`open`, `frame` (a finished pixel frame), `present` (a display list
instead of pixels), `menus`, `close`, `configure` (title, size,
resizable, zoomable), `place` (a *role* — announcement or panel — never a
position, since an application does not know the screen), `resize`,
`sizeable`, `raise_window`, `pick` (a file panel), `clip_set`/`clip_get`,
`help_page`/`help_open`/`help_html`, `send_message`, and the board
services — `board_names`, `current_device`, `device_stats`, `board_run`,
`board_input`, `board_stop`, `board_save`, `board_port`, `board_mcu`,
`board_release`, `board_reclaim`, `board_find` — through which an
application uses a board the desktop already owns.

**The window session — `tiku_remote.h`.** A process boundary that is a
deployment property. A window is a surface plus an event route; menus
travel as the `tiku_menuset_t` they are; picks come back as commands. The
wire is little-endian structs, `[u32 type][u32 length][payload]`, over a
stream socket at `$HOME/.config/tracker/desk.sock`, with a version in
`HELLO` guarding the rest. The messages: `HELLO`/`WELCOME`, `OPEN`,
`CONFIGURE`, `FRAME` and `SURFACE` (a shared surface when both ends are
on one machine), `DRAW` (a display list), `MENUS`, `PLACE`, `RAISE`,
`SIZEABLE`, `EVENT`, `PICK`, `CLOSE`/`CLOSED`, `SHELL_IN`/`SHELL_OUT` and
`BASIC_IN`/`BASIC_OUT` (a board's console riding the session), `TELL`,
`SAY`, `DESCRIBE`, `READY`, `SHUTDOWN`. The same session is what a
*board* speaks over its channel, which is why a board's window and an
application's window are the same thing to the desktop.

**The client — `tiku_client.h`.** `tiku_client_run(&descriptor)`
connects, starts the descriptor, and pumps until it is done. Built with
`TIKU_APP_SO`, the same file exports `tiku_app_v1` for `tiku-run` to
load; built as a program, `main()` calls the client.

**Native controllers — `tiku_window_app.h`.** For an application that has
window controllers of its own (the Reader, the Image viewer): a private
workspace whose windows are published over the protocol — only pixels,
menus and events cross.

**The conductor — `tiku_conduct.h`.** Driving a desktop from outside:
`INJECT` one input event, `QUERY` one pixel or one named string, over any
stream. Off unless asked for with `-conduct`, since a device anything can
puppet has a hole in it. The driven scripts use it when they drive a
*real* desktop rather than the in-process adapter.

## 10. The Media Kit

*The moments a shell may sound.* `kits/media/`.

`tiku_sound.h` is a table of named events in the R5 manner — the startup
chime, the beep, a window opening, a window minimising — each with a
clip, an on/off and a share of one volume. A shell raises an event at the
moment it already knows about and never learns what plays; the kit hands
PCM to a backend a chunk per turn so the loop never waits on the card.
`tiku_score.h` writes a sound down as text (`wave`, `attack`, `release`,
`note`, `rest`) so a system sound is reviewable and renders at any rate;
`tiku_wav.h` reads a dropped clip. The card is one file per host:
PulseAudio on Linux, an audio queue on CoreAudio on the Mac. With no
backend every event is silent and still counted, which is what a scripted
run proves against.

---

# Part III — The Tracker and its Shells

## 11. The Tracker core

`TikuTracker/src/core` and `src/ui` are the file manager, ported from a
first-hand reading of Tracker's source (`spec/ARCHITECTURE.md` cites
where each structure was read; `FEATURES.md` holds 889 behaviours as
contract rows, each marked done or differing). The core never includes a
drawing header: it is behaviour, drawn by the ui layer.

**The pose view — `src/ui/poseview/`.** One engine serving icon, mini-icon
and list mode from one state, as `BPoseView` did: the pose list, the
filtered list the type-ahead leaves, the visible order, the selection,
the columns, the grid and drop target for icon placement. Twelve files
split it by concern — columns, drawing, extent, icon mode, keys, loading,
rename, selection, sort, spatial placement, type-ahead. Directory reading
is asynchronous from the first day: a listing lands in batches from a
worker, and the notification path is the same code path as the initial
read, so a window is told and never polls.

**Spatial state lives on the objects.** Icon positions, column sets and
view state are attributes of the folder (`_trk/pinfo`, `_trk/columns`,
`_trk/viewstate`, the names Tracker used), through `tiku_state.h` — on
the host as extended attributes, for a device in a sidecar the desktop
keeps for it.

**Around the view.** `titleview` — the column strip and what a drag on it
means (a press picks resize or reorder; thresholds decide click, reorder,
resize or removal). `cwmenu` — the context menu, built at open time from
the selection. `navigator`, `navmenu` — the path bar and its menus.
`find` — the Find window and its query builder. `springmenu` — the
spring-loaded folder menu a drag hovers. `nodeicon` — which icon a node
wears, from its kind, its type, its thumbnail or its own art.

**Commands and models — `src/core`.** `drag` and `drop_protocol` — the
drag with Tracker's double-click and copy/move rules; `launch` — which
application opens a document, asked of a registry the way Tracker asked
its MIME database (exact type beats supertype beats "anything"); `types`
— the type registry; `select`, `match` — Select by pattern; `info` — the
Get Info model; `panel` — the file panel model; `frame` — where windows
come back; `switcher` — the window switcher; `associations`,
`prefs`, `deskprefs`, `soundprefs`, `background`, `color`, `alert`,
`repaint`, `task` (the load meter), `state_pipeline` (how state is read
in stages).

**Windows — `src/ui/windows/`.** The folder window (its lifecycle,
editing and rendering split across three files), About, Get Info, Find,
Select, Open With, the file panel, preferences (Tracker, desktop,
Deskbar, fonts, sounds) and the switcher. Each is a window whose draw and
event callbacks the shell installs into the workspace.

## 12. The Desktop shell

`TikuTracker/src/shell/desktop/` is the spatial desktop: one composition,
21 files, all sharing one `desktop_t` through
`tiku_trk_desktop_private.h`, which nothing outside the directory
includes.

### 12.1 The turn

`tracker_run()` in `tiku_trk_desktop.c` is the loop. Each turn:

1. **`desktop_turn_begin`** — apply what the workers finished; reload the
   settings file if it changed; pump the remote sessions (application and
   board windows) and the conductor; poll the reports.
2. **Events** — drain the host (or the script adapter) and dispatch each
   to `desktop_pointer_down/move/up`, the key handler, resize, close.
   Every routing decision — which window, which overlay, which drag —
   happens here in `tiku_trk_desktop_events.c`.
3. **`desktop_turn_end`** — pump the devices (a listing still arriving
   keeps drawing), sweep, pump notices; if anything is dirty and a frame
   is due (16.7 ms since the last), `draw_all` then `blit`; tick the
   splash and the shutdown; then sleep until the next task is due or the
   host has input, whichever is sooner.

`draw_all` is the whole picture in order: the backdrop; the Deskbar under
or over the windows according to its raise setting; the workspace (every
window, then any ghosts); overlays — the Get Info hover, the strip drag,
the drag picture, the menu bars, the open menu (drawn last, since it is
above everything), the status overlay, the splash, the Force Quit
overlay. A partial repaint restricts all of it to a damage rectangle,
which the workspace's content cache honours: a window being dragged
repaints from cached pixels, and the shadow's reach is part of the
damage.

`blit` presents the surface through the host, which diffs it against the
last frame and uploads only the tiles that changed. The desktop measures
itself — draw time per frame, present time, frames over 16 ms, cache hits
— and writes it into the report `SIGUSR2` asks for.

### 12.2 Where things live

| file | owns |
|---|---|
| `lifecycle` | options, boot, the host, the splash, shutdown |
| `events` | every pointer and key, the capture rules (a window sent a press is sent the release), the strip, the band, drags |
| `draw` | `draw_all`, `blit`, the damage, the overlays |
| `commands` | the menu commands: File, Window, Attributes, the system menu |
| `globalbar` | the global menu bar and the Devices menu |
| `deskbar` | the Deskbar's picks: raise, minimise, close, the leaf |
| `mounts` | the device mounts, the sweep, the notice pump |
| `wire` | asking a board what changed, as a job per board, never on the turn |
| `boardrun` | running a program on a mounted board for an application |
| `remote` | the windows of out-of-process sessions: frames, menus, configure |
| `embed` | the services contract for a descriptor hosted in-process (demos) |
| `apps` | the application registry, launching, Open With |
| `panels` | the file panel and the alerts the desktop asks |
| `vfs` | the VFS-over-owner service for device utilities |
| `associations`, `deviceprefs`, `forcequit`, `report`, `startup` | what their names say |

### 12.3 Windows of three kinds

The workspace does not distinguish them, but the shell does:

- **Tracker's own** — folder windows, Get Info, Find, preferences. Their
  draw callbacks are the ui layer's; their events are routed by the
  shell's rules.
- **Remote** — an application's, or a board's. `tiku_trk_desktop_remote.c`
  turns `OPEN` into a workspace window whose draw callback blits the
  last frame the session sent (or plays its display list), publishes
  the menus it sent, and forwards events with content-relative
  coordinates. Closing is the runtime's gesture: Cmd-W or the close box
  send `CLOSED`, never a request the application could swallow.
- **Embedded** — a descriptor hosted in the desktop's own process
  through `tiku_trk_desktop_embed.c`, which fulfils the same services
  contract in-process. Used for demonstrations; installed applications
  never take this path.

### 12.4 The session

`src/shell/session/` restores and persists the desk: every open folder
window, each view's state and columns, the Deskbar's dock and width, in
a sidecar store for the device or the local one. Startup opens the paths
it was given, else the saved session, else the home. Shutdown finishes
the file work under way, bounded, answering any conflict by renaming.
`tiku-desk` (`system/desk/`) is the program still standing when the desk
is not: it starts `TikuDesktop`, keeps its stderr, and when the desk
ends badly writes down how and starts it once more, told where that
account is, so the first thing on screen says what happened.

## 13. The Deskbar

`TikuTracker/src/deskbar/` is the bar: three sections stacked as the
original has them — the leaf menu (Find, Force Quit, Settings, the
Applications and Device Tools submenus, recent folders, Shut down), the
running-application list whose windows nest under each team in expando
mode, and the tray of replicants with the clock. It docks at any corner
or as a strip along an edge, mini or expanded, and its preferences are a
window of its own. Behaviour cites the `DB-*` rows of `FEATURES.md`,
read from Deskbar's source. The shell publishes the workspace's windows
to it every frame, so the team list is never stale, and its picks (raise,
minimise, close) come back as commands the shell answers through the
workspace — a minimise flies a ghost of the window to the row that keeps
it.

## 14. The browser shell

`TikuTracker/src/shell/tracker/` is the single-window browser: one folder
at a time with a navigator, the same pose view, the same file operations
and the same preferences, in thirteen files. It exists so that the
Tracker is proved as a *product* rather than as a desktop: every script
that can run in both shells does, and a behaviour that only one shell
had — a queued file operation that never ran — was found exactly that
way.

---

# Part IV — Applications

## 15. Writing an application

The smallest application the toolkit can run is `apps/examples/hello.c`:

```c
static int
hello_start(void **state, const tiku_app_services_t *services)
{
    hello_state_t *st = calloc(1, sizeof *st);

    st->services = services;
    /* The application owns its pixels.  The services carry a finished
     * frame across; nothing draws into a window from outside. */
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

Then a descriptor, an export and a main:

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

And a manifest, `hello.app`, that the desktop reads without executing
anything:

```ini
version=1
id=hello
name=Hello
module=hello.so
category=demos
icon=application
types=
```

`mk/apps.mk` builds the module against the PIC kits into
`build/apps/hello/`, copies the manifest beside it as `app.manifest`, and
the Deskbar's Applications (or Demos) menu offers it by its `category`.
`types` advertises the document types it reads, which is what puts it in
Open With; `help` names a page in the help root; an optional `startup`
migrates a legacy preference identity and never auto-launches anything.

**What the runtime does for you.** Launching is `tiku-run <module>
[document]` in a fresh process; the runner loads the module, adds the
bundle directory to *its child's* environment only, and connects to the
desk socket. Windows are placed, framed, focused and closed by the
desktop. Menus are published as data and republished when their state
changes; the global bar shows them while the window is focused. Text
input arrives assembled; the clipboard is a service. A fault takes down
the application, not the desk. This is process and crash isolation, not
a security sandbox: the application runs under the user's account and
links the kits.

**What you must do.** Return `0` from `pick` and `event_in` unless the
application is finished. Keep every window's surface at the size the
last `RESIZE` said. Hear `closed`, or you will paint into a window that
is gone. Do slow work on a worker (`tiku_work`) and apply it in `tick`.
Never include a `tiku_trk_*` header — the boundary check will refuse the
build, and the desktop's private state is not yours.

**Testing it.** Build the source with `TIKU_APP_EMBED` into a unit test,
hand it a `tiku_app_services_t` of stubs, and drive `start`, `event_in`,
`tick` and `pick` directly — `TikuTracker/tests/test_notes.c` does
exactly this. Then a driven script: a fixture copies the module into the
test's apps directory, the script opens it from the Applications menu,
and `expect-title`, `expect-menu`, `expect-narrate` and `expect-file-has`
prove what it did.

## 16. The applications that ship

| bundle | what it is | notes |
|---|---|---|
| Editor (`apps/edit`) | a text editor in the PE manner: multiple documents, find, syntax colouring | its own `PE_REFERENCE.md` |
| Terminal (`apps/term`) | a terminal over a pty | the client's short wait exists for its sake |
| TikuBasic, TikuC (`apps/ide`) | the IDE: projects, a build for the host or a board, messages that land on a line, a BASIC debugger | shares one body of code; BASIC builds the board's interpreter for the host (`apps/basichost`) |
| Sticky Notes (`apps/notes`) | a note per window and per file, important ones first in its Notes menu | selects by hand: double-click a word, triple-click a line |
| Reader / Help (`apps/reader`) | documents and the help pages, with bookmarks and navigation | Help requests launch it in help mode with structured data over a private descriptor |
| Image (`apps/image`) | an image viewer over `tiku_image` | a native controller through `tiku_window_app` |
| Console (`apps/console`) | a board's console, and a network console | rides the session's `SHELL_IN`/`SHELL_OUT` |
| Bench (`apps/bench`) | drives the TikuBench suites on a board and reports | |
| Device Monitor, VFS Inspector (`apps/deviceutils`) | live readings and the device namespace, written through `tiku_vfs_rpc` | writes require a read, compare and confirm |
| Calculator, developer Calculator (`apps/calc`, `apps/calculator`) | a plain one and one with bases and widths | |
| Hex (`apps/hex`) | a hex editor | |
| the examples (`apps/examples`) | hello, clock, counter, sketch, twowin, sensors, a framework demo | the least an application can be, each teaching one thing |
| the samples (`apps/samples`) | the widget gallery, the sound gallery, the probe, a metrics generator | standalone programs, not bundles |

---

# Part V — Boards

## 17. A board on the desk

A TikuOS board reaches the desktop by cable, socket or radio, and the
desk treats it three ways at once.

**As a volume.** `-devices` scans the bus (`tiku_sys_serial_ports`),
asks each candidate its name, and offers it in the Devices menu at the
right end of the global bar: *Connect to nRF54L15 (cu.usbmodem…)*.
Connecting opens a session over the transport, mirrors the namespace,
and mounts a Tiku backend at `/devices/<name>`. A folder window on
`/devices/nrf54l15/sys` lists `mem`, `cpu`, `power`, `radio` with typed
columns; `/data` is a real directory you can drop a file into. What
changed is asked of the board every few hundred milliseconds by a job
on its worker — one in flight per board — so a board that goes quiet
costs the loop nothing, and a mount that goes is detached at once.

**As a window.** A board built with `TIKU_APPL_GUI` speaks the window
session over a channel framed on its console line (`tiku_frame`,
`tiku_channel`). It opens a window, and because it has no pixels it
`DRAW`s a display list: the desktop's own widgets, drawn by the board,
at six hundred bytes a frame. The Board window is the same kind of
window an application opens. A board with no shell can hold a port of
its own; a board on a radio or the network dials in through a relay to
a desktop that `-listen`s.

**As a computer.** The About box (`tiku_trk_about_window.c`) asks each
connected board for its estate — `/sys/mem/sram_map`, `nvm_map`,
`/sys/cpu/freq`, uptime, boot count — and draws the SRAM and NVM bars
with their regions (static, stack pool, tier, code, store, persist),
one board per page with a stepper. The Device Monitor plots readings;
the VFS Inspector edits nodes; the IDE builds for the board and runs a
program on it through `board_run`, hearing what it prints through
`board_said`. The current device — chosen in the Devices menu — is where
an application's board work defaults.

**Identity and state.** A board is known by the identity key
`tiku_ident` derives from the nodes it publishes; its pose positions and
window state live in a sidecar keyed on that string, so a board comes
back where it was, on whichever port it appears.

## 18. Two boards on one desk

Nothing above is per-board. The namespace holds eight backends; each
board has its own session, channel, worker job and sidecar; the About
box pages through them; the Devices menu lists them and marks the
current one. On the day of writing an nRF54L15-DK and an Apollo510 EVB
were connected together for the first time: both discovered, both
mounted, both in About with their own estates, the numbers matching
what each board's own shell reported. The `boardboth` script proves a
second topology — one board on the cable and one dialling in over IP —
in the test suite.

---

# Part VI — Proving It

## 19. Tests, lanes and gates

`make test` runs four layers, and each is meant to be read as much as
run.

**Unit tests** — 157 programs under `TikuTracker/tests/test_*.c`, one
per subject, discovered by name. They print `[PASS] ROW` lines against
the contract's row ids (`IV-022`, `DB-030`, `CW-034`) so a failure names
the behaviour rather than the line.

**Driven scripts** — 264 lanes under `TikuTracker/tests/shell/*.script`,
run by `run.sh` in both shells with no display: the window is drawn into
memory exactly as it would be on screen. A script is gestures and
assertions:

```
click 71 8            menu-pick Important     key n ctrl
press 400 300         move 450 315            release 470 320
expect-title Milk     expect-pixel 293 156 fbca0a
expect-menu New note|Important||Yellow|Green|Blue|Pink||Delete note|Quit|
expect-narrate b.txt  expect-file-has /tmp/trk_home/note-003.txt Milk
expect-windows 3      expect-read /devices nrf54l15|apollo510|
shot /tmp/trk_look.png                        quit
```

`wait N` is N loop turns, never seconds, so a lane is deterministic; a
fixture under `setup/<lane>.sh` lays out the home the lane runs in;
headers say what a lane needs (`# board: yes`, `# board: both`,
`# board: discover`, `# prefs: factory`). A lane can pin a pixel, a
title, a menu's full text, a window count, a file's contents, a
narration, a sound raised — which is how a change to the look is
measured rather than eyeballed: the shadows moved nine pixel-pinned lanes
and each was re-pinned with its intent kept.

**Gates** — the kit table, the architecture boundary, the application
boundary, the toolkit check, the contract (`FEATURES.md` has no duplicate
rows), the menu-command check (every command a person can pick reaches a
handler), and the shipping check (the shipping shells carry no script
words).

**The conductor** — for driving a real, displayed desktop: `-conduct`
opens the `INJECT`/`QUERY` channel and `build/dev/trk-conduct` drives it
from outside.

Two things the suite deliberately cannot see: what a board on the bus
adds (three lanes fail with a board plugged in, and say so), and what
only a person can judge — which is what the `shot` verb is for.

## 20. Two hosts, one tree

Linux and macOS build the same sources; what differs is said in three
places and nowhere else.

- **`kits/support/tiku_sys.h`** and its two bodies: processes, memory,
  the fault PC, serial enumeration and rates.
- **`mk/toolchain.mk`**: the Darwin block (`-D_DARWIN_C_SOURCE`, the
  forced `tiku_darwin.h` prelude that supplies what libc spells
  differently, the frameworks, `TIKU_HOST`).
- **The per-host files** the platform seam names: the volume table, the
  sound card, the window host, the command key.

On the Mac the desktop draws through `tiku_cocoa.m` — an AppKit window
whose view blits only the damaged tiles, the shell's own loop pumping
AppKit from inside `wait` and `poll`, Retina through the scale the host
reports — so no X server is installed or started; `make TIKU_HOST=x11`
keeps the Xlib path and its two outside-looking test suites. `make app`
writes `build/TikuDesktop.app` with the applications inside and a
launcher that knows which host it carries. The scripts do not know which
host drew, which is the point.

---

# Appendix A — Glossary

**Backend** — a table of file operations over one store: the host's
files, or a board. **Behaviour file** — a control's state and input,
compiled on the board too; its paint lives beside it. **Channel** — a
socket pair that carries a board's window session over its console.
**Conductor** — the inject/query channel for driving a displayed desktop.
**Descriptor** — an application: its entry points. **Display list** —
what was drawn, as the interface's own calls, for a board to send.
**Ghost** — the last picture of a window on its way out. **Job** — work a
worker does for the loop. **Lane** — one driven script. **Model** — the
one place that knows what kind of thing a pose wraps. **Mount** — a
backend at a path prefix. **Pose** — one entry in a view. **Seam** — one
declaration, one file per host. **Services** — what an application draws
through. **Session** — a prompt-aware conversation with a board; also the
window session an application or board speaks. **Sidecar** — where a
board's UI state lives, keyed by its identity. **Tier** — a board's
memory arena, as its maps report it. **Turn** — one pass of the shell's
loop. **Workspace** — the compositor of in-process windows.

# Appendix B — Reading order

For the architecture: this book, then `TikuTracker/spec/ARCHITECTURE.md`
(what Tracker was, and what the rewrite must reproduce),
`spec/ARCH-runtime-boundaries.md` (why applications are processes),
`apps/ARCHITECTURE.md` (manifests, the ABI, the runtime), and
`apps/examples/README.md`.

For a kit: its header preambles, in the order this book's chapters name
them, and the `doc/` folder where one exists — `message.md`,
`namespace.md`, `display-list.md`, `toolkit.md`, `controls.md`,
`text-input.md`, `sensors.md`.

For a behaviour: its row in `FEATURES.md`, the test that prints that
row, and the lane that drives it.

For the port: `PLAN-macos.md`, which is a record of what each milestone
found rather than a plan any more.

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
TikuTracker/src/   core/ ui/ ui/poseview/ ui/windows/ ui/commands/
                   deskbar/ deskbar/bar/ shell/{desktop,tracker,session,
                   adapters,script}/
system/            run/tiku_run.c  desk/tiku_desk.c
apps/              edit term ide notes reader image console bench
                   deviceutils calc calculator hex examples samples basichost
```

*Ambuj Varshney, and the tree's own comments, September 2026.*
