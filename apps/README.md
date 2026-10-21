# Applications

These are end-user applications, built as separate process-hosted modules
using the public kits. Desktop controllers and preferences belong in
`system/`; demos belong in `examples/`; test-only apps belong in
`tests/fixtures/`.

| Source directory | Applications |
| --- | --- |
| `editor/`, `terminal/` | Text Editor and Terminal |
| `ide/` | TikuBasic and TikuC, sharing the IDE implementation |
| `basichost/` | Optional host build of the firmware BASIC interpreter |
| `bench/`, `console/` | TikuBench and TikuConsole |
| `reader/`, `image/` | TikuReader/Help and TikuImage |
| `marker/` | TikuMarker: Markdown documents, read with the Reader's modules |
| `pdf/` | TikuPDF: datasheets, manuals and papers |
| `circuit/` | TikuCircuit: schematics, Gerber artwork, drill files and Gerber jobs, read-only |
| `archive/` | TikuArchive: ZIP, tar and gzip archives, browsed and extracted |
| `diff/` | TikuDiff: two files side by side, two folders, two archives |
| `grab/` | TikuGrab: a window, or the whole desk, as a picture |
| `sdr/` | TikuSDR: what an ESP32-C61's radio hears, as a spectrum analyser |
| `deviceutils/` | Device Monitor and VFS Inspector |
| `calculator/` | Ordinary Calculator; stable runtime ID `calc` |
| `developer-calculator/` | Developer Calculator; stable runtime ID `calculator` |
| `hex/`, `notes/` | Hex editor and Notes |

Each production `.app` manifest declares the stable application ID, module,
menu category, icon, types and startup identity. Source-folder names need not
match these IDs. Products live in `build/runtime/apps/<id>/`.

Run `make sdk` from the repository root, then `make -j6` in this directory.
Set `SDK=/path/to/sdk` to use an installed SDK. Individual application
directories also have Makefiles. See
[independent application builds](../docs/architecture/application-builds.md)
for source exports, application packages and installation.
See [application architecture](../docs/architecture/applications.md) for the
SDK, IPC boundary, manifests and adding an application.
