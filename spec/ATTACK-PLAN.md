# Attack plan for the open rows

Every row in FEATURES.md whose verdict is `open`, classified by the ONE thing
it waits on, and the clusters ordered so what should be built first comes
first.  Produced by reading all 501 rows, not by grouping the area prefixes.

## The shape of it

The 501 rows are not mostly waiting on TikuOS — only 9 touch the serial/watch transport at all, 86 have no blocker whatsoever (the pose engine itself, 55 of them core or important), and roughly 110 more wait on four host-side services BeOS handed Tracker for free: a per-node state store, a kind/icon resolver over the manifest descriptors, a device/mount model, and a settings file.

That is the finding worth acting on: the port is not mostly blocked on the
device.  It is blocked on a handful of host-side services that BeOS gave
Tracker for free, and on the pose engine itself, which nothing blocks at all.

## Clusters, in build order

| # | cluster | rows | core | imp | pol | size |
|---|---------|-----:|-----:|----:|----:|------|
| 1 | Live listing + watch broker | 9 | 2 | 5 | 2 | medium |
| 2 | Pose engine: selection, keyboard, filter, sort, rename | 49 | 2 | 29 | 18 | large |
| 3 | Column headers and sort interaction | 18 | 1 | 10 | 7 | medium |
| 4 | Per-node state store and session restore | 22 | 3 | 13 | 6 | medium |
| 5 | Device and mount model | 33 | 4 | 19 | 10 | large |
| 6 | Kind and icon resolver | 32 | 4 | 17 | 11 | large |
| 7 | Scroll extent, origin and auto-scroll | 14 | 1 | 7 | 6 | medium |
| 8 | Root view over attached devices | 17 | 1 | 8 | 8 | medium |
| 9 | Transfer engine: copy, move, merge, refuse | 15 | 3 | 7 | 5 | medium |
| 10 | Progress window and conflict alerts | 10 | 2 | 4 | 4 | medium |
| 11 | Trash and restore | 16 | 3 | 10 | 3 | medium |
| 12 | File clipboard | 18 | 2 | 12 | 4 | medium |
| 13 | Drag and drop | 36 | 2 | 19 | 15 | large |
| 14 | Folder navigation menus and recent items | 10 | 2 | 7 | 1 | medium |
| 15 | Navigator bar and browser mode | 8 | 1 | 5 | 2 | small |
| 16 | Get Info window | 18 | 3 | 12 | 3 | medium |
| 17 | Settings store and preferences pages | 21 | 2 | 9 | 10 | medium |
| 18 | Queries and the Find panel | 17 | 2 | 12 | 3 | medium |
| 19 | Merged (virtual) directories | 10 | 1 | 7 | 2 | medium |
| 20 | Open-with handler resolution | 19 | 2 | 13 | 4 | medium |
| 21 | Repaint economies and background images | 9 | 0 | 3 | 6 | small |
| 22 | Add-ons as shell commands | 9 | 0 | 6 | 3 | small |
| 23 | Deskbar | 40 | 6 | 23 | 11 | large |
| 24 | File panels | 22 | 4 | 12 | 6 | medium |
| 25 | Scripting suite | 10 | 2 | 6 | 2 | small |
| 26 | Image thumbnails | 13 | 3 | 7 | 3 | medium |
| 27 | No-analogue leftovers | 6 | 0 | 2 | 4 | small |

## What each one is

### 1. Live listing + watch broker  (9 rows: 2 core, 5 important, 2 polish — medium)

One listing pipeline: a background task issues the namespace list over the serial session in batches, feeds poses to the window while a barber pole spins, and is invalidated when the window switches device or folder so stale rows cannot arrive. The same entry point takes `watch` events, which on TikuOS say only 'touched, re-read' — so it must diff a re-read against the current pose list instead of trusting created/removed/moved ordering the way PoseView::FSNotification does. In front of it sits a watch broker over TIKU_VFS_WATCH_MAX (8 slots) with an explicit eviction/retry rule, because the inherited failure mode is a window that silently stops updating.

**Order:** Depends on nothing but the N1 session client. Every live row in every later cluster enters through this one function, and the 8-slot watch table has to have an owner before clipboard, trash, info windows and queries all start subscribing. Nine rows, but it is the seam the other 492 are drawn on.

Rows: PVL-088, PVN-047, AW-003, PVL-089, PVL-090, TS-038, PVN-006

