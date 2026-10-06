# TikuSDR

A board's radio receiver as a spectrum analyser.  The board (an ESP32-C61
whose firmware has the receiver: `TIKU_DRV_SDR_ESP_ENABLE=1`) captures raw
I/Q, computes each spectrum itself and sends one line per spectrum;
TikuSDR keeps, stitches and draws them.

The window is laid out as an analyser's: a panel of settings at the left,
the spectrogram above, the spectrum on a ten-division graticule below it,
and a bar at the foot to record, pause and say what is happening.  It
arrives behind a splash, TikuBasic's card from the interface kit (2.5 s,
or until a key or a click; the window opens as it leaves, and a recording
opened from Tracker skips it); About TikuSDR shows the card again, and the
desktop hoists it into its Tiku menu.

- **Frequency**: Center, Span, Start and Stop in MHz, typed in (k, M or G
  after a number work too).  Start or Stop moves Center and Span to match;
  Center or Span moves Start and Stop.  Up to 24 MHz is one spectrum at
  the narrowest sample rate that holds it (4, 8, 10, 20 or 40 MS/s, so 2
  MHz shows 15.6 kHz bins): a 40 MS/s capture is flat over its middle 24
  MHz alone, 15-30 dB down beyond.  Wider is swept in 16 MHz slices,
  anywhere from 2200 to 2700 MHz, all at the gain the board holds (an
  older board's slices, each at its own automatic gain, have their noise
  floors levelled to a common one). The Mode button distinguishes a single
  capture from a sweep, independently of connection status; it swaps the last
  live view and the last wide one (the 2.4 GHz band, 2396-2492 MHz, until
  another is typed).  The RBW list beside it chooses the resolution
  bandwidth (the Hann window's 3 dB width, 1.44 bins): Auto, or any the
  board can give the view, finer ones swept in narrower slices at lower
  rates (16, 8, 6 or 3 MHz apart at 20, 10, 8 or 4 MS/s, at most 64);
  `sdr_model_rbw_plan` picks one capture where one holds the view, else
  the sweep sending the fewest bins.  Recordings say their version: a
  version 1 one held up to 40 MHz in one spectrum, and still plays so.
- **Traces**: three at once, each Off, Clear & Write, Average (over a count
  of spectra), Max Hold or Min Hold, in black, red and blue.  Trace 1
  opens as the average of four.
- **Markers**: four, each reading a trace; out on the highest free peak,
  moved by Peak Search, Next Peak, Peak Left and Right, by a typed
  frequency or a drag that climbs onto the peak let go beside, following
  a drifting peak while Track is on; To Center makes one the view's
  centre.  Their readings stand in the graticule's corner, each against
  M1.
- **Display**: the spectrogram on or off (the spectrum then takes its
  room).
- The graticule's scale: Ref on whole tens over the traces shown, 2, 5, 10 or
  20 dB a division, moved only when the traces leave its room; the frame
  says RBW and bin (the FFT's spacing), the rate, the gain and the range.
  The spectrogram is coloured
  from just under the noise to the reference level.

TikuSDR borrows the board from the desktop as soon as it opens, as
TikuConsole does, and gives it back when the window closes; the SDR
menu's Disconnect and Connect give it back and borrow it again.  Levels are
the board's own decibels, uncalibrated.  The board holds its gain at step 60
and marks such lines `held`; a capture loud enough to clip is taken 16 steps
lower, and TikuSDR raises a line by the steps it was held below 60 (about a
decibel each), so levels stand still instead of chasing every burst.

Only ESP32-C61s are offered, as their die says.  With none connected
through the desktop's Devices menu, TikuSDR says "No software-defined
radio detected" over its window as soon as the desktop has said which
boards it has (out of process that is a turn after start, so
`board_names` answers -1 until then); its one button, OK, closes TikuSDR,
and one connected while it is up is borrowed instead.  Connect is offered
only while one is connected, and a recording opened from Tracker needs no
board.

The **File** menu: **Record** writes every line the board sends, with its
time, to a plain text `.sdrlog` file (about 20-30 MB an hour); **Open
Recording...** plays one back through the same view, at the pace it was
made, and Tracker opens a `.sdrlog` in TikuSDR.  The format is at the head
of every recording, so a script can read one too.  **Export CSV...**
writes `time_s,freq_mhz,db` rows, of the whole recording on screen or of
the spectra the spectrogram holds; **Save Picture...** writes the
spectrogram and the spectrum, with their annotations, as a PNG.

| File | What it holds |
| --- | --- |
| `tiku_sdr_model.c` | A view's range and how it is asked for, SPEC lines in, sweeps stitched, what each trace type keeps, the graticule's scale, the spectrogram |
| `tiku_sdr_marker.c` | Peaks found, markers put on them, moved, kept on a drifting peak, read out |
| `tiku_sdr_link.c` | The board borrowed, spoken to from the tick, given back |
| `tiku_sdr_tape.c` | Recordings written, played back, and exported as CSV |
| `tiku_sdr_view.c` | The panel and its sections, the spectrogram, the graticule and its annotations, the bar, the picture |
| `tiku_sdr_app.c` | The descriptor, menus, file panels, the panel's lists and fields, keys and pointer |

## Saved setups and measurement tools

Display preferences are saved under `~/.config/TikuDesktop/TikuSDR.prefs`.
Named setups use `.sdrpreset`; references use `.sdrref`; recording annotations
use a `.sdrmarks` sidecar. Small documents are versioned text, validated before
application and atomically replaced. Restoring a setup does not restore gain,
arm a trigger, or issue a recording command.

The Measurements section provides a relative-dB activity/trigger threshold,
pre/post-trigger timing and a bookmark label. The Trace menu switches the
waterfall to activity bars for Wi-Fi, BLE or 802.15.4 and exports the counts.
Only fully observed channels count. Percentages represent observations above
threshold, not airtime or protocol classification.

Triggers are one-shot and reserve their destination at arm time. The raw
pre-trigger ring holds at most 256 lines, trimmed at complete-frame boundaries;
post-trigger recording ends on a wall-clock deadline. Retuning or changing
acquisition disarms it. Recordings version 3 preserve FFT/rate overrides,
and with them a chosen RBW;
versions 1 and 2 remain readable. Playback supports labelled bookmarks and
interval loops with bounded seeking.

Receiver capabilities come from `sdr info`. Overrides are limited to reported
and implemented choices; legacy firmware retains automatic acquisition.
Capture health distinguishes stale data, rejected spectra, incomplete sweeps
and local buffer overflow. Clipping remains explicitly unknown unless a future
firmware protocol supplies that information.

Implementation is split between `tiku_sdr_workspace.c` (persistence and
annotations), `tiku_sdr_measure.c` (activity, triggers and health), and
`tiku_sdr_acquisition.c` (capabilities and acquisition selection). See
`assets/help/apps/sdr.html` for the full user guide and `tests/apps/test_sdr.c`
for receiver-free protocol, persistence, trigger and playback tests.
