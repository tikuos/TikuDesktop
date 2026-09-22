# Desktop rendering and responsiveness

The desktop retains its existing software-rendered appearance. The performance
path changes how much is repainted and when a frame is presented, not the theme,
font rendering, or window behavior.

## What changed

- Idle Reader/Help pointer movement does not invalidate the window unless the
  hovered tool or link changes. Button capture, text selection, and scrollbar
  dragging still take the normal event path.
- Reader tooltip/button/status updates invalidate their old and new footprints.
  Window movement invalidates the union of the old and new frames.
- A repaint boundary survives nested clip resets, so all desktop layers can
  composite a damaged region without painting outside it.
- Window contents are cached for pure movement. Chrome is still painted normally.
  Full invalidations refresh content; resize, focus, theme/font changes, app
  frames, timers, and device updates retain the conservative full-redraw path.
  Recording/narration never uses the pixel cache.
- The cache budget is 64 MiB per workspace. Incomplete/offscreen captures and
  allocation failures fall back to ordinary drawing. Closing a window releases
  its cache; cached surfaces retain native HiDPI pixels.
- Hosted application frames use the kit's bulk blit instead of one drawing call
  per pixel.
- Native presentation compares 64 × 32 pixel tiles and uploads changed regions
  only. The first frame, a native resize, and Expose force a complete upload.
  Rectangle-list overflow coalesces damage rather than dropping it.
- Native input wakes the main loop immediately. Drawing is paced to a 16.667 ms
  software frame budget, while input and timers remain serviced. Event batches
  are bounded; motion coalescing never crosses a button/key event. Scripted runs
  retain their deterministic one-instruction-per-turn timing.

Existing `dirty = 1` writes always mean a full redraw. Only known bounded changes
use `desktop_damage()` (`dirty == 2`); they cannot weaken a pending full redraw.
New content-changing code should keep using the conservative invalidation unless
it also explicitly invalidates the relevant window-content cache.

## Measurements

From `applications/`:

```sh
make build/tests/test_render_perf
./build/tests/test_render_perf
TIKU_FRAME_STATS=1 ./build/TikuDesktop
```

The benchmark compares every cached drag frame with an independently rendered
full reference, over 120 moves and four populated utility windows. One local
optimized-build run measured:

| Logical desktop | Scale | Full median | Cached median | Repainted area |
| --- | --- | --- | --- | --- |
| 1000 × 680 | 1× | 933 µs | 201 µs | 19.2% |
| 1920 × 1080 | 1× | 1323 µs | 216 µs | 6.3% |
| 1000 × 680 | 2× | 3225 µs | 866 µs | 19.2% |

These are CPU-side rendering measurements for a reproducible synthetic scene,
not display refresh rates or guaranteed real-world FPS. Tests assert correct
pixels and reduced work, not machine-dependent timing thresholds.

`TIKU_FRAME_STATS` prints diagnostics to stderr on exit. The existing on-demand
desktop report includes the same counters: full/partial frames, logical repaint
area, render and presentation time, recent render median/p95 (128 samples),
content callbacks/cache hits, cache memory, and native upload rectangles/bytes.
Upload bytes count pixel payload, excluding X11 protocol overhead.

This is software pacing, not compositor VSync. Background/device work retains
its existing heartbeat; intentionally delayed hover hints retain their delay.
Expensive document loading or other synchronous work is not made asynchronous
by these rendering changes.

## Regression coverage

```sh
make test-host-render test-host-clipboard
TRK_SHELL_ONLY=renderperf.script sh tests/integration/shell/run.sh
./build/tests/test_document_reader
```

The native tests run on a private Xvfb/Xephyr server, never the user's clipboard.
They check sparse upload pixels, unchanged-frame skipping, Expose repair,
motion/release ordering, and interruptible waits. The desktop script compares
hover and live-drag frames with a forced full redraw. Reader tests include 4,000
unchanged hovers, old/new tooltip footprints, and dragging away from a button.
The portable compositor suite also covers HiDPI, odd native sizes, stride
padding, damage overflow, nested clipping, full invalidation, and cache cleanup.

Rebuild shells and hosted apps together after interface-header changes:

```sh
make -j4 shells apps tests
```