### 2. Pose engine: selection, keyboard, filter, sort, rename  (49 rows: 2 core, 29 important, 18 polish — large)

Tracker's decision-dense math ported onto the Model layer N1 already proved: selection anchors and pivots, band select with command-toggle, deactivate/reactivate semantics, arrow and type-ahead navigation, multi-term Shift+Space filtering, sort comparators with NaturalCompare, rename armed on the slow second click, date-column degradation through four format pairs, symlink underlining, the Select-by-pattern dialog. Nothing here waits on a subsystem — it is the engine itself. The one new dependency is an editable text widget on the applications/desktop drawing floor so rename-in-place has somewhere to type.

**Order:** The largest single pile (49 rows, 31 core+important) and it is unblocked today. It is also what N2 means: without it there is no window to hang any other cluster off. Building it first converts the most contract rows per unit of work of anything in the 501.

Rows: MA-025, MA-043, PVS-014, PVS-018, PVS-023, PVK-031, PVK-047, PVL-092, PVL-099, AW-080

### 3. Column headers and sort interaction  (18 rows: 1 core, 10 important, 7 polish — medium)

List mode is how a device namespace is actually read, and its columns are descriptor-derived — name, kind, value, size, perms from the manifest — not BFS attributes. Build the title view: click to sort with Shift for secondary, live divider resize with rows reflowing beneath, header drag to reorder, drag-off-the-bar to delete a column, double-click to fit widest value, titles scrolling in lockstep with the body. Column identity keys off the descriptor name, so the offered set differs per device and must degrade sanely when a board's manifest changes between sessions.

**Order:** Follows immediately from the pose engine — it is the visible half of list mode. It also fixes the column record shape that the state store then has to persist, so doing it before the state store avoids serializing a format twice.

Rows: TS-010, TS-007, TS-008, PVL-036, PVL-038, PVL-033, TS-003, TS-020

### 4. Per-node state store and session restore  (22 rows: 3 core, 13 important, 6 polish — medium)

Device nodes have no free-form attributes, so `_trk/pinfo` and `_trk/viewstate` become records in the N1 sidecar store keyed by scored device identity — the one where a uid mismatch is an absolute veto so a renamed board cannot adopt another's arrangement. Build the serialized column/view-state record (title, width, alignment, descriptor, display-as), the parent-inherits-then-DefaultFolderTemplate fallback, rejection of corrupt or absurd saved values, and the set of open windows written on quit and replayed at start. On reopen the session has to tolerate a device that is not plugged in this time.

**Order:** This is what makes folders remember, which is the whole spatial premise. It is also a hard dependency for desktop icon positions, query-result window layout, panel geometry and browser-mode history — four later clusters read it and none can be finished without it.

Rows: MA-058, AW-004, AW-006, CW-005, CW-006, CW-009, MA-060, MA-070, CW-066, TS-032

### 5. Device and mount model  (33 rows: 4 core, 19 important, 10 polish — large)

A volume here is an attached board: mount = opening a session on a serial port, unmount = unplug or close, boot volume = the host filesystem. Build the volume list with live arrival/departure from port enumeration, shown-name rules (Disks, device name, Trash, Desktop differ from the path leaf), read-only answered from manifest capability bits rather than a filesystem flag — remembering that a container is writable when entries can be created in it, which is what tripped /data in N1 — free space from the device's own tier accounting, and the unmount path that closes that device's windows, drops its rows elsewhere and breaks its links. Every read-only alert Tracker raises becomes a capability refusal.

**Order:** 33 rows in itself, and it gates the answer 'may I write here' that the transfer engine, trash, clipboard and drag-and-drop all ask before doing anything. Doing file operations before this exists means writing the refusal logic twice.

Rows: PVN-008, PVN-043, MA-006, FS-012, PVN-040, AW-096, FS-015, FS-041, MA-052, PVK-044

### 6. Kind and icon resolver  (32 rows: 4 core, 17 important, 11 polish — large)

Replace the MIME database with the manifest's typed descriptors: the Kind column text, the icon chain where a node's own icon beats any type icon, the descriptor-to-column mapping the Attributes popup offers per type, and the type tree the Find panel's type menu is built from. Build the resolver plus its cache, eviction and repaint when a descriptor changes, and the accumulating pass so a burst of device notices repaints the window once instead of per row. Per the N1 reconciliation, device leaves get their own node kind so the kind/icon/open machinery fires on them while device directories stay virtual directories.

