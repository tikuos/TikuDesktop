# TikuCircuit

Build with `make -C applications apps`, then open **Graphics & Design >
TikuCircuit** in TikuDesktop, or double-click a schematic, Gerber, drill or
job file in Tracker. TikuCircuit is a read-only viewer; it runs in its own
process through `tiku-run`, and its readers and renderer are the
translation kit's circuit kit ([kit guide](../../kits/translation/doc/circuit.md)).

- **Files.** KiCad schematics (`.kicad_sch`) with their linked sheets,
  EAGLE XML schematics (`.sch`), Gerber layers (`.gbr`, `.ger`, `.gbrx`,
  `.gtl` and the other common layer names), decimal-coordinate drill files
  (`.drl`, `.xnc`, `.xln`) and Gerber jobs (`.gbrjob`). The manifest
  advertises them to Tracker's File Associations; existing choices win. The
  contents decide how a file is read, not its name.
- **Menus.** File: Open..., Open Gerber Job..., Reload, Open Example, Clear
  Drawing, Export View as PNG..., Save View Settings, Close. Edit: Copy Selection Details, Find..., Find Next. View:
  Fit Drawing, Zoom In, Zoom Out, Cancel, Show Sheets (Show Layers for a
  stack), Previous and Next Sheet, Show Selection Details. Layers: Add
  Layer..., Layer Visible, Raise Layer, Lower Layer, Layer Appearance...,
  Isolate Layer, Show All Layers, Remove Layer. Tools: Measure Distance,
  Clear Measurement, Copy Measurement, Millimetres, Inches, Mils.
- **Keys.** Ctrl+O, Ctrl+R, Ctrl+W, Ctrl+C (outside Find), Ctrl+F, Ctrl+G,
  Ctrl+[ and Ctrl+] (Command on macOS); `+`, `-` and `0` or `F` zoom and
  fit; arrows pan; Escape cancels the work in flight. Drag pans and the
  wheel zooms about the pointer.
- **Measure.** Choose Tools > Measure Distance (or `M` outside Find), then
  click two points. The footer shows straight-line distance and signed dX/dY;
  the first click starts a live preview and a third starts a new measurement.
  Choose mm, inches or mils in Tools. Coordinates use the viewer's right/down
  axes, not the source format's CAD axes. Wheel zoom and arrow-key pan keep
  the endpoints attached to the drawing. Escape clears the measurement; a
  second Escape leaves measurement mode. Copy Measurement copies the completed
  result. These are unsnapped screen-picked points, not manufacturing checks.
- **Layer appearance.** Select a layer and choose Layers > Layer Appearance.
  Pick a palette colour or enter six hexadecimal RGB digits. Set opacity from
  0 to 100 with the slider or number field, then Apply; Cancel changes nothing.
  Isolate Layer hides the other layers. Show All Layers makes them visible
  again without changing their opacity.
- **Export.** File > Export View as PNG saves the current rendered viewport,
  not the whole drawing at a new resolution. It includes a completed visible
  measurement and a footer identifying the inspection preview and its warnings.
  Wait for rendering before exporting. Choose a local `.png` destination;
  existing non-PNG files and symbolic links are refused. Encoding runs on a
  worker and the destination is replaced atomically only after a complete
  write. Export is not fabrication output.
- **Save view settings.** Save the current units, panel visibility, and layer
  order, visibility, colour and opacity for this document. Settings are applied
  when you explicitly open that file again. Reload keeps current unsaved choices.
  Settings do not restore zoom, measurements, selected sheets, removed layers,
  or manually added files. Only matching layers already loaded from the document
  receive saved appearance; new layers keep their defaults. Settings live under
  `$XDG_CONFIG_HOME/TikuCircuit`, or `$HOME/.config/TikuCircuit`. Invalid settings
  are ignored. No CAD source file is modified and no document opens automatically.
- **Open and Reload.** Both run on a worker and replace the shown document
  with its first view only when both are ready; a failure says why in the
  status line and keeps what is shown. One import runs at a time and a newer
  request replaces an older one. Reload refuses a project with an
  unavailable sheet and keeps the selected sheet instance; Open of the same
  file opens it as any file, marking unavailable sheets with `!`.
  Cancelling the file panel changes nothing.
- **Sheets and layers.** The list beside the canvas holds a project's sheet
  instances or a stack's layers, front to back. Layers > Add Layer puts a
  file at the front of the shown stack; a click on a layer's box hides or
  shows it, and a click on an object selects its layer. Reload of a stack
  rereads its files and keeps visibility, order, colour and opacity.
  Removing the layer a stack was opened from names the stack after its new
  front layer.
- **Selection Details.** Click an object or use Find; the panel under the
  canvas scrolls, and Copy Selection Details copies the whole description,
  shown or hidden.
- **Linked files.** A schematic's links and a job's entries are read only
  inside the chosen file's directory, one component at a time without
  following symlinks; absolute paths, URLs, backslashes, drive letters and
  `$` expansion are refused. Each file is read whole, at most 8 MiB, and
  refused if it changes while it is read.
- **Large windows.** A canvas larger than the kit renders at once
  (4,194,304 pixels, 8,192 on a side) is drawn from the largest image it
  renders, scaled to fill the window.

`build/tests/test_circuit_viewer` drives the window without a desktop:
imports, reload, sheets and layers, the picker, Find, Selection Details,
the confined resolver, large windows, resize and closing during work.
`build/tests/test_circuit_association` covers Tracker's routing of the file
types. The `tests/integration/shell/circuit*.script` lanes run the
application inside TikuDesktop.
