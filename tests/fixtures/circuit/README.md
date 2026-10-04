# Authored circuit fixtures

These schematics, boards and artwork fixtures were written for TikuCircuit's tests and are licensed
under Apache-2.0 with the repository. They do not contain a third-party symbol
library or customer design. They are hand-authored, not copied from KiCad's
demo designs. The `tikucircuit_test` generator identifies these authored tests;
it does not claim that KiCad created them.

## Drill fixtures

`drill-board.kicad_pcb` is an authored, four-pad board used only as an export
oracle. TikuCircuit does not read native PCB files. KiCad CLI 7.0.11 accepted it:

```sh
kicad-cli pcb export drill --output /tmp/circuit-drill-reference/ \
  tests/fixtures/circuit/drill-board.kicad_pcb
kicad-cli pcb export drill --excellon-oval-format route \
  --generate-map --map-format svg --output /tmp/circuit-route-reference/ \
  tests/fixtures/circuit/drill-board.kicad_pcb
```

`drills.drl` and `drills-routed.drl` retain the exported tools, geometry and
plating attributes. The volatile date comments were removed and the software
version shortened to 7.0.11. The first uses G85 slots; the second uses
G00/M15/G01/M16 routes. Neither fixture uses third-party footprints or designs.

| Feature | Expected scene coordinates (mm, Y down) | Diameter |
| --- | --- | --- |
| Hole 1 | Centre (10, 10) | 1 |
| Hole 2 | Centre (20, 10) | 1.5 |
| Vertical slot | (10, 19) to (10, 21) | 1 |
| 45-degree slot | (19.293, 19.293) to (20.707, 20.707) | 1 |

`test_circuit_drill` checks these constants and verifies that both export
styles produce identical geometry. It also checks selected rendered pixels.
`routes.xnc` is a separate hand-authored analytic fixture, not a KiCad export.
Its two radius-10 quarter circles run from source (0, 0) to (10, 10). The CCW
arc has scene centre (0, -10) and sweep -pi/2; the CW arc has scene centre
(10, 0) and sweep +pi/2. The remaining objects are a straight route and a hole.
Further test strings cover inches, modal axes and attribute snapshots.

These are deliberately small oracles, not qualification against an arbitrary
manufacturer's Excellon dialect or a large XNC corpus.

## Gerber artwork fixtures

`artwork-board.kicad_pcb` is an authored four-pad board, not an application
input format. KiCad CLI 7.0.11 generated `artwork.gbr` with:

```sh
kicad-cli pcb export gerbers --layers F.Cu --disable-aperture-macros \
  --exclude-refdes --exclude-value --output /tmp/circuit-artwork-reference/ \
  tests/fixtures/circuit/artwork-board.kicad_pcb
```

The committed fixture preserves geometry and relevant attributes, removes
volatile creation/project metadata and shortens the generator version. It
contains no third-party footprint library or customer design.

| Object | Exported geometry in scene millimetres (Y down) |
| --- | --- |
| Circular pad | Centre (10, 10), diameter 2 |
| Rectangular pad | Centre (20, 10), width 3, height 2 |
| Obround pad | Centre (10, 20), width 2, height 4 |
| Rotated rectangular region | (20.707107, 22.121320), (17.878680, 19.292893), (19.292893, 17.878680), (22.121320, 20.707107) |

`test_circuit_gerber` compares against these fixed coordinates and selected
pixels. Through-holes belong to the separate drill data and are not holes in
this copper artwork. The viewer does not invent them.

`polarity.gbr` is a separate hand-authored coverage fixture. A diameter-10
disk at (10, 10) is followed by an 8/4 ring at the same centre; the ring's hole
leaves the disk intact. A later clear 2-by-2 square removes its centre. A second
ring at (25, 10) exposes the empty layer through its hole. Further objects are
an 8-by-4 obround and an eight-millimetre hexagon, each with a diameter-2 hole.
Tests check both pixels and selection in clear and transparent areas.

Additional inline fixtures cover exact quarter/full circles, inches, modal
coordinates, attribute semantics and rejected geometry. These tests do not
establish compatibility with arbitrary CAD exports or a manufacturing corpus.

## Single-sheet schematic fixtures

`basic.kicad_sch` exercises embedded symbols, pins, text, wires, a local label,
a junction, and a no-connect marker. `transforms.kicad_sch` has an asymmetric
symbol placed normally, rotated, and rotated with each mirror axis.

Both files were independently accepted by KiCad CLI 7.0.11, whose SVG export
was generated with:

```sh
kicad-cli sch export svg --exclude-drawing-sheet -o /tmp/circuit-reference \
  tests/fixtures/circuit/transforms.kicad_sch
```