**Order:** Second-largest service pile. Open-with, the Attributes menu, the query type menu and every icon in the Deskbar read it, so four downstream clusters are stubs until it exists. It is also the most visible single change: rows stop looking generic.

Rows: MA-012, Q-004, IV-001, IV-038, MA-018, MA-002, IV-039, PVL-094, TS-025, PVN-054

### 7. Scroll extent, origin and auto-scroll  (14 rows: 1 core, 7 important, 6 polish — medium)

Extent is columns-by-row-count in list mode and the icon bounding box in icon mode, grown incrementally as poses stream in from the async listing and fully recomputed only when a removal touched an edge. Build the per-mode scroll origin, the pulse-driven range refresh that never fires while a button is held, scrollbars detached during bulk relayout so intermediate positions cannot feed back, clamping of restored origins that arrive NaN or absurd from the state store, and the edge auto-scroll bands with their half-second delay.

**Order:** Cheap, self-contained view math that the async listing immediately stresses (extent must grow as batches land) and that drag-and-drop and band-select both call for edge scrolling. Building auto-scroll here means the drag cluster does not have to invent it.

Rows: PVL-049, PVL-050, PVL-056, PVL-052, PVL-058, PVL-060, PVL-009, CW-013

### 8. Root view over attached devices  (17 rows: 1 core, 8 important, 8 polish — medium)

The top-level container is a view of attached boards and host roots, not a BeOS desktop of partitions. It is immovable, unclosable, all-workspaces, accepts the first click, refuses list mode however the saved state reads, keeps icons on-screen and re-lays offscreen ones when the screen changes (positions saved against the old frame first), and chains one iterator per contributing source into a union view. Background colour, brightness-driven label flipping and the replicant shelf are the tail.

**Order:** It is the window the user lands in, and its union iterator is the same machinery the virtual-directory cluster needs later. It reads the state store for icon positions, so it must come after that; it does not block file operations, so it need not come earlier.

Rows: AW-017, PVL-008, PVL-018, PVL-030, AW-019, AW-020, PVN-051, CW-010, IV-051

### 9. Transfer engine: copy, move, merge, refuse  (15 rows: 3 core, 7 important, 5 polish — medium)

The decision-dense FSUtils port over the serial session: replacing a folder merges into it rather than deleting it, a folder cannot move into itself or a descendant, a file may not replace a folder or symlink, attributes/permissions/times are preserved where the backend can carry them (never the icon position), cancel removes a half-written destination file, and a failing entry is reported without aborting the walk. On this transport a copy is a chunked streaming loop with per-chunk flow control — the board cannot drain UART while writing NVM — so 'copy a file' is resumable state, not a read/write pair.

**Order:** Depends on the volume model for its writability answers and on nothing else. It is the substrate under trash, clipboard paste and every drop, so all three collapse into thin front-ends once it exists.

Rows: FS-020, FS-023, FS-031, FS-029, FS-024, FS-025, FS-033, FS-049, FS-010

### 10. Progress window and conflict alerts  (10 rows: 2 core, 4 important, 4 polish — medium)

One shared status window with a row per concurrent job, working pause and stop buttons, text throttled to ten updates a second without losing the filename, and the speed/estimate alternation. At serial throughput a long job is the normal case, not the exception, so this is the only feedback the user has that anything is happening. It carries the single up-front Cancel/Prompt/Skip-all/Replace-all question when several names collide, the confirmation gate for touching system paths, and the bounded 'ask to stop, then wait' contract on quit that a device write which cannot be aborted has to answer explicitly.

**Order:** Immediately after the transfer engine, because without it the engine is unusable rather than merely incomplete — a copy over serial with no progress and no stop button reads as a hang.

Rows: FS-074, FS-017, FS-072, FS-027, FS-076, AW-009, FS-071

### 11. Trash and restore  (16 rows: 3 core, 10 important, 3 polish — medium)

A device namespace has no trash folder and /data is usually the only mutable store, so trashing a device file means moving it host-side while recording its device identity plus original path so Restore can put it back — recreating missing parents and refusing an occupied path. Build the union trash view over every mounted source, the full/empty icon flipping live as items arrive, delete-inside-trash skipping confirmation with Shift+Delete bypassing trash entirely, and the per-source trash directory created at mount.

