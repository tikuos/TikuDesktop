# A board's interface on the desktop

The programs here run **on a TikuOS board**, not on the desktop.  They are
built into the firmware through the applications overlay
(`applications.mk`, included by the firmware's Makefile) and talk to
TikuDesktop over the board's console, the same line the shell uses.
Two roads, one library directory (`gui/`):

| Road | What the board does | What the desktop does | Header |
|---|---|---|---|
| **Forms** | declares rows bound to its own variables; publishes `/gui/<form>/` | draws the rows as a described panel; a press writes the row's node | `tiku_gui.h` |
| **Drawing** | opens a window and writes the desktop's own command stream | draws the list as a window; presses come back as events | `tiku_draw.h` |

The forms road costs a line of text per control and needs nothing but
the namespace mount.  The drawing road gives a process a window of its
own, in a few hundred bytes a frame.

## Build and flash (nRF54LM20-DK)

From the firmware repository, with this repository cloned into
`applications/`:

    make MCU=nrf54lm20a TIKU_SHELL_ENABLE=1 TIKU_APPL_GUI=1 \
         TIKU_APPL_GUI_DEMO=1 TIKU_APPL_DRAW_DEMO=1
    make flash MCU=nrf54lm20a TIKU_SHELL_ENABLE=1 TIKU_APPL_GUI=1 \
         TIKU_APPL_GUI_DEMO=1 TIKU_APPL_DRAW_DEMO=1

`TIKU_APPL_GUI` builds the library and its one mainline hook (the `/gui`
root and the console channel, attached together behind the macro); the
two `_DEMO` flags add the samples.  Without the macro the firmware is
byte-identical to a tree that never had this directory:

    arm-none-eabi-nm main.elf | grep -c tiku_gui      # 0 without, 10+ with

## Talk to it

The board's console is the first virtual COM port of the DK's J-Link,
`/dev/ttyACM0` at 115200 8N1.  Any terminal shows the forms road as
files:

    tikuOS:/> ls /gui/demo
    tikuOS:/> read /gui/demo/panel
    tikuOS:/> write /gui/demo/lamp 1
    tikuOS:/> read /gui/demo/status

The desktop, with the board mounted:

    build/TikuDesktop -devices -p /dev/ttyACM0 -mount /devices/board1

The board's forms appear as rows in the Deskbar under its name and open
themselves the first time the board rings; a board window opens itself
when the desktop greets the board.  The harness runs the board lane of
every `# board:` script with

    TRK_PORT=/dev/ttyACM0 make test

## Writing a process

A form (`samples/panel_demo.c`):

```c
static tiku_gui_form_t demo;
static int temp, lamp;

tiku_gui_form_init(&demo, "demo", "Board one", &my_process);
tiku_gui_gauge(&demo, "temp", "Temperature", &temp, 0, 100);
tiku_gui_toggle(&demo, "lamp", "Lamp", &lamp, lamp_changed);
tiku_gui_add(&demo);
/* in the thread: */
tiku_gui_event(ev, data);      /* runs lamp_changed here, in this thread */
tiku_gui_changed(row);         /* after the process moved a value itself */
```

A window (`samples/draw_demo.c`):

```c
static tiku_draw_window_t win;
static unsigned char list[TIKU_DRAW_LIST_MAX];   /* the window's list */

tiku_draw_open(&win, "Board window", 240, 160, &my_process, list, sizeof list);
tiku_draw_begin(&win);
tiku_draw_text(&win, 12, 22, TIKU_DRAW_TEXT, TIKU_DRAW_BOLD, "Drawn by the board");
tiku_draw_button(&win, 12, 60, 80, 24, 0u, "Count");
tiku_draw_present(&win);
/* in the thread: */
while ((w = tiku_draw_event(ev, data)) != NULL) {
    if (w->event.kind == TIKU_DRAW_POINTER_DOWN) { ... }
}
```

Text is measured with `tiku_draw_text_width()` from `tiku_metrics.h`,
which `make check-metrics` on the desktop side keeps equal to the faces
the desktop draws with.

## The wire

Both roads share the console.  The forms road is the shell's own text
commands.  The drawing road is the desktop's window session, one message
per frame: `0xC0 0xF1`, the frame SLIP-escaped, `0xC0`.  The kernel's
console (`kernel/console/`) cuts every frame out of the line before the
shell's line editor sees a byte and hands it whole to the channel its
marker names -- this library registers `0xF1` at boot, the IP stack the
IPv4 nibble -- and the desktop's session does the same on its side, so
typed commands and frames interleave freely.  The plan and its milestones
are in `../PLAN-gui.md`; the link under the session in `../PLAN-link.md`.

Inside the frame the link (`kernel/link/tiku_link_console.c` here,
`kits/device/tiku_link_serial.c` on the desktop) keeps its own four
bytes around the message: a control byte naming the kind -- DATA, ACK or
SYN -- with a bit asking for an acknowledgement, a sequence number, the
message, and a CRC-16 (CCITT-FALSE, high byte first) over all of it.  A
frame whose CRC disagrees is dropped whole, so a byte the console loses
while the board paints or writes NVM costs a message, never a corrupt
one.  The board sends its frames unpaced and never waits; the desktop,
whose frames are the ones a busy board drops, paces its own through a
window of two the board acknowledges from inside the console pump,
writes the window again when an acknowledgement is late, and after four
tries counts the window lost and greets the board afresh.  A SYN opens
the desktop's stream: the board restarts its numbering when it takes
one, so a board that has already talked to another desktop starts again
from zero.

A board with an address need not be on the cable at all.  With the IP link
kit built (`TIKU_KITS_NET_LINK_IP_ENABLE=1`) and this library's desk node
(`TIKU_APPL_GUI_IP=1`), writing an address to `/gui/desk` -- `write
/gui/desk 172.16.7.1:7749` -- dials a desktop over TCP and moves the
session there; the windows are held until that desktop greets, as they are
at boot, and `write /gui/desk off` brings the session back to the console.
Inside the connection a message is its 32-bit length and its bytes: TCP
delivers them whole and in order, so nothing of the console link's CRC,
numbering or pacing is needed.  Reading `/gui/desk` says where the link
stands and what has crossed it.

The shell is not needed for any of this.  With `TIKU_SHELL_ENABLE=0` the
kernel's console runs a process of its own that drains the wire while a
channel is registered (`tiku_console_pumping()` says whether it is), and
this library's channel is registered at boot, so the drawing road works on
an image with no line editor.  The forms road does not: it is the shell's
text commands.  The desktop finds such a board on its port alone, since no
prompt answers, and keeps the channel when the link's opening SYN is
acknowledged (`tiku_channel_open_port` and `tiku_channel_probe` in
`kits/device/`); the window comes up, the namespace does not.