The first, second, and third polyline coordinates from that export, in mm:

| Instance | First | Second | Third |
| --- | --- | --- | --- |
| U1, normal | 41, 38 | 45, 38 | 41, 34 |
| U2, 90 degrees | 58, 39 | 58, 35 | 54, 39 |
| U3, 90 degrees + mirror x | 78, 41 | 78, 45 | 74, 41 |
| U4, 90 degrees + mirror y | 102, 39 | 102, 35 | 106, 39 |

The importer test compares against these values, not against its own
transformation function. This checks geometry, not pixel-identical font
rendering. KiCad is a development oracle, not a runtime or test dependency.

## Hierarchy and instance references

`hierarchy.kicad_sch` places `channel.kicad_sch` twice: Channel A uses R1 unit
1, while Channel B uses R2 unit 2. The child properties still say R1/unit 1,
so incorrectly reusing the same decoded scene cannot pass this test. The
two-unit library is deliberately small and authored here.

KiCad CLI 7.0.11 accepted the hierarchy. Export with the absolute root path so
the CLI resolves the child reliably:

```sh
kicad-cli sch export svg --exclude-drawing-sheet -o /tmp/circuit-hierarchy \
  /path/to/applications/tests/fixtures/circuit/hierarchy.kicad_sch
```

Its root, Channel A and Channel B SVGs give these independent checks:

| Object | Expected geometry or display |
| --- | --- |
| Channel A body | Rectangle from (47, 39) to (53, 41) mm |
| Channel B body | Circle centred on (50, 40) mm, radius 2 mm |
| References | R1A and R2B, including multi-unit suffixes |
| Child IN marker | First vertex (35, 40); third (36.27, 40.635) mm |
| Child OUT marker | First vertex (63.73, 40) mm |
| Root IN sheet pin | First vertex (31.27, 40) mm |
| Root OUT sheet pin | First vertex (70, 40) mm |
| Sheet properties | Name has no prefix; filename has `File: ` prefix |

`test_circuit_project` compares fixed coordinates, scene types and display
strings. It also tests ambiguous project buckets, duplicate UUIDs, missing
instance units, nested sheets, cycles, source changes and resource limits.
The app tests separately check actual filesystem containment and hard-link
identity. None of these tests invokes KiCad at runtime.

## Global labels and do-not-populate symbols

`labels.kicad_sch` has all five global label shapes, cardinal label rotations,
a hidden intersheet-reference variable, and two DNP symbols, including a
rotated/mirrored instance. Its footprint and datasheet strings are authored
test data; `example.invalid` cannot be a production datasheet service.

KiCad CLI 7.0.11 accepted this fixture. Its SVG establishes these observations:

| Object | Connection / body direction |
| --- | --- |
| INPUT | Anchor (30, 30), body to the right, point at the anchor |
| OUTPUT | Anchor (65, 30), body to the left, point at the far end |
| BIDIR | Anchor (95, 30), body upward, points at both ends |
| TRI | Anchor (110, 30), body downward, points at both ends |
| PASSIVE | Anchor (30, 40), rectangular body to the right |
| U1 cross | Diagonals between x=47.9/52.1 and y=55/65 mm |
| U2 cross | Diagonals between x=80/90 and y=57.9/62.1 mm |

The SVG cross stroke is 0.4572 mm; coordinate rounding differs by at most
0.0001 mm. Pin text and far-away instance properties do not enlarge it.
Label endpoints cannot be compared directly because the preview substitutes
a different font; the tests check anchor, direction, tip placement and text
containment. Additional generated test inputs exercise every shape at every
cardinal angle. Visible unresolved intersheet-reference variables are refused.

## Property orientation and alignment

`properties.kicad_sch` contains twelve instances: rotations 0, 90, 180 and 270
degrees, each with no mirror, mirror x and mirror y. Each reference has a
stored angle of 0, and each value has a stored angle of 90. All have left
justification. KiCad CLI 7.0.11 accepted and exported the fixture.

Property positions stay in sheet coordinates. The exported text directions
change with symbol rotation; glyphs remain upright. Mirroring changes which
side of the anchor contains the text, without reflecting the glyph outlines.
The following signs were read from SVG text centres relative to the stored
anchors. `+` means right for horizontal text or up for vertical text; `-` means
left or down. Triples are no mirror / x / y.

| Symbol rotation | Reference direction; sides | Value direction; sides |
| --- | --- | --- |
| 0 | Horizontal; + + - | Vertical; + - + |
| 90 | Vertical; + - + | Horizontal; - - + |
| 180 | Horizontal; - - + | Vertical; - + - |
| 270 | Vertical; - + - | Horizontal; + + - |