**Order:** Needs both the volume model (per-source trash, unmount) and the transfer engine (the move itself). Ahead of clipboard and drag because trash is the destructive path and its refusal rules should be settled before two more front-ends start invoking them.

Rows: FS-039, FS-048, AW-027, FS-040, AW-029, PVN-027, FS-047, IV-046, MA-005

### 12. File clipboard  (18 rows: 2 core, 12 important, 4 polish — medium)

Cut and copy put device+path refs on a clipboard, paste moves cut items and copies copied ones, a second paste always copies, Shift+Paste links, and pasting into an item's own folder duplicates copies while ignoring cuts. Cut poses draw at half opacity everywhere until the operation commits or another application takes the clipboard. Because entries must follow renames and moves and vanish when a device unmounts, the clipboard is itself a watch subscriber and has to tolerate the wholesale unwatch_all the rules engine already does on this platform.

**Order:** 18 rows, mostly small, and it delivers move/copy with no drag machinery at all — over a serial link the keyboard transfer path is arguably the primary one. Doing it before drag-and-drop means the expensive cluster inherits settled semantics rather than defining them.

Rows: FS-051, FS-055, FS-054, FS-060, FS-057, FS-058, FS-061, PVL-096, IV-023

### 13. Drag and drop  (36 rows: 2 core, 19 important, 15 polish — large)

The full protocol: a translucent bitmap of the dragged poses cropped to the cursor box, hilite on the folder pose under the pointer with dragged poses never valid targets, Option at drop time forcing copy only into a different folder, the secondary-button move/copy/link/cancel menu with capability-driven disables, drop-point layout preserving relative offsets while list-mode sources stack vertically, and edge auto-scroll reusing the scroll cluster's bands. Cross-device drops route straight into the transfer engine, so all four refusal alerts are capability answers. Spring-loaded folders and negotiated foreign drops are the large tail, and every spring-load hover is a listing round-trip over the wire.

**Order:** The single largest subsystem and it depends on nearly everything before it — poses, scroll, volumes, the transfer engine, trash. It is high-feel, which is why it is not last, but sequencing it here means it is assembly rather than invention.

Rows: PVS-035, PVS-038, PVS-034, PVS-043, PVS-045, PVS-050, FS-003, CW-017, PVL-100, PVS-051

### 14. Folder navigation menus and recent items  (10 rows: 2 core, 7 important, 1 polish — medium)

Menus that read the namespace live: folder submenus filled progressively in chunks and re-read on every reopen — each chunk is a listing round-trip, so it must never block the menu thread — with subfolders as submenus, files disabled, sorted, truncated, and an Empty folder placeholder. Plus the navigate-into-this-folder submenu that leads context menus, the parent-hierarchy menu off the count area, Recent folders and Favorites honouring configured counts, copy-to/move-to destination trees, and the New submenu that appears once document templates exist.

**Order:** Small, high-feel, and it is the first place the chunked-listing discipline from cluster 1 gets reused outside the pose view. It needs the async listing and the settings store's counts, but blocks nothing else.

Rows: CW-055, CW-056, CW-057, CW-045, CW-036, CW-058, CW-037, PVL-097, TS-043

### 15. Navigator bar and browser mode  (8 rows: 1 core, 5 important, 2 polish — small)

Back/Forward/Up with a 32-entry per-window history, a Cmd+L location field that accepts a typed device path and dropped refs, and a single-window browse mode that rebinds the arrow shortcuts and collapses duplicate windows when left. On a namespace reached one round-trip per level, typing `/dev/gpio` beats walking to it — this is worth more here than it is on a local disk.

**Order:** Eight rows for the biggest per-row gain in felt navigability, and it only needs the pose view plus the state store for history. Cheap enough to slot in whenever the session has room.

Rows: CW-046, CW-048, TS-048, CW-052, CW-053, PVK-041

### 16. Get Info window  (18 rows: 3 core, 12 important, 3 polish — medium)

The best 'what is this node' surface a device namespace can have: descriptor type and kind, capability bits rendered as the permissions grid, size, dates, the device+path location, link target, and an Attributes tab listing every attribute with a decoded value and readable type — which on a board means the manifest's own columns. Add in-place rename in the header, one window per node with cascade on repeats, background folder-size computation, volume capacity refreshed each second, and close-on-unmount. It answers questions `cat` on the shell cannot.

