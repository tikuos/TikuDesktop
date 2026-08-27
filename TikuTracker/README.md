# TikuTracker — the file manager for TikuOS

The real Tracker's architecture and decisions, rebuilt in a codebase we own,
with the original source as the specification.  Full functionality; nothing
stripped.  `HANDOVER.md` is where to start: it carries the working method
and the current state of the 889-row contract.  `NOTES.md` is the build log
for the numbered phases from N1 on; N0 is the section below.

## N0 is complete (2026-08-11)

| Artefact | What it is |
|---|---|
| `FEATURES.md` | **The fidelity contract** — 889 behavior rows extracted from the real source, each citing the file and symbol it was read from |
| `spec/ARCHITECTURE.md` | Structural overview read first-hand: Model, the three pose lists, async directory reading, spatial state on objects, TaskLoop |
| `spec/ARCH-<area>.md` | Per-area architecture notes from the readers (12 files) |

How it was produced: 12 agents read the source in parallel (PoseView split
four ways, plus ContainerWindow, FSUtils, Model/attributes, queries, icons,
columns/settings, app-level, Deskbar), then 3 adversarial verifiers opened
the cited files, rejected what they could not confirm, corrected wrong
citations, and hunted for behaviors the readers missed.  **68 rows exist
only because verification looked for gaps** — those would have been silently
dropped.  30 rows carry a verifier correction; several were describing a
rule backwards.

Verification coverage is partial (442 of 889 rows checked, weighted toward
`core` rows and rows asserting specific numbers or modifier keys), and one
verifier reported a truncated input batch.  Both facts are stated in
`FEATURES.md` rather than smoothed over.

## The decisions N1 must make

312 rows carry a **TIKU** note where the device namespace differs from BFS.
They cluster into six real design questions, and none of them may be
answered by quietly dropping the row:

| Rows | Question |
|---:|---|
| 55 | **Where does per-node UI state live?** BFS stores it *on the object* (`_trk/pinfo` per entry, `_trk/viewstate` per folder). Local files get xattrs; TikuOS nodes have no free-form attributes. |
| 42 | **What replaces the MIME/type system?** Tracker resolves icons, "kind" text, open-with and launching through it. TikuOS has typed descriptors instead — richer in some ways, absent in others. |
| 37 | **How does `watch` map onto node monitoring?** Tracker's liveness assumes created/removed/moved/stat/attr events with specific ordering; TikuOS `watch` says "touched, re-read". |
| 34 | **What are volumes, Trash and the Desktop** when a device is a mountable thing? |
| 26 | **How do capabilities appear?** TikuOS refuses writes by capability; Tracker's model is volume read-only plus permissions. |
| 13 | **Queries** — the one place TikuOS is *better*: live queries over the namespace are natural, while local-file queries have no index to stand on. |

## Rules of engagement

- The source stays open: `~/kintsugi/src/kits/tracker`, `src/apps/deskbar`.
- No behavior is invented where the source has an answer.
- Rows close only when the behavior matches the source; only the user may
  waive a row, in writing, in `FEATURES.md`.
- Every phase from N2 on ends with running software, not a document.
