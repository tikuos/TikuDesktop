# TikuSDR

A board's radio receiver as a spectrum analyser.  The board (an ESP32-C61
whose firmware has the receiver: `TIKU_DRV_SDR_ESP_ENABLE=1`) captures raw
I/Q, computes each spectrum itself and sends one line per spectrum;
TikuSDR keeps, stitches and draws them.

The window is laid out as an analyser's: a panel of settings at the left,
the spectrogram above, the spectrum on a ten-division graticule below it,
and a bar at the foot to record, pause and say what is happening.

- **Frequency**: Center, Span, Start and Stop in MHz, typed in (k, M or G
  after a number work too).  Start or Stop moves Center and Span to match;
  Center or Span moves Start and Stop.  Up to 40 MHz is one spectrum at
  the narrowest sample rate that holds it (4, 8, 10, 20 or 40 MS/s, so 2
  MHz shows 15.6 kHz bins); wider is swept in 16 MHz slices, anywhere from
  2200 to 2700 MHz, each slice's noise floor levelled to a common one.
  Live (40 MHz about the centre), Band (2396-2492 MHz) and Full (all of
  it) are three such views.
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
  room), and the band's channel plans beneath the graticule -- Wi-Fi
  channels 1-13, BLE's advertising channels 37-39 and 802.15.4 channels
  11-26.
- The graticule's scale: Ref on whole tens over the traces shown, 2, 5, 10 or
  20 dB a division, moved only when the traces leave its room; the frame
  says RBW, the rate, the gain and the range.  The spectrogram is coloured
  from just under the noise to the reference level.

TikuSDR borrows the board from the desktop as soon as it opens, as
TikuConsole does, and gives it back when the window closes; the SDR
menu's Disconnect and Connect give it back and borrow it again.  Levels are
the board's own decibels: uncalibrated, and moved by its automatic gain.

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