`test_circuit_kicad` uses these fixed signs and anchor coordinates as its
oracle, not a second call to the importer's transformation function.

## Text direction and pin text

`orientation.kicad_sch` has two local labels turned 180 and 270 degrees, a
symbol turned 180 degrees whose library text has angle 0, and a symbol whose
library sets `(pin_names (offset 0))`, with a horizontal pin showing its name
and number. KiCad CLI 7.0.11 accepted it, and its SVG export

```sh
kicad-cli sch export svg --exclude-drawing-sheet -o /tmp/circuit-orientation \
  tests/fixtures/circuit/orientation.kicad_sch
```

gives these extents of the stroked glyphs, in mm:

| Text | Reads | Extent |
| --- | --- | --- |
| LEFTNET, anchor (30, 50), 180 degrees | Left to right, upright | x 22.52-29.78, y 48.22-49.49: left of and above the anchor |
| DOWNNET, anchor (80, 50), 270 degrees | Bottom to top | x 78.22-79.49, y 50.22-58.63: below and left of the anchor |
| LIBTXT, symbol at (50, 80) turned 180 degrees | Left to right, upright | x 47.28-52.90, y 80.31-81.58 |
| NAME, pin from (100, 80) to (110, 80) | Left to right | x 102.79-107.27, y 78.31-79.58: above the pin |
| 7, the same pin's number | Left to right | x 104.58-105.42, y 80.31-81.58: below the pin |

`test_circuit_kicad` checks each text's direction and the side of its anchor
or pin it lies on. The substitute font's metrics differ from KiCad's stroke
font, so extents are compared with a margin. The export of `basic.kicad_sch`
likewise puts its pin numbers right of their vertical pins on x = 40, at
x 40.29-41.29.

## Text at other angles

`angles.kicad_sch` has free text at 45, -90 and 90.7 degrees, local labels at
45 and -90 degrees, and a symbol whose library text is at 450 and 1800 tenths
of a degree (45 and 180 degrees), placed upright as U1 and turned 90 degrees
as U2. KiCad CLI 7.0.11 accepted it; its SVG export
(`kicad-cli sch export svg --exclude-drawing-sheet`) draws, in mm:

| Text | Reads | Glyph extent |
| --- | --- | --- |
| T45, anchor (20, 20) | Left to right | x 20.22-23.24, y 18.26-19.59 |
| TM90, anchor (40, 20) | Left to right | x 40.22-44.70, y 18.32-19.59 |
| T907, anchor (60, 20) | Bottom to top | x 58.57-59.84, y 15.24-19.53 |
| L45, anchor (20, 40) | Left to right | x 20.40-23.31, y 38.16-39.49 |
| LM90, anchor (40, 40) | Left to right | x 40.40-44.76, y 38.22-39.49 |
| SLANT in U1 | Bottom to top | x 31.38-32.65, y 60.24-65.50 |
| TURNED in U1 | Bottom to top | x 25.54-26.81, y 71.58-78.54 |
| SLANT in U2 | Left to right | x 60.36-65.62, y 67.24-68.51 |
| TURNED in U2 | Left to right | x 71.46-78.42, y 73.08-74.35 |

Free text and labels read upward only when their angle's whole degrees are 90
or 270. Library text is centred on the box its angle turns it into, and reads
across when the symbol is upright and the angle is zero, or when the symbol is
turned a quarter and the angle is not; the 180-degree text in the upright U1
therefore reads upward. `test_circuit_kicad` checks every direction, the side
of the anchor free text lies on, and the library texts' centres within the
difference the substitute font's widths make.

## KiCad aperture macros

`roundrect-board.kicad_pcb` and `freepoly-board.kicad_pcb` are authored
boards, not application input. KiCad CLI 7.0.11 generated `roundrect.gbr` and
`freepoly.gbr` from them with `kicad-cli pcb export gerbers --layers F.Cu
--output /tmp/circuit-macro-reference/ <board>`. The creation date, project
ID and generator comment were removed and the version shortened to 7.0.11;
the geometry is as exported. `roundrect.gbr` uses KiCad's RoundRect macro:
2 x 1 mm, corner ratio 0.25 at (10, 10); 2 x 1 mm, ratio 0.5 at (20, 10),
whose body polygon has no area; 1 x 1 mm, ratio 0.5 at (30, 10), whose body
and side lines have no length; and the second pad turned 30 degrees at
(40, 10). `freepoly.gbr` uses KiCad's FreePoly macro, which writes the start
again as its last vertex before the closing point: the arrow (1, 0),
(0.5, -0.75), (-0.5, -0.75), (-0.5, 0.75), (0.5, 0.75) at (10, 10), and turned
90 degrees at (20, 10). `test_circuit_gerber_extended` checks points inside and
outside each pad, computed from the boards' pad definitions.

