# TikuConsole in TikuDesktop

Build with `make -C applications apps shells`, restart TikuDesktop, then open
**Device Tools > TikuConsole**. This is a native desktop-kit application, with
no GTK or Python dependency. The existing **Terminal** remains the host shell.

## Device workflow

Connect and choose a default device in the desktop's Devices menu. Console
initially selects that device; its dropdown can choose another mounted board.
Opening the application does not touch a serial port. **Open console** explicitly
requests exclusive access to the selected device's serial connection. The MCU,
port and device snapshot are captured before release; the console waits until
the desktop's close worker has finished before opening that port.

While Console owns the port, the device is temporarily unavailable to other
desktop applications and file windows. Target and baud settings are locked.
**Return to desktop** closes Console's descriptor, discards unsent input and
requests reconnection. **Files...** performs the same handoff, then opens
`/devices/<name>/data` in the desktop file manager, where normal file operations
provide upload/download and management without duplicating the transfer
protocol. Finish any interactive program before handing the board back; a
firmware that cannot return to its normal shell may require reconnection.

A vanished target never silently switches to a different board. Failed handoffs
and unplug events are shown in the status area. No automatic port scanning,
firmware builds, flashing, reset command or test run happens at startup.

## Console

- **Command mode:** edit one command, then Send or Enter. Up/Down recalls the
  last 32 commands and restores an unfinished draft. History is session-local
  and cleared when selecting a different device.
- **Direct mode:** click the output pane to send individual keys, arrows and
  committed UTF-8 text. Ctrl-C interrupts; other Ctrl-letter codes work too.
  Tab stays available for navigating controls; Ctrl-I sends shell completion.
  Direct paste accepts a single plain-text line only, never embedded commands
  separated by newlines or control bytes. Paste into the command field to review
  a command before sending it.
- **Scrollback:** 1,024 rows, wrapped at 80 codepoints. Find filters retained
  history; scrolling or selecting a line pauses following, and Follow tail
  resumes it. Copy selected line and Console > Copy filtered history use the
  desktop clipboard. Oversized copies are refused rather than truncated; narrow
  Find or select one line to fit the clipboard's 4-KiB limit.
- **Display:** incremental UTF-8 and ANSI foreground/bold styling, carriage
  return, backspace and common line-editing sequences. This is a shell console,
  not a full-screen VT100 emulator; complex cursor addressing, background
  colours and grapheme-width layout are not implemented.
- **Recording:** opt-in, received wire bytes (including ANSI and SLIP) to a
  unique mode-0600 `/tmp/tikuconsole-*` file. It can include echoed passwords or
  other secrets. Copy log path locates it; Clear view never clears the recording.
  Copy files you want to retain elsewhere because the OS may clean `/tmp`.

## Networking and device tools

**Network** implements the standalone console's rootless ICMP-over-SLIP ping
workflow: an explicit Ping enables SLIP, sends 1–100 probes, validates matching
replies and shows round-trip history using the shared sensor chart widget.
Loss is reported after the response deadline, never as a zero-latency result.
The host SLIP address is `172.16.7.1`; the default board address is `172.16.7.2`.
The board address and probe count are editable. SLIP on/off and RP2350 WiFi
scan/status shortcuts print their responses in Console. Firmware must include
the respective network/WiFi features; MCU eligibility alone does not prove that
a firmware has them.

**Device** contains the captured device parameters, Console-only baud setting,
and Board info / Memory shortcuts for fresh shell output. Device statistics
are a snapshot, not live telemetry while the port is exclusively held.

## Parity boundaries

The standalone GTK console remains available unchanged. This integration ports
the serial shell, desktop-managed file workflow and rootless ping, not every
standalone capability. Firmware feature/build/flash controls, WiFi credential
management, privileged TUN/NAT Internet forwarding and the BLE console client
are not ported. Serial devices only are supported here; the desktop's IP/BLE
window transports are not raw serial endpoints. Options are session-local.

## Verification

`applications/build/tests/test_console` uses a pseudo-terminal as a fake board:
ANSI/UTF-8 parsing, bounded scrollback, search/copy, command history, serial I/O,
atomic output queuing, paste rejection, private logs, Files handoff, wrong-device
acknowledgements, cancellation and timeouts. Synthetic ICMP packets cover
checksums, bounds, address matching, duplicate replies, GUI-frame separation and
loss accounting. It renders the Console, Network and Device tabs at both font
sizes. `test_console_handoff` verifies that embedded and out-of-process release
notifications happen only after closing the previous backend.

No physical devices, flash tools, TUN interfaces, routes or firewall rules are
used by these tests.
