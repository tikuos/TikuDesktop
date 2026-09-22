# Toolkit examples

Small applications demonstrate the public `tiku_app_descriptor_t` lifecycle.
They run in their own processes, whether launched as standalone executables or
as modules through `system/tiku-run`.

| Example | Demonstrates |
| --- | --- |
| `hello.c` | Open a window and paint once |
| `clock.c` | Tick and repaint only when the value changes |
| `counter.c` | Pointer, keyboard and menu events |
| `sketch.c` | Drag tracking |
| `twowin.c` | Independent window lifetimes |
| `sensors.c` | Plots, meters, sparklines and paused snapshots |
| `framework/` | Shared descriptor, events, menus and Help |

## Build and run

```sh
make all
./build/runtime/system/TikuDesktop

# Against the running desktop:
./build/examples/tiku-example-sketch
./build/examples/tiku-example-sensors
# Or:
./build/runtime/system/tiku-run "$PWD/build/runtime/apps/sketch/sketch.so"
```

The application connects to `$HOME/.config/tracker/desk.sock` and retries for
up to eight seconds. Its menu appears in the global bar while its window is
focused. The Demos menu discovers module bundles from their manifests.

Sensor Monitor is explicitly simulated; it does not access a device.
It exercises missing samples, disconnection, automatic/fixed scales, ranges,
trace selection and pause. See the
[sensor widget guide](../../kits/interface/doc/sensors.md).

## Application ownership

Apps receive services for windows, frames, menus, clipboard, file picking and
device operations. Content input coordinates are local to the window. Menus
cross the process boundary as plain data; republish when their state changes.

Apps own surfaces, models and workers; the desktop owns composition, chrome,
focus and placement. Native controllers can use `tiku_window_app` to publish
an app-owned workspace without importing desktop internals.

The historical `TIKU_APP_EMBED`/`TIKU_EXAMPLE_EMBED` build switches select a
descriptor/module instead of a standalone main; they do **not** authorize
loading it in the desktop. Even `apps/trusted/` starts a separate process.

## Adding an application

Start from an example, keep its implementation in `apps/<name>/`, and add
build rules in `mk/apps.mk`. Supply an `app.manifest` with a stable ID,
display name, module leaf filename, category, icon and accepted file types.
No shell source changes are needed to register an ordinary application.

See [application architecture](../../docs/architecture/applications.md) for the complete manifest,
ABI, binary layout, migration rules and checks.