## Layers, jobs and curved Gerber regions

`stack.gbrjob` is authored test data referencing the existing drill, artwork
and polarity fixtures. Its order puts drills first, then two artwork layers.
`partial.gbrjob` deliberately contains a missing file and `../outside.gbr` to
exercise unavailable entries and confinement; neither path is created by tests.
They are display-composition fixtures, not a manufacturing package.

`curved-region.gbr` comes from KiCad CLI 7.0.11's front-copper export of the
authored `drill-board.kicad_pcb`, with generation timestamps removed:

```sh
kicad-cli pcb export gerbers --layers F.Cu --output /tmp/circuit-gerbers/ \
  tests/fixtures/circuit/drill-board.kicad_pcb
```

The four pads include a 45-degree rotated obround represented by a region
with circular edges. This covers rounded arc coordinates, G75 inside a region
and repeated endpoint D01 commands emitted by KiCad. Tests check object count,
the tessellated contour and coverage at its known centre. Separate analytic
fixtures test multiple contours, layer-local clear operations, transparency,
front-to-back hit selection and exact opacity blending. KiCad is not needed
to run the tests.

## Extended formats

`eagle.sch`, `macros.gbr`, `repeats.gbr` and `cutins.gbr` are authored fixtures,
not third-party designs or library copies. Production imports never modify them.

The EAGLE fixture declares XML version 9.6.2. It contains two sheets, a two-gate
device, an asymmetric symbol, a mirrored/rotated gate, ordinary and detached
name/value attributes, a custom rating attribute, pin-to-pad mappings, a net,
a bus, graphics and a numbered frame. It validates with `xmllint --nonet
--noout --dtdvalid eagle.dtd eagle.sch` against CadSoft's published EAGLE 7.7
DTD, copyright CadSoft 2016, retrieved unchanged from
[the vendor DTD mirror](https://raw.githubusercontent.com/drtrigon/eagle/master/doc/eagle.dtd).
The DTD is not vendored or loaded by the application. `xmllint` can warn that
the relative `eagle.dtd` is absent before validating against the explicit DTD.

Independent analytic expectations: gate A's first wire starts at (18,-23) mm
in scene coordinates; gate B's MR90 wire starts at (52,-27) mm. Its OUT pin
maps to pad 4. Display names are U1A/U1B. Both gates retain the 3.3 V custom
attribute, including the smashed instance. The second sheet's frame numbers
columns 3,2,1 from left to right and rows A,B from top to bottom. SHEET becomes
2/2. Every text transform has positive determinant: mirror placement must
not produce backwards glyphs. Tests verify these values, project navigation,
atomic reload and font-backed rendering. XML validation is not an EAGLE
rendering oracle; no EAGLE-generated image corpus is claimed. The semantics
follow Autodesk's EAGLE documentation for instances, text, pins, wires and
frames.

The macro fixture covers arithmetic and variables, all current primitive
codes (1/4/5/7/20/21), and historical 2/6/22. Seven flashes are placed in three
rows. At (5,-5) mm, the first macro has a transparent centre; (7,-5) is filled.
The thermal at (15,-15) has four diagonal sectors and clear axial gaps.
Separate tests place macro holes over previous dark geometry and apply a
whole macro with clear polarity, distinguishing isolated exposure from
layer-wide clearing. Render-allocation failures exercise each temporary mask.

The repeat fixture expands a dark/clear pair into two columns and three rows
(12 operations). Source positions run Y first, then X. Object 2 is at
(5,-12) mm and object 6 at (8,-15) mm before conversion to screen axes.
Nested-block tests use four flashes and enforce the final expansion budget.

The cut-in fixture has a 20 x 20 mm outer rectangle with 4 x 4 mm holes centred
at (5,5) and (12,12). Each horizontal bridge is traversed exactly twice in
opposite directions. Both holes remain clear in pixels and picking; points
such as (9,9) remain filled. Invalid crossings are refused atomically.

As an additional independent visual check, **gerbv 2.10.0** rendered all three
Gerber fixtures to PNG; the macro shapes, six repeated rings and two cut-in
holes agree with the TikuCircuit views. gerbv warns about the region-only
fixture's absent aperture declarations, but renders the regions. Scale, margins,
colour and antialiasing differ: this is not an exact pixel-diff oracle. gerbv
is not a runtime or regression-test dependency. The Gerber semantics follow
Ucamco's 2026.05 specification. Analytic assertions remain executable without
external CAD tools.