**Order:** Reads the type/icon resolver and the volume model, blocks nothing. Placed here because it is the highest-value read-only surface once those two exist, and three core rows are stuck behind it.

Rows: AW-066, AW-067, AW-070, AW-078, AW-079, AW-068, AW-077, AW-074, AW-071

### 17. Settings store and preferences pages  (21 rows: 2 core, 9 important, 10 polish — medium)

One plain-text settings file, lazily loaded, rewritten wholesale with non-defaults only, plus the pages that edit it and apply every change immediately while hiding rather than quitting. A dozen rows scattered through other clusters — hide dot files, date format, inactive-selection drawing, thumbnail gating, recents counts, disks-icon exclusivity, space-bar colours — need nothing more than this store existing and broadcasting a change to every open window. Defaults and Revert enable on a real diff against captured state, the AutoMounter page pattern being the model for settings a different service owns.

**Order:** Later than its row count suggests, because almost every dependent row can be hard-defaulted until it lands, and none of the earlier clusters are blocked by it. Once built, it retires a long polish tail across five other clusters.

Rows: TS-045, TS-058, TS-059, PVL-078, PVN-052, IV-022, TS-051, Q-052, DB-050

### 18. Queries and the Find panel  (17 rows: 2 core, 12 important, 3 polish — medium)

The one place TikuOS beats BFS: the device can filter its own namespace and the result set is small enough to stream back, so a live query over /sys or /dev is natural where a local-file query has no index to stand on. Build query files as real files — a saved query opens results, a template opens the Find panel pre-filled, searching from a template writes a new query — with attribute rows and operators driven by the descriptor type, relative-date queries that re-arm at the next minute/hour/midnight boundary, target devices remembered by identity and skipped when absent, unmount force-closing result windows, and the week-old transient sweep run only while idle.

**Order:** Depends on the type resolver for its type menu and the volume model for target identity, so it cannot come earlier. It goes ahead of open-with and Deskbar because it is the differentiating feature of this port rather than a re-creation of a 1998 desktop.

Rows: Q-028, Q-029, Q-025, Q-031, Q-040, Q-037, Q-045, Q-050, AW-090, CW-033

### 19. Merged (virtual) directories  (10 rows: 1 core, 7 important, 2 polish — medium)

Several contributing paths presented as one directory — several boards, or /sys plus /dev plus a host directory — with first-holder-wins naming, lower-priority entries shadowed and revealed when the winner disappears, definition files watched so an edit reloads the window, and subdirectories opening as merged views in turn. On TikuOS this is not a legacy curiosity: it is one window over a fleet, which is the thing this file manager can do that a shell cannot.

**Order:** Sits on the union iterator the root view already needs and on the watch broker's re-read discipline, so most of the cost is already paid by then. Its one large row (nested merged subdirectories) is the natural stopping point if time runs short.

Rows: PVN-061, PVN-066, PVN-059, PVN-062, PVN-063, PVN-064, PVN-067

### 20. Open-with handler resolution  (19 rows: 2 core, 13 important, 4 polish — medium)

There is no application roster on a board, so 'open with' becomes a host-side handler table keyed by descriptor type: which host program, or which shell verb, receives a node's data. Build the ranked match (exact beats supertype beats superhandler), the union of supporting apps plus preferred app plus superhandlers widening when nothing matches, the filters that exclude non-executables, trashed apps and other Tracker copies, the Relation column explaining each match, and a dedicated launch looper that is never a window thread. A failed launch must diagnose specifically rather than just fail.

**Order:** Reads the type resolver, so it cannot precede it, and its payoff is thin until there is a host application set worth choosing between. Ahead of the remaining clusters only because double-click-does-something is a real user expectation.

Rows: AW-040, AW-036, AW-041, AW-042, AW-039, MA-017, PVS-044, AW-046, AW-048

### 21. Repaint economies and background images  (9 rows: 0 core, 3 important, 6 polish — small)

Slide rows down by a blit on insert, close the gap on delete, move exactly one row on a sort-key change, repaint only the rubber band's difference sliver, invalidate rather than draw onto dirty pixels over a textured background. These are framebuffer economies from a machine with no compositor; here the latency the user perceives comes from serial round-trips and listing time, not pixel fill.

