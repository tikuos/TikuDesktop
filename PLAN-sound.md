# Sound in TikuDesktop

*A plan for system sounds: the startup chime, the alert, windows opening
and closing, the way out.  2026-09-05.*

## 1. Is it possible

Yes.  Nothing in the tree makes a sound today, and the desktop links only
X11 and libm, so this is a new kit rather than a change to one.  Three
things settle its shape:

- **BeOS R5 had system sounds** as a table of named events, each assigned
  a sound or silence in the Sounds preferences: Startup, Beep, Window
  Open, Window Close, Window Minimized, Window Zoomed, Window Activated,
  Key Down, Mouse Down, New E-mail.  The desktop already has the moments
  those hang on: the boot screen's stages, the alert, a window's open and
  close, the closing screen.
- **The host owns the card.**  On this machine PipeWire holds both sound
  cards, so a from-scratch writer on `/dev/snd/pcm*` finds the device
  busy.  What is available everywhere a modern Linux desktop runs is the
  PulseAudio simple client API, served by PipeWire's `pipewire-pulse` or
  by PulseAudio itself: three calls, `pa_simple_new`, `pa_simple_write`,
  `pa_simple_free`, in `libpulse-simple.so.0`.  Its headers are not
  installed here and need not be: the kit declares those three prototypes
  itself and opens the library with `dlopen` at run time, so the build
  gains no dependency and a machine without the library gets silence,
  not a link error.
- **The sounds are synthesised, not shipped.**  The mark is drawn, not a
  picture, and the sounds follow: a small score format (notes, envelope,
  waveform) rendered to PCM at startup.  No binary assets, every sound
  reviewable as text, and the same score plays on a board with a DAC or a
  PWM pin when the device host comes.

The design is a kit and a table.  `kits/media/` (the seventh kit, BeOS's
name for it) offers `tiku_sound_play(event)`, with a backend behind it
that the shell never sees: the host backend above now, a device backend
later.  The shells raise events at the moments they already have; the
Sounds preferences say which events sound and how loud; scripts hear the
events without a card.

## 2. Milestones

| # | Milestone | Scope | Cost | Done when |
|---|---|---|---|---|
| **0** | The kit and its seam | `kits/media/tiku_sound.h/.c`: the event table (`TIKU_SOUND_STARTUP`, `_BEEP`, `_WINDOW_OPEN`, `_WINDOW_CLOSE`, `_WINDOW_MINIMIZE`, `_WINDOW_ZOOM`, `_WINDOW_ACTIVATE`, `_SHUTDOWN`), `tiku_sound_init/play/set_enabled/set_volume/free`, a backend struct (`open`, `write`, `close`) with a silent backend as the default; `media` added to `check_kits.py` (depends on support only); `test_sound.c` proves the table, the volume law and that a disabled event plays nothing | half a day | `make test` green with the kit linked and every event silent |
| **1** | The host backend | `tiku_sound_pulse.c`: `dlopen("libpulse-simple.so.0")`, the three prototypes declared in the kit, a mono 16-bit 44.1 kHz stream opened on first play and closed after two seconds idle; played from the shell's turn, never blocking it (a ring of pending PCM drained a chunk per turn); absent library or a refused connection is silence with one line on stderr | one day | `./build/TikuDesktop` on this machine plays a test tone through PipeWire; with the library hidden it runs silent and says so once |
| **2** | The sounds, as scores | `kits/media/tiku_score.h/.c`: a score is a list of (frequency, duration, amplitude) with a waveform (sine, triangle, square) and an attack/release envelope, rendered to PCM once; the eight system sounds written as scores in `assets/sounds/*.score`, baked into a header at build time the way the strings catalogue is (`genscores.py`, checked by hash like the font); the startup chime the longest at under two seconds, the ticks under a tenth | one day | every score renders without clipping; `test_score.c` checks lengths and peak amplitude; the eight play from a `tiku-sound-gallery` sample |
| **3** | The moments wired | the desktop raises: STARTUP as the boot screen's first stage is written, BEEP when an alert opens, WINDOW_OPEN/CLOSE/MINIMIZE/ZOOM/ACTIVATE from the window controller, SHUTDOWN as the closing screen begins; the browser shell raises BEEP and the window pair; each is one call at the place that already knows the moment.  Scripts: `-mute` is implied by `-script`; the shell keeps a log of raised events and the harness gains `expect-sound <event>` and `expect-no-sound`, so the moments are proved without a card, in both drivers | one day | scripts `sounds.script` (each moment raises its event once) and `alertsound.script` pass; nothing plays under `-script` |
| **4** | The Sounds preferences | a Sounds row in the Deskbar preferences (or its own window in the R5 manner): a per-event on/off, a master volume slider, a Test button per event; persisted as `sound.<event>` and `sound.volume` lines in the settings file the desktop already writes; defaults: startup, beep, shutdown on; the window ticks off | one day | the window round-trips through the settings file; `soundprefs.script` toggles an event and proves the log |
| **5** | The device backend | when the device host comes: a backend that writes the rendered score through the namespace to a `/dev/pwm/<n>` or `/dev/i2s` node the board offers, at the board's rate; the scores already render at any rate, so nothing above the seam changes | when the device work starts | the startup chime from a board's speaker |

Order: 0, 1, 2 in that order (each needs the last), then 3 and 4 in
either order, 5 later.  Four days for milestones 0 to 4.

## 3. Decisions that are yours

1. **Which moments sound by default.**  R5 shipped with Startup and Beep
   on and the rest off; the proposal follows it.  Ticks on every window
   are the kind of thing people switch off in a day.
2. **The chime.**  A score is text, so the startup chime can be written
   and heard in an afternoon and changed at will; whether it is a
   rising fifth, the R5 three notes, or something of TikuOS's own is a
   taste decision, and the gallery in milestone 2 is where to make it.
3. **Whether the browser shell sounds at all.**  It is the one that
   ships on a device; the proposal gives it the beep and the window
   pair only, and leaves the chime to the desktop.
4. **The library.**  The PulseAudio simple API is the choice that works
   under both PipeWire and PulseAudio with no build dependency.  ALSA's
   own client library would also do, with more calls; the raw `/dev/snd`
   writer is kept for a host with no sound daemon, as a later backend.

## 4. What this touches and what it does not

- New: `kits/media/`, `assets/sounds/`, `genscores.py`, two tests, two
  scripts, one preferences row.
- Changed: `check_kits.py`'s table (one row), the desktop's boot stage,
  alert, window controller and closing screen (one call each), the
  script engine (two verbs), the settings file (two kinds of line).
- Not touched: any drawing, the namespace, the device backend, the
  firmware.  Sound is a kit the shells call, never something they know
  the inside of.
