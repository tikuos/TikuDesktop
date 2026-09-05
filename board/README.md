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

`TIKU_APPL_GUI` builds the library and its one mainline hook (a `/gui`
root and the console frame decoder, both behind the macro); the two
`_DEMO` flags add the samples.  Without the macro the firmware is
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
per frame: `0xC0 0xF1`, the message SLIP-escaped, `0xC0`.  The shell's
read loop cuts those frames out before its line editor sees a byte, and
the desktop's session does the same on its side, so typed commands and
frames interleave freely.  The plan and its milestones are in
`../PLAN-gui.md`.