**Order:** Zero core rows and no cluster depends on it. Implement the naive correct repaint inside the pose engine and only revisit if the wasm canvas build shows a measurable frame cost — treat this cluster as evidence-gated.

Rows: PVL-045, PVL-047, PVL-048, PVL-070, PVL-075, CW-020, PVL-071

### 22. Add-ons as shell commands  (9 rows: 0 core, 6 important, 3 polish — small)

Tracker's add-on model maps unusually well onto TikuOS: an add-on is a shell command or a BASIC program run against the selected nodes, off the window thread, messaging its result back. Build the type-matched submenu with matching entries first, the settings-file-driven Cmd+Opt bindings applied live from a watched file, and the install/remove path that rebuilds menus without a restart. No plugin ABI, no dlopen — the extension point is the shell that already exists.

**Order:** Zero core rows, so it cannot displace anything above it, but it is the cheapest route to extensibility and it composes with the existing BASIC and shell work rather than adding a new mechanism. Good filler once the transfer engine and menus are in.

Rows: CW-038, AW-022, AW-023, DB-040, DB-041, CW-039, AW-102

### 23. Deskbar  (40 rows: 6 core, 23 important, 11 polish — large)

A taskbar for applications/desktop: the leaf menu rebuilt from a folder on every open, three recents submenus, the running-application roster with Tracker pinned first, the tray and clock with its calendar and replicant handling, ten-zone dragging, auto-hide and auto-raise, the Ctrl+Tab switcher, and its own four-page preferences window. None of it is file management, and none of it has a device-side population — the roster is host processes, so this cluster exists only if applications/desktop is meant to be a usable desktop environment.

**Order:** Six core rows keep it in the contract, but nothing else depends on it and the user feels none of it while managing files. If it is built, build the leaf menu and clock and stop there; the switcher and dock modes are listed below as waive candidates.

Rows: DB-002, DB-014, DB-048, DB-054, DB-057, DB-061, DB-003, DB-016, DB-033, DB-052

### 24. File panels  (22 rows: 4 core, 12 important, 6 polish — medium)

Serving open and save panels to other applications, running inside the requesting app's process and replying with refs or directory-plus-name. That contract presupposes an application ecosystem this port does not have — the only caller is Tracker itself, for repointing a broken link, choosing a copy-to destination and saving a query. What is genuinely worth building is one internal chooser: start-directory fallback chain, double-click navigates rather than chooses, save refuses root and dot names and prompts on clash, and the keyboard set with Escape precedence.

**Order:** Four core rows, but three-quarters of them describe an inter-application protocol with no second party. Sequenced late deliberately so the internal chooser can be built as a thin reuse of the pose view rather than as a service.

Rows: AW-049, AW-051, AW-052, AW-055, AW-050, AW-057, AW-060, PVN-029, PVS-009

### 25. Scripting suite  (10 rows: 2 core, 6 important, 2 polish — small)

Tracker's BMessage scripting exposed windows, poses and selections to other programs through a property_info table with index and previous/next specifiers that degrade rather than fault. On TikuOS that capability already exists in the shell and agent channel, so building this adds a second, incompatible interface onto the same state. If anything is built it should be a small verb set on the existing channel — get/set selection, open a window on a path, empty trash — not the specifier machinery.

**Order:** Two core rows keep it nominally in the contract, but it is the clearest case of porting a mechanism whose purpose is already served. Last among things that might be built at all.

Rows: PVK-048, PVK-051, PVK-050, PVK-052, AW-099, PVK-055, AW-082

### 26. Image thumbnails  (13 rows: 3 core, 7 important, 3 polish — medium)

Decode an image file, scale to 128x128 preserving aspect with transparent margins, persist it back onto the file as a WebP attribute, two worker queues split at 128 KB, never the same node twice concurrently, cache valid only while newer than the file. Device nodes have no attribute to persist into and a sensor board has no images; pulling a whole file across a serial link to render a 128-pixel preview is the wrong trade in exactly the direction this project cares about.

**Order:** Three core rows, entirely host-backend value, no dependents. Last of the buildable clusters, and the persistence half is proposed for waiver below.

Rows: IV-028, IV-029, IV-031, IV-030, IV-034, IV-032, IV-036, AW-098, TS-053

### 27. No-analogue leftovers  (6 rows: 0 core, 2 important, 4 polish — small)

