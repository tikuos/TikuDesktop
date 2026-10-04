# TikuSDR

A board's radio receiver as a live spectrum and a waterfall.  The board
(an ESP32-C61 whose firmware has the receiver: `TIKU_DRV_SDR_ESP_ENABLE=1`)
captures raw I/Q, computes each spectrum itself and sends one line per
spectrum; TikuSDR keeps, stitches and draws them.

- **Live**: 40 MHz around a frequency, 256 bins, about ten spectra a
  second.  The receiver's filter is flat over the middle 24 MHz; the edges
  are shaded.
- **Band**: 2396-2492 MHz from six slices, about five sweeps a second,
  each slice's noise floor levelled to a common one (each is heard at its
  own automatic gain).  Click anywhere in it to tune Live there.
- Beneath the plot: Wi-Fi channels 1-13, BLE's advertising channels 37-39
  and 802.15.4 channels 11-26.  Hover for a frequency's level and channels.

Connect borrows the board from the desktop, as TikuConsole does, and gives
it back on Disconnect or when the window closes.  Levels are the board's
own decibels: uncalibrated, and moved by its automatic gain.

The **File** menu: **Record** writes every line the board sends, with its
time, to a plain text `.sdrlog` file (about 20-30 MB an hour); **Open
Recording...** plays one back through the same view, at the pace it was
made, and Tracker opens a `.sdrlog` in TikuSDR.  The format is at the head
of every recording, so a script can read one too.  **Export CSV...**
writes `time_s,freq_mhz,db` rows, of the whole recording on screen or of
the spectra the waterfall holds; **Save Picture...** writes the window
below its toolbar as a PNG.

| File | What it holds |
| --- | --- |
| `tiku_sdr_model.c` | SPEC lines in, the band stitched, averages, peaks, waterfall |
| `tiku_sdr_link.c` | The board borrowed, spoken to from the tick, given back |
| `tiku_sdr_tape.c` | Recordings written, played back, and exported as CSV |
| `tiku_sdr_view.c` | Toolbar, spectrum, channel rulers, waterfall, status, picture |
| `tiku_sdr_app.c` | The descriptor, menus, file panels, keys and pointer |
