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
| `pdf/` | TikuPDF: datasheets, manuals and papers |
| `archive/` | TikuArchive: ZIP, tar and gzip archives, browsed and extracted |
| `deviceutils/` | Device Monitor and VFS Inspector |
| `calculator/` | Ordinary Calculator; stable runtime ID `calc` |
| `developer-calculator/` | Developer Calculator; stable runtime ID `calculator` |
| `hex/`, `notes/` | Hex editor and Notes |

Each production `.app` manifest declares the stable application ID, module,
menu category, icon, types and startup identity. Source-folder names need not
match these IDs. Products live in `build/runtime/apps/<id>/`.

Run `make apps` from the repository root (or this forwarding Makefile).
See [application architecture](../docs/architecture/applications.md) for the
SDK, IPC boundary, manifests and adding an application.