Six rows describing BeOS mechanisms with no counterpart anywhere in this port: byte-order twin attribute names (_be/_le) and the swapped-twin read path, the version resource in an application binary, localized filename lookup, creating query indices on every attribute-capable volume, and an unknown-colour-space fallback in a renderer that has exactly one pixel format. There is nothing to build; the endianness pair is meaningless when host and board agreement is fixed by the session protocol.

**Order:** Depends on nothing, unblocks nothing, and would produce code that can never execute. Listed as a cluster only so the six rows get closed explicitly in FEATURES.md rather than lingering as open contract.

Rows: AW-014, MA-007, MA-020, MA-032, MA-065, IV-020

## Proposed waivers

Rows worth dropping from the contract rather than building.  Only the user
can waive a row; these are the arguments, not decisions.

- **AW-014, MA-007, MA-020, MA-032, MA-065, IV-020**  
  The whole no-analogue cluster. Byte-order twin attributes and the swapped-twin read path are meaningless when host/board byte order is fixed by the session protocol; version resources, localized filename lookup, per-volume query index creation and a second colour space have no counterpart in the manifest, the drawing floor or the namespace. Implementing them produces code that can never execute.

- **IV-030, IV-032, IV-033, IV-035, IV-036, IV-060, AW-098**  
  The thumbnail persistence and worker-queue half. Device nodes have no free-form attribute to hold a 128x128 WebP, so the persistence path is host-POSIX-only, and pulling an entire image file over the serial session to render a preview inverts the cost model the whole project is built on. Keep IV-028/029/031 (show a preview if the backend can cheaply produce one, never block the window) and waive the rest.

- **PVK-048, PVK-049, PVK-050, PVK-051, PVK-052, PVK-053, PVK-054, PVK-055, AW-082, AW-099**  
  The entire scripting suite. It re-creates programmatic control of Tracker through a BMessage property_info table with deliberately unstable index specifiers — a capability TikuOS already has in the shell and agent channel. Building it means maintaining two incompatible remote-control interfaces onto the same window state. If the user wants the capability, re-spec it as a handful of verbs on the existing channel rather than closing these ten rows as written.

- **AW-049, AW-053, AW-061, AW-062, AW-063, AW-064, AW-088, PVK-022**  
  The inter-application half of file panels. 'Serve open and save panels to other applications, running in the requesting app' presupposes an application ecosystem that does not exist in this port — the only caller is Tracker itself. Keep the internal chooser rows (AW-050/051/052/055/057/059/060, PVN-029) and waive the service contract, the shared panel template and the Favorites menu.

- **DB-033, DB-048, DB-049, DB-054, DB-055, DB-056, DB-057, DB-058, DB-059, DB-060**  
  The switcher and dock-mode rows inside Deskbar. A Ctrl+Tab application switcher with MRU ordering, animated icon strips, and a ten-zone dock is a desktop-environment feature, not a file-manager one, and there is no device-side application population to switch between. Worth asking whether the whole 40-row Deskbar cluster belongs in this contract at all; if it stays, the leaf menu, clock and running-window list are the parts that earn their place.

- **PVL-045, PVL-046, PVL-047, PVL-048, PVL-070, PVL-075, TS-040**  
  The incremental-repaint rows. Every one of them is a blit-instead-of-redraw economy tuned for a 1998 framebuffer; here perceived latency comes from serial round-trips and directory listing, not pixel fill, and zero of the seven are graded core. Waive as a class with the rule 'repaint correctly, measure before optimising' — and reopen only if the wasm canvas build produces a measured frame cost.

- **CW-044, PVS-051, PVS-052, PVS-054, PVS-053**  
  Spring-loaded folders. Hovering a drag to pop open a navigable hierarchy costs one listing round-trip per hover over the serial session, which turns the feature into a stutter rather than a shortcut, and it is two large rows plus three polish rows on top of an already-36-row drag cluster. Propose replacing it with explicit click-to-open-during-drag, recorded as a deviation.

- **AW-011, DB-067, DB-058, AW-104**  
  Singleton rows describing host-environment features with no owner here: close-all-in-workspace (there are no workspaces on this floor), locale change re-reading localized application names and reflowing the tray, switcher animation, and the desktop replicant shelf. Each is one row that would drag in a subsystem nothing else uses.
