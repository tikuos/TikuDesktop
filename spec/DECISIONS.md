# DECISIONS.md — the six N1 design decisions, reconciled

Resolved 2026-08-11 by six design agents reading the FEATURES.md rows for
their cluster, the Tracker source those rows cite, and the TikuOS namespace;
then reconciled by an adversarial judge that found **14 conflicts** between
them and **8 gaps** no decision owned.  The reconciliation is authoritative
where it contradicts an individual decision.

## Reconciliation verdicts (authoritative)

### C1. D2 (type model) vs D4 (volumes) — Model kind enum

**Problem.** D2 says device directories reuse `kDirectoryNode` and adds exactly one kind, `kDeviceNode`. D4 replaces the kind enum entirely with a 10-value `tk_node_kind_t` that has `TK_NODE_VIRTUAL_DIR` but NO device-leaf kind. These are the same struct field with incompatible contents. D4's ENTIRE read-only story is `TargetVolumeIsReadOnly()` short-circuiting on `IsVirtualDirectory()` before it consults the volume (PoseView.cpp:8919) — under D2's assignment, /sys and /dev are ordinary directories, so New folder, Cut, Paste, the read-only tint and FS-078's drag-out-creates-a-link are all ENABLED on the device namespace and D4's ~10 free rows evaporate.

**Verdict.** SPLIT THE VERDICT. D4 wins on directories: every device directory except a dynamic store is `kVirtualDirectoryNode` (Tracker already HAS this kind — D4 renamed it, it is not new), and /data is a real `kDirectoryNode`. D2 wins on leaves: `kDeviceNode` is added, `IsFile()` includes it, so the type table / icon chain / open-action machinery fires. Net new kinds = exactly one, which is what D2 argued for, but D2's sentence 'device dirs reuse kDirectoryNode' is overruled. Keep Tracker's 11 values verbatim; do not adopt D4's replacement enum — it silently drops query/query-template distinctions D6 needs.

### C2. D1 (state) vs D4 (volumes) vs D6 (queries) vs D2 (type) — device identity string

**Problem.** Four spellings of one key. D1: `devid` = "uid-<32 hex>" or "prov-<16 hex>", produced by a scored match (uid=8/id=4/mcu=2/name=1, accept >=7, uid mismatch = absolute veto). D4: `tk_volume_t.identity[64]` holds the RAW `/sys/device/uid`, and state keys are "<uid>:<path>". D6: `tiku_trk_ref_t.store[40]` and `tiku_trk_scope_t.dev_uid[40]` hold the raw uid, with a DIFFERENT fallback algorithm (name+mcu+version, no id, no veto). D2: preferred-handler map keyed on an undefined `device_id`. Buffers disagree (64 vs 40) and the two matching algorithms can bind the same board to different records — the sidecar says device A, the saved query scope says device B.

**Verdict.** D1 WINS on both the algorithm and the string form; it is the only one that handled the ambiguous case (provisional flag + veto). One function `tiku_trk_ident_key()` emits one canonical devid; `tk_volume_t.identity`, `tiku_trk_ref_t.store`, `tiku_trk_scope_t.dev_uid` and D2's `device_id` all hold THAT string. Single width `TIKU_TRK_DEVID_MAX 40` ('uid-' + 32 hex + NUL = 37). D6's name+mcu+version scoring is DELETED. D4's synthetic `tk_dev_t` survives but is runtime-only and must never be persisted or matched against — enforce it, D4 already flagged the stale-clipboard-ref failure in its own risks.

### C3. D1 (state) vs D4 (volumes) — who owns device window/pose state

**Problem.** Both decisions implement device UI-state storage, differently. D1: per-device sidecar file keyed by devid, with three reserved node keys (TRK_KEY_DEV_ROOT, TRK_KEY_DESKTOP, ordinary paths), and the Desktop slot deliberately held OUTSIDE any device sidecar so wiping device B cannot disturb where device A's icon sits. D4: `tk_state_key()` producing a flat "<uid>:<path>" in a shared store on the local Desktop directory, with no reserved keys and no per-device partition. D4's scheme cannot express D1's structural guarantee — the arrangement of ALL device icons ends up in one keyspace, and 'forget this device' becomes a prefix grep over shared state.

**Verdict.** D1 WINS entirely. `tk_state_key()` is deleted. D4 calls `tiku_trk_state_read/write(store, node_key, name, ...)` against `trk_store_sidecar_open(devid)` using D1's three-slot key scheme. D4 retains only its (correct, and agreeing) observation that a device volume is permanently in the AW-005 condition. This is pure duplicated implementation and must collapse to one before either is coded — two stores writing overlapping state is the worst possible outcome.

### C4. D2 (type) vs D4 (volumes) — what the device root IS

**Problem.** D2's classifier rule 1 is a well-known-path table, first match wins, mapping "/" -> `tiku-dir/root`. On a device volume "/" is the device root, which D4 calls the mount root (`TK_NODE_VOLUME`) and which D2 separately gives the type `tiku-dir/device`. Because the path table matches first and is not volume-scoped, `tiku-dir/device` is UNREACHABLE — a dead entry in the registry. Consequence: the device root gets the wrong icon, the wrong Kind string, and (via D1) is looked up under the wrong window-state slot, breaking CW-004/PVL-103/MA-064's three-way split that D1 went to trouble to preserve.

**Verdict.** D4's kind model wins and D2's classifier is CORRECTED. `tiku_trk_type_classify()` must take the volume, not a bare absolute path — a signature change that has to happen before the classifier is written. On a device volume "/" classifies `tiku-dir/device` with kind `TK_NODE_VOLUME`; `tiku-dir/root` is reserved for the host-side Devices pseudo-root. D2's path table becomes volume-relative for every entry, not just this one.

### C5. D2 (type) vs D5 (capabilities) vs D3 (watch) — when a device node re-types, and whether capability is per-node

**Problem.** D2: classification runs at manifest load/reload ONLY; capability changes arrive with a manifest reload (its MA-014 differ says so explicitly). D5: capability changes arrive via a pushed `/sys/vfs/cap_gen` event with NO manifest reload, invalidating cached verdicts and re-running enablement. D3: defines `TK_FIELD_CAP` ('device: req_cap changed') as a per-node field bit on a CHANGED event. D3's field is UNPRODUCIBLE: the caller capability is one ambient global word (`static tiku_vfs_cap_t vfs_caller_cap`, tiku_vfs.c:634) swapped wholesale by `tiku_shell_io_set_backend()`. TikuOS has no per-node capability change at all.

**Verdict.** D5 WINS on the mechanism; D2 and D3 are both corrected. The honest split: `req_cap` (what the node requires) is static per firmware image and moves only at manifest reload — D2 is right. The HELD cap is ambient, global and moves via cap_gen — D5 is right. So the verdict changes while the type does not. Therefore: (a) `Model_ReclassifyFromManifest()` must NOT be triggered by cap_gen; (b) D5's cap_gen handler must NOT re-classify or re-icon; (c) DELETE D3's `TK_FIELD_CAP` and `TK_FIELD_DESC` — neither can be produced per-node. Replace with a whole-backend `TK_FS_BACKEND_CAP_CHANGED` (ref.node == 0), handled exactly like D3's existing lost-flag -> Refresh() path.

### C6. D3 (watch) vs D6 (queries) vs D5 (capabilities) — the 8-slot watch table

**Problem.** Three decisions spend slots from one table and only two of them count. D3 budgets <=half for the monitor and asks for -DTIKU_VFS_WATCH_MAX=16. D6 reserves 2 for the user and arms a `sub` per `fresh=cached` query-dependency node. D5 subscribes to /sys/vfs/cap_gen — one slot, unbudgeted, mentioned by nobody. On stock firmware (WATCH_MAX=8) that is 4+2+1 = 7 committed before the first query dependency is armed. FAR worse and confirmed in the source: every shell-side watch is keyed on the SAME process, and `rules_rearm()` calls `tiku_vfs_unwatch_all(&tiku_shell_process)` (tiku_shell_rules.c:239) on every rule add/delete/clear (:314, :341, :366). A user typing `on /dev/led0 ...` at the device shell drops EVERY Tracker subscription. D3 names this and mandates re-subscribe-every-pump. D6 does not: `tiku_trk_qdev_arm()` arms once at open and disarms at close, so a live query silently stops updating and does not even fall back to polling (D6's poll tier only covers `fresh=live`).

**Verdict.** NEW COMPONENT, owned by neither: `tiku_trk_watch_broker`. It holds the desired subscription set with priorities, re-asserts it idempotently on EVERY pump (D3's discipline, generalized to all clients), and reports /sys/watch/{used,free}. D3's monitor and D6's query runner become clients. Fixed priority: cap_gen (1 slot, pinned — it gates the correctness of every menu in the app) > displayed directory > visible writable/live rows > query dependencies. Keep D6's reserve of 2 for the user's own rules/watch. Any subscription that cannot be re-asserted DEMOTES TO POLL AND IS DISPLAYED — D3's 'N polled' and D6's `tiku_trk_qlive_t.pushed/polled` are the same instrument and must be one.

### C7. D2 (type) vs D5 (capabilities) — the firmware manifest format, edited twice with two version stories

**Problem.** Both decisions rewrite `manifest_line()` in kernel/vfs/tiku_vfs.c. D2 appends named tokens (`,scale=N`, `,f=hex`, `,f=secret`) to the meta field, widens `meta[48]->[72]`, and argues NO rev bump is needed because named tokens are forward-compatible. D5 changes the cap column from single-token to comma-joined multi-token via `tiku_vfs_cap_str()`, fixes the header row, and bumps TIKU_VFS_MANIFEST_REV 2->3. D5's change is positionally identical and SILENTLY REINTERPRETED by an old parser, so D2's no-bump argument does not cover it. Concrete bug neither noticed: multi-token caps overflow the existing, proven host buffer `tiku_desk_node_t.cap[8]` — "hw,sys,fs,net" is 13 chars. D2's meta widening is genuinely required, not cosmetic: "i32,celsius,live,periph,-40..125" is already 32 of 48, and ",scale=-3,f=secret" pushes it to 50.

**Verdict.** ONE manifest change, ONE rev bump to 3, covering both, in a single commit. Host side must widen BOTH: `TIKU_DESK_META_MAX 56->72` (D2 caught it) AND `cap[8]->cap[24]` (nobody caught it). Keep D5's header-row fix — the header names four columns while every line carries five, which is a real defect. D2's named-token argument still buys forward compatibility for FUTURE meta additions and should be kept as a rule; it just does not exempt this change.

### C8. D2 (type) vs D4 (volumes) — how a dynamic store is recognised

**Problem.** D2's classification rule 2 keys on `is_dir && has dynops` to produce `tiku-dir/store`, and rule 3 uses `dyn_child` to produce `tiku-file/*`. D4 uses a literal /data whitelist and flags it in its own risks ('a whitelist of one'). Neither noticed that THE MANIFEST DOES NOT EXPOSE DYNOPS: `manifest_line()` emits path/type/perms/meta/cap only, and `manifest_rec()` returns early on `node->children == NULL` with the comment 'leaf, or a dynamic dir: nothing static to descend'. A dynamic directory is indistinguishable from a childless static one. D2's rule 2 is therefore not implementable, /data classifies as `tiku-dir/folder`, `tiku-file/*` is unreachable, and every /data file falls to `tiku-node/opaque` — losing Kind text, icons, Open With and the mime_alias bridge for the ONLY device subtree with real files in it.

**Verdict.** FIRMWARE, one line, and it closes D4's stated risk at the same time: emit a distinct type character for a dynamic directory — `(n->type==TIKU_VFS_DIR) ? (n->dyn ? 'D' : 'd') : 'f'` (the field is `dyn`, not `dynops`). D2's predicate then becomes real and D4's literal /data whitelist is deleted in favour of it, so a future second store (FAT32 on eMMC, PSRAM-backed) is classified correctly instead of being silently read-only. Fold into the single manifest change above; it is part of the rev-3 bump.

### C9. D2 (type) vs D5 (capabilities) — double-click TOGGLE on an unknown node

**Problem.** D2 makes opening a `tiku-control/switch` write the inverted value immediately with no confirmation, arguing capability is the guard and citing Tracker running an executable on double-click. D5's rule is that prediction may be MORE permissive than truth, and its `TK_DENY_UNKNOWN` verdict (truncated manifest, ls-discovered nodes) deliberately leaves menu items ENABLED so the truncated tail stays usable. Composed: a node with no descriptor and no known capability is double-click-actuatable on a guess. D5's asymmetry is right for menu enablement and wrong for an unconfirmed hardware actuation.

**Verdict.** D5's verdict model wins; D2's TOGGLE is NARROWED, not removed. TOGGLE fires without confirmation only when the verdict is `TK_ALLOW` from a COMPLETE manifest (`facts.cap_known == 1`). Under `TK_DENY_UNKNOWN`, opening a switch opens the value EDITOR instead — the user still reaches the action in one more click, and a node of unknown provenance never actuates hardware from a double-click. D2's fidelity argument is preserved exactly where the facts are known and refused where they are guessed.

### C10. D3 (watch) vs D1 (state) — pose deletion vs sidecar records

**Problem.** D3 emits REMOVED for every node at session detach and calls `tk_ino_forget()` from DeletePose. D1's sidecar records are path-keyed and MUST be retained-and-inert for vanished paths, which is what makes reflashing back restore the arrangement. Nobody states that the state store is exempt from pose-teardown cleanup. The obvious implementation — 'clean up state when the pose goes away' — deletes the user's entire device arrangement on every unplug, and it will look correct in review.

**Verdict.** D1 WINS, and it must be written as an INVARIANT rather than left as a convention: `tiku_trk_state_store` has NO removal path reachable from DeletePose, unmount, or listing generation change. `trk_state_remove()` exists solely for the user-invoked 'forget arrangements for paths this device no longer has' action that D1's own risks call for. D3's `tk_ino_forget()`/`tk_ino_drop_backend()` operate on the interning table ONLY.

### C11. D4 (volumes) vs D3 (watch) vs D1 (state) — who declares a device unmounted, and when

**Problem.** Three timeouts for one event. D4: debounce through TK_VOLST_STALLED, up to 3 reconnects x 3 s ~ 9 s worst case, then post TK_VOL_UNMOUNTED. D3's PVN-043 differ: detection is 'bounded by the pump timeout (default 6 s)'. D1: flush on link loss or a failed `tiku_desk_session_reconnect()`. Under D3 the monitor posts DEVICE_UNMOUNTED at 6 s while D4's roster is still on reconnect attempt 2 — the window closes and the poses are dropped mid-recovery, which is exactly the destructive outcome D4's debounce exists to prevent.

**Verdict.** D4 OWNS THE LIFECYCLE; D3 consumes it. The roster is the only component that decides mounted/unmounted — D4 is also right to overrule PVN-008's suggested mapping, because a device cannot report its own unplug and mount is a HOST fact. D3's monitor never synthesizes DEVICE_MOUNTED/UNMOUNTED; it reports link failure UP to the roster and receives the verdict back. D1's flush hooks the roster's TK_VOL_UNMOUNTED, after the debounce — which works only because D1 chose host-side storage; had state lived on the device the flush would be impossible by then.

### C12. D3 (watch) vs D6 (queries) — two 'one true event vocabulary' claims

**Problem.** D3 defines `tk_fsevent_t` with 8 opcodes and declares `tk_poseview_fs_notification()` the single entry point preserving PVN-001. D6 claims the same (`ONE tiku_trk_notice_t fed to one pose-view entry point') but its API actually defines a SECOND enum, `tiku_trk_qnotice_t` with 7 values. ENTER/LEAVE/UPDATE duplicate CREATED/REMOVED/CHANGED; SCOPE_GONE duplicates DEVICE_UNMOUNTED. Two enums both asserting they are the one enum is how PVN-001 gets quietly broken.

**Verdict.** D3's `tiku_trk_fsevent_t` is the vocabulary INTO the view; D6's notices are the query runner's output, UPSTREAM of it. Pipeline: backend -> fsevent -> queue -> query runner evaluates the predicate -> fsevent {CREATED|REMOVED|CHANGED} into the query's pose view. Delete ENTER/LEAVE/UPDATE; map SCOPE_GONE -> TK_FS_DEVICE_UNMOUNTED. KEEP SCAN_BEGIN/SCAN_END (genuine progress signal, no D3 equivalent) and KEEP D3's distinct TK_FS_QUERY_UPDATE opcode, which is what lets PVN-017's 'a LEAVE never closes the window' discriminator survive.

### C13. D6 (queries) vs D1 (state) — writing to /data

**Problem.** D1 rejects device-side storage for UI state on three grounds (4 KB durable budget, NVM wear / Ambiq flush amplification, /data may not exist) and permits only an explicit, user-invoked, 4096-capped publish gated on `trk_dev_state_can_publish()`. D6 independently writes `/data/queries/<name>.query` plus a `queries/.index` sidecar on a user Save, carrying neither the cap nor the gate. Same storage, same wear budget, two policies.

**Verdict.** D6 is ALLOWED — it is user-invoked, so it respects D1's principle — but it routes through D1's `trk_dev_state_can_publish()` gate and D1's `TRK_PUBLISH_MAX` cap. Exactly one place in the app decides whether a UI feature may consume device storage. Not a deep conflict, but the policy must not fork; two independent 'it's fine, the user asked' paths is how the budget gets spent twice.

### C14. D5 (capabilities) vs D2 (type) — duplicate copies of the same manifest columns on the Model

**Problem.** D2 adds `tiku_trk_facts_t fFacts` to the Model, which already carries `readable`/`writable` bits and `req_cap`. D5 independently adds `unsigned perm`, `uint8_t req_cap`, `uint8_t cap_known` to the same struct. Two copies of the same three manifest columns, refreshed on DIFFERENT triggers (D2 on manifest reload, D5's cached verdicts on cap_gen). They will drift, and the failure is silent: a menu item enabled off a stale `req_cap` while the icon and Kind reflect the fresh one.

**Verdict.** `fFacts` is the SINGLE copy. D5's `perm`/`req_cap` are deleted and `tiku_trk_permit_of()` reads `m->fFacts`. `cap_known` moves INTO `fFacts` (it is a manifest-provenance fact, not a permission fact). D5's `permit_gen` + `cached[TK_ACT_COUNT]` stay — that is a genuine memo of a derived verdict, not a second copy of an input. Likewise D2's `fBackend` byte is deleted in favour of D5's `backend` vtable pointer, which is the thing that actually dispatches; D2's `strncmp(type,"tiku-",5)` type-registry seam is untouched because it keys on the type string, not the model.

## Gaps owned by no decision (must be assigned before coding them)

- NOBODY OWNS THE MANIFEST RELOAD, and it is a use-after-free. D2 depends on 'manifest reload' for re-classification, D5 for req_cap, D3 for listing generations, D6's mirror IS the index, D4 loads it at mount. But `tiku_desk_ns_load()` documents that it replaces any previous structure — every `tiku_desk_node_t*` held by a model, pose or facts struct is invalidated. Nobody names the trigger, the protocol, or what happens to open windows mid-reload. Required rule: models hold interned ids or owned path strings, NEVER `tiku_desk_node_t*`; D2's `tiku_trk_facts_t.path` is a `const char *` and must be owned or interned. Needs an explicit reload protocol with a generation counter before any of the five consumers is written.

- NOBODY OWNS THE TRUNCATED MANIFEST, and D6 is the one that breaks. D2 lists it as its top-severity risk ('this cluster depends on it more than any other'), D5 builds TK_DENY_UNKNOWN around it, D6 has an `index_partial` flag, D4 defers the firmware fix as 'optional (a)', D3 ignores it entirely. Since D6's index IS the manifest, a truncated manifest yields an INCOMPLETE query result presented as complete — the one place TikuOS was supposed to beat BFS. `index_partial` is the only honest instrument and it is not wired to any refusal. Either the paged-manifest firmware fix becomes a prerequisite for D6, or a partial index must refuse to answer a query rather than answer it wrongly.

- NOBODY OWNS CONCURRENCY OVER THE NON-REENTRANT SESSION. `tiku_desk_session` explicitly forbids re-entry from its notify callback. D4 introduces one worker thread per device and one request token protocol; but D3's monitor pump, D5's `refresh_caps()`, D6's `qdev_run()`, D1's `state_flush()` and D2's classifier reads ALL issue device I/O. Five callers, one non-reentrant session, one worker, no defined queue, no priority classes. D4 names it in risks ('a slow df or a large recv will stall an unrelated window') and does not resolve it. This must be a real component, not a convention.

- NOBODY OWNS THE VALUE RENDERER, and four decisions consume it. D2 owns `display_as` and `scale` (a FIXED value rendered without its exponent is a wrong number on screen). D3's differ HASHES 'the rendered value text' and its own risk notes that a truncated read must be treated as always-changed rather than hashed. D5's editor commits a string. D6's predicate evaluates `value` terms as typed. One shared function must render, report truncation, and expose both the typed and the rendered form — and it must exist before D3's differ, which is the earliest consumer.

- NOBODY OWNS UNDO ACROSS BACKENDS. D5's risk names it ('undo records must be written per item, or a partial failure leaves an un-undoable half-state') because D5's prune-and-report rule makes partial completion NORMAL rather than exceptional. D4 makes a /data move a write-then-unlink with no atomic rename. Tracker's undo model is per-operation. No decision says whether a device write is undoable at all, or how a half-completed mixed local+device operation unwinds.

- NOBODY RESOLVES WHERE DEVICE VOLUMES LIVE IN THE TREE. D4 has TK_NODE_ROOT = 'Disks' holding both kinds in one roster; D2 has a separate 'Devices' root reusing kRootNode plus a `tiku-dir/root` type. Two roots or one? If device volumes are peers of local volumes in a single Disks window (which is what D4's roster and D1's TRK_KEY_DESKTOP assume), D2's separate Devices root is wrong and its type entry is dead.

- D1's WIPE DETECTION READS A NODE THAT DOES NOT EXIST. D1 claims firmware 'None' and reads `/sys/persist/primed` and `/sys/boot/cold_boots`. `/sys/persist/primed` exists (tiku_vfs_tree_persist.c:78). `/sys/boot/cold_boots` DOES NOT — /sys/boot's children are reason, count, stage, rstiv, hang, clock/, mpu/. Either rebase on `/sys/boot/count` (exists) or add the node and stop claiming zero firmware.

- NAMING IS NOT UNIFIED: four prefixes across six decisions (`trk_` in D1, `tiku_trk_` in D2/D6, `tk_` in D3/D4/D5). The one file already written is `tiku_trk_task.h`, and CLAUDE.md mandates `tiku_<module>_<action>()`. Mechanical, but it is six headers of churn if deferred, and it interacts with the deduplication above — you cannot tell that D1's `trk_state_read` and D4's `tk_state_key` are the same feature while they are spelled differently.

## Tracker behavior these decisions put at risk

Each line names the FEATURES rows that break if the resolution is not
followed.  This is the list to re-read before any of these modules is
called done.

- Read-only tint, and New folder / Cut / Paste refusal on /sys, /dev, /proc (PVL-076, PVL-102, CW-031, PVK-044, FS-078) — LOST if D2's 'device dirs reuse kDirectoryNode' wins over D4's virtual-directory classification, because TargetVolumeIsReadOnly() short-circuits on IsVirtualDirectory() before it ever consults the volume. Revisit conflict 1.

- Live query results (Q-038) and every push-driven device row (PVN-005, PVN-006, PVN-011, PVN-026) — SILENTLY STOP the first time anyone touches a rule at the device shell, because rules_rearm() calls tiku_vfs_unwatch_all() on the shared shell process. D6 has no re-arm loop and no fallback for fresh=cached nodes. Revisit conflict 6.

- The user's entire device icon arrangement (PVL-029, MA-064, CW-004, PVL-103) — DELETED on unplug if pose-teardown cleanup is allowed to reach the state store, which is the obvious implementation and will pass review. Revisit conflict 10.

- Menu enablement correctness on every device node (CW-031, PVK-044, MA-016, FS-045) — WRONG whenever req_cap is cached in both D2's fFacts and D5's model fields and only one is refreshed; the failure is silent and directional (an item enabled off stale data). Revisit conflict 14.

- Kind column text, icon chain and Open With for all of /data (MA-028, IV-002, IV-056, AW-040..AW-048, DB-035) — DEGRADE to tiku-node/opaque because dynops is invisible in the manifest, so tiku-file/* and its mime_alias bridge to local applications are unreachable. This is the only device subtree with real files in it. Revisit conflict 8.

- The device root's icon, Kind string and window-state slot (CW-004, PVL-103, MA-064, CW-069) — WRONG because D2's unscoped path table types it as tiku-dir/root and leaves tiku-dir/device dead. Revisit conflict 4.

- Every device pose in every open window (PVN-001, PVN-049, PVL-088, PVL-090) — USE-AFTER-FREE on manifest reload if any model, pose or facts struct holds a tiku_desk_node_t*, which tiku_desk_ns_load() invalidates wholesale. Revisit gap 1.

- Query completeness and the 'TikuOS is better than BFS here' claim (Q-017, Q-021, Q-038) — a truncated manifest yields an incomplete result set PRESENTED AS COMPLETE; index_partial is set and wired to nothing. Revisit gap 2.

- Existing, proven, hardware-validated host code — tiku_desk_node_t.cap[8] overflows on the first multi-token capability string ('hw,sys,fs,net' = 13 chars). Revisit conflict 7.

- 'Double-click never actuates hardware on a guess' — D2's TOGGLE fires on nodes carrying D5's TK_DENY_UNKNOWN verdict, i.e. exactly the nodes whose capability metadata was lost to manifest truncation. Revisit conflict 9.

- Unmount consequences firing mid-recovery (PVN-043, PVS-057, FS-060, AW-077, AW-096) — D3's 6 s pump timeout closes windows and drops poses while D4's debounce is still on reconnect attempt 2, defeating the debounce's entire purpose. Revisit conflict 11.

- PVN-001's single-handler promise — broken by two coexisting event enums each claiming to be the one vocabulary. Revisit conflict 13.

## Build order

1. 0. Unify the prefix to `tiku_trk_` before a single header is written. Matches the one file that already exists (src/tiku_trk_task.h) and the project convention. Cheap now; six headers of churn later, and it currently hides the D1/D4 state-store duplication.

2. 1. ONE firmware commit, manifest rev 2->3, everything the host will parse: header row names all five columns; `'D'` type char for dynamic dirs (`n->dyn ? 'D' : 'd'`, one line — unblocks D2's store/file types and kills D4's /data whitelist); `meta[48]->[72]` plus D2's named `scale=` / `f=hex|secret` tokens; D5's multi-token cap column via `tiku_vfs_cap_str()`; `/sys/vfs/{epoch,nsgen,cap,cap_gen}` with TIKU_VFS_TREE_VFS_NCHILD 5->9 in ONE edit (the `_Static_assert` breaks if D3's and D5's nodes land separately); the 1-line `tiku_vfs_watch()` DIR fix (tiku_vfs.c:842 — the kernel already notifies parents at :221/:237 and nobody can subscribe); `vfs_cap_gen` bump + notify inside `tiku_vfs_caller_cap_set()`. Splitting this forks the version story and breaks the build.

3. 2. Host mirror catch-up in applications/desktop/src/tiku_desk_ns.{h,c}: `TIKU_DESK_META_MAX 56->72` AND `cap[8]->cap[24]` (the second is an overflow in proven code that no decision caught), parse `'D'`, parse multi-token caps, parse named meta tokens. Define and document the manifest-reload protocol here (gap 1) — generation counter, and the rule that no consumer may hold a `tiku_desk_node_t*` across a reload.

4. 3. `tiku_trk_ident` — the single devid function (conflict 2). Everything that persists anything keys on its output, so it precedes every store.

5. 4. `tiku_trk_state_store` — D1's AttributeStreamNode seam plus the three-slot key scheme, xattr / sidecar / memory implementations, and the no-removal-from-pose-teardown INVARIANT (conflict 10). D2's preferred-handler map, D4's window state and D6's query index all persist through it, so it cannot come later.

6. 5. `tiku_trk_devio` — the single prioritised request queue over the non-reentrant session (gap 3). Build it BEFORE its five clients; retrofitting it afterwards means rewriting five call sites and it is the component most likely to be skipped under time pressure.

7. 6. `tiku_trk_facts` + the shared value renderer (gap 4) + the merged kind enum + `tiku_trk_type_classify()` taking a VOLUME (conflicts 1, 3, 4, 8, 14). The Model's shape must be settled before permit, monitor or query compile against it. `fFacts` is the single copy of the manifest columns.

8. 7. `tiku_trk_permit` — D5's one predicate, six verdicts, predict-for-enablement / attempt-for-effect. Depends on facts + devio + the cap_gen node. Delete D3's TK_FIELD_CAP and TK_FIELD_DESC here (conflict 5) so the mistake never reaches the event vocabulary.

9. 8. `tiku_trk_watch_broker` — the new component neither D3 nor D6 owns (conflict 6): desired-set + priorities + re-assert on every pump + demote-to-poll + report used/free. It must exist before ANYTHING arms a subscription, including D5's cap_gen pin.

10. 9. `tiku_trk_fsevent` + `_fsq` + `_dirsnap` + `_ino` — D3's synthesis layer, as clients of the broker, consuming the shared renderer for value hashing (and treating a truncated read as always-changed).

11. 10. `tiku_trk_roster` — D4's volume lifecycle, sole authority on mount/unmount (conflict 11). D3's monitor reports link failure to it rather than synthesizing unmount; D1's state flush hooks its TK_VOL_UNMOUNTED.

12. 11. `tiku_trk_typedb` + Open With table + open actions — the rest of D2, with TOGGLE narrowed by the permit verdict (conflict 9) and the preferred-handler map persisted via step 4.

13. 12. `tiku_trk_pred` + the query runner — D6 LAST, because it consumes facts, permit, broker, fsevent and the state store. Its notices lower into `tiku_trk_fsevent_t` (conflict 13); its device-side save routes through D1's publish gate (conflict 12); it MUST adopt the broker's re-assert discipline or an unrelated `on` command silently kills it. Gate its ship on the truncated-manifest answer (gap 2) — a query that answers from a partial index is worse than one that refuses.


---

# The six decisions, as designed


## Where per-node UI state lives (pose info, view state, window state) for the device-namespace backend

**Decision.** ONE answer: **all UI state for device-namespace entries lives host-side, in a per-device sidecar file keyed by the device's die UID, written through the same attribute-name vocabulary Tracker already uses. The device stores nothing. No new firmware.**

Five parts.

**(1) The seam is Tracker's own AttributeStreamNode, ported to C.** Tracker already abstracts "where attributes come from" — `AttributeStreamFileNode` vs `AttributeStreamMemoryNode`, driven through `WindowStateNodeOpener`, `GetLayoutState/SetLayoutState`, and `SetupDefaultState`'s `NamesToAcceptAttrFilter`. That interface is the answer; do not invent a second one. Every caller (`ReadPoseInfo`, `SavePoseLocations`, `SaveState`, `RestoreState`, `RestoreColumnState`, `SetupDefaultState`) is written ONCE against `trk_state_store_t`. Two implementations behind it: `trk_store_xattr` (local, full fidelity) and `trk_store_sidecar` (device). That is how ~59 rows stay a single code path.

**(2) Node key.** The xattr backend ignores the key (the file IS the node). The sidecar backend keys on the device VFS path string, plus three reserved keys that reproduce Tracker's three-slot split (`MA-064`): `"\x01root"` for the device root (`_trk/d_*`, the analogue of `WindowStateNodeOpener` redirecting the Disks window to the Desktop directory), `"\x01desk"` for the Tiku Desktop, ordinary paths for everything else.

**(3) Sidecar location and lifetime.** `$XDG_DATA_HOME/tracker/devices/<devid>.state`, ONE file per device, plus a shared `devices/index`. Wiping, reflashing, losing or discarding a device touches exactly one file. Crucially, **the desktop arrangement of device icons is NOT in any device's sidecar** — it lives in the host Desktop's own store under `"\x01desk"`, so a wiped device cannot disturb where the user put every other device. And because `/sys/device/uid` is a die identifier that a wipe cannot change, even the wiped device's own arrangement survives its own wipe; we exceed the requirement rather than merely meeting it.

**(4) Device identity is a scored match, copied from `MatchArchivedVolume` (`Q-027`).** Probe `/sys/device/uid`, `/sys/device/id`, `/sys/device/mcu`, `/sys/device/name` — four existing `cat`s through `tiku_desk_ns_read()`. Weights uid=8, id=4, mcu=2, name=1, accept best >= 7 (so uid alone binds; without a uid, id+mcu+name = 7 binds). **A uid mismatch is an absolute veto**, whatever else scores — that is the one thing the die ID buys, and without it a renamed board would steal another board's arrangement. Parts with no unique ID get a `prov-` devid derived from FNV1a64(id|mcu|name) and the header's `provisional` flag set; the UI must label that binding, because two identical boards with the same name genuinely are ambiguous and we say so rather than guessing silently.

**(5) Two hosts: each keeps its own arrangement; neither can corrupt the other's.** This is not a concession, it is what the source already does. Tracker relocates state to the *viewer's* storage whenever the object cannot hold it — root and Trash pose info go to the Desktop directory (`PVL-029`, `CW-004`), and `AW-005` has Tracker embed window state centrally for any volume without writable attributes. A device namespace is permanently in the `AW-005` condition, so Tracker's own fallback becomes the default instead of the exception. Sharing is therefore explicit, never implicit: `trk_dev_state_export/import`, and — only when the manifest shows a writable dynamic `/data` — a user-invoked "Publish arrangement to device" / "Adopt arrangement from device" that moves the same sidecar bytes, capped at 4096 and refused above it. Never automatic, never on window close, never a write per drag: `/data` is optional (compiled only with the file store), its capacity is carved and shared with user data, and on Ambiq `.persistent` is SRAM mirrored to MRAM where a chatty writer causes flush amplification.

**Writing discipline.** Keep Tracker's dirty gate verbatim (`PVL-084`, `MA-071`, `TS-036`): `fStateNeedsSaving` OR a field-by-field `BViewState` diff against the shadow copy. The sidecar is only touched at `ClearPoses`/`SwitchDir`/window-close/link-loss, batched into one compaction per flush. Link loss and failed `tiku_desk_session_reconnect()` are the device analogue of unmount (`AW-096`): flush before tearing views down.

**Wipe detection with no firmware.** On rebind, read `/sys/persist/primed` and `/sys/boot/cold_boots`; a nonzero `primed` on an established binding means cells were re-primed (layout change or NVM corruption), so the host re-validates paths against the fresh manifest and warns instead of silently applying stale positions. Records for paths that vanished are RETAINED and inert, so reflashing back restores the arrangement.

**Rationale.** I considered four options and rejected three.

**Rejected — a /data file on the device.** It is the option that makes two hosts agree, and it is the one the fidelity contract itself argues against three separate times. `MA-058`'s TIKU note is explicit: "Note the 4 KB durable budget on the smallest targets — one blob per folder will not fit there." `TS-029`'s note: "Device-namespace directories may be read-only, so the store must tolerate 'cannot persist here'." `MA-071`/`TS-036` both flag NVM wear, and the Ambiq trap is that `.persistent` is physically SRAM mirrored to MRAM, so a per-scroll or per-drag write amplifies into flush traffic on a microwatt part. Worse, `/data` is not even guaranteed to exist — it is compiled only when the file store is present and its VFS presentation sits behind the shell gate. Storing the desktop's arrangement in a subsystem that may be absent, may be full of user data, and is wear-sensitive means the UI's convenience feature can eat the device's storage. It survives only as the explicit, capped, user-invoked publish path.

**Rejected — a new namespace convention** (e.g. `/sys/ui/<hash>` nodes, or an extra descriptor field carrying position). It needs firmware on every target, it puts host UI concerns inside the kernel's typed-descriptor model — which is deliberately "a value with a type", not a property bag — and it fails the same wear and budget tests as `/data` while also being an ABI the kernel then has to keep. `PVN-013`'s note offers "a per-node visibility bit on the typed descriptor" as an alternative; I decline it for the same reason. A node the firmware wants hidden should simply not be in the manifest; the descriptor should not learn about desktops.

**Rejected — hybrid with automatic sync** (host authoritative, mirrored to the device in the background). This is the worst of both: it inherits the wear and capacity problems, and it adds a conflict-resolution problem between two hosts that the design would then have to solve badly. Automatic mirroring of positions is exactly the per-drag write the contract warns about.

**Chosen — host-side, because the source already does this.** Tracker has a working, tested precedent for "the object cannot hold its own UI state": root and Trash pose info are written to the Desktop directory node, not to the volume (`PoseView.cpp:SavePoseLocations`, the `FSGetDeskDir` branch), and `TTracker::QuitRequested` embeds window state in the session message whenever `(deviceFlags & (B_FS_HAS_ATTR|B_FS_IS_READONLY)) != B_FS_HAS_ATTR`. So the honest equivalent for a device namespace is not an invention — it is promoting Tracker's own fallback to the default, which is precisely what `AW-005`'s TIKU note asks for. It also means the spatial feel survives at full strength (positions are real, per-entry, durable, restored before the pose is shown), it costs zero firmware, it cannot be defeated by a read-only or storage-less target, and per-device file partitioning makes the "wiped device must not lose other devices' arrangement" requirement structural rather than a rule someone has to remember.

One firmware addition was considered and rejected as redundant: a `/sys/device/epoch` monotonic counter bumped whenever a persist cell is primed (~40 lines in `tiku_vfs_tree_boot.c` plus a cell). `/sys/persist/primed` and `/sys/boot/cold_boots` already answer the same question with nodes that exist today.


| Tracker concept | Local files | TikuOS nodes |
|---|---|---|
| _trk/pinfo — per-entry icon position + fInvisible | user._trk/pinfo xattr, 24-byte BeOS PoseInfo layout, native name written and the _le/_be twin removed (matches AttributeStreamFileNode::Write) | sidecar record keyed by the node's VFS path, name "_trk/pinfo"; device stores nothing |
| fInitedDirectory — 'this position was recorded for THIS parent' | real ino_t of the parent directory, unchanged | FNV1a32 of the parent's VFS path, stored in the same field; 0 is the invalid sentinel replacing -1LL |
| _trk/columns + _trk/viewstate — per-folder column set, sort, mode, icon size, both scroll origins | xattrs on the folder node, BColumn v22 / BViewState v11 raw streams, unchanged | two sidecar records keyed by the folder's VFS path, same stream bytes, same version gates and same _Sanitize clamps |
| _trk/windframe, _trk/windwkspc, _trk/winddecor — window geometry | xattrs on the folder node, frame divided/multiplied by plain-font-size/12 | sidecar records keyed by the folder path; NodeHasSavedState(parent) becomes 'sidecar contains _trk/windframe for the parent key' |
| _trk/d_columns, _trk/d_viewstate, _trk/d_windframe, _trk/d_pinfo — the Disks/root slot redirected to the Desktop directory | xattrs on the Desktop directory node, unchanged (WindowStateNodeOpener redirect) | same names under the reserved key TRK_KEY_DEV_ROOT in the device's own sidecar |
| _trk/desk_viewstate + kAttrExtendedPoseInfo — the Desktop's own layout and per-screen-frame icon positions | xattrs on the Desktop directory, unchanged | host Desktop store under TRK_KEY_DESKTOP — NEVER inside any device sidecar, so where the user put device A's icon is untouched when device B is wiped |
| The foreign-endian twin (_trk/columns vs _trk/columns_le) and the read-native-then-swap fallback | kept in full: read native, fall back to the foreign name with byte swapping; write native and RemoveAttr the twin | gone. The sidecar has one documented byte order and a per-record CRC; a record whose gate, version or CRC does not match reads as 'no saved state' |
| Volume identity for reopening state after a remount (EmbedUniqueVolumeInfo / MatchArchivedVolume, 5/4/3/2/1 scored, accept >= 9) | unchanged — creationDate/capacity/deviceName/volumeName/fshName | trk_dev_ident_t scored uid=8 id=4 mcu=2 name=1, accept >= 7, with a uid mismatch as an absolute veto; probes are four cats on existing /sys/device/* nodes |
| Volumes without writable attributes: state embedded centrally, SetSaveStateEnabled(false) | unchanged — the rare case (read-only mounts, FAT) | the normal case, promoted from exception to default: the sidecar IS the central store |
| SavePoseLocations skipped on read-only / no-attr volumes; layout stays in memory, no error | unchanged | trk_store_memory_open() when the sidecar directory is unwritable (portable mode, read-only home); TRK_STORE_PERSISTENT clear, layout live for the session, dropped silently on close |
| Unmount saves every window's pose locations before the volume vanishes (SaveAllPoseLocations) | unchanged | link loss or a failed tiku_desk_session_reconnect() triggers trk_state_flush() on that device's sidecar before views are torn down |
| StateNeedsSaving dirty gate: fStateNeedsSaving OR a field-by-field BViewState diff against a shadow copy | unchanged | unchanged and mandatory — the sidecar is appended only at ClearPoses/SwitchDir/close/disconnect, batched into one compaction per flush |
| Wipe / re-enumeration detection (there is no Tracker equivalent) | n/a | on rebind, read /sys/persist/primed and /sys/boot/cold_boots; nonzero primed on an established binding means cells were re-primed, so paths are re-validated against a fresh manifest and the user warned |


**Firmware needed.** None. Every input the decision needs already exists as a node: /sys/device/{uid,id,mcu,name} for identity, /sys/vfs/manifest for structure, /sys/persist/primed and /sys/boot/cold_boots for wipe detection, and the optional publish path rides the existing /data dynamic directory with no kernel change (it is simply unavailable, and its menu items disabled, on targets without the file store). One addition was considered and rejected as redundant: a monotonic /sys/device/epoch counter bumped whenever a persist cell is primed, ~40 lines in kernel/vfs/tree/tiku_vfs_tree_boot.c plus one TIKU_PERSIST_CELL — /sys/persist/primed already answers the same question.

**Rows governed** (59): PVL-009, PVL-025, PVL-028, PVL-029, PVL-030, PVL-041, PVL-083, PVL-084, PVL-098, PVL-103, PVS-040, PVS-046, PVN-013, CW-001, CW-002, CW-003, CW-004, CW-005, CW-006, CW-007, CW-008, CW-009, CW-010, CW-057, CW-066, CW-067, CW-069, FS-031, FS-037, MA-058, MA-059, MA-060, MA-061, MA-062, MA-063, MA-064, MA-065, MA-066, MA-068, MA-069, MA-070, MA-071, IV-016, TS-029, TS-030, TS-031, TS-032, TS-033, TS-035, TS-036, Q-025, Q-041, Q-052, AW-005, AW-015, AW-020, AW-063, AW-064, AW-096


**Rows that will differ** (26) — these become `differs` in the contract:

- **PVL-029**: Local files: identical (kAttrPoseInfo xattr). Device nodes: the position is durable and per-entry as promised, but it is stored host-side in the device's sidecar keyed by (devid, VFS path), not on the object. Consequence the contract must record: a SECOND host connecting to the same device sees its own arrangement, not yours — the arrangement is a property of the viewer, not of the device. Sharing is possible only through the explicit export/import or the /data publish command.
- **PVL-030**: Local/Desktop: identical (ExtendedPoseInfo per screen frame). Device nodes INSIDE a device window are never desktop poses, so kAttrExtendedPoseInfo is never written for them and there is no per-resolution position for a device node. Device ICONS on the Tiku Desktop do keep the full per-frame behaviour, via the host Desktop store.
- **AW-020**: Same difference as PVL-030: the save-against-old-frame-then-relayout sequence runs for device icons on the Desktop, but not inside device windows, which have no per-screen-frame position concept.
- **PVL-028**: The fInitedDirectory inode check becomes an FNV1a32 parent-path check for device nodes (0 is the invalid sentinel in place of -1LL); the +/-50000 sanity bound is unchanged. The 11-attempt / 10 ms retry for items created within the last 5 seconds is DROPPED for device nodes — there is no create-then-write-attribute race because the sidecar record is written before the node is announced to the view. The retry is kept unchanged for local files.
- **PVL-025**: Local: identical (FSSetPoseLocation before the pose appears). Device: PlaceFolder's pre-seed writes the sidecar record first and only then issues the device-side create, so the ordering guarantee holds by construction rather than by racing the filesystem.
- **PVL-098**: Same as PVL-025 — the click-point / mouse-point position is pre-seeded into the sidecar rather than into an attribute. Behaviour identical to the user.
- **PVN-013**: Local: identical (fInvisible read from kAttrPoseInfo). Device: invisible comes from the sidecar pose record. There is deliberately NO per-node hidden bit on the typed descriptor — PVN-013's TIKU note offered that as an option and it is declined. A node the firmware wants hidden must not appear in /sys/vfs/manifest.
- **CW-057**: Same as PVN-013: folder menus hide device entries whose sidecar pose record says invisible. The 'union of every volume's trash' and 'a menu on the root lists volumes' clauses have no device analogue and are governed elsewhere.
- **CW-001**: Local: identical (kAttrWindowFrame on the folder's own node, font-scaled). Device: the frame is a sidecar record keyed by the folder's VFS path. A folder that disappears from the manifest keeps its record inert rather than having it garbage-collected, so reflashing the old firmware restores the window exactly.
- **CW-004**: Behaviourally identical, mechanically different: instead of switching to kAttrDisksFrame/_trk/d_windwkspc on the Desktop directory node, the device root uses the same attribute NAMES under the reserved key TRK_KEY_DEV_ROOT inside that device's sidecar.
- **PVL-103**: Same as CW-004 — the root/Disks column and view-state split is preserved exactly, as a reserved key prefix rather than a redirected node.
- **CW-069**: Preserved: NeedsDefaultStateSetup returns false for the device root, so it never inherits template state and falls back to the cascade rect plus its own TRK_KEY_DEV_ROOT frame record.
- **MA-064**: The three non-colliding slots (folder / Disks / Desktop) all survive, but for device nodes they are three key prefixes inside one host file instead of three attributes on nodes. Additionally, the Desktop slot is deliberately moved OUT of the device sidecar into the host Desktop store, so wiping a device cannot disturb the desktop arrangement of other devices.
- **MA-065**: The foreign-endian twin is removed for the device backend: the sidecar has one documented byte order and no _le/_be pair. The rejection discipline is kept in a stronger form — a record whose gate word, version or CRC32 does not match reads as 'no saved state' rather than being misread. Local files keep the full read-foreign-and-swap fallback.
- **TS-030**: Dropped for the device backend (single architecture, as the row's TIKU note requests). Kept for local files. Also correcting the row body: writes do NOT emit both names — AttributeStreamFileNode::Write writes the native name and RemoveAttr's the foreign one on success, which is what both backends do.
- **FS-031**: Local: identical (kSkipAttributes excludes kAttrPoseInfo from CopyAttributes). Device: there is no attribute stream to copy at all, so the sidecar pose record is deliberately not carried to the destination — the same net effect by a different route. What a device node's descriptor fields do on copy is a separate decision and is not governed here.
- **FS-037**: Local: identical. Device: SetupPoseLocation becomes a view-state copy — the source's sidecar pose record is consulted and a new record written under the destination key, subject to the same three conditions (loc == NULL, different parent, source has a valid record).
- **PVS-040**: In-folder repositioning is behaviourally identical; the flagged poses are flushed to the sidecar rather than to attributes at the next save point. List mode still does nothing.
- **PVS-046**: Command-drop grid snapping is identical; the 'remove the stale kAttrPoseInfoForeign attribute' step is a no-op on the device backend, which has no foreign twin.
- **MA-058**: Column archive format is byte-identical (v22, min v21, conditional trailing displayAs). The row's TIKU note about the 4 KB durable budget is resolved by not storing on the device at all; the only device-resident blob is the optional user-invoked publish file, hard-capped at TRK_PUBLISH_MAX = 4096 with a legible refusal above it.
- **MA-071**: Behaviourally identical, and the dirty gate is mandatory rather than an optimisation. The 'read-only volume keeps its layout in memory instead of erroring' path is realised as a third store implementation (trk_store_memory_open) used whenever the sidecar directory is unwritable.
- **AW-005**: Inverted rather than changed: the condition that makes Tracker embed state centrally is permanently true for device windows, so central storage is the default path for them and SetSaveStateEnabled(false) is never needed — the sidecar is a real persistent store, not a session-scoped message.
- **AW-096**: Unmount becomes disconnect: link loss or a failed reconnect flushes the device's sidecar before views are torn down. The 3-second bounded poll for closing active query windows has no device analogue and is not reproduced.
- **AW-015**: Default query-template state is written into local template nodes exactly as in the original; device namespaces contribute no templates and the kAttrViewState_be name in the row is written under the single native name.
- **AW-063**: File-panel state remains in the shared local settings template node (never in a device sidecar), even when the panel is browsing a device namespace — matching the original's 'one shared template across panels and applications'.
- **Q-025**: GetLayoutState/SetLayoutState work unchanged over the store abstraction. The row's own TIKU note is honoured separately: the 5000x1 ms spin on EntryHasWindowOpen becomes an event wait on the VFS watch notification.


**Risks.**

- Provisional identity is genuinely ambiguous. Two identical boards, same MCU, both still named "tiku", on a part whose tiku_common_unique_id() returns nothing, score 7 against each other and will swap arrangements. Mitigation is honesty, not cleverness: the binding is flagged provisional in the header and labelled in the UI, and the first fix offered is "give this device a name" (a write to /sys/device/name, which is a persist cell and survives). Do not silently pick the best match and hide the doubt.
- Two hosts diverging is a real fidelity loss, not a cosmetic one. In BeOS the arrangement is a property of the object and everyone who opens the folder sees the same thing. Here the device is the object and it holds nothing, so the spatial memory is per-viewer. The export/publish path narrows the gap but does not close it, and no automatic sync should be added later without re-opening the wear and conflict questions this decision rejected it on.
- Path-keyed records are fragile against firmware that renames nodes. Renaming /dev/led0 to /dev/led/0 orphans the record silently — the icon reappears auto-placed with no error. The manifest_hash field lets the host detect that the shape changed and offer a re-validate pass, but there is no rename detection and no attempt at one; a heuristic here would guess wrong and move the user's icons.
- The sidecar directory is per-user and per-machine. Moving to a new host, or running as a different user, loses every device arrangement with no warning unless the user exports first. This is the flip side of choosing host-side storage and should be surfaced in the UI at least once, not discovered.
- The optional /data publish path is the one place a UI feature can consume device storage. It must stay user-invoked, capped, and refuse rather than truncate. If it is ever wired to window-close or to a background sync, it reintroduces exactly the flush-amplification and capacity problems that ruled out device-side storage in the first place.
- Sidecar growth is unbounded by design, because records for vanished paths are retained so that reflashing restores the arrangement. A device whose namespace churns across many firmware builds accumulates dead records. Compaction only removes duplicates, not orphans; a byte ceiling with an explicit "forget arrangements for paths this device no longer has" action is needed before that becomes a real file-size problem.
- The local xattr on-disk PoseInfo layout must be the naturally-padded 24-byte BeOS form, not a packed struct. Getting this wrong is silent: positions authored by real Tracker on a shared BFS/Haiku volume are misread as nonsense, tripped by the +/-50000 sanity check, and the icons quietly auto-place. Verify against an actual BFS-authored _trk/pinfo, not against a round-trip through our own writer.


<details><summary>Proposed C</summary>

```c
/* ---- tiku_trk_state.h : the AttributeStreamNode seam, in C -------------- */

typedef struct trk_state_store trk_state_store_t;

#define TRK_STORE_WRITABLE   0x01u  /* writes can succeed                   */
#define TRK_STORE_PERSISTENT 0x02u  /* survives app exit                    */
#define TRK_STORE_SIDECAR    0x04u  /* keyed by node_key, not by the object */
#define TRK_STORE_FOREIGN_RD 0x08u  /* honours the foreign-endian twin      */

typedef struct trk_state_store_ops {
    ssize_t (*read)    (trk_state_store_t *st, const char *node_key,
                        const char *name, const char *foreign_name,
                        uint32_t type, void *buf, size_t len);
    ssize_t (*write)   (trk_state_store_t *st, const char *node_key,
                        const char *name, const char *foreign_name,
                        uint32_t type, const void *buf, size_t len);
    ssize_t (*contains)(trk_state_store_t *st, const char *node_key,
                        const char *name, uint32_t type);
    int     (*remove)  (trk_state_store_t *st, const char *node_key,
                        const char *name);
    int     (*flush)   (trk_state_store_t *st);
    void    (*close)   (trk_state_store_t *st);
} trk_state_store_ops_t;

struct trk_state_store {
    const trk_state_store_ops_t *ops;
    uint32_t flags;
    void    *impl;
};

/* Reserved node keys (device backend only) -- MA-064's three slots. */
#define TRK_KEY_DEV_ROOT  "\x01root"   /* _trk/d_columns, _trk/d_viewstate  */
#define TRK_KEY_DESKTOP   "\x01desk"   /* _trk/desk_viewstate               */

trk_state_store_t *trk_store_xattr_open(void);                 /* local     */
trk_state_store_t *trk_store_sidecar_open(const char *devid);  /* device    */
trk_state_store_t *trk_store_memory_open(void);   /* AW-005 no-persist case */

/* Convenience wrappers that all of PoseView/ContainerWindow call. */
ssize_t trk_state_read (trk_state_store_t *, const char *node_key,
                        const char *name, void *buf, size_t len);
ssize_t trk_state_write(trk_state_store_t *, const char *node_key,
                        const char *name, const void *buf, size_t len);

/* ---- pose info --------------------------------------------------------- */

#define TRK_SANE_POSE_LOCATION 50000.0f     /* kSanePoseLocation            */

/* In-memory form, shared by both backends. */
typedef struct {
    uint8_t  invisible;        /* PoseInfo::fInvisible                      */
    uint8_t  pad[3];
    uint64_t inited_parent;    /* local: ino_t.  device: FNV1a32(parent)    */
    float    x, y;             /* unscaled against 32 px (IV-016)           */
} trk_pose_info_t;

/* On-disk form for the LOCAL xattr backend ONLY: byte-identical to BeOS
 * PoseInfo (bool + 7 pad + ino_t + BPoint = 24 bytes, natural alignment).
 * Do not pack it -- a packed struct misreads BFS/Haiku-authored attributes. */
typedef struct {
    uint8_t fInvisible;
    uint8_t _pad[7];
    int64_t fInitedDirectory;
    float   x, y;
} trk_pose_info_disk_t;

#define TRK_INITED_INVALID  ((uint64_t)0)   /* device sentinel for -1LL      */

int  trk_pose_info_read (trk_state_store_t *, const char *node_key,
                         const char *parent_key, trk_pose_info_t *out);
int  trk_pose_info_write(trk_state_store_t *, const char *node_key,
                         const char *parent_key, const trk_pose_info_t *in);
uint32_t trk_path_hash(const char *path);           /* FNV-1a 32, never 0   */

/* ---- device identity : the MatchArchivedVolume analogue (Q-027) -------- */

typedef struct {
    char uid[64];        /* cat /sys/device/uid   -- "" when part has none  */
    char id[32];         /* cat /sys/device/id    -- "tiku-XXXX"           */
    char mcu[32];        /* cat /sys/device/mcu                            */
    char name[40];       /* cat /sys/device/name  -- persist cell, editable */
    char endpoint[128];  /* "/dev/ttyUSB0" or "10.0.0.7:2323"              */
} trk_dev_ident_t;

#define TRK_IDENT_W_UID   8
#define TRK_IDENT_W_ID    4
#define TRK_IDENT_W_MCU   2
#define TRK_IDENT_W_NAME  1
#define TRK_IDENT_ACCEPT  7
#define TRK_IDENT_VETO   (-1)   /* both uids present and different         */

int  trk_dev_ident_probe(tiku_desk_ns_t *ns, trk_dev_ident_t *out);
int  trk_dev_ident_score(const trk_dev_ident_t *probed,
                         const trk_sidecar_hdr_t *stored);
/* devid: "uid-<32 hex>" when uid is present, else "prov-<16 hex>". */
int  trk_dev_ident_key(const trk_dev_ident_t *id, char *buf, size_t max,
                       int *provisional_out);

/* ---- sidecar file format ----------------------------------------------- */

#define TRK_SIDECAR_MAGIC    0x54524B53u   /* 'TRKS'                        */
#define TRK_SIDECAR_REC_GATE 0x54524B52u   /* 'TRKR', written last          */
#define TRK_SIDECAR_VERSION  1u
#define TRK_SIDECAR_MAX_REC  10000u        /* mirrors Tracker's attr cap    */
#define TRK_PUBLISH_MAX      4096u         /* refuse to publish above this  */

typedef struct {
    uint32_t magic;          /* TRK_SIDECAR_MAGIC                          */
    uint32_t version;
    uint32_t record_count;
    uint32_t bytes_used;
    uint8_t  provisional;    /* identity binding is not uid-backed         */
    uint8_t  _pad[3];
    char     dev_uid[64];
    char     dev_id[32];
    char     dev_mcu[32];
    char     dev_name[40];   /* last seen, for display only                */
    uint64_t last_seen;
    uint32_t manifest_hash;  /* FNV1a32 of /sys/vfs/manifest at last save  */
    uint32_t persist_primed; /* /sys/persist/primed at last save           */
} trk_sidecar_hdr_t;

typedef struct {
    uint32_t rec_gate;       /* TRK_SIDECAR_REC_GATE, written last         */
    uint16_t key_len;
    uint16_t name_len;
    uint32_t type;           /* B_RAW_TYPE etc.                            */
    uint32_t data_len;
    uint32_t crc32;          /* over key + name + data                     */
    /* char key[key_len]; char name[name_l
```
</details>


## What replaces MIME types (42 rows) — type model for local files + device namespace nodes in one Model

**Decision.** ONE type-string namespace, TWO registries behind ONE resolver, disjoint by reserved prefix.

Model keeps exactly one type field (`char fType[TIKU_TRK_TYPE_LEN]`, the direct heir of `fMimeType`) plus one byte `fBackend` (fs | tiku). Every type string is `supertype/subtype`. A string beginning `"tiku-"` is answered by a compiled-in node-type table; every other string is answered by the platform MIME database. That single `strncmp(type, "tiku-", 5)` is the ONLY place in the codebase that knows two type systems exist. No caller branches on backend. Local files therefore keep 100% of Tracker's MIME behavior unchanged; device nodes get a real type string that flows through the identical code paths (icon cache keys, Kind column, supertype widening, Open-With ranking, per-type column sets, query constraints, most-used list).

SIX reserved device supertypes (chosen so the supertype rung the rows demand is genuinely meaningful, not a single "tiku/*" bucket):
  tiku-dir/*      root, device, folder, sys, dev, proc, store
  tiku-sensor/*   temperature, voltage, current, power, energy, light, raw, count, time  (fresh=live OR ecost>=periph)
  tiku-control/*  switch, level, enum, text, command                                     (node is writable)
  tiku-meter/*    counter, uptime, bytes, frequency, ratio, ident, state, value          (readable, not live)
  tiku-file/*     data, basic, text, csv, json, log                                      (/data dynamic-dir children)
  tiku-node/*     opaque, unknown                                                        (no descriptor / probed by ls)

`tiku-node/opaque` is the exact device analogue of `application/octet-stream`: it is the generic-icon rung, the superhandler token, and the never-blank default. `Model::MimeType()` is never empty after classify — the FinishSettingUpType invariant is preserved verbatim.

CLASSIFICATION (the device's `FinishSettingUpType`) is a pure function of manifest facts, first match wins:
  1. well-known path table  ("/"→dir/root, "/sys"→dir/sys, "/dev"→dir/dev, "/proc"→dir/proc, "/data"→dir/store) — this is IV-007's node-identity→resource mechanism transferred unchanged
  2. is_dir && has dynops   → tiku-dir/store ;  is_dir → tiku-dir/folder
  3. child of a dynops dir  → tiku-file/<ext table>          (extension only; never sniffed)
  4. desc == NULL           → readable ? tiku-node/opaque : tiku-node/unknown
  5. writable               → tiku-control/<switch|level|enum|text|command>   (command when !readable)
  6. fresh==live || ecost>=periph → tiku-sensor/<unit map>
  7. otherwise              → tiku-meter/<unit map>
Classification runs at manifest load and at manifest reload ONLY — never on a value notification. A value ringing does not re-type a node.

KIND (fBaseType) gains exactly ONE value: `kDeviceNode`, a value-bearing leaf. Device directories reuse `kDirectoryNode`, the device connection root reuses `kVolumeNode`, the "Devices" root reuses `kRootNode` — so IsDirectory()/IsContainer()/sort buckets/window choice all work with no new cases. `IsFile()` includes kDeviceNode; a new `IsDevice()` marks the places that must differ. Backend-ness is `fBackend`, orthogonal to kind, because "local vs device" is not a kind — it is who answers the read.

OPENING A DEVICE NODE is never nothing. The registry carries a per-type `open_action`:
  tiku-dir/*            → CONTAINER   (a window, exactly like a folder)
  tiku-control/switch   → TOGGLE      (writes the inverted value now — opening a thing does the thing, same contract as double-clicking an executable)
  tiku-control/level|enum|text|command → EDIT (in-place value editor, reusing BTextWidget's edit machinery and MA-019's "write refused → 0 bytes → alert" contract)
  tiku-sensor/*         → PLOT        (subscribe via `sub add`, tick-poll fallback, rolling window)
  tiku-meter/*          → INSPECT     (Get Info: value + full descriptor)
  tiku-file/*           → TEXT        (cat into a viewer; editable when writable)
  tiku-node/*           → INSPECT
The capability bits are the guard, not a special case: an open whose required cap is not held is refused with the device's own status text in an alert, in the shape of AW-039's launch-failure diagnosis.

OPEN WITH SURVIVES, unranked-list-and-all. Handlers become compiled-in entries (Plot, Value editor, Text viewer, Get Info, Hex/raw, Container) each declaring a `supports` string. `MatchMimeTypeString`/`SupportsMimeType` (MA-017) run UNCHANGED over that string: no '/' → supertype match → kModelSupportsSupertype; equal → kModelSupportsType; `tiku-node/opaque` → superhandler. The relation column, the sort order, the separators, the "make preferred" button state, the confirmation alert for kNoRelation — all of AW-040..AW-048 close on the same code, over a 6-entry table instead of a query over app signatures. Preferred handler is a HOST-side persisted map: per-node key (device-id, path) then per-type key; nothing is ever written to the device.

/data FILES get types by extension against a compiled table, and each tiku-file/* entry declares `mime_alias` (e.g. tiku-file/csv → "text/csv"). That alias is the bridge between the two registries: it is what makes a /data file dragged to a local folder type-check, and what lets local applications appear in its Open With list.

Kind-column text, icons, and per-type columns all resolve through the same two-registry ladder with Tracker's own fallback order: exact type → supertype → generic → hard-coded. The Kind column's control flow (MA-028) is byte-for-byte Tracker's; only the registry behind `short_description` changes.

**Rationale.** The alternative I rejected is the obvious one: give Model two parallel type fields (a MIME string for files, a `tiku_vfs_desc_t` struct for nodes) and branch at every use site. That loses on the rows themselves. Forty-two rows are written against a STRING WITH A SLASH: Q-004 widens a supertype by appending "/*", IV-002's chain has an explicit supertype rung, MA-017 ranks by "handler type with no '/' is a supertype pattern", IV-056 falls back to `GetSupertype()->GetIcon`, Q-015 builds supertype submenus, CW-026 promotes empty supertype menus. A struct has no supertype. Keeping the string keeps those rows as PORTS rather than rewrites — every one of them closes as `done` instead of `differs`, which is the whole point of the contract.

Reserved-prefix disjointness rather than a domain tag byte: a tag byte forces `if (domain == NODE)` at ~20 call sites, and every one of those is a place a future contributor forgets. A prefix means the resolver is a strncmp in one function; wrong answers become impossible rather than merely unlikely, because the IANA supertype space and `tiku-*` cannot collide.

Six supertypes rather than one (`tiku/*`) because IV-002's TIKU note explicitly demands the supertype rung survive, and a single bucket makes it a synonym for the generic rung. Splitting on the axis the descriptor already encodes — writable vs live-read vs cheap-read — is free: it needs no new firmware field, and it produces the three groupings a user actually wants to select ("all sensors", "all controls") in the Find panel and the Attributes menu.

Deriving the class from (writable, fresh, ecost, unit, dynops, path) rather than adding a `class=` field to the manifest respects rule 4: everything needed is already in the five manifest columns that `tiku_desk_ns` already parses and three boards already answer. The only firmware I ask for is `scale` and `flags`, and I ask for those because DF_SECRET is a safety property (a page that renders a secret node's value is a real leak) and `scale` is a correctness property (a FIXED value rendered without its exponent is simply a wrong number on screen) — neither is derivable.

Making the switch's open action TOGGLE rather than "open an inspector" is the fidelity-preserving choice, not the reckless one. Tracker's contract is that opening a thing does the thing; double-clicking an executable runs it, with no confirmation. A device switch is the closest honest equivalent, and the existing capability check already provides the refusal path that a POSIX permission bit provides for the executable. Inventing a confirmation dialog here would be inventing behavior the source does not have.

Keeping Open With rather than collapsing it (which CW-032's note offers as an out) because the rows are not really about a type→app registry — AW-042's ranked relation column, AW-043's "preselect only when all documents agree", AW-045's live button state and AW-046's kNoRelation warning are UI behaviors that work perfectly over a six-entry table and would otherwise be 6 rows silently dropped. The table is small; the behavior is not.

Adding exactly ONE kind (kDeviceNode) rather than the family a device namespace tempts you into (kSensorNode, kActuatorNode, kMountNode) because MA-001's kinds exist to decide four things — icon, sort bucket, window, renameability — and for device nodes all four come from the type string and the capability bits. More kinds would duplicate the type table in a second, coarser vocabulary that then drifts.


| Tracker concept | Local files | TikuOS nodes |
|---|---|---|
| MIME type string (Model::fMimeType) | unchanged — real MIME string from BNodeInfo/xattr/shared-mime-info | synthetic string `tiku-<supertype>/<subtype>` from tiku_trk_type_classify() |
| Supertype (widening, fallback rung) | text/*, image/*, application/* … | tiku-dir, tiku-sensor, tiku-control, tiku-meter, tiku-file, tiku-node |
| MIME database (short+long description, icon, attrs) | platform MIME db (shared-mime-info) | compiled-in const table `tiku_trk_typedb[]` in rodata |
| Generic type `application/octet-stream` | unchanged | `tiku-node/opaque` |
| Preferred app, per node (BEOS:PREF_APP) | unchanged — attribute on the file | host-side preference map keyed by (device-id, path) |
| Preferred app, per type (default application) | unchanged — MIME db default | host-side preference map keyed by type string |
| Open With candidate set + ranking (SupportsMimeType, MatchMimeTypeString) | unchanged — query over app signatures, full ranking | compiled `tiku_trk_handler[]` (Plot, Value editor, Text viewer, Hex, Get Info, Container) with a `supports` string |
| Launch / open (TTracker::OpenRef) | unchanged — container window, Find window, or launch the preferred app | per-type `open_action`: CONTAINER / TOGGLE / EDIT / PLOT / INSPECT / TEXT |
| Type icon + 7-rung fallback chain | all 7 rungs live | rungs 3 (type icon), 5 (supertype icon), 6 (generic), 7 (linked-in R_FileIcon) live; rungs 1,2,4 structurally absent |
| Node-supplied icon / thumbnail (BEOS:ICON, Media:Thumbnail) | unchanged | none; ShouldGenerateThumbnail() false for every `tiku-*` type |
| Per-type viewable attributes (BMimeType::GetAttrInfo → column submenus) | unchanged — MIME db attr records | `attrs[]` in the type table: Value, Unit, Range, Fresh, Cost, Cap, Path |
| Value rendering hint (display_as) | unchanged — "checkbox", "duration", "rating" | BOOL descriptor → "checkbox"; FIXED → scaled decimal via desc.scale; DF_HEX → hex; DF_SECRET → masked |
| Query by type (FindPanel::PushMimeType) | unchanged — BEOS:TYPE B_EQ with "/*" widening | prefix compare over the host mirror: no '/' → match `sel` + '/' |
| Most-used types list (persistent, decaying) | unchanged | same list, same file — `tiku-*` strings persist like any other |
| mimeset / sniff-if-generic (update_mime_info, GuessMimeType) | unchanged — sniff, persist, retry launch | never sniffed; /data children typed by a compiled extension table |
| Model kind (fBaseType, 11 values) | unchanged 11 values from st_mode + BEOS:TYPE | +1 value `kDeviceNode`; device dirs reuse kDirectoryNode, device root reuses kVolumeNode |
| Drop-target type check (CanHandleDrops / IsDropTargetForList) | unchanged — dir takes anything, app takes declared types | tiku-dir/store → kCanHandle; tiku-control/* → kNeedToCheckType (text payload only); else kCannotHandle |
| Kind column text (KindAttributeText::ReadValue) | unchanged — short description → raw string → "Unknown" | type table short_desc → supertype short_desc → raw string → "Unknown" |
| Type-change notification (RefreshMimeTypeList on kAttrMIMEType) | unchanged — inotify attr change on BEOS:TYPE | manifest reload only; a value notification never re-types |


**Firmware needed.** Yes, but small and genuinely required: ~25 lines total in TikuOS.

kernel/vfs/tiku_vfs.c, manifest_line() (~15 lines): after the existing four positional meta fields and the optional `lo..hi`, append two NAMED tokens — `,scale=%d` when desc.scale != 0, and `,f=hex` / `,f=secret` for DF_HEX / DF_SECRET. Widen the local `char meta[48]` to `meta[72]`.

kernel/vfs/tiku_vfs.c, tiku_vfs_desc_str() (~8 lines): mirror the same two tokens so `cat`-ing a single descriptor and reading the manifest agree.

Host-side (not firmware): TIKU_DESK_META_MAX 56 -> 72 in applications/desktop/src/tiku_desk_ns.h.

Why it cannot be derived: DF_SECRET is a safety property — without it the Tracker will render a secret node's value in the Value column and in Get Info, which is a real disclosure, and no combination of vtype/unit/fresh/cost implies it. desc.scale is a correctness property — a FIXED value shown without its exponent is a wrong number on screen (a 23.5 C reading displayed as 235), and scale is likewise underivable. DF_HEX is cosmetic and rides along for free.

Named rather than positional tokens specifically so that firmware predating this change parses cleanly: tiku_trk_meta_parse() treats a missing scale as 0 and missing flags as none, so the three already-proven boards keep working with no reflash and simply never show a masked or scaled value.

Everything else in this decision — the six supertypes, the classifier, icons, Kind text, open actions, Open With, per-type columns, query-by-type — is derived entirely from the five manifest columns that firmware already emits and that tiku_desk_ns.c already parses. No new node, no new shell verb, no new event.

**Rows governed** (65): MA-001, MA-002, MA-003, MA-012, MA-014, MA-016, MA-017, MA-018, MA-022, MA-024, MA-028, MA-080, MA-081, IV-001, IV-002, IV-003, IV-004, IV-005, IV-007, IV-008, IV-010, IV-011, IV-028, IV-037, IV-038, IV-039, IV-056, PVL-034, PVL-075, PVL-080, PVL-095, PVS-035, PVS-044, PVS-052, PVN-035, CW-025, CW-026, CW-032, CW-038, TS-014, TS-025, Q-004, Q-007, Q-012, Q-013, Q-015, Q-016, AW-013, AW-030, AW-035, AW-037, AW-040, AW-041, AW-042, AW-043, AW-044, AW-045, AW-046, AW-047, AW-048 …


**Rows that will differ** (28) — these become `differs` in the contract:

- **MA-002**: For device nodes there is no BEOS:TYPE re-read and no query / query-template / virtual-directory promotion — those device types do not exist. Classification comes from the manifest descriptor at load time. The preferred application is NOT read from the node; it is looked up in a host-side map keyed by (device-id, path) then by type. The thumbnail-driven `fIconFrom = kNode` branch never fires. Local files: unchanged.
- **MA-003**: Defaults for device kinds are `tiku-*` strings from the compiled table (tiku-dir/folder, tiku-dir/root, tiku-node/opaque) rather than BeOS metamime strings. The observable invariant — the Kind column is never blank — is preserved exactly.
- **MA-012**: TIKU_EVENT_VFS carries no attribute name, so every device notification takes the attrName==NULL full-re-read path. Additionally, classification is deliberately NOT re-run on a value notification: a type change on a device is picked up at manifest-reload latency, not immediately. Tracker re-types on the very next attr event.
- **MA-014**: "Permissions changed -> reclassify and redraw" holds, but capability bits only arrive with a manifest reload, so the reclassify fires at reload rather than on a per-node stat event. There is no device analogue of the executable bit flipping a file into an app.
- **MA-016**: CanHandleDrops for a device node is a capability test, not a supported-types test: tiku-dir/store -> kCanHandle; tiku-control/* with the required cap held -> kNeedToCheckType (accepts a text payload only); everything else -> kCannotHandle. A read-only device DIRECTORY returns kCannotHandle where a POSIX directory would return kCanHandle — the "directories take anything" rule does not hold on the device side.
- **MA-017**: The four-level ranking (kDoesNotSupportType < kSuperhandlerModel < kModelSupportsSupertype < kModelSupportsType) is preserved verbatim, but IsSuperHandlerSignature() now matches `application/octet-stream` OR `tiku-node/opaque`. A handler declaring one does not become a superhandler for the other registry's types.
- **MA-018**: Device nodes are never sniffed and no guessed type is ever written back (there is nowhere to write it). /data children are typed by a compiled extension table only, so a file with a wrong or missing extension keeps type tiku-file/data indefinitely — Tracker would have sniffed the content and got it right. Local files keep KnowsMime()/GuessMimeType()/Mimeset() unchanged.
- **IV-001**: The node-supplied-icon rung is structurally absent for device nodes: a VFS node has no icon attribute and no thumbnail. GetNodeIcon() is never called for a `tiku-*` type.
- **IV-005**: The two attribute probes are replaced by a compile-time answer: classify pre-sets fIconFrom = kUnknownNotFromNode for every device node. The hint is always "no own icon", so it is cheaper than Tracker's but also never says yes.
- **IV-002**: For device nodes the 7-rung chain loses rungs 1 (node icon), 2 (preferred app named by the node) and 4 (icon supplied by the type's preferred app) — there are no app-supplied per-type icons on TikuOS. Rungs 3 (type icon), 5 (supertype icon), 6 (generic = tiku-node/opaque) and 7 (linked-in R_FileIcon) all fire, as the row's TIKU note requires. Local files keep all 7.
- **IV-028**: ShouldGenerateThumbnail() returns false for every `tiku-*` type; a device node never shows a thumbnail and never takes the kNode icon path. Local image files are unaffected by this decision.
- **IV-037**: The up-front "this file already carries a thumbnail" determination is always false for device nodes; the kNode path is never pre-selected.
- **AW-013**: Nothing is installed at first run for device types: the node-type table is const, compiled in, and not user-editable, so the "install once, don't clobber user edits" rule is vacuous for `tiku-*` types. It still governs the local MIME side unchanged.
- **AW-030**: A device node declares no preferred handler, so "container-ness can be overridden by a declared handler" can only be overridden by the user's own host-side preference. A device directory always opens as a container unless the user has explicitly set a handler for it.
- **AW-037**: The mimeset-and-retry-once rung is removed for device nodes: an open that fails is reported once with the device's own status text (EPERM/EACCES/ERANGE). There is no re-classify and no second attempt. Local files keep SniffIfGeneric and the two-pass loop.
- **AW-040**: The candidate set is the compiled handler table filtered by relation, not a query over kAttrAppSignature. The "if nothing but superhandlers was found, widen to every application on the system" fallback widens to the ~6 registered viewers, so the widened and unwidened lists are nearly the same size.
- **AW-041**: The filters have nothing to filter on the device side: there are no non-executables, no apps in Trash, no second copy of Tracker, no B_ARGV_ONLY and no empty signatures among compiled handlers. Only the `background` bit is honoured. The filter is retained (and still fully applied for local applications) but is a no-op for `tiku-*` types.
- **AW-042**: The Relation column and its sort order are preserved, but kPreferredForFile now means "preferred for this (device-id, path)" from host state rather than from a per-file attribute; and the "Handles any file" wording becomes "Handles any node" when the superhandler token is tiku-node/opaque.
- **AW-044**: "Open and make preferred" writes only the host-side preference map — nothing is written to the device, and BMimeType::SetAppHint has no analogue and is dropped for `tiku-*` types. The per-type loop over distinct document types is otherwise unchanged.
- **AW-071**: The Get Info "Opens with:" pop-up is built from the handler table rather than BMimeType::GetSupportingApps, and choosing an entry writes the host preference map instead of BNodeInfo::SetPreferredApp. It is offered for device FILE nodes and suppressed for device directories (matching Tracker's suppression for directories/volumes).
- **MA-081**: OpenWithRelationAttributeText reports relations against handler-table entries; the descriptions read "Handles any node" for a tiku-node/opaque superhandler and "Handles any <supertype>" for e.g. tiku-sensor.
- **MA-080**: IsDropTargetForList over a device node tests the dragged payload against what the node accepts (plain text for tiku-control/*, anything for tiku-dir/store) rather than against an application's declared supported-types list. IsSuperHandler is meaningless for device nodes and returns false.
- **PVS-044**: Dropping file refs on a device node never launches anything. A drop on tiku-control/* writes the dragged plain-text payload (the existing clipping path); a drop of file refs on any device node is refused with the capability/read-only alert. Holding Control still forces the text path but cannot force refs through, so the "Control bypasses the type check" escape hatch is narrower on the device side.
- **PVS-052**: Spring-loaded menu entries over a device directory enable by capability (node writable AND the required cap held) rather than by Model::SupportsMimeType. Device containers are NOT unconditionally enabled the way directories and volumes are — a read-only device directory is greyed.
- **Q-004**: The device type constraint is evaluated in the host mirror by supertype prefix compare rather than as a B_EQ on an indexed BEOS:TYPE attribute. Result sets are identical, but a device-scoped query cannot run while the device is disconnected: it is refused rather than answered from the stale mirror.
- **Q-015**: The type menu shows two families of supertypes — the installed MIME supertypes and the six `tiku-*` ones. The `tiku-*` family is omitted entirely when no device is connected, which the existing "empty supertypes are removed" rule handles without new code.
- **DB-035**: Deskbar's AppCanHandleTypes has no device analogue: a device node dragged onto a running application is matched by its `mime_alias` if it has one, and refused if it does not. There is no equivalent of an application declaring it handles `tiku-sensor/*`.
- **DB-018**: A device entry's icon comes from the node-type table rather than from BAppFileInfo::GetIcon, so the "application's own icon" rung never fires; the generic and transparent-fill fallbacks are unchanged.


**Risks.**

- Double-clicking a tiku-control/switch actuates hardware with no confirmation. This is deliberate fidelity — Tracker runs an executable on double-click with no confirmation either — and the capability check is the guard. But a mis-aimed double-click in a /dev listing will toggle a relay. Mitigation available without inventing behavior: nodes whose req_cap includes HW are exactly the ones the device already refuses to untrusted channels, so the Tracker should run its device sessions at the lowest cap that still works and let the device say no.
- The six-supertype split keys on `fresh` and `ecost`, which are per-node author choices in firmware. A sensor whose descriptor was written with fresh=cached (an easy mistake — several existing nodes may have it) classifies as tiku-meter and gets INSPECT instead of PLOT. The failure is soft and visible, but it means the Tracker's type quality is bounded by descriptor hygiene across the whole namespace. Worth an audit pass over existing tree/*.c descriptors before N2.
- Untyped nodes (desc == NULL) are still the majority of the tree today, and they all land on tiku-node/opaque: one icon, one Kind string ("Node"), INSPECT for everything. A first screenshot of /sys will look far more uniform than a folder of local files does. The fix is descriptor coverage in firmware, not more Tracker cleverness, and the decision should not be judged on how a mostly-untyped namespace renders.
- Classification is refreshed only on manifest reload, so a firmware that changes a node's type or capability at runtime shows a stale Kind and a stale icon until the next reload. TikuOS's static node table makes this rare, but the dynamic /data store and any future runtime-attached subtree can hit it. The mitigation is that the reload is cheap (one `cat`) and can be re-run on a watch event for the namespace root's children.
- The compiled node-type table is a second registry the project now maintains by hand. It will drift from the descriptor vocabulary in kernel/vfs/tiku_vfs.h (units especially — a new unit added to vfs_unit_names lands on the tiku-meter/value default silently). A _Static_assert cannot span the two repos; the honest guard is a small test that parses the firmware's vfs_unit_names table and fails when the Tracker's unit map lacks an entry.
- The manifest is truncated by the device's shell read buffer on any sizeable tree (tiku_desk_ns already handles this by ls-walking the tail). Nodes recovered that way carry no descriptor and no capability, so they all classify as tiku-node/opaque or tiku-node/unknown — meaning on a big namespace the type quality degrades exactly where there is most to see. Paging the manifest is a separate decision, but this cluster depends on it more than any other.


<details><summary>Proposed C</summary>

```c
/* =====================================================================
 * applications/tracker-new/src/model/tiku_trk_type.h
 * One type-string namespace, two registries, one resolver.
 * ===================================================================== */

#define TIKU_TRK_TYPE_LEN   64
#define TIKU_TRK_TYPE_PFX   "tiku-"          /* the ONLY disjointness rule */

/* Reserved device types.  Every one is supertype/subtype so Tracker's
 * widening, ranking and icon-fallback code runs unchanged. */
#define TIKU_TRK_T_DIR_ROOT     "tiku-dir/root"
#define TIKU_TRK_T_DIR_DEVICE   "tiku-dir/device"
#define TIKU_TRK_T_DIR_FOLDER   "tiku-dir/folder"
#define TIKU_TRK_T_DIR_SYS      "tiku-dir/sys"
#define TIKU_TRK_T_DIR_DEV      "tiku-dir/dev"
#define TIKU_TRK_T_DIR_PROC     "tiku-dir/proc"
#define TIKU_TRK_T_DIR_STORE    "tiku-dir/store"     /* dynops dir (/data) */
#define TIKU_TRK_T_NODE_OPAQUE  "tiku-node/opaque"   /* == octet-stream    */
#define TIKU_TRK_T_NODE_UNKNOWN "tiku-node/unknown"
/* sensor/*: temperature voltage current power energy light raw count time
 * control/*: switch level enum text command
 * meter/*:  counter uptime bytes frequency ratio ident state value
 * file/*:   data basic text csv json log                                  */

typedef enum { TIKU_TRK_SRC_NONE, TIKU_TRK_SRC_MIME, TIKU_TRK_SRC_NODE }
        tiku_trk_typesrc_t;

/* THE SEAM.  Every type-driven operation calls this first. */
static inline tiku_trk_typesrc_t
tiku_trk_type_source(const char *type)
{
    if (type == NULL || *type == '\0')            return TIKU_TRK_SRC_NONE;
    if (strncmp(type, TIKU_TRK_TYPE_PFX, 5) == 0) return TIKU_TRK_SRC_NODE;
    return TIKU_TRK_SRC_MIME;
}

/* Supertype extraction, shared by both registries (Tracker's "/*" rule). */
int  tiku_trk_type_supertype(const char *type, char *out, size_t max);
/* Widening test: `sel` may be a bare supertype ("tiku-sensor") or a full
 * type.  This is FindPanel::PushMimeType's constraint, evaluated locally. */
int  tiku_trk_type_matches(const char *node_type, const char *sel);

/* --------------------------------------------------------------------
 * Facts a device node presents — parsed from ONE manifest line.
 * ------------------------------------------------------------------ */
typedef struct {
    const char *path;
    unsigned    is_dir     : 1;
    unsigned    readable   : 1;
    unsigned    writable   : 1;
    unsigned    dyn_dir    : 1;   /* directory with dynops (a store)     */
    unsigned    dyn_child  : 1;   /* discovered under a dynops dir       */
    unsigned    has_desc   : 1;
    unsigned    has_range  : 1;
    unsigned    hex        : 1;   /* DF_HEX                              */
    unsigned    secret     : 1;   /* DF_SECRET                           */
    uint8_t     vtype;            /* tiku_vfs_vtype_t                    */
    uint8_t     unit;             /* tiku_vfs_unit_t                     */
    uint8_t     fresh;            /* tiku_vfs_fresh_t                    */
    uint8_t     ecost;            /* tiku_vfs_ecost_t                    */
    uint8_t     req_cap;          /* tiku_vfs_cap_t                      */
    int16_t     scale;            /* decimal exponent for FIXED          */
    int32_t     vmin, vmax;
} tiku_trk_facts_t;

/* Parse the manifest's packed meta field:
 *   "-"  |  "vtype,unit,fresh,cost[,lo..hi][,scale=N][,f=hex|secret|…]"
 * Tokens are NAMED after the 4 positional ones, so firmware that predates
 * scale/flags parses cleanly (scale=0, flags=0).  Returns 0 / -1. */
int tiku_trk_meta_parse(const char *meta, tiku_trk_facts_t *out);

/* Build facts from a mirrored node (tiku_desk_node_t + parent context). */
int tiku_trk_facts_from_node(const tiku_desk_ns_t *ns,
                             const tiku_desk_node_t *n,
                             tiku_trk_facts_t *out);

/* THE DEVICE'S FinishSettingUpType.  Pure; never touches the link.
 * Writes a NUL-terminated `tiku-*` string; never fails, never empty. */
void tiku_trk_type_classify(const tiku_trk_facts_t *f,
                            char out[TIKU_TRK_TYPE_LEN]);

/* --------------------------------------------------------------------
 * The node-type registry — the MIME database's opposite number.
 * All const, all rodata, no install step (cf. AW-013).
 * ------------------------------------------------------------------ */
typedef enum {
    TIKU_TRK_OPEN_CONTAINER = 0, /* open a window on it                  */
    TIKU_TRK_OPEN_INSPECT,       /* Get Info / descriptor view           */
    TIKU_TRK_OPEN_EDIT,          /* in-place value editor -> vfs write   */
    TIKU_TRK_OPEN_TOGGLE,        /* bool: write the inverted value now   */
    TIKU_TRK_OPEN_PLOT,          /* subscribe + rolling time series      */
    TIKU_TRK_OPEN_TEXT           /* text/blob viewer                     */
} tiku_trk_open_t;

typedef struct {                 /* mirrors a MIME db attr record        */
    const char *name;            /* "tiku:value", "tiku:unit", …         */
    const char *public_name;     /* "Value", "Unit", "Range", "Cost"     */
    uint32_t    attr_type;       /* B_STRING_TYPE / B_INT32_TYPE / …     */
    uint16_t    width;
    uint8_t     align;
    unsigned    editable : 1;
    unsigned    viewable : 1;    /* TS-025's "viewable" flag             */
    const char *display_as;      /* "checkbox" for BOOL, else NULL       */
} tiku_trk_attr_t;

typedef struct {
    const char             *type;        /* "tiku-sensor/temperature"    */
    const char             *short_desc;  /* Kind column                  */
    const char             *long_desc;   /* Get Info                     */
    const char             *mime_alias;  /* local MIME it behaves as     */
    uint16_t                icon_id;     /* index into the linked icons  */
    tiku_trk_open_t         open_action;
    const tiku_trk_attr_t  *attrs;
    uint8_t                 attr_count;
} tiku_trk_typeinfo_t;

const tiku_trk_typeinfo_t *tiku_trk_typedb_fin
```
</details>


## watch -> node monitoring (PoseView liveness / FSNotification pipeline)

**Decision.** ONE typed event vocabulary reaches PoseView, and it is synthesized by the backend, never by the view.

Both backends produce `tk_fsevent_t` — an opcode (CREATED/REMOVED/MOVED/STAT_CHANGED/ATTR_CHANGED/DEVICE_MOUNTED/DEVICE_UNMOUNTED/QUERY_UPDATE), a `tk_node_ref_t {dev,node}`, a `fields` bitmask (the explicit field selector that replaces BeOS's "is the `attr` string present?" discriminator and the aliasing AttrHashString), a per-backend monotonic `seq`, and a listing `gen`. Local files translate inotify 1:1. The device backend SYNTHESIZES the same opcodes. `tk_poseview_fs_notification()` is the single entry point (PVN-001) and still returns false to park (PVN-002). PoseView contains ZERO device-awareness.

Device-side synthesis is a three-tier ladder, probed once per session, in this priority:

  TIER PUSH   — `sub add <path>`; device emits `~<path>`. Requires the firmware below.
  TIER EPOCH  — one `cat /sys/vfs/nsgen` + `cat /sys/vfs/epoch` per poll; if both
                unchanged, the poll costs ~20 bytes and emits nothing.
  TIER POLL   — `ls <dir>` re-listing every interval. Zero firmware; what runs today.

Every tier feeds the SAME differ. `tk_dirsnap_t` holds the last listing as (name, ino, valhash) sorted by name; `tk_dirsnap_commit()` emits the delta in a FIXED order that is itself the ordering guarantee: (1) all REMOVED, (2) all CREATED, (3) all CHANGED. Remove-before-create for the same name is exactly PVN-062's single-visible-step replacement; change-after-create means a value change on a brand-new entry never needs the pending cache. `seq` is assigned at emission so replay order (PVN-004) survives.

IDENTITY: TikuOS has no inodes, so `tk_ino_intern(backend, abs_path)` interns absolute paths to monotonically-issued 64-bit ids in a per-backend table. Path IS identity (the tree is static, node pointers are stable), and interning — not hashing — means zero collisions and a stable key for the pending cache, `fInsertedNodes`, FindPose and the zombie list.

COALESCING happens at queue insert, not at the view. `tk_fsq_post()` merges a CHANGED into an already-pending CHANGED for the same ref (safe: CHANGED means "re-read", which is precisely TikuOS watch semantics) and DROPS a CHANGED when a CREATED for that ref is still pending. CREATED/REMOVED/MOVED never coalesce away. On top of that sits the accumulator, generalized from PVN-054's MetaMimeChangedAccumulator with its exact constants — `tk_fsq_set_accum(q, 200000, 5000000)`: 200 ms of quiet, 5 s hard cap. Local events bypass the accumulator (they are free); device events always go through it. This is the answer to "same-value writes still ring".

THE VALUE GATE is the second half of that answer and it lives BELOW the queue: the differ hashes the rendered value text, and a CHANGED whose valhash is unchanged is never posted at all. MA-040/MA-072's re-read-and-compare gate is thereby applied one layer earlier than Tracker applies it, which is what makes it affordable on a 9600-baud link.

PENDING CACHE: park the ref + accumulated `fields` mask + name, NOT the payload (the wire never carried a value anyway). `tk_pending_replay()` re-enters the notification and the view re-reads current state. 10 s lifetime (PVN-003) — but swept on every accumulator flush as well as in PoseCreatedOrMoved, closing the leak the verifier found in the original.

LOADER RACE: `tk_dev_lister_t` is a yielding state machine with its cursor in the struct (protothread locals do not survive a yield). The monitor subscribes/snapshots the directory BEFORE the first `ls` (PVN-007 ordering), so a change during the load is queued, not lost. Events are NOT suppressed during load; `tk_nodeset_t` (`fInsertedNodes`) is what guarantees single insertion (PVN-049/PVL-090), and `gen` discards chunks and events belonging to an abandoned listing (PVN-048/PVL-089).

BACKPRESSURE, stated honestly: `tk_fsq_post()` returning -1 sets a per-backend `lost` flag; the view then does a full `Refresh()` (PVN-053), rate-limited to once per 5 s per backend. Liveness is never silently lost — it degrades to a whole-folder re-read.

SLOT BUDGET (PVN-010): the watch table is 8 slots and there is no setrlimit analogue. `tk_dev_monitor_watch_node()` subscribes in priority order — (1) the displayed directory, (2) visible rows that are writable or FRESH_LIVE, in view order — and returns TK_WATCH_POLLED when the table is full. A demoted node falls to the poll tier and the count view renders "N polled". Loud, never silent.

REFRESH COST, the number that decides the tiers. MSP430 @ 9600 baud = 960 B/s. `ls /dev` ~200 B with echo+prompt = ~210 ms; one `cat` ~40 B = ~42 ms. TIER POLL refresh of a 12-row folder with values = 1 ls + 12 cats ≈ 730 ms, so the poll interval floor is 2 s and the value gate is mandatory. TIER EPOCH turns the common (nothing changed) case into ~20 B = 21 ms, so 1 Hz costs 2% of the link — this is the single change that makes a live folder on an MSP430 honest. RA8P1 over TCP/CDC at 115200+: `ls` ≈ 2 ms, so TIER POLL at 4 Hz is already free and PUSH makes it event-driven. `tk_dev_monitor_set_budget()` caps the monitor at 25% of link bandwidth by default and refreshes the viewport first, so a 200-row device folder keeps the visible rows live instead of starving them evenly.

**Rationale.** Three alternatives were on the table.

(a) Extend TIKU_EVENT_VFS to carry an opcode + name in firmware. Rejected: it changes the kernel event ABI for every consumer (rules, BASIC ON CHANGE, `watch`), and it still would not survive the wire — the shell protocol is line-oriented text, so an opcode would have to be re-encoded into `~` lines anyway. The invention belongs host-side, where it costs no MCU bytes.

(b) Make the view diff a re-listing on every event (the PVN-001 note's second option). Rejected outright: it puts device knowledge inside PoseView, breaks PVN-001's single-handler promise, and re-lists a folder per notification — 730 ms per event on MSP430.

(c) The chosen split: firmware stays a dumb "touched" bus; the backend owns synthesis. This works because the differ is required ANYWAY for the legacy-firmware tier, and once you have a differ that emits typed opcodes, the push tier is just a cheaper trigger for the same differ. One code path serves all three tiers and both firmware ages, which is why the tier ladder does not multiply the behavior surface.

Two facts in the existing code forced the shape and made it cheap. First, `tiku_vfs.c:221,237` ALREADY calls `tiku_vfs_notify(parent_dir)` on every dynamic-child create and unlink — directory liveness for /data is fully implemented in the kernel and blocked solely by one line in `tiku_vfs_watch()` rejecting `node->type != TIKU_VFS_FILE`. That is a 1-line fix that unlocks PVN-005/011/026/040 for the only device subtree where entries actually appear and vanish. Second, `tiku_desk_session.h` already routes `~<path>` lines to a notify callback and `tiku_desk_ns_subscribe()` already probes for `sub` and falls back to polling — the host half of the push tier is written and proven on three boards. The remaining firmware is a shell verb, not an architecture.

Coalescing at queue insert rather than at the view is deliberate: Tracker's only accumulator (PVN-054) sits in the view because BeOS's kernel monitor already coalesced. TikuOS explicitly does not, so the missing tier has to be reintroduced somewhere, and the queue is the only place that sees every backend and can also apply the value-hash gate before a single byte of re-read is spent.

Ordering REMOVED→CREATED→CHANGED was chosen over arrival order for the differ because the differ has no arrival order to preserve — it observes a set difference, not a stream. Fixing the order makes PVN-062's replacement and PVN-063's un-shadowing fall out for free instead of needing special cases.


| Tracker concept | Local files | TikuOS nodes |
|---|---|---|
| B_NODE_MONITOR / B_PATH_MONITOR / B_QUERY_UPDATE all entering one handler | inotify IN_* translated 1:1 in tk_local_backend.c | synthesized by tk_dev_monitor from ~ lines or the dirsnap differ |
| node_ref {dev_t, ino_t} | real st_dev/st_ino | dev = backend slot index; node = tk_ino_intern(backend, abs_path) |
| B_ENTRY_CREATED / B_ENTRY_REMOVED | IN_CREATE / IN_DELETE / IN_MOVED_TO / IN_MOVED_FROM | /data dynamic children: parent-dir notify (already in tiku_vfs.c:221,237) or dirsnap membership delta |
| B_ENTRY_MOVED | IN_MOVED_FROM+IN_MOVED_TO cookie-paired into one MOVED - full fidelity | NOT produced: surfaces as REMOVED then CREATED |
| B_STAT_CHANGED | IN_MODIFY/IN_ATTRIB -> fields = SIZE\|MTIME\|MODE from the stat delta | fields = TK_FIELD_VALUE when the value hash moved |
| B_ATTR_CHANGED with an attr name | xattr name carried verbatim in ev.attr | ev.attr = NULL, fields names the descriptor field (VALUE/DESC/CAP) |
| B_DEVICE_MOUNTED / B_DEVICE_UNMOUNTED | mount table watch | session attach/detach on the serial or TCP link |
| watch_node(dir, B_WATCH_DIRECTORY\|B_WATCH_CHILDREN\|...) - one watch covers everything | one inotify watch on the directory, full mask | tiku_vfs_watch() on the DIR node = membership only; per-child values need their own slot or the poll tier |
| TTracker::WatchNode retry via setrlimit(RLIMIT_NOVMON) | raise fs.inotify.max_user_watches / report | tk_dev_monitor_watch_node() returns TK_WATCH_POLLED; count view shows 'N polled' |
| pendingNodeMonitorCache (whole BMessage, keyed device+node) | same, ref + fields mask | same, ref + fields mask |
| MetaMimeChangedAccumulator, AccumulatedRunLater(200000, 5000000) | bypassed - local events flush immediately | tk_fsq_set_accum(q, 200000, 5000000) over ALL device events |
| AddPosesTask spawn_thread + per-chunk window lock | tk_lister_t state machine, 50 entries or 300 ms per chunk | same state machine driven by paged ls; cursor lives in the struct |
| fInsertedNodes + fZombieList closing the loader/monitor race | tk_nodeset_t of interned ids | tk_nodeset_t of interned ids |
| CachedEntryIterator(0,40,true) - 40-entry batch in inode order | batching kept, inode sort dropped (no seek cost on NVM) | batching kept and load-bearing: one ls per chunk, viewport rows first |
| Query results live via B_QUERY_WATCH_ALL | kernel-side match/unmatch retained where available | predicate re-evaluated host-side on each CHANGED, using tk_fsq's QUERY_UPDATE opcode |


**Firmware needed.** ~260 lines total, in four independent pieces, each shippable alone.

1. Let tiku_vfs_watch() accept a DIR node - 1 LINE (kernel/vfs/tiku_vfs.c:841,
   drop `|| node->type != TIKU_VFS_FILE`, keep the NULL guard). The kernel
   ALREADY calls tiku_vfs_notify(parent) on every dynamic-child create and
   unlink (tiku_vfs.c:221 and :237); that notify currently reaches nobody
   because no one can subscribe to a directory. This single line turns on
   /data creation and deletion liveness - PVN-005/011/026/040 for the only
   subtree where entries actually come and go. Highest value per line in the
   whole design.

2. `sub` shell command - ~140 lines .c + ~25 lines .h, plus one command-table
   entry in kernel/shell/tiku_shell.c, one Makefile line, one flag in
   tiku_shell_config.h, and ~8 lines in the shell's existing TIKU_EVENT_VFS
   hook (tiku_shell.c:1441-1450, alongside tiku_shell_rules_on_vfs and
   tiku_shell_cmd_watch_on_vfs) to print `~<path>\n`. `sub add|del|list
   <path>` keeps a TIKU_VFS_WATCH_MAX-sized path table and re-subscribes
   idempotently each tick, exactly as tiku_shell_cmd_watch_tick() already
   does, because rules_rearm() drops everything wholesale. The HOST half is
   already written and proven on three boards: tiku_desk_session.h routes
   `~<path>` lines to a callback and tiku_desk_ns_subscribe() already probes
   for `sub` and falls back to polling.

3. `/sys/vfs/epoch` and `/sys/vfs/nsgen` - ~40 lines (two uint32 counters plus
   two read handlers in kernel/vfs/tree/tiku_vfs_tree_watch.c). `epoch` bumps
   in tiku_vfs_notify(); `nsgen` bumps ONLY in vfs_dyn_write()/vfs_dyn_unlink()
   on success, i.e. membership changes only. The split matters: a device with
   one driver-rung sensor node would otherwise keep `epoch` moving forever and
   make it useless as a "nothing happened" oracle, while `nsgen` stays still.
   This is what makes TIER EPOCH work and is the difference between a 21 ms
   and a 730 ms idle poll on a 9600-baud MSP430.

4. RECOMMENDED, not required: `ls -v <dir>` emitting a value-hash column -
   ~40 lines in kernel/shell/commands/tiku_shell_cmd_ls.c (render each child,
   FNV-1a it, print the hex). Collapses a 12-row device folder refresh from
   13 round trips to 1: ~730 ms -> ~250 ms on MSP430. Do this second if the
   `sub` command slips.

ZERO new lines but worth setting: TIKU_VFS_WATCH_MAX is already `#ifndef`-
guarded, so Tracker builds pass -DTIKU_VFS_WATCH_MAX=16 for ~64-128 bytes of
SRAM and double the push-tier budget. Not available on the tightest MSP430
configs, which is exactly why the demote-to-poll path (PVN-010) has to work
rather than merely exist.

**Rows governed** (49): PVN-001, PVN-002, PVN-003, PVN-004, PVN-005, PVN-006, PVN-007, PVN-008, PVN-009, PVN-010, PVN-011, PVN-012, PVN-014, PVN-019, PVN-021, PVN-022, PVN-023, PVN-026, PVN-028, PVN-030, PVN-031, PVN-032, PVN-034, PVN-037, PVN-038, PVN-040, PVN-043, PVN-047, PVN-048, PVN-049, PVN-050, PVN-053, PVN-054, PVN-056, PVN-058, PVN-065, PVL-089, PVL-090, PVL-091, PVL-092, PVL-094, PVK-032, MA-011, MA-012, MA-040, MA-072, Q-025, Q-038, CW-016


**Rows that will differ** (18) — these become `differs` in the contract:

- **PVN-002**: The pending cache parks the node ref + accumulated fields mask + name, NOT the notification payload. Replay re-enters the notification and the view re-reads CURRENT state, so a parked change delivers the value as of replay, not as of the original event. For the device backend this is not a loss - the wire never carried a value - but a local file that changed and changed back within the parking window will replay as a no-op where BeOS would have replayed the intermediate.
- **PVN-003**: Kept at 10 s, but expiry is swept on every accumulator flush as well as inside PoseCreatedOrMoved. The original only ever sweeps in PoseCreatedOrMoved (RemoveOldEntries is defined and never called, per the verifier correction), so a parked entry for a node that never appears leaks until some unrelated pose is created. We fix that leak deliberately; the observable difference is that an expired entry is freed on a timer instead of on an unrelated event.
- **PVN-005**: For DEVICE directories only. There is no B_WATCH_CHILDREN analogue: a DIR watch (after the 1-line firmware fix) rings on child ADD/REMOVE only. Per-child value changes need either their own watch slot or the poll tier. Local directories keep the full single-watch behavior via inotify.
- **PVN-006**: Inverted for the device backend. In Tracker per-entry watches are the rare escape hatch because the container watch suffices; on a device they are the PRIMARY mechanism for value liveness and are rationed against 8 (or 16) slots. Symlink-target and volume-pose freshness on the device side is served by the poll tier, not by a dedicated watch.
- **PVN-010**: No setrlimit(RLIMIT_NOVMON) analogue and no retry. On exhaustion tk_dev_monitor_watch_node() returns TK_WATCH_POLLED, the node is demoted to the poll tier, and the count view renders 'N polled'. Liveness is preserved at higher latency and is visible in the UI rather than silently raised.
- **PVN-022**: Device backend only: a rename produces REMOVED + CREATED rather than a MOVED, so the 'a rename implies we may have missed other notices, re-read every column' path is reached by way of a fresh row rather than an update. The row's user-visible outcome (all columns correct) holds; row identity does not survive, so selection, scroll anchoring and an in-flight in-place rename edit are lost. Local files keep true MOVED.
- **PVN-023**: Device backend only: because a device rename is REMOVED+CREATED, the row is removed and re-inserted at its sorted position instead of being relocated by _CheckPoseSortOrder. Final ordering is identical; the incremental blit optimization does not apply and the row visibly disappears and reappears.
- **PVN-026**: Device backend only: a move out of / into the displayed directory is already REMOVED / CREATED on the wire, which happens to match this row's behavior exactly - but the two halves of a cross-directory move are NOT correlated, so a move between two open device windows shows as an independent removal and creation with no shared identity.
- **PVN-028**: A view merging device-namespace nodes with local files takes the query / virtual-directory branch UNCONDITIONALLY (no single parent directory), as the row's TIKU note anticipates. Consequence inherited from PVN-065: every move in a merged view is a remove-plus-create.
- **PVN-031**: The 100-iteration, 10 ms-snooze B_BUSY retry loop is replaced by an event-driven retry budget (3 attempts, re-armed by the next event for that node). No blocking sleep on the UI thread. A device node whose read fails transiently is retried on the next notification instead of within one second of the first.
- **PVL-091**: Same divergence as PVN-031, stated from the loader side: a transiently unreadable device node is held as a zombie and retried on its next event rather than spun on. If no further event arrives the row stays stale rather than resolving within a second.
- **PVN-034**: AttrHashString is dropped for explicit field ids (the source itself flags the hash as aliasing-prone). One real behavioral difference: Tracker performs NO re-sort for a stat message carrying no 'fields', whereas the device poll tier always sets TK_FIELD_VALUE, so any value change on the sort column always re-sorts. Local stat events preserve the fields-less no-re-sort rule exactly.
- **PVN-050**: Inode-order batching is dropped (no seek cost on NVM or over a serial link). The 40-entry batching itself is kept and becomes load-bearing rather than an optimization, and gains a viewport-first ordering that Tracker does not have: visible rows are enumerated and value-read before off-screen ones.
- **PVN-054**: Widened, not weakened. The accumulator's exact constants (200 ms delay, 5 s cap) and merge-if-same-key discipline are kept, but it now sits under ALL device events rather than only MIME notices. Local events bypass it entirely and stay immediate, so a local folder is if anything more responsive than Tracker's while a device folder is deliberately damped.
- **PVN-040**: Device backend only: 'mount' maps to a session attaching, gated by capability bits from /sys/vfs/manifest rather than volume.IsPersistent(). Because the TikuOS tree is static, a device node cannot appear mid-session outside /data - so the live 'new volume icon appears' behavior fires at session attach and for /data subtree appearance, and never for /sys or /dev.
- **PVN-043**: Device backend only: unmount maps to session loss, which is detected by link timeout rather than announced. A view rooted at a lost backend closes and aliases into it are marked broken exactly as the row requires, but detection is bounded by the pump timeout (default 6 s) rather than immediate.
- **Q-038**: Device-backed live queries re-evaluate their predicate host-side on each CHANGED event; there is no kernel-side match/unmatch. Rows still appear and disappear without re-running the search, but the latency is the monitor tier's latency (push: immediate; epoch/poll: the poll interval, floor 2 s on a 9600-baud link), and the watch-slot budget caps how many concurrent live device queries can be push-driven.
- **PVN-053**: Unchanged in what it does, but it gains a new trigger with no Tracker analogue: an event-queue overflow sets a per-backend lost flag and forces a full Refresh(), rate-limited to once per 5 s per backend. Tracker simply loses overflowed monitor messages and drifts.


**Risks.**

- The 8-slot watch table is the binding constraint and it is SHARED with rules, BASIC ON CHANGE and the `watch` shell mode. A Tracker window showing a device folder can consume every slot and silently break an armed rule - or, worse, rules_rearm()'s wholesale unwatch_all() drops Tracker's subscriptions. The monitor MUST re-subscribe idempotently every pump (as tiku_shell_cmd_watch_tick already does) and MUST budget at most half the table. Raising TIKU_VFS_WATCH_MAX to 16 mitigates but does not remove this.
- `~<path>` lines interleaving with command output on a shared UART. The session layer demuxes them line-anchored, but a `~` arriving mid-`cat` of a multi-line value could be spliced into the value text. Use the IPv4-version-anchor lesson from shell_net_demux: anchor on line start AND require the path to resolve, never on a bare leading character.
- TIER EPOCH is only as good as the epoch/nsgen split. If any driver rings tiku_vfs_notify() at high rate (a GPIO edge counter, a sensor threshold), `epoch` is permanently dirty and every poll degrades to a full re-list. `nsgen` is the safe half; treat `epoch` as advisory only and never as a reason to skip a value re-read of a subscribed node.
- Refresh storms: overflow -> lost -> Refresh() -> a 730 ms full re-read on MSP430, which under sustained event pressure can consume the entire link and cause the next overflow. The 5 s per-backend rate limit is load-bearing, not a nicety, and must be tested by driving a device node in a tight loop.
- Value-hash gating hides a real class of change: two different values that render to the same text (a truncated read, a node whose read handler is buffer-limited) will not emit a CHANGED. tiku_vfs_read is snprintf-style and a return >= buffer size means truncation - the differ must treat a truncated read as always-changed rather than hashing the truncated text.
- The lister is a yielding state machine and every cursor must live in tk_lister_t. A cursor left as a local is the protothread-locals trap and will silently restart or skip a chunk - and on the device backend a skipped chunk looks exactly like a folder that legitimately has fewer entries.
- Identity interning grows unboundedly across a long session with a churning /data. tk_ino_forget() must be called from DeletePose and tk_ino_drop_backend() from unmount, or the table is a slow leak that also makes a reused filename inherit a stale id.


<details><summary>Proposed C</summary>

```c
/* ===================== tk_fsevent.h - the one vocabulary ================= */

#define TK_NAME_MAX 64

typedef enum {
    TK_FS_ENTRY_CREATED = 1,   /* B_ENTRY_CREATED    */
    TK_FS_ENTRY_REMOVED,       /* B_ENTRY_REMOVED    */
    TK_FS_ENTRY_MOVED,         /* B_ENTRY_MOVED      */
    TK_FS_STAT_CHANGED,        /* B_STAT_CHANGED     */
    TK_FS_ATTR_CHANGED,        /* B_ATTR_CHANGED     */
    TK_FS_DEVICE_MOUNTED,      /* B_DEVICE_MOUNTED   */
    TK_FS_DEVICE_UNMOUNTED,    /* B_DEVICE_UNMOUNTED */
    TK_FS_QUERY_UPDATE         /* B_QUERY_UPDATE     */
} tk_fs_opcode_t;

typedef struct { uint32_t dev; uint64_t node; } tk_node_ref_t;

/* Explicit field selector. Replaces both the "is the attr string present"
 * discriminator (PVN-030) and AttrHashString's admitted aliasing (PVN-034). */
#define TK_FIELD_NAME   0x0001u
#define TK_FIELD_SIZE   0x0002u
#define TK_FIELD_MTIME  0x0004u
#define TK_FIELD_MODE   0x0008u
#define TK_FIELD_VALUE  0x0010u   /* device: rendered node value        */
#define TK_FIELD_DESC   0x0020u   /* device: tiku_vfs_desc_t changed    */
#define TK_FIELD_CAP    0x0040u   /* device: req_cap changed            */
#define TK_FIELD_ICON   0x0080u
#define TK_FIELD_TYPE   0x0100u
#define TK_FIELD_ALL    0xFFFFu

typedef struct tk_fsevent {
    uint8_t       opcode;      /* tk_fs_opcode_t                        */
    uint8_t       backend;     /* == ref.dev                            */
    uint16_t      fields;      /* TK_FIELD_*; 0 = unknown -> full read  */
    uint32_t      gen;         /* listing generation (PVL-089)          */
    uint64_t      seq;         /* per-backend monotonic order           */
    tk_node_ref_t ref;
    tk_node_ref_t dir;         /* parent at destination                 */
    tk_node_ref_t from_dir;    /* MOVED only                            */
    int64_t       when_us;
    const char   *attr;        /* xattr name, or NULL = re-read all     */
    char          name[TK_NAME_MAX];
    char          from_name[TK_NAME_MAX];
} tk_fsevent_t;

/* ===================== tk_fsq.h - coalescing queue + accumulator ========= */

typedef struct tk_fsq tk_fsq_t;

tk_fsq_t *tk_fsq_new(size_t capacity_pow2);
void      tk_fsq_free(tk_fsq_t *q);

/* @return 1 inserted, 0 merged into a pending event, -1 overflow (sets lost) */
int  tk_fsq_post(tk_fsq_t *q, const tk_fsevent_t *ev);

/* PVN-054's shape and constants: tk_fsq_set_accum(q, 200000, 5000000). */
void tk_fsq_set_accum(tk_fsq_t *q, int64_t delay_us, int64_t cap_us);
void tk_fsq_set_immediate(tk_fsq_t *q, uint8_t backend, int immediate);

int  tk_fsq_drain(tk_fsq_t *q, int64_t now_us, tk_fsevent_t *out, int max);
void tk_fsq_mark_lost(tk_fsq_t *q, uint8_t backend);
int  tk_fsq_lost(const tk_fsq_t *q, uint8_t backend);  /* -> Refresh() */
void tk_fsq_clear_lost(tk_fsq_t *q, uint8_t backend);

/* ===================== tk_ino.h - identity interning ==================== */

uint64_t tk_ino_intern(uint8_t backend, const char *abs_path);
int      tk_ino_path(uint8_t backend, uint64_t ino, char *out, size_t max);
void     tk_ino_forget(uint8_t backend, uint64_t ino);
void     tk_ino_drop_backend(uint8_t backend);   /* on unmount */

/* ===================== tk_dirsnap.h - the differ ======================== */

typedef struct tk_dirsnap tk_dirsnap_t;

tk_dirsnap_t *tk_dirsnap_new(uint8_t backend, const char *dir);
void tk_dirsnap_free(tk_dirsnap_t *s);
void tk_dirsnap_begin(tk_dirsnap_t *s, uint32_t gen);
int  tk_dirsnap_ingest(tk_dirsnap_t *s, const char *name, int is_dir,
                       uint32_t valhash);          /* FNV-1a of value text  */
/* Emits REMOVED, then CREATED, then CHANGED. Unchanged valhash emits
 * nothing at all - the value gate (MA-040/MA-072). @return events emitted. */
int  tk_dirsnap_commit(tk_dirsnap_t *s, tk_fsq_t *q);
void tk_dirsnap_invalidate(tk_dirsnap_t *s);       /* force full re-emit    */

/* ===================== tk_fs_backend.h ================================== */

#define TK_BK_PUSH      0x01u   /* firmware answered `sub`                  */
#define TK_BK_DIRWATCH  0x02u   /* tiku_vfs_watch() accepts a DIR node      */
#define TK_BK_EPOCH     0x04u   /* /sys/vfs/epoch + /sys/vfs/nsgen exist    */
#define TK_BK_MOVE      0x08u   /* backend can report a true rename         */
#define TK_BK_XATTR     0x10u

typedef struct tk_fs_backend tk_fs_backend_t;

typedef struct tk_fs_backend_ops {
    uint32_t caps;
    int  (*probe)(tk_fs_backend_t *b);
    int  (*watch_dir)(tk_fs_backend_t *b, const char *dir);
    int  (*watch_node)(tk_fs_backend_t *b, const char *path, int prio);
    void (*stop_watch_all)(tk_fs_backend_t *b);
    int  (*list_begin)(tk_fs_backend_t *b, const char *dir, uint32_t gen);
    int  (*list_step)(tk_fs_backend_t *b, tk_dirent_t *out, int max);
    int  (*read_node)(tk_fs_backend_t *b, const tk_node_ref_t *r,
                      tk_nodeinfo_t *out);
    int  (*pump)(tk_fs_backend_t *b, int64_t now_us, int timeout_ms);
} tk_fs_backend_ops_t;

/* ===================== tk_dev_monitor.h ================================= */

enum { TK_TIER_PUSH = 0, TK_TIER_EPOCH, TK_TIER_POLL };
enum { TK_WATCH_PUSHED = 0, TK_WATCH_POLLED = 1, TK_WATCH_ERR = -1 };

typedef struct tk_dev_monitor tk_dev_monitor_t;

tk_dev_monitor_t *tk_dev_monitor_new(tiku_desk_ns_t *ns, uint8_t backend,
                                     tk_fsq_t *q);
int  tk_dev_monitor_probe(tk_dev_monitor_t *m);        /* sets tier + caps  */
int  tk_dev_monitor_tier(const tk_dev_monitor_t *m);
int  tk_dev_monitor_watch_dir(tk_dev_monitor_t *m, const char *dir);
int  tk_dev_monitor_watch_node(tk_dev_monitor_t *m, const char *path,
                               int prio);              /* TK_WATCH_*        */
int  tk_dev_monitor_slots_free(const tk_dev_monitor_t *m);   /* PVN-010     */
int  tk_dev_monitor_polled_count(const tk_dev_monitor_t *m); /* count view  */
void tk_dev_monitor_set_budget(tk_dev_monitor_t *m, uint32_t bytes_per_sec);
void tk_dev_monitor_set_viewport(tk_dev_monitor_t 
```
</details>


## Volumes, Trash, Desktop — what a TikuOS device is in Tracker's world

**Decision.** A connected TikuOS device IS a volume — specifically a removable, non-boot, persistent, trash-less volume — and it is admitted through Tracker's existing volume roster, mount/unmount notification and no-Trash-on-this-volume machinery rather than through any new concept. Four rules:

(1) VOLUME. `tk_roster` is the BVolumeRoster analogue and holds both kinds. Local volumes come from mountinfo with their real `st_dev`; device volumes get synthetic ids from `TK_DEV_DEVICE_BASE` (0x40000000) so they can never alias a real `st_dev` — which means Tracker's `CheckDevicesEqual` ladder (FS-001/PVS-037) already gives the right answer for every cross-store drag (COPY) with no new rule. Volume flags carry the per-volume answers Tracker asks BVolume for; a device volume is `PERSISTENT|REMOVABLE|KNOWS_QUERY` and is NOT `HAS_TRASH`, NOT `HAS_ATTRS`, NOT `BOOT`. Display name = `cat /sys/device/name`. Identity (Q-027, sidecar keys) = `cat /sys/device/uid`, not the by-id link — the uid survives a replug on a different port. Capacity/free = `df` (the /data store); a board with no store reports capacity 0 and therefore gets no space bar and shows "-", which is what MA-024/TS-054 already say.

(2) SUBTREE CLASSIFICATION — the move that closes ~10 rows for free. At mount, a device volume's root children are classified: `/data` is a real directory (create/write/unlink/send/recv); `/sys`, `/dev`, `/proc` and anything else are VIRTUAL DIRECTORIES. `BPoseView::TargetVolumeIsReadOnly()` already returns true for `IsVirtualDirectory()` BEFORE it consults the volume, so New folder, Cut, Paste, the read-only tint, and drag-out-creates-a-link (FS-078) all fall out of existing code. Writability inside a virtual directory is then per-node, from the manifest's rw column plus `req_cap` — which is exactly what PVL-102's TIKU note demands.

(3) DESKTOP. The Desktop is real and always exists (AW-001/AW-017/AW-018): one full-screen, undecorated, bottom-of-stack X11 window (`_NET_WM_WINDOW_TYPE_DESKTOP`, override-redirect fallback), created before ReadyToRun; on wasm it is the canvas backdrop layer. Its iterator chain is Tracker's own: the local Desktop directory, then one volume pose per roster volume (or the single Disks root pose), then the Trash pose created last. A device appears on the Desktop when plugged and vanishes when unplugged, announced by SYNTHESIZED `TK_VOL_MOUNTED`/`TK_VOL_UNMOUNTED` messages shaped exactly like `B_DEVICE_MOUNTED`/`B_DEVICE_UNMOUNTED` — the idiom AW-026 already uses for the "/" icon. The by-id scan runs on the TaskLoop at 1 Hz; "mounted" is a HOST fact (does the endpoint answer a prompt), so it is the roster's observer list, NOT a watch on the device namespace root — I am rejecting PVN-008's suggested mapping, because a device cannot tell you it was unplugged. Unplug is debounced: link-open-but-no-prompt is STALLED (poses get a stale veil, reconnect is attempted up to 3 times using the proven `tiku_desk_session_reconnect`); only endpoint-gone or reconnect-exhausted posts UNMOUNTED, at which point PVN-043 runs unchanged. Device volume poses subscribe to NOTHING — no device watch slot is spent on a Desktop icon (PVN-006); name and capacity refresh on mount, on any write under /data, and otherwise at most once per 10 s.

(4) TRASH — refuse the trash, offer the delete, through Tracker's own path. A device volume reports no Trash directory, so `MoveSelectionOrEntryToTrash()` splits the selection into `entriesToTrash` and `entriesToDeleteOnTheSpot` and runs two jobs, exactly as it already does for removable media without a Trash (FS-044). The alert is Tracker's own string: "Some of the selected items cannot be moved to the Trash. Would you like to delete them instead? (This operation cannot be reverted.)" No invented trash convention on /data, and no blanket refusal. Deletion reaches `rm` only where `tiku_vfs_unlink` reaches — i.e. /data; every node under a virtual directory has no `TK_CAP_DELETE`, so Delete is disabled, not attempted. Dragging a device volume itself to the Trash EJECTS it (closes the session, drops it from the roster) — FS-041's rule verbatim, with "the boot volume refuses" becoming "the local root volume refuses". The host Trash is completely untouched: real per-volume Trash directories, `_trk/original_path` restore-to-origin, the union Trash window, Empty Trash across local volumes — full fidelity, because local files have xattrs and a real filesystem.

Rejected alternatives, briefly: a `/data/.Trash/` convention would need a durable original-path side table on the scarcest storage in the system, would double NVM wear per trash (the store is slot-based and has no rename), and would be INVENTED behavior where the source already has an answer. Blanket refusal would make the file manager weaker than the shell it talks to.

**Rationale.** Every part of this is a mapping onto machinery Tracker already has, which is why it costs no rows and no firmware.

The Trash question looked like it needed invention until `BPoseView::MoveSelectionOrEntryToTrash` (PoseView.cpp:6225-6272) turned up: Tracker already partitions a selection by whether each item's device has a Trash, warns once, and launches two jobs. A TikuOS device is just a volume that lands entirely in the second bucket. Choosing that over a `/data/.Trash/` convention keeps FS-044/FS-046/FS-047 closable verbatim and keeps the destructive/undoable split honest — deletion on a device really is irreversible, and a fake trash would lie about that. It also respects the storage: TFS has no rename, so "trash" would be a full slot copy plus an unlink, and the original-path table would have to live in NVM.

Classifying /sys, /dev, /proc as virtual directories rather than inventing a "device read-only" concept is the highest-leverage line in the decision. `TargetVolumeIsReadOnly()` (PoseView.cpp:8919) short-circuits on `IsVirtualDirectory()` before touching BVolume, so PVL-076's tint, PVK-044/CW-031's menu enablement, FS-078's drag-out-creates-a-link and the New-folder/Cut/Paste refusals are all already written. It also answers PVL-102's own note — "same disk, different answer depending on what the window represents" — with Tracker's existing answer rather than a new one, and leaves per-node writability (manifest rw + req_cap) as the finer grain on top.

Synthetic device ids in a disjoint range rather than a new "is this the same store" predicate: FS-001's TIKU note asks for an explicit decision instead of a device-id compare, and the honest finding is that the device-id compare is CORRECT here provided the ids cannot alias. Cross-store drags default to copy; same-device drags within /data default to move. Nothing new to reason about at every drop site.

`/sys/device/name` (durable persist cell, writable, CAP_FS) and `/sys/device/uid` already exist, which is why MA-052 (rename a volume renames the volume itself, fails cleanly when refused) and FS-066 (undo a volume rename) close identically with a `write`, and why Q-027's "remembered by volume identity, not device number" is better served than on BFS.

Debouncing unplug is the one place I add behavior Tracker has no analogue for, and it is required rather than decorative: serial links glitch, the reconnect path is already proven in the desktop client, and PVN-043's consequence (close the window, drop the poses, break the symlinks) is too destructive to fire on a 200 ms hiccup. The actions when it does fire are unchanged; only the trigger is debounced.

Making the Desktop a real full-screen window rather than a metaphor is the only reading of AW-001/AW-017/AW-018 that keeps them closable, and it is what the app is: the new Tracker owns the desktop, so "the Desktop is a special always-open PoseView" is literally true, with device volume icons sitting on it beside local ones.


| Tracker concept | Local files | TikuOS nodes |
|---|---|---|
| BVolume / the volume roster | one entry per mountinfo mount, real st_dev, flags from statfs + ST_RDONLY | one entry per answering endpoint (by-id serial link or configured TCP), synthetic dev id from TK_DEV_DEVICE_BASE, one tiku_desk_session_t + tiku_desk_ns_t each |
| Volume identity (Q-027: remembered across remount) | filesystem UUID, else the mount path | cat /sys/device/uid |
| BVolume::GetName / SetName (MA-006, MA-052, FS-066) | volume label; BVolume::SetName analogue via the fs | cat /sys/device/name; write /sys/device/name <new> |
| BVolume::Capacity / FreeBytes (PVL-072, TS-054, MA-024, AW-069) | statfs, polled at 1 Hz by Pulse | df -> tiku_data_df_t {cap_bytes, used_files*slot_bytes, backing} |
| B_DEVICE_MOUNTED (PVN-040, PVN-008, AW-026) | node monitor B_WATCH_MOUNT | tk_roster_scan() at 1 Hz over /dev/serial/by-id + configured TCP endpoints; posts a synthesized TK_VOL_MOUNTED shaped like B_DEVICE_MOUNTED |
| B_DEVICE_UNMOUNTED (PVN-043, PVS-057, FS-060, Q-046, AW-077) | node monitor | endpoint gone from the by-id scan, or TK_ROSTER_RECONNECT_N reconnects exhausted, posts TK_VOL_UNMOUNTED |
| IsVirtualDirectory() (PVL-102, FS-078, CW-031, PVK-044, PVL-076) | not used (no virtual dirs on a POSIX mount) | /sys, /dev, /proc and any non-/data root child of a device volume |
| A real directory that accepts new children | any writable directory | /data only (touch, mkdir, rmdir, rm, write, send, recv) |
| Per-entry writability (PVK-044, CW-031) | st_mode + the volume's read-only flag | manifest rw column + node req_cap vs the session's caller cap |
| FSGetTrashDir (FS-038, FS-044, FS-048, FS-081, PVN-027) | find_directory(B_TRASH_DIRECTORY) per volume; created 0755 and marked invisible; full behavior | tk_vol_trash_dir() returns TK_ERR_NO_TRASH -- a device volume has no Trash and never gets one |
| Move to Trash (PVK-038, FS-046, FS-047, AW-095) | full: trash, restore, undo, Shift+Del deletes, Del-in-Trash deletes silently | selection falls entirely into entriesToDeleteOnTheSpot -> Tracker's own confirm alert -> rm, per item, via the device worker |
| Restore to origin / _trk/original_path (FS-039, FS-040, MA-030) | full: xattr written on trash, walked up parents on restore, removed on success | never written -- device entries are never trashed; the Original path column renders "-" |
| Empty Trash (FS-048, PVN-056, IV-046, AW-029) | full: every local volume's trash, icon flips live | device volumes contribute nothing to the union and are skipped |
| Drag a volume to the Trash = unmount (FS-041) | umount; the boot volume refuses with Tracker's alert | tk_roster_unmount(dev, eject=1): close the session, stop the worker, drop the poses |
| Mount > submenu + Unmount Cmd+U (CW-034, CW-022, CW-042, AW-096) | partitions with a filesystem; the boot volume's item disabled | discovered by-id endpoints + configured TCP endpoints, checkmarked when connected; "Connect to..." replaces "Settings..." |
| The Desktop window (AW-001, AW-017, AW-018, AW-104) | the local Desktop directory ($XDG_DESKTOP_DIR, else ~/Desktop), inotify-watched | device volumes contribute a volume POSE only, never dirents |
| Desktop integrated dirent chain (PVN-051, AW-026, AW-027) | local Desktop dir, then volume poses (or the Disks root pose), then the Trash pose last | one volume pose per connected device, inserted through the synthesized entry-created idiom |
| Desktop pose positions + view state (PVL-029, MA-064, PVL-103, CW-004, CW-069) | kAttrPoseInfo / kAttrExtendedPoseInfo / kAttrDesktopViewState / kAttrDisksViewState as real xattrs | host-side sidecar keyed "<uid>:<path>", stored on the local Desktop directory / Tracker settings dir |
| Volume pose freshness (PVN-006, MA-021, AW-097) | per-entry node watch on the volume and its mount point | no device watch slot at all; refresh on mount, on any write under /data, else at most once per 10 s |
| Query target volumes (Q-017, Q-018, Q-021, Q-027, Q-039, Q-046) | KNOWS_QUERY cleared until an indexer exists | KNOWS_QUERY set -- the manifest is the index |
| AddPosesTask batching (PVL-088, PVN-050) | background thread, 50-model chunks, 300 ms flush, barber pole | one tk_devio worker thread per device volume; the UI thread never touches the session |


**Firmware needed.** None. Every piece rides mechanisms TikuOS already ships and the desktop client already proved on three boards: `ls`, `cat`, `write`, `rm`, `touch`, `mkdir`, `rmdir`, `send`, `recv`, `df`, `info`, `/sys/vfs/manifest`, `/sys/device/{name,uid,mcu,version}`, `sub` (with the proven poll fallback). In particular the two places that looked like they needed firmware do not: volume naming and rename are `/sys/device/name` (an existing durable persist cell with CAP_FS), and volume identity is `/sys/device/uid`. Two OPTIONAL follow-ons, neither blocking: (a) the paged-manifest fix already recorded as the S0 firmware gap (`manifest <path>` or an offset argument), ~60-80 lines in kernel/vfs/tree/tiku_vfs_tree_watch.c plus a bounded renderer in tiku_vfs.c -- until then the host's ls-walk completion covers it and descriptors stay optional; (b) a `/sys/data/generation` counter bumped on every /data mutation, ~15 lines, which would let the free-space bar refresh on a subscription instead of the 10 s poll and would close AW-097 more tightly.

**Rows governed** (86): AW-001, AW-017, AW-018, AW-019, AW-021, AW-025, AW-026, AW-027, AW-029, AW-104, PVN-051, PVN-058, PVL-008, MA-069, CW-004, CW-010, CW-053, CW-069, PVL-103, MA-064, PVL-029, AW-004, PVN-008, PVN-040, PVN-041, PVN-043, PVN-025, PVN-006, PVN-071, MA-004, MA-005, MA-006, MA-021, MA-052, MA-024, IV-006, IV-045, IV-052, PVL-072, PVL-074, PVL-076, PVL-102, PVL-088, PVN-050, TS-046, TS-054, AW-069, AW-097, AW-096, CW-022, CW-034, CW-042, CW-057, FS-001, PVS-037, FS-012, FS-028, FS-036, FS-038, FS-039 …


**Rows that will differ** (25) — these become `differs` in the contract:

- **FS-038**: Sending items to the Trash uses the item's own volume's Trash. A TikuOS device volume has NO Trash directory and never gets one: tk_vol_trash_dir() returns TK_ERR_NO_TRASH. The '<name> copy' collision handling and the per-volume lookup are unchanged for local volumes.
- **FS-039**: The original path is never recorded for a device entry, because a device entry is never trashed. Local files keep the full behavior (_trk/original_path written on trash, removed on successful restore).
- **FS-040**: Restore has no device path at all -- there is nothing in a device Trash to restore. Local unchanged (parents recreated, refuses to overwrite).
- **FS-048**: Empty Trash's skip predicate gains a clause: it now skips read-only, non-persistent AND trash-less volumes, so device volumes are skipped. Local volumes unchanged.
- **FS-081**: FSCreateTrashDirs no longer covers 'every mounted volume': device volumes are excluded from trash-dir creation at startup. The invisible-marking and re-marking behavior is unchanged for local volumes.
- **PVN-027**: The Trash window's union is over volumes that HAVE a trash; device volumes contribute nothing, so trashing on a device never appears there (it cannot -- deletion on a device is immediate).
- **PVK-038**: On a device selection the plain Delete key does not move anything to the Trash: it enters Tracker's no-trash confirmation (Cancel / Delete) and, on confirmation, deletes outright via rm. Shift+Delete behaves the same as plain Delete there. Delete is DISABLED entirely for nodes under /sys, /dev, /proc (no TK_CAP_DELETE). Local behavior is unchanged in all three cases the row lists.
- **PVK-052**: Scripted 'delete Entry' on a device ref cannot move to Trash and has no UI to confirm with, so it FAILS with TK_ERR_NO_TRASH rather than deleting silently. A caller wanting deletion must use the explicit destructive verb. Local refs still move to the Trash as the row says.
- **FS-028**: A same-volume move is NOT a rename on a device: the shell has no mv, so a move within /data is write-new-then-unlink through the host. It is therefore proportional to file size, not instant, and the progress bar counts bytes rather than one unit per item. Ordering is write-then-unlink, so an interrupted move leaves a duplicate, never a loss. Local same-volume moves stay instant renames.
- **FS-036**: The mount-point skip is a node-kind check, not an st_dev compare: a copy never descends from a real directory into a virtual directory subtree (/sys, /dev, /proc). Equivalent in effect and stricter -- it also prevents serializing a device's virtual namespace when the source is /data.
- **PVN-041**: The second clause has no device analogue -- there is no 'mounted inside another persistent volume' relation for an endpoint. A device volume is admitted on TK_VOLF_PERSISTENT alone. Local volumes keep both tests.
- **PVN-043**: The consequences of unmount are identical, but the trigger is debounced: a device whose link stalls enters TK_VOLST_STALLED (poses veiled stale) and is reconnected up to TK_ROSTER_RECONNECT_N times before TK_VOL_UNMOUNTED is posted, so a transient serial glitch no longer closes the window. Local volumes unmount immediately as before.
- **PVN-025**: The stat()-under-/ inode-recovery trick has no device analogue. A device volume rename is either made by us (write /sys/device/name) or observed on the name-refresh cadence, and updates the cached volume name directly.
- **MA-021**: A device volume renamed from the board's own shell is picked up within about 10 s (the name/capacity refresh cadence), not instantly, and there is no second mount-point watch. This is deliberate: a device volume pose spends ZERO device watch slots, so all 8 stay available for open windows.
- **AW-097**: Free-space bars refresh once per second only for local volumes. Device volumes refresh on mount, on any successful write or unlink under /data, and otherwise at most once per 10 s -- never on the 1 Hz Pulse, because each refresh is a df round trip over UART.
- **AW-069**: Device volume/root info refreshes capacity, used and free on the same 10 s cadence rather than once a second; the Filesystem field shows the /data backing string (MRAM / FRAM / RAM*) reported by df.
- **IV-006**: A device volume has no filesystem-supplied icon to fetch. It gets a built-in 'TikuOS device' icon, with a distinct stalled variant drawn while the volume is in TK_VOLST_STALLED. The share-icon rule is unchanged for local shared volumes.
- **CW-034**: For devices the Mount > submenu lists discovered by-id serial endpoints and configured TCP endpoints (checkmarked when connected) rather than partitions; 'Settings...' becomes 'Connect to...' (host:port / port+baud); 'Mount all' connects every discovered endpoint. 'The boot volume's entry is disabled' becomes 'the local root volume's entry is disabled'. The Desktop-and-root-popup-get-no-Unmount rule is unchanged.
- **MA-064**: The three storage slots (folder / Disks / Desktop) are preserved and never collide, but for device entries they live in a host-side sidecar keyed '<uid>:<path>' rather than on the object, because a device node carries no attributes. User-visible consequence: device window state does not travel with the board to another host.
- **AW-004**: The saved open-window set records device windows by '<uid>:<path>'. At startup a window whose device is not connected is NOT opened, and its entry is RETAINED in the saved set so it reopens the next time that board is plugged in -- rather than being dropped as a dead ref.
- **PVN-008**: Volume mount/unmount events come from the roster's observer list (a 1 Hz host-side by-id/TCP scan), not from a watch on the device namespace root as the row's TIKU note suggested -- a device cannot report its own unplug. The folder-attribute and MIME-database subscriptions are unchanged for local files; a device volume has no MIME database, so that subscription is absent there.
- **MA-005**: Trash and Desktop kinds exist only on the local side. A device volume has neither, so the associated rename/delete refusals on those kinds simply never arise there; the refusals that DO apply to a device (mount root, virtual directory) come from TK_NODE_VOLUME and TK_NODE_VIRTUAL_DIR instead.
- **MA-030**: The Original path column renders '-' for every device entry, unconditionally, since no device entry can carry an original-path record. This is the row's own documented failure rendering, so no new UI, but it will never show anything on a device window.
- **Q-036**: 'Include Trash' has no effect when the query's target is a device volume (there is no trash subtree to include or exclude). The option, its persistence in the query, and the generic exclude-this-subtree filter hook it rides on are all kept for local targets.
- **PVN-050**: Inode-order batching is dropped entirely (no payoff on NVM and no inode to sort by on a device); the batching itself -- 50-model chunks, 300 ms early flush, barber pole -- is kept for both backends, since it is what makes a UART-bound listing tolerable.


**Risks.**

- Deletion on a device is irreversible and now reachable by the Delete key. The mitigation is entirely in the confirmation, and the confirmation is a modal the user will learn to dismiss. Name the device in the alert text and default the focused button to Cancel; consider an explicit per-volume 'allow deletion' toggle that starts OFF for a newly seen uid.
- A /data move is write-new-then-unlink with no atomic rename underneath. The ordering guarantees the failure mode is a duplicate rather than a loss, but a cancelled move leaves the store with both copies and a slot budget that may not have room for the retry. The move job must check free slots via df BEFORE starting, the way FS-015 already checks free space.
- Bulk copy in and out of /data goes through send/recv, and the known failure there is flow control -- the board cannot drain UART while writing NVM (79 drops observed, project_tikuos_console_bulk_transfer). Per-chunk flow control is a hard prerequisite before the copy path is exposed in the UI, or a large drag onto a device will corrupt files silently.
- One worker thread and one session per connected device, with the session explicitly non-reentrant (its notify callback must not re-enter). Two windows on the same device serialise behind that thread; a slow df or a large recv will stall an unrelated window's listing. Requests need a priority order (interactive list/read ahead of background df and bulk transfer) or the UI will feel dead during transfers.
- The 1 Hz by-id scan opens candidate endpoints to see whether they answer a prompt. On MSP430 rigs DTR toggling on open RESETS the target (project_ezfet_dtr_reset), so an over-eager scanner would reboot the board it is trying to show. The scanner must never open an endpoint it has not been told about or has not previously identified, and must hold sessions open rather than reopening them.
- Reconnect debouncing hides real disconnections for up to TK_ROSTER_STALL_MS times TK_ROSTER_RECONNECT_N (about 9 s worst case). During that window the poses shown are stale and a write posted against them will fail late. The stale veil must be visually unmistakable, not a subtle tint.
- Synthetic device ids must be allocated so they never collide across a mount/unmount/remount cycle within one session, or a stale entry_ref from the clipboard (FS-060) or a saved window (AW-004) will resolve against the wrong board. Allocate monotonically and never reuse an id within a process lifetime; match saved state on uid, never on dev.
- Classifying every non-/data root child as a virtual directory is a whitelist of one. A future device that mounts a second real store (FAT32 on eMMC, a PSRAM-backed store) will be misclassified read-only until tk_tiku_classify learns about it. The classifier should key off a device-reported property rather than the literal name /data as soon as one exists.


<details><summary>Proposed C</summary>

```c
/* ---- volume roster: the BVolumeRoster analogue, two backends ---- */

typedef int32_t tk_dev_t;
#define TK_DEV_NONE         ((tk_dev_t)-1)
#define TK_DEV_DEVICE_BASE  ((tk_dev_t)0x40000000)  /* never aliases st_dev */

typedef enum { TK_VOL_LOCAL = 0, TK_VOL_TIKU } tk_vol_kind_t;

typedef enum {
    TK_VOLST_MOUNTING = 0,  /* session opening / manifest loading        */
    TK_VOLST_READY,         /* prompt answering                          */
    TK_VOLST_STALLED,       /* link open, no prompt; reconnecting        */
    TK_VOLST_GONE           /* endpoint vanished; unmount posted         */
} tk_vol_state_t;

#define TK_VOLF_PERSISTENT  0x01u  /* gets a Desktop icon (PVN-041)      */
#define TK_VOLF_READONLY    0x02u  /* BVolume::IsReadOnly                */
#define TK_VOLF_HAS_TRASH   0x04u  /* FSGetTrashDir succeeds             */
#define TK_VOLF_HAS_ATTRS   0x08u  /* state may be stored on objects     */
#define TK_VOLF_KNOWS_QUERY 0x10u
#define TK_VOLF_REMOVABLE   0x20u  /* Unmount / Cmd+U enabled            */
#define TK_VOLF_SHARED      0x40u
#define TK_VOLF_BOOT        0x80u  /* refuses unmount (FS-041, CW-034)   */
/* A TikuOS device volume is exactly:
   PERSISTENT | KNOWS_QUERY | REMOVABLE   (no TRASH, no ATTRS, no BOOT) */

typedef struct tk_volume {
    tk_dev_t        dev;
    tk_vol_kind_t   kind;
    tk_vol_state_t  state;
    uint32_t        flags;              /* TK_VOLF_*                     */
    char            name[64];           /* /sys/device/name, or label    */
    char            identity[64];       /* /sys/device/uid, or fs UUID   */
    char            endpoint[256];      /* by-id link or "tcp:host:port" */
    char            mount_path[256];    /* local only; "" for a device   */
    uint64_t        capacity;           /* 0 = unknown -> no bar, "-"    */
    uint64_t        free_bytes;
    const char     *fs_kind;            /* df backing: "MRAM"/"FRAM"/... */
    uint32_t        cap_polled_ms;      /* rate-limits the space bar     */
    tiku_desk_session_t *session;       /* NULL when TK_VOL_LOCAL        */
    tiku_desk_ns_t      *ns;
    struct tk_devio     *io;
    uint8_t         reconnect_tries;
} tk_volume_t;

enum {                                   /* message what codes           */
    TK_VOL_MOUNTED   = 'TvMt',           /* shaped like B_DEVICE_MOUNTED */
    TK_VOL_UNMOUNTED = 'TvUm',
    TK_VOL_CHANGED   = 'TvCh'            /* name / capacity / state       */
};

#define TK_ROSTER_SCAN_MS       1000
#define TK_ROSTER_STALL_MS      3000
#define TK_ROSTER_RECONNECT_N   3

void         tk_roster_init(void);
int          tk_roster_count(void);
tk_volume_t *tk_roster_at(int i);
tk_volume_t *tk_roster_find_dev(tk_dev_t dev);
tk_volume_t *tk_roster_find_identity(const char *uid);   /* Q-027        */
tk_volume_t *tk_roster_boot(void);
void         tk_roster_watch(BMessenger who);            /* PVN-008      */
void         tk_roster_unwatch(BMessenger who);
void         tk_roster_scan(void);        /* TaskLoop, TK_ROSTER_SCAN_MS */
tk_dev_t     tk_roster_mount_serial(const char *path, int baud);
tk_dev_t     tk_roster_mount_tcp(const char *host, int port);
int          tk_roster_unmount(tk_dev_t dev, int eject); /* FS-041, CW-034 */

/* ---- Trash: FSGetTrashDir's analogue, and the split it drives ---- */

#define TK_ERR_NO_TRASH   (-3001)
int  tk_vol_trash_dir(tk_dev_t dev, char *out, size_t max);
static inline int tk_vol_has_trash(const tk_volume_t *v)
{ return (v->flags & TK_VOLF_HAS_TRASH) != 0; }

/* Verbatim reuse of Tracker's own partition; a device volume simply
   lands entirely in to_delete.  Strings are Tracker's existing ones. */
void tk_split_trashable(const tk_ref_list_t *sel,
                        tk_ref_list_t *to_trash,
                        tk_ref_list_t *to_delete);

/* ---- model kinds + per-node capabilities ---- */

typedef enum {
    TK_NODE_PLAIN = 0, TK_NODE_DIR, TK_NODE_SYMLINK,
    TK_NODE_VOLUME,          /* mount root (MA-004)                      */
    TK_NODE_ROOT,            /* "Disks"                                  */
    TK_NODE_TRASH, TK_NODE_DESKTOP,
    TK_NODE_QUERY, TK_NODE_QUERY_TEMPLATE,
    TK_NODE_VIRTUAL_DIR      /* /sys /dev /proc on a device volume       */
} tk_node_kind_t;

#define TK_CAP_READ    0x01u
#define TK_CAP_WRITE   0x02u
#define TK_CAP_DELETE  0x04u   /* only where tiku_vfs_unlink reaches     */
#define TK_CAP_RENAME  0x08u
#define TK_CAP_CREATE  0x10u   /* may hold new children (New folder)     */
#define TK_CAP_TRASH   0x20u   /* NEVER set on a device entry            */
uint32_t tk_model_caps(const tk_model_t *m);

/* /data -> TK_NODE_DIR; everything else under a device root ->
   TK_NODE_VIRTUAL_DIR.  Called once per path at model construction. */
tk_node_kind_t tk_tiku_classify(const tk_volume_t *v, const char *path);

/* Mount-root flag replaces Model's inode-identity trick (MA-004). */
static inline int tk_model_is_mount_root(const tk_model_t *m)
{ return m->kind == TK_NODE_VOLUME; }

/* ---- device I/O worker: the session is never touched from the UI ---- */

typedef struct tk_devio tk_devio_t;
tk_devio_t *tk_devio_start(tk_volume_t *v);
void        tk_devio_stop(tk_devio_t *io);
int tk_devio_list  (tk_devio_t*, const char *path, BMessenger r, int32 tok);
int tk_devio_read  (tk_devio_t*, const char *path, BMessenger r, int32 tok);
int tk_devio_write (tk_devio_t*, const char *path, const char *val,
                    BMessenger r, int32 tok);
int tk_devio_unlink(tk_devio_t*, const char *path, BMessenger r, int32 tok);
int tk_devio_df    (tk_devio_t*, BMessenger r, int32 tok);
int tk_devio_recv  (tk_devio_t*, const char *path, const char *local,
                    BMessenger r, int32 tok);   /* device -> host copy   */
int tk_devio_send  (tk_devio_t*, const char *local, const char *path,
                    BMessenger r, int32 tok);   /* host -> device copy   */

/* ---- Desktop ---- */

tk_deskwindow_t *tk_deskwindow_new(void);   /* before ReadyToRun (AW-001) */
int  tk_de
```
</details>


## Capabilities and refusals — how TikuOS capability bits become Tracker's menu enablement, drop rejection, editor read-onlyness and refusal alerts, across a merged local-POSIX + device-namespace view

**Decision.** ONE predicate, THREE verdict classes, PREDICT-for-enablement / ATTEMPT-for-effect, and NEVER a prediction that claims success.

1. **One truth source.** Every read-only test in the port goes through a single function `tk_permit_of(model, action, &permit)` implemented over a per-model backend vtable. `BPoseView::TargetVolumeIsReadOnly()` and `SelectedVolumeIsReadOnly()` survive as thin wrappers over it (`tk_target_is_readonly()`, `tk_selection_is_readonly()`) so every existing Tracker call site is untouched in shape. The local backend answers from statvfs `ST_RDONLY` + `faccessat(W_OK)` + Tracker's kind rules (`IsQuery || IsQueryTemplate || IsVirtualDirectory` before the volume test, verbatim from PoseView.cpp:8919) — local files keep FULL fidelity. The device backend answers from the manifest's `perms` and `cap` columns against the channel's held capability. FS-012's two-layer split (silent `InitCopy` + alerting `MoveSelectionInto`) collapses into this one layer, which is exactly what its TIKU note asks for.

2. **Refusal is a typed verdict, not a bool.** `tk_verdict_t` = `TK_ALLOW`, `TK_DENY_STRUCTURAL` (the object has no such operation at all — no write handler, no unlink op, no rename op), `TK_DENY_READONLY` (writable object in a read-only container/volume), `TK_DENY_KIND` (Tracker's well-known-kind refusals: root, Trash, Desktop, virtual dir), `TK_DENY_CAP` (device policy: the channel lacks `req_cap`), `TK_DENY_UNKNOWN` (metadata absent — a truncated manifest, an ls-discovered node). The bool Tracker used throws away information the device already gives us; the enum is what makes the alerts distinguishable and the UI honest.

3. **Predict for enablement.** Menu items, drop-context-menu verbs, spring-loaded menu entries, drop-target highlight, the inline editor's editability and the read-only background tint are all driven by the predicted verdict, evaluated at the same moments Tracker evaluates them (each menu open — `TShortcuts::Update`; each `UpdateDropTarget`; each `StartEdit`). Prediction is mandatory, not optional, because the TCP and BLE shell backends are `TIKU_VFS_CAP_NONE` (`tiku_shell_io_tcp.c:377`, `tiku_shell_cmd_ble.c:39`): over the transport the Tracker actually uses, EVERY node with a non-zero `req_cap` is refused. Without prediction the whole of /dev looks clickable and fails on every click.

4. **Attempt for effect, with one asymmetry.** Every command handler re-evaluates immediately before acting (Tracker does this too: enablement in `TShortcuts::Update`, then `CheckVolumeReadOnly` at execution). At that second check: `TK_DENY_STRUCTURAL` / `TK_DENY_READONLY` / `TK_DENY_KIND` abort locally with the alert — these cannot go stale. `TK_DENY_CAP` and `TK_DENY_UNKNOWN` **still attempt on the device**, because a cached capability can be stale and the device is the authority. Encoded as `TK_ATTEMPTABLE(v)`. This is the answer to "predict or attempt": predict to disable, attempt to be sure, and let prediction be wrong only in the permissive direction.

5. **A refused write shows the device's verbatim text, always.** Three tiers. (a) Predicted refusal → no alert ever; the item was disabled, exactly as Tracker never alerts from a greyed item. (b) Attempted refusal with a device answer → one modal `B_STOP_ALERT`, Cancel only, `B_CLOSE_ON_ESCAPE`, two lines: a Tracker-shaped sentence, then the device's line reproduced byte-for-byte in a monospace secondary line (`write: cannot write '/dev/led0' (EPERM)`). We never paraphrase the device — paraphrase creates a second error vocabulary that drifts from the firmware's. EPERM gets a new sentence naming the missing capability; EACCES reuses Tracker's own read-only strings where the action matches (FS-045's "Files cannot be moved or deleted from a read-only volume."); ERANGE/EINVAL are not capability matters and fall to MA-078's existing type-ladder alerts with the value uncommitted. (c) Attempted with no answer (link dropped, timeout) → `B_WARNING_ALERT` "The device did not answer. \"<name>\" may or may not have changed.", node marked stale and re-read. That third case is a failure mode Tracker does not have and must not be silently folded into a read-only alert.

6. **Mid-session capability change is pushed, not polled.** New firmware nodes `/sys/vfs/cap` (the ambient caller capability as comma tokens) and `/sys/vfs/cap_gen` (a counter bumped inside `tiku_vfs_caller_cap_set()`, which also calls `tiku_vfs_notify()` on that node so the existing `sub` push path carries it). The desktop subscribes at connect. On the event: re-read `/sys/vfs/cap`, invalidate every model's cached verdicts, and re-run the enablement pass on every open window — the same code path as a menu open, so no new logic. Downgrade demotes an active inline editor to `MakeEditable(false)/MakeSelectable(true)` with its buffer preserved (MA-048's exact path) rather than closing it; window tint and lock badges update live. Upgrade updates menus, tint and badges but does NOT promote an open editor back to editable — becoming editable mid-keystroke is a surprise, and re-opening is one click. An in-flight bulk operation is not aborted; its next per-item write returns EPERM and the operation's existing error path reports it. Any EPERM refusal ALSO triggers an immediate `/sys/vfs/cap` re-read, so a mirror that went stale without an event self-heals in one round trip.

7. **Mixed selections prune and report.** `FS-045`'s check on only the first selected item's device is a real Tracker bug that a merged local+device view would trip constantly. The permitted subset is performed and ONE summary alert lists the refusals ("3 of 7 items could not be changed"), following the shape Tracker already uses when it prunes rather than refusing wholesale (`ExcludeTrashFromSelection`, PVL-085).

8. **Concrete device answers to the operation questions.** Device FILE nodes are drop targets for a single text value only, never for files; device DIR nodes are not drop targets at all except a dynamic store (`/data`, which has `dynops.write`/`unlink`) gated on `CAP_FS`. New folder is structurally disabled everywhere in the device namespace except `/data`. Device nodes are never trashable and never renamable: Delete on a device selection reuses Tracker's OWN existing alert "…cannot be moved to the Trash. Would you like to delete it instead?" (the `entriesToDeleteOnTheSpot` path already in `MoveSelectionOrEntryToTrash`), which then unlinks only under `/data`; `CanEditName()` returns false for device nodes so F2/Cmd+E do nothing, matching how Tracker excludes root/Trash/Desktop from editing entirely rather than opening a dead editor. MA-048's selectable-not-editable editor is preserved for the VALUE column of an unwritable device node. Cut/Move-to out of the device namespace degrades to Copy for non-`/data` sources — which is precisely FS-013's existing `sourceIsReadOnly → kMoveSelectionTo becomes kCopySelectionTo` rule, reached honestly.

**Rationale.** **Why predict at all, rather than attempt-and-report everywhere.** Because the transports the desktop actually uses confer `TIKU_VFS_CAP_NONE`. A pure attempt-and-report UI over TCP would present every hardware node as writable and refuse every single click. Prediction is what makes the window truthful. Conversely, **why not predict-only**: the ambient caller cap is one global word swapped by `tiku_shell_io_set_backend()`, changeable at any moment by something the desktop did not initiate, and the manifest arrives truncated on real boards (`tiku_desk_ns_truncated()` already exists precisely because of this) — so a mirror is a cache, and a cache must never be the last word on a refusal. The asymmetry in rule 4 is the whole design: prediction may be MORE permissive than truth (harmless, produces an alert) and may never be LESS permissive on the policy axis (would forbid a legitimate action from stale data).

**Why one predicate instead of keeping Tracker's two.** `TargetVolumeIsReadOnly` and `SelectedVolumeIsReadOnly` are two different questions (container vs. objects) and both survive — but their *implementation* must not be two independent read-only tests, which is how FS-012 ended up with a paste that fails silently while a drop alerts. PVL-102 is the row that proves the shape is already per-node in Tracker itself: a query on a writable disk is a read-only destination. TikuOS just makes that the normal case rather than the exception.

**Why verbatim device text.** The firmware already has a stable, greppable error vocabulary (`tiku_vfs_strerror`: ENOENT/EACCES/EPERM/EINVAL/ERANGE) and the shell already prints `write: cannot write '<path>' (<code>)`, which `tiku_desk_ns_write()` already captures into an `err` buffer. Rewriting that into prose on the host would fork the vocabulary and make firmware-side error changes invisible. The Tracker-shaped sentence gives the user the familiar register; the verbatim line gives them (and the log) the fact.

**Why `TK_DENY_UNKNOWN` is a distinct value and not folded into ALLOW or DENY.** Folding into DENY would disable menu items for nodes discovered by `tiku_desk_ns_complete()`'s ls-walk, which carry no capability metadata — the truncated-manifest tail would become silently unusable. Folding into ALLOW would lose the ability to render "we don't know" differently (no lock badge, no tint) from "we know it's fine". It stays separate and it enables.

**Alternatives rejected.** (a) *Per-node capability probes* (a trial write to each node at load): actuates hardware, costs energy, and is exactly what the manifest exists to avoid. (b) *A host-side capability policy file* mirroring firmware policy: two policies drift; the manifest is already self-describing and is the mechanism TikuOS has. (c) *Reusing POSIX mode bits for device nodes* by synthesising `0444`/`0644`: throws away which capability is missing, which is the one piece of information the user needs to fix the problem (reconnect over console rather than TCP). (d) *Refusing whole mixed selections* (Tracker's literal FS-045 behavior): defensible for a homogeneous volume, wrong when half the selection is local files that would have succeeded.


| Tracker concept | Local files | TikuOS nodes |
|---|---|---|
| BVolume::IsReadOnly() | statvfs(2) ST_RDONLY on the containing mount, cached per st_dev and invalidated on mount/unmount | manifest perms column ('rw'/'r-'/'-w'/'--') = structural writability of the node itself |
| POSIX permission bits (write access to the object) | faccessat(AT_FDCWD, path, W_OK, AT_EACCESS) — full fidelity, unchanged | manifest cap column (req_cap) tested against /sys/vfs/cap (held caller cap): granted iff (req_cap & ~held) == 0 |
| BPoseView::TargetVolumeIsReadOnly() — drives New folder, Cut, Paste, tint | IsQuery\|\|IsQueryTemplate\|\|IsVirtualDirectory first, then volume read-only — verbatim from PoseView.cpp:8919 | tk_permit_of_target(dir, TK_ACT_CREATE): ALLOW only for a dynamic store (/data) with CAP_FS held; TK_DENY_STRUCTURAL for every static device directory |
| BPoseView::SelectedVolumeIsReadOnly() — drives Delete, Move to, Duplicate, editor editability | unchanged, including the query multi-select branch that walks each item's own parent volume | tk_permit_selection() over the whole selection, per item, per backend — never first-item-only |
| CheckVolumeReadOnly() + its B_STOP_ALERT | unchanged text and alert flags | tk_check_permit(m, act) — same alert shape, new EPERM sentence naming the missing capability, plus the device's verbatim line |
| MA-031 Permissions column ('drwxr-xr-x') | unchanged ten-character Unix mode string from ModeAttributeText::ReadValue | ten-character device access string: kind(1) + rw(2) + gate(1: '!' when capability-refused now) + required-cap token(3: 'hw ','sys','fs ','net','---') + reserved(3: '---') |
| Model::OpenNode(writable) — write-open fails on a read-only node | unchanged (open(O_RDWR) fails) | no open at all: tk_permit_of(m, TK_ACT_WRITE) is consulted before the write, and the write is a stateless `write <path> <value>` |
| Model::WriteAttr returning 0 bytes on refusal | unchanged, including the BModelWriteOpener transient promotion | tk_backend.write_value() returns -1 with err[] filled from `write: cannot write ...`; the widget's alert carries that text |
| Model::CanHandleDrops / IsDropTargetForList (MIME-based) | unchanged: directories take anything, symlinks resolve, executables check declared types | device FILE: drop target iff permit(WRITE)==ALLOW\|UNKNOWN AND the payload is a single text value; device DIR: never, except /data (dynops) with CAP_FS |
| ShowDropContextMenu enable loop (all verbs off when destination read-only; Move here off when source read-only) | unchanged | same loop, predicate is tk_permit_of_target(dest, TK_ACT_CREATE) and tk_permit_of(src, TK_ACT_MOVE_OUT); Create link here is always disabled for a device destination or source |
| TShortcuts::Update*Item enablement family | unchanged predicate set (HasSelection, SelectionIsReadOnly, TargetIsReadOnly, IsRoot/IsTrash/InTrash/IsQuery/IsVirtualDirectory, IsCurrentFocusOnTextView) | same predicates, now backed by tk_permit; additionally re-run on a /sys/vfs/cap_gen event, not only on menu open |
| ReadOnlyTint on the pose view background (PVL-076, Q-059) | unchanged, including the offscreen column-redraw tint | tint driven by tk_permit_of_target(window target, TK_ACT_CREATE\|TK_ACT_WRITE); individual unwritable rows additionally carry a lock badge |
| BTextWidget::StartEdit read-only path (MA-048) | unchanged: MakeEditable(false)/MakeSelectable(true), no B_PASTE filter, ReadOnlyTint on the Desktop | identical treatment for the VALUE column when permit(WRITE) != ALLOW; the NAME column never opens on a device node (CanEditName false) |
| EditModelName refusal ladder (MA-051/MA-054/FS-080) | unchanged: length, name-in-use, well-known-directory confirmation, localized names, read-only volume | TK_DENY_STRUCTURAL for TK_ACT_RENAME on every device node; the name-in-use test still runs directory-scoped across the MERGED local+device listing |
| FSGetTrashDir / trashability (FS-038) | unchanged per-volume Trash | never trashable; Delete routes to Tracker's existing 'cannot be moved to the Trash, delete instead?' alert, then tiku_vfs_unlink for /data children (CAP_FS) or a disabled item elsewhere |
| InitCopy destination prechecks (read-only + free space, FS-012/FS-015) | unchanged, including the +4 KB slack and the same-volume/Trash skip | read-only precheck becomes tk_permit_of_target; free-space precheck uses /data's published free bytes when present, and is skipped (with the failure moving to per-item) when absent |
| CopyFolder's st_dev mount-point skip (FS-036) | unchanged st_dev comparison | backend identity comparison: a local folder copy never descends into a device-namespace mount |
| Add-ons submenu built from loadable objects (CW-038/CW-068) | unchanged | capability-gated shell verbs discovered from the namespace once per connect; a verb whose required cap is not held is listed disabled, not hidden |
| AW-078 Permissions tab 3x3 rwx grid | unchanged, including immediate-apply and the beep-on-failure re-read | read-only required-vs-held capability grid: one row per TIKU_VFS_CAP_* bit, two columns (required by this node / held by this channel) |
| AW-005/MA-071 view state written beside the object, skipped on read-only | unchanged BFS-attribute-equivalent sidecar + diff-before-write gate | TK_ACT_SAVE_VIEWSTATE is always TK_DENY_STRUCTURAL for the device namespace; state goes to the host-side session store instead, silently |


**Firmware needed.** ~60 lines, all in kernel/vfs. (1) `tiku_vfs_cap_str()` replacing the single-token `vfs_cap_name()` so a combined mask renders "hw,sys" instead of the lossy "cap" — ~18 lines in tiku_vfs.c. (2) `vfs_cap_gen` counter + bump inside `tiku_vfs_caller_cap_set()` plus a `tiku_vfs_notify(tiku_vfs_resolve("/sys/vfs/cap_gen"))` on change, and the `tiku_vfs_caller_cap_gen()` accessor — ~12 lines in tiku_vfs.c (resolve-on-change needs no new registration API; capability changes happen once per backend swap, so the walk cost is irrelevant). (3) Two read handlers and two table entries for `/sys/vfs/cap` and `/sys/vfs/cap_gen` — ~28 lines in kernel/vfs/tree/tiku_vfs_tree_watch.c, plus bumping `TIKU_VFS_TREE_VFS_NCHILD`. (4) One-line fix to the manifest header row, which currently names four columns while every data line carries five, and `TIKU_VFS_MANIFEST_REV` 2 -> 3. Everything else — prediction, enablement, badges, tint, alerts, bulk pruning, self-healing on refusal — is host-side and uses mechanisms that already exist (`/sys/vfs/manifest`, `sub add`, `tiku_vfs_strerror`, `tiku_desk_ns_write()`'s captured error text). Older firmware without the two new nodes is handled without a special case: `tiku_desk_ns_refresh_caps()` returns 1, every device node evaluates `TK_DENY_UNKNOWN`, and the UI degrades to attempt-and-report.

**Rows governed** (49): PVL-076, PVL-085, PVL-102, PVS-035, PVS-039, PVS-043, PVS-052, PVK-036, PVK-038, PVK-042, PVK-044, PVN-071, CW-022, CW-031, CW-037, CW-040, CW-043, FS-011, FS-012, FS-013, FS-014, FS-026, FS-036, FS-038, FS-045, FS-059, FS-080, MA-010, MA-016, MA-019, MA-031, MA-044, MA-048, MA-051, MA-052, MA-054, MA-056, MA-062, MA-071, MA-078, MA-079, Q-044, Q-059, AW-005, AW-070, AW-078, TS-022, IV-043, DB-020


**Rows that will differ** (21) — these become `differs` in the contract:

- **FS-045**: Tracker tests only the FIRST selected item's device and refuses the whole command. We test every item and PRUNE: the permitted subset is performed and one summary alert reports the rest ("3 of 7 items could not be changed"). Local-only selections therefore behave identically in every homogeneous case; they diverge only for a mixed local+device selection, which Tracker cannot produce.
- **FS-012**: The silent path is removed. Tracker has two checks — InitCopy returns B_READ_ONLY_DEVICE with NO alert (a paste onto a read-only volume fails silently) while MoveSelectionInto alerts. We keep one layer: every read-only or capability refusal produces exactly one alert, including the paste path. A user who pastes onto a read-only destination now sees a dialog where Tracker showed nothing.
- **FS-038**: Device-namespace nodes are never trashable — there is no Trash on a device. Delete on a device selection reuses Tracker's own existing "…cannot be moved to the Trash. Would you like to delete it instead?" alert; answering Delete unlinks only under a dynamic store (/data, CAP_FS). For every other device node Move to Trash and Delete are disabled outright.
- **MA-051**: Device nodes are never renamable (TK_DENY_STRUCTURAL for TK_ACT_RENAME): CanEditName() returns false, so F2 and Cmd+E do nothing on them. Tracker's refusal list is a hardcoded kind list; ours is a capability/structure test, and it covers all of /sys and /dev.
- **AW-070**: The Get Info title is not editable at all for a device node — no editor opens, rather than an editor that opens and fails on commit. The row's reopen-on-rejection behavior is unreachable for device nodes; it is preserved verbatim for local files.
- **MA-048**: Preserved exactly for the VALUE column of an unwritable device node (selectable, not editable, no paste filter). Two additions Tracker has no analogue for: (a) if the channel's capability is LOWERED while an editor is open, the editor demotes to selectable-not-editable in place with its buffer preserved; (b) if the capability is RAISED mid-edit, the editor does NOT promote back to editable — it stays selectable until reopened.
- **AW-078**: For device nodes the 3x3 read/write/execute grid is replaced by a read-only two-column capability grid (required by this node / held by this channel), one row per TIKU_VFS_CAP_* bit. Nothing is settable, so the immediate-apply-and-beep-on-failure interaction the row names is absent for device nodes. Local files keep the grid, the immediate apply and the beep unchanged.
- **MA-031**: The Permissions column for a device node renders a ten-character DEVICE access string (kind, rw, capability-gate '!', required-cap token, reserved) rather than a Unix mode string. Fixed width and column feel are preserved; the character vocabulary is not the same. Local files render the real mode string unchanged.
- **PVL-102**: Writability is per-node, so one device window can contain both writable and unwritable rows — a state Tracker cannot reach. The window background tint follows the TARGET directory (the TargetVolumeIsReadOnly analogue) and individual unwritable rows carry a lock badge, which Tracker has no equivalent of.
- **PVL-076**: The tint is additionally re-evaluated on a /sys/vfs/cap_gen notification, so a window can become tinted mid-session without any user action. Tracker's tint is fixed for the life of a window.
- **MA-016**: A device FILE node is a drop target only for a single text VALUE, never for files; a device DIR node is not a drop target at all except a dynamic store (/data) gated on CAP_FS. Tracker's directories-take-anything rule does not carry over to the device namespace, because a static namespace directory cannot gain children.
- **FS-015**: There is no free-space precheck for a device destination unless the store publishes free bytes (/data). Where it is absent the check is skipped, so the failure mode moves from a pre-copy dialog to a per-item error alert mid-operation.
- **CW-022**: No Mount/Unmount for device rows — the mount analogue is connect/disconnect on the session, and Unmount is disabled for every device selection. Trash items are absent for device nodes (Delete replaces Move to Trash, per FS-038 above).
- **PVS-043**: Tracker has exactly four refusal alerts in MoveSelectionInto; we have six: the four unchanged, plus a capability refusal ("…requires the 'hw' capability, which this connection does not hold.") and a no-answer warning ("The device did not answer…"). Every device-originated alert additionally reproduces the device's line verbatim on a second, monospace line.
- **FS-059**: Paste into the device-namespace mount row is refused with an alert of the same shape as the Disks-root refusal but reworded for a device ("You must drop items on a writable node."), because "disk icons in the Disks window" names nothing that exists there.
- **FS-011**: Link creation is disabled up front (the menu item greys) for a device destination AND for a device source, rather than failing at execution with "The target disk does not support creating links." The execution-time alert remains for local filesystems that cannot store symlinks.
- **MA-019**: The "write refused, report 0 bytes" contract becomes "write refused, report a status code and the device's verbatim text". The widget still shows its error alert at exactly the same point; the alert body now names the reason instead of being generic.
- **CW-031**: Enablement is recomputed on each menu open as Tracker does, AND on every /sys/vfs/cap_gen notification. One deliberate over-permissiveness: nodes whose capability metadata is unknown (truncated manifest, ls-discovered) evaluate TK_DENY_UNKNOWN and their menu items stay ENABLED — the action is attempted and may then fail with an alert. Disabling them would silently make the truncated tail of a large namespace unusable.
- **DB-020**: The force-terminate gesture is gated on a capability bit in addition to the Cmd+Ctrl+Shift modifier combo, so it is unavailable over a CAP_NONE channel. Tracker gates it on the modifiers alone.
- **AW-005**: For device windows the fallback path becomes the only path: view state is never written beside the object (TK_ACT_SAVE_VIEWSTATE is structurally denied) and always goes to the host-side session store. No alert is shown, matching Tracker's silence in this case, but the "barred from writing state on close" flag is set unconditionally rather than conditionally.
- **MA-052**: A device mount has no user-settable label today, so renaming the mount row is disabled rather than renaming a volume. If durable mounts later carry a label, this becomes a normal write to that label node under the same capability rules.


**Risks.**

- Manifest truncation is real and already handled by tiku_desk_ns_truncated()/_complete(): the ls-walked tail carries no capability metadata, so those nodes evaluate TK_DENY_UNKNOWN and stay enabled. The user sees alerts on nodes that a complete manifest would have greyed. Mitigation is honest, not silent, but a large namespace over a small read buffer will feel noisier than a local folder.
- The caller capability is one ambient global word for the whole device (tiku_shell_io.c syncs it to the active backend). Two Tracker windows on one session necessarily share it, which is correct — but a second, differently-trusted session to the same board will legitimately show different enablement for the same node, and that will look like a bug to a user with both windows open. The Get Info capability grid is the only place that explains it.
- Over TCP and BLE the shell backends confer TIKU_VFS_CAP_NONE, so on the transport the desktop actually uses, every node with a non-zero req_cap is refused. The correct-and-honest UI is therefore a device window where most of /dev is greyed with a lock badge. That is the truth, but it will be read as "the app is broken" until there is a way to raise a remote channel's capability (authentication), which this decision deliberately does not invent.
- cap_gen notification depends on tiku_vfs_notify() reaching a subscriber through the existing `sub` path. If the desktop's watch slot is dropped by rules_rearm()'s unwatch_all() (a documented hazard for subsystems sharing the shell process as subscriber), the push silently stops. The EPERM-triggers-cap-re-read rule is the backstop, but between a silent cap drop and the next refused write the UI is optimistic.
- Verbatim device text is not localized and will appear untranslated inside otherwise-translated alerts. This is deliberate — the device's vocabulary is the authority — but it is a visible inconsistency in a non-English UI.
- Until firmware rev 3 lands, a node with a combined capability mask renders as the single token "cap" in the manifest. Those nodes must be treated as TK_DENY_UNKNOWN (attempt-and-report) rather than guessed at; if that mapping is forgotten they will be wrongly greyed or wrongly enabled.
- The prune-and-report rule for mixed selections means a partially-completed bulk operation is now a normal outcome rather than an exception. Undo records must be written per item as the operation proceeds, or a partial failure leaves an un-undoable half-state — Tracker's undo model assumes the operation ran or did not.


<details><summary>Proposed C</summary>

```c
/* ─────────────────────────────────────────────────────────────────────────
 * applications/tracker-new/src/tk_permit.h  — the single refusal predicate
 * ───────────────────────────────────────────────────────────────────────── */

typedef enum {
    TK_ACT_OPEN = 0,        /* Open / cat                                    */
    TK_ACT_WRITE,           /* edit value, paste a value in, drop a value    */
    TK_ACT_RENAME,          /* Edit name (Cmd+E / F2 / slow second click)    */
    TK_ACT_CREATE,          /* New folder, New from template, drop-in, Paste */
    TK_ACT_DELETE,          /* Shift+Delete / Delete inside Trash            */
    TK_ACT_TRASH,           /* Move to Trash                                 */
    TK_ACT_MOVE_OUT,        /* Cut, Move to (source side)                    */
    TK_ACT_LINK,            /* Create link / Create relative link            */
    TK_ACT_SAVE_VIEWSTATE,  /* write columns/view state beside the object    */
    TK_ACT_COUNT
} tk_action_t;

typedef enum {
    TK_ALLOW = 0,
    TK_DENY_STRUCTURAL,  /* no such operation exists on this object at all   */
    TK_DENY_READONLY,    /* writable object, read-only container/volume      */
    TK_DENY_KIND,        /* root / Trash / Desktop / query / virtual dir     */
    TK_DENY_CAP,         /* device policy: channel lacks the required cap    */
    TK_DENY_UNKNOWN      /* metadata absent (truncated manifest, ls-walked)  */
} tk_verdict_t;

/* Disable the menu item for anything denied EXCEPT unknown. */
#define TK_DENIED(v)       ((v) != TK_ALLOW && (v) != TK_DENY_UNKNOWN)
/* May still be tried against the device: only the policy axis can go stale. */
#define TK_ATTEMPTABLE(v)  ((v) == TK_ALLOW || (v) == TK_DENY_UNKNOWN \
                                            || (v) == TK_DENY_CAP)

typedef struct {
    tk_verdict_t verdict;
    uint8_t      missing_cap;      /* TIKU_VFS_CAP_* bits not held; 0 if n/a */
    const char  *alert;            /* Tracker-shaped sentence; never NULL    */
    char         device_text[160]; /* device's verbatim line; "" if none     */
} tk_permit_t;

void tk_permit_of       (const tk_model_t *m,   tk_action_t a, tk_permit_t *out);
void tk_permit_of_target(const tk_model_t *dir, tk_action_t a, tk_permit_t *out);

/* Selection form: fills refused[] with the models that must be pruned.
 * Returns the count that MAY proceed. */
int  tk_permit_selection(const tk_pose_view_t *v, tk_action_t a,
                         const tk_model_t **refused, int max_refused,
                         int *n_refused);

/* Tracker-shaped wrappers — the only predicates the ported call sites use.
 * Same names, same meaning, same call sites as Shortcuts.cpp/PoseView.cpp. */
bool tk_target_is_readonly   (const tk_pose_view_t *v);   /* == TargetVolumeIsReadOnly   */
bool tk_selection_is_readonly(const tk_pose_view_t *v);   /* == SelectedVolumeIsReadOnly */
bool tk_can_edit_name        (const tk_pose_view_t *v);   /* == CanEditName              */
/* Execution-time gate: alerts and returns false when the action must not run.
 * The direct analogue of PoseView.cpp's static CheckVolumeReadOnly(). */
bool tk_check_permit(const tk_model_t *m, tk_action_t a);

/* Alerts. Tier (a) never calls these — a disabled item does not alert. */
int  tk_refusal_alert(const tk_permit_t *p, tk_action_t a, const char *name);
int  tk_refusal_alert_bulk(const tk_permit_t *worst, tk_action_t a,
                           int refused, int total);   /* "3 of 7 items…"     */
int  tk_no_answer_alert(const char *name);            /* link dropped        */

/* ─────────────────────────────────────────────────────────────────────────
 * applications/tracker-new/src/tk_backend.h — where a verdict comes from
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct tk_backend {
    tk_verdict_t (*permit)(void *ctx, const tk_model_t *m, tk_action_t a,
                           uint8_t *missing_cap);
    int          (*write_value)(void *ctx, const tk_model_t *m,
                                const char *val, size_t len,
                                char *err, size_t errmax);   /* 0 ok, -1 no  */
    int          (*unlink)(void *ctx, const tk_model_t *m,
                           char *err, size_t errmax);
    unsigned     (*caps_held)(void *ctx);        /* TIKU_VFS_CAP_* mask      */
    unsigned     (*cap_generation)(void *ctx);   /* bumps on any cap change  */
    const char  *(*cap_name)(void *ctx, uint8_t bit);  /* "hw","sys","fs"…   */
} tk_backend_t;

extern const tk_backend_t tk_backend_posix;  /* statvfs + faccessat + kinds  */
extern const tk_backend_t tk_backend_tiku;   /* manifest + /sys/vfs/cap      */

/* Fields added to the model (Tracker's Model equivalent). */
struct tk_model {
    /* … existing Tracker fields … */
    const tk_backend_t *backend;
    void               *backend_ctx;   /* tiku_desk_ns_t* for device nodes   */
    unsigned            perm;          /* TIKU_DESK_P_READ | _P_WRITE        */
    uint8_t             req_cap;       /* device only; 0 = open to anyone    */
    uint8_t             cap_known;     /* 0 when the manifest was truncated  */
    unsigned            permit_gen;    /* cap_generation() at last evaluation*/
    tk_verdict_t        cached[TK_ACT_COUNT];
};

/* Device access string for the Permissions column (MA-031 slot).
 * 10 chars: kind(1) rw(2) gate(1) cap(3) reserved(3). */
void tk_access_string(const tk_model_t *m, char out[11]);

/* ─────────────────────────────────────────────────────────────────────────
 * applications/desktop/src/tiku_desk_ns.h — additions (host side)
 * ───────────────────────────────────────────────────────────────────────── */

/* tiku_desk_node_t gains: */
unsigned req_cap;    /* cap column parsed to TIKU_VFS_CAP_* bits            */
int      cap_known;  /* 0 for ls-discovered nodes (truncated manifest tail) */

unsigned tiku_desk_ns_caps_held     (const tiku_desk_ns_t *ns);
unsigned tiku_desk_ns_cap_gene
```
</details>


## Queries over a namespace — the predicate language, live evaluation, and the saved query file (Tracker's crown jewel)

**Decision.** ONE predicate AST, evaluated HOST-SIDE against TWO indexes, with liveness GRADED BY THE DESCRIPTOR — and the query stays a file you open.

1. PREDICATE. All three Find modes (Q-002) compile to one bounded AST, `tiku_trk_pred_t`. By-name compiles to `name contains <t>`, or `name glob <t>` when the text contains '*' (Q-003's exact rule, only '*' triggers pattern mode). By-attribute compiles rows to terms combined by the per-row And/Or (Q-005) with Tracker's typed value parse kept verbatim, K/M/G suffixes and the `value %= 2` bool quirk included (Q-008). By-formula is the AST's own concrete syntax — so Q-011's "prefill the formula from the rows" is literally `tiku_trk_pred_print()`, and the conversion stays one-way as the source has it. The grammar is ours, not BFS's: `name contains log and (value > 30 or unit is celsius) and cap is-not hw`.

2. THE INDEX. Device: `/sys/vfs/manifest` IS the attribute index — one read gives path, kind, perms, descriptor (vtype/unit/fresh/cost/range) and capability for every node. Mirrored by `tiku_desk_ns_t`, this is a complete, in-memory, queryable index of the whole namespace — the thing BFS has and Linux lacks. Structural predicates (name, path, kind, vtype, unit, cap, perm, range) are answered from the mirror with zero device traffic. Only `value` terms cost a `cat`, and they are evaluated LAST, cost-ordered by `desc.ecost`, against only the candidates that survived the free terms, under a per-pass `value_budget`. Local: no index, so a streamed recursive walk (Tracker's TQueryWalker shape) on the task loop, 64 entries per slice; identical predicate, identical results, worse latency — degradation is latency and liveness, never semantics.

3. LIVENESS — the crown jewel. TikuOS watch says only "touched, re-read" with no opcode and 8 slots, so the HOST manufactures the missing match/unmatch events: on any change notice, `tiku_trk_query_on_change(q, path)` re-runs the predicate for that one node and emits ENTER / LEAVE / UPDATE. Which nodes get watched is decided by the descriptor, and this is the part that is better than BFS:
   - `fresh=static` → evaluated once, never re-checked (a whole class of nodes costs nothing to keep live);
   - `fresh=cached` → event-armed: one `sub` subscription, rings on writes and on driver `tiku_vfs_notify()`;
   - `fresh=live` → interval-polled, because sampling costs energy; the interval comes from `desc.ecost` (free/cheap 1 s, periph 5 s, bus 15 s). No event exists for a read-on-demand ADC and pretending otherwise would be a lie.
   Watch slots are budgeted against `/sys/watch/free` with 2 slots always left for the user's own `watch`/rules; arms beyond budget fall back to polling per node. Local: inotify on every directory in scope, IN_CREATE/DELETE/MOVED/ATTRIB/CLOSE_WRITE feeding the same `on_change`; over the watch budget or on IN_Q_OVERFLOW, those subtrees poll (30 s) or trigger a full Refresh. Every result row knows whether it is pushed, polled or static, and `tiku_trk_query_liveness()` reports the totals to the count view — degradation is displayed, not hidden.
   Both backends and the inotify/`sub`/synthesized-query notices converge on ONE `tiku_trk_notice_t` fed to one pose-view entry point, preserving PVN-001. A LEAVE never closes the window; only removal of the query file does (PVN-017).

4. THE QUERY IS A FILE. Keep Tracker's indirection whole — running a search writes a record and asks the app to open it (Q-023), because Q-025/Q-026/Q-028/Q-029/Q-045/CW-033/CW-067/MA-053/PVL-102 all hang off "a query is a thing on disk". The record is a small text file `<name>.query` / `<name>.qtemplate` with a `#tikuquery 1` magic first line (kind from content, MA-002's shape), NOT xattrs — because it must be byte-identical on a local filesystem and in the device's `/data` store, which has no attributes at all. It carries Tracker's two parallel representations unchanged: the executable `pred=` string (all the results window reads) plus the editable UI keys `mode=/initstr=/class=/attr.N.*/scope.N=/exclude.N=/moreopts=/dynamicdate=/temporary=/lastchange=/recent=1/focus=`. Queries live in `$XDG_DATA_HOME/tracker/queries` (fallback `~/queries`), and a device-scoped query may also be saved to `/data/queries/<name>.query` on the board itself — that is Q-034's "save to a path" answer. Templates and Recent are found through that directory plus a `queries/.index` sidecar rebuilt on mismatch, replacing BFS's self-referential `_trk/recentQuery` bootstrap with an explicit registry (the row's own TIKU note asks for exactly this).

5. SCOPE. "All disks" becomes "Everywhere"; a scope is a local subtree, a connected device, or a device subtree (Q-017/Q-019). Folder filters stay path-prefix tests applied in BOTH places Tracker applies them — inside the iterator so no pose is created, and again in the ref filter so live arrivals are filtered (Q-020). Device identity for reopening a saved query is `/sys/device/uid` (exact = accept), falling back to name+mcu+version scoring, the direct analogue of MatchArchivedVolume (Q-027). A device disconnecting fires SCOPE_GONE, tears down its runner and releases its watch slots (Q-046) — the leak that row exists to prevent.

6. WHAT WE REFUSE TO DO. No query engine in firmware. The manifest already exports the index; putting predicate evaluation on a microwatt MCU costs code and energy for zero fidelity gain, and the host already owns the regex engine. Rules/`on` are not hijacked as a match mechanism either (4 slots, 24-char paths, and they belong to the user).

**Rationale.** Host-side evaluation over a mirrored manifest is the only option that makes the device backend STRICTLY better than BFS rather than a cripple: the manifest is a complete typed index of a namespace small enough to hold entirely, which is exactly what Tracker's queries assume and what Linux cannot offer. Evaluating on the device was rejected — it needs a predicate parser, a matcher and a result channel in firmware (many hundreds of lines on the part with the least room), and it would still have to stream results over the same shell link, so it buys nothing but energy cost on the wrong side of the wire. Watching every match was rejected because 8 shared slots make it fail at the fourth result and leak slots on window close; watching the query's DEPENDENCY set graded by `desc.fresh` instead costs zero slots for static nodes, one slot for nodes that ring, and admits honestly that a read-on-demand sensor has no event to offer. Grading by `desc.ecost` is the reason this is not just a port: BFS liveness is free because the FS already knows; ours costs energy, so the descriptor fields TikuOS already carries become the scheduling policy, and the cost is shown rather than spent silently. Keeping the query as a file (and as a text record rather than xattrs) is what lets a single format work on ext4, on the device's `/data`, and in a wasm build, and it preserves the dozen rows that treat a query as an openable, editable, renameable, transient-GC-able object; collapsing it to "a command that prints results" would have quietly killed Q-025/Q-026/Q-028/Q-029/Q-037/Q-045/CW-033/CW-067/MA-053 at once. For local files the honest answer is that there is no index and we do not pretend there is one: same predicate, walked instead of looked up, with the scan state and the polled-subtree count on screen. Inventing a background indexer here would be a second product hiding inside this decision.


| Tracker concept | Local files | TikuOS nodes |
|---|---|---|
| BQuery predicate built by PushAttr/PushString/PushOp | tiku_trk_pred_t AST compiled from the panel; evaluated per entry | the same AST, same evaluator |
| BFS attribute index (what makes a query instant) | NONE — streamed recursive walk, 64 entries per task slice | /sys/vfs/manifest mirrored into tiku_desk_ns_t = a full typed index |
| MIME attribute registry (BMimeType::GetAttrInfo) feeding the attribute menu | stat fields (name/size/last_modified) + xattr user.* enumeration | manifest columns + descriptor fields: name path kind value vtype unit fresh cost cap perm min max |
| MIME type ANDed onto every query; supertype widened with "/*" | MIME kept (xdg/extension); "image" -> "image/*" unchanged | node class token kind[/vtype][:unit]; '*' widening is a mask test |
| B_QUERY_WATCH_ALL — kernel pushes match/unmatch | inotify on scope dirs; host re-runs predicate for the changed entry | sub (watch) for fresh=cached, ecost-derived interval poll for fresh=live, nothing for fresh=static |
| Query file in ~/queries carrying attributes | $XDG_DATA_HOME/tracker/queries/<name>.query, text record, magic first line | same record writable to /data/queries/<name>.query via the dynops store |
| kAttrQueryVolume + MatchArchivedVolume 5-field score | filesystem UUID + mount point | /sys/device/uid exact; fallback score over name+mcu+version |
| QueryRefFilter Trash exclusion | XDG trash dir prefix in the generic exclude list | no trash concept; the exclude-subtree hook remains |
| Templates/Recent menus built by querying _trk/recentQuery + InstallIndices | queries dir scan + queries/.index sidecar (path, kind, lastchange) | device-saved queries listed from the same index after an ls of /data/queries |
| TQueryWalker skipping volumes without KnowsQuery | skip mounts that cannot be read / are excluded | skip nodes without read perm and nodes flagged DF_SECRET |
| Location column (kAttrPath) — what makes results usable | absolute path | device display name + absolute VFS path |
| Size column from st_size | st_size unchanged | undefined -> "-", unless a descriptor declares a payload length |
| Dynamic dates re-evaluated at next midnight/hour via RunLater | host wall clock + tiku_trk_loop delayed task; full behavior | no time-valued attribute, so a device-only query never sets the flag |
| Delete-the-file-to-close-the-window + 5000x1ms spin | delete kept; the spin becomes an event wait on the window registry | same, plus the /data unlink path for device-stored queries |


**Firmware needed.** One optional new shell command, `sub` — ~220 lines total. It is NOT required for correctness: every part of this design runs today over `cat /sys/vfs/manifest`, `cat`, `ls`, `tree`, with the poll fallback `tiku_desk_ns` already implements and ships. What `sub` buys is push liveness for `fresh=cached` nodes (sub-second query updates instead of a 1 s poll floor, and far fewer link round-trips on an idle device).

Breakdown: `kernel/shell/commands/tiku_shell_cmd_sub.c` ~150 lines — `sub add <path>` / `sub del <path>` / `sub list` wrapping `tiku_vfs_watch()`/`tiku_vfs_unwatch()` with the shell process as subscriber, plus its own 8-entry node-pointer array (~32 B SRAM) so `sub`'s subscriptions are distinguishable from those the `watch` mode and the rules engine make on the same process; `tiku_shell_cmd_sub_on_vfs(node)` prints `~<path>\r\n` via `tiku_vfs_path_of()` when a rung node is in that array. Header ~40 lines. Wiring ~25 lines: command-table entry in `tiku_shell.c`, `TIKU_SHELL_CMD_SUB` flag in `tiku_shell_config.h`, the `.c` in the Makefile shell block, and one call added beside the existing `tiku_shell_cmd_watch_on_vfs()` / `tiku_shell_rules_on_vfs()` dispatch on `TIKU_EVENT_VFS`. It must also survive `rules_rearm()`'s wholesale `tiku_vfs_unwatch_all()` by re-subscribing idempotently, the same self-heal the `watch` mode already does.

Explicitly NOT built in firmware: any predicate parser, matcher, or result channel. Also considered and rejected: a paged/offset manifest read to fix truncation — the existing `tree` walk plus per-node probing covers it host-side, and a second manifest surface would be a new format to keep in sync.

**Rows governed** (69): Q-002, Q-003, Q-004, Q-005, Q-006, Q-007, Q-008, Q-009, Q-010, Q-011, Q-013, Q-015, Q-016, Q-017, Q-018, Q-019, Q-020, Q-021, Q-022, Q-023, Q-024, Q-025, Q-026, Q-027, Q-028, Q-029, Q-030, Q-031, Q-032, Q-033, Q-034, Q-035, Q-036, Q-037, Q-038, Q-039, Q-040, Q-041, Q-042, Q-044, Q-045, Q-046, Q-047, Q-048, Q-049, Q-050, Q-051, Q-052, Q-053, Q-055, Q-056, Q-057, Q-058, Q-059, PVN-001, PVN-011, PVN-017, PVN-028, PVN-039, PVN-071 …


**Rows that will differ** (28) — these become `differs` in the contract:

- **Q-004**: For local files the MIME constraint is unchanged (type from xdg/extension, supertype widened with "/*"). For device scopes there is no MIME type: the constraint is a node-class token `kind[/vtype][:unit]` (e.g. `file/i32`, `file/*:celsius`, `dir/*`, `any`) tested as a mask against the manifest's kind + descriptor. 'All files and folders' becomes 'Anything' and, as in the source, pushes no constraint at all.
- **Q-006**: The three built-in by-attribute rows are Name / Size / Modified only when the scope contains a local volume. For a device-only scope the built-in rows are Name (contains/is/is-not/starts-with/ends-with), Kind (is/is-not), Value (is/greater-than/less-than, only on nodes whose descriptor declares a numeric vtype) and Capability (is/is-not) — because device nodes have neither a size nor a modification time. First item and first operator are still pre-marked.
- **Q-007**: Same behavior, different registry: the extra attributes come from the manifest's descriptor columns for the selected node class (vtype, unit, fresh, cost, cap, perm, min/max) instead of BMimeType::GetAttrInfo, and for local files from the union of xattr names seen on the selected type. The source's index-buggy `attr:viewable` gate (FindPanel.cpp, no index argument) is NOT reproduced — every declared attribute is offered.
- **Q-009**: Fully implemented for local files (the host has a wall clock and a relative-date parser). A device-only query can never set the dynamic-date flag, because no device attribute carries a time; the flag is still stored and honoured in the record so a mixed-scope query behaves as the source does.
- **Q-010**: By-formula accepts OUR predicate grammar (the AST's serialization), not the BFS query language. A BFS predicate string pasted in is rejected with a named parse error rather than silently mismatching. The source's silent truncation at 1023 characters is replaced by a refusal with a 'formula too long' error.
- **Q-015**: The type menu keeps its shape (All first, then most-used, then a tree of supertypes with subtypes, empty supertypes removed) but for device scopes its contents are node classes derived from the manifest rather than installed MIME types; the two-subtypes-sharing-a-description ' (subtype)' disambiguation has no device analogue and applies to local MIME entries only.
- **Q-016**: The decaying most-used list is kept with the same rules (count+name lines, sorted by count then case-insensitive name, decrement on write, clamp 20, drop below -10, never the first) but lives in the tracker settings directory as one file covering both MIME types and device node classes.
- **Q-017**: There are no volumes. 'All disks' becomes 'Everywhere'; the menu lists local mounts, connected devices (by /sys/device/name) and added subtrees. The source's known defect that the menu is not rebuilt when a volume mounts later IS fixed here: device connect/disconnect rebuilds the scope menu, because a device appearing mid-session is the normal case rather than the rare one.
- **Q-023**: Same indirection (write the record, ask the app to open it, results in a separate window), but the store is $XDG_DATA_HOME/tracker/queries (falling back to ~/queries) rather than B_USER_DIRECTORY/queries, the mode is 0755 rather than 0777, and a device-scoped query may additionally be saved into /data/queries on the board.
- **Q-024**: Auto-generated names follow the same recipe ('Name = <text>' / 'Formula <text>' / '<class>: <attr> <op> <value>, …' + strftime ' - %b %d, %I:%M:%S %p'), but the truncation limit is the target store's: 255 bytes minus the '.query' suffix locally, and the TFS name limit when saving to a device. '/' is still replaced by '_'.
- **Q-025**: Delete-to-close is kept, but the 5000-iteration 1 ms spin is replaced by an event wait on the window registry with no fixed bound and no polling — the row's own TIKU note asks for this. Consequence: a window that refuses to close blocks the re-run indefinitely instead of proceeding after 5 s; the wait is cancellable.
- **Q-026**: All restored state is identical, but it is read from named keys in the query record file rather than from per-file attributes (mode, initstr, class, attr.N.*, scope.N, exclude.N, moreopts, focus, focus-selection). Text-selection restore applies in formula mode only, as in the source.
- **Q-027**: Volume identity becomes store identity: a device is matched on /sys/device/uid (exact match accepted outright), falling back to a weighted score over name + mcu + version; a local mount is matched on filesystem UUID then mount point. A scope whose store is absent this session is skipped and retained in the record, exactly as the source keeps unmounted volumes.
- **Q-031**: Templates are discovered by scanning the queries directory (plus /data/queries on connected devices) against a queries/.index sidecar, not by running a `_trk/recentQuery == 1` query on every volume. No filesystem indices are installed at boot. De-duplication by resolved path and the Exists() re-check are kept.
- **Q-032**: Same 10-most-recent, most-recent-first, templates-above-queries-when-requested behavior, sourced from the queries/.index sidecar sorted by lastchange descending; de-duplication is by path (the source de-duplicates by ref+changeTime because BQuery can return a file twice, which cannot happen with a directory scan).
- **Q-033**: The change stamp and the recent marker are `lastchange=` and `recent=1` keys inside the record instead of the attributes _trk/qrylastchange and _trk/recentQuery, and nothing corresponding to TTracker::InstallIndices runs at boot — the index sidecar takes its place.
- **Q-036**: 'Include Trash' is offered only when the scope contains a local volume; device scopes have no trash. The generic exclude-subtree list it rides on is always present and is what implements the option. A device-only query neither shows nor stores the option.
- **Q-037**: Transient-query GC is kept in full (30 min after start, 5 min idle, 100-entry batches, only temporary queries not open in a window), but the age test uses a real 7 x 86400 second comparison instead of the source's tm_mday-only arithmetic, which was day-of-month arithmetic rather than a week.
- **Q-038**: THE headline difference, stated in full. There is no kernel-side match/unmatch event on either backend. Local: inotify on every directory in scope, and the host re-runs the predicate for the changed entry to produce enter/leave/update. Device: liveness is graded by the node's descriptor — fresh=static nodes are evaluated once and never re-checked; fresh=cached nodes are event-armed with one watch subscription each (requires the new `sub` firmware, else they poll); fresh=live nodes are polled at an interval derived from desc.ecost (free/cheap 1 s, periph 5 s, bus 15 s) because sampling costs energy and no event exists. The device watch table is 8 slots shared with the user's `watch` and rules, so at most (free - 2) nodes per device are event-armed and the remainder poll. Every row therefore updates within its own latency class rather than instantly, and tiku_trk_query_liveness() reports pushed/polled/static counts, the poll interval and the value-read count to the count view so the degradation is visible.
- **Q-039**: One search per volume becomes one runner per scope, and a single result list may union LOCAL and DEVICE results in one window — something the original never had to reconcile. CountEntries() still returns 0 (total unknown in advance) and rewind still rewinds every runner.
- **Q-042**: Default columns are unchanged (Name 145 / Location 225 / Size 80 / Modified 150). For device results the Size and Modified cells render '-' (undefined, per MA-024) — so a device-only query ships a per-search-type default template for class 'tiku/node' with Name / Location / Value / Kind, which is the row's own per-type template mechanism rather than a new one. The Location cell is the device display name plus the absolute VFS path.
- **Q-046**: Same teardown, wider trigger: live searches bound to a store are torn down and their watch slots released not only on an explicit unmount/disconnect but also on transport loss (link timeout), because a serial or TCP session can vanish without a clean unmount. Windows are notified with SCOPE_GONE and close as the source's CloseActiveQueryWindows does.
- **Q-050**: The Spencer regex engine is ported in full (no glob-only fallback; the query engine runs on the host, not the MCU), but the case-insensitivity implementation is deliberately fixed rather than reproduced: matching folds per character instead of lowercasing both pattern and text, so uppercase character classes keep working in ignore-case mode. This is the deviation the row's TIKU note asks for.
- **Q-053**: Not reproduced — a genuine defect in the original. Choosing Query > 'Open…' does NOT toggle the 'Include Trash' option; the missing `break` is not carried over. Recorded here so a fidelity diff reads as intentional.
- **MA-001**: A device namespace node is a new Model kind alongside file and directory, taken from the manifest's kind column plus the descriptor, not derived from a stat mode. A query itself is still its own kind, but determined by the record's magic first line and extension rather than by BEOS:TYPE.
- **MA-024**: Device nodes report an undefined size, so the Size column shows '-' — matching the row's rule for folders and queries — unless the node's descriptor declares a payload length, in which case that is shown.
- **PVN-001**: The single-handler property is preserved, but the notice carrying an opcode is manufactured by the query engine rather than delivered by the kernel: inotify events and device `~path` notices are both turned into ENTER/LEAVE/UPDATE by re-running the predicate for the named node, then fed to the same pose-view entry point as folder-window notices.
- **PVN-039**: Preserved for local results (size/last_modified changes refresh the row as stat changes). For device results the analogue is a value change on the node, which refreshes the Value column by the same path; there are no size or modification-time cells to refresh.


**Risks.**

- Manifest truncation is the top correctness risk: `cat /sys/vfs/manifest` renders into the device's read buffer and a large tree arrives cut mid-line, which would silently make query results INCOMPLETE rather than wrong. Mitigation: the mirror's existing truncation flag drives a `tree`/`ls` completion walk, nodes discovered that way carry no descriptor so predicates over descriptor fields evaluate UNKNOWN and are resolved by an explicit per-node probe instead of being excluded, and qlive.index_partial is surfaced in the results window. This must be tested against a board whose tree overflows the buffer, not only against one that fits.
- Energy: a careless value predicate (`value > 0` with no structural term) becomes a continuous ADC/I2C load across every live node on the device — the exact failure the descriptors exist to prevent. Mitigation is cost-ordered planning, per-pass value_budget, ecost-derived poll intervals and a warning when a plan's cost class is PERIPH/BUS over many nodes; but nothing stops a user from confirming it, so the value_reads counter must stay visible.
- Watch-slot starvation: 8 device slots shared with the user's `watch` mode and the rules engine, and rules_rearm() drops every subscription wholesale via unwatch_all(). Query subscriptions must re-subscribe idempotently every tick like the `watch` mode does, or a rule edit silently kills every live query on that device with no error anywhere.
- Push liveness depends on firmware that does not exist yet: no `sub` command is in the tree today (only bt's unrelated `sub`), so on shipping firmware every event-armed node actually polls. The design degrades correctly, but the crown-jewel demo is a 1 s poll floor until the ~220-line command lands.
- Local walk cost: a scope of $HOME with no index means seconds-to-minutes of walking, inotify watch exhaustion against /proc/sys/fs/inotify/max_user_watches, and IN_Q_OVERFLOW storms that force a full Refresh. Bounded batches and reported degradation cover the honesty requirement, but a user who expects BFS-speed results will experience this as the feature being slow, and no amount of reporting changes that.
- Two backends in one result window make identity subtle: device nodes have no inode, so identity is (store uid, path) and a device renamed by writing /sys/device/name must not re-key existing results. Getting this wrong shows up as duplicate rows after a reconnect.
- The formula grammar is a new user-visible language and therefore a compatibility commitment from the first release; the AST serialization and the parser must round-trip exactly or saved queries will not reopen as they were saved.
- Porting RegExp.cpp brings its own limits into a host-side parser fed by user text (kSubExpressionMax, the 32767-byte program cap, no {n,m} or \d/\w). Pattern compilation must be bounded and errors named, or a pathological pattern in a saved query becomes a hang at window-open time.


<details><summary>Proposed C</summary>

```c
/* ---- applications/tracker-new/src/tiku_trk_pred.h ---- */
#define TIKU_TRK_PRED_MAX_NODES 64
#define TIKU_TRK_PRED_POOL      512     /* attr + literal text pool */

typedef enum {                       /* Q-005/Q-006/Q-047 operator set */
    TIKU_TRK_OP_CONTAINS = 0, TIKU_TRK_OP_IS, TIKU_TRK_OP_IS_NOT,
    TIKU_TRK_OP_STARTS_WITH, TIKU_TRK_OP_ENDS_WITH,
    TIKU_TRK_OP_GT, TIKU_TRK_OP_LT, TIKU_TRK_OP_GE, TIKU_TRK_OP_LE,
    TIKU_TRK_OP_BEFORE, TIKU_TRK_OP_AFTER,
    TIKU_TRK_OP_GLOB, TIKU_TRK_OP_REGEX
} tiku_trk_op_t;

typedef enum { TIKU_TRK_PN_TERM = 0, TIKU_TRK_PN_AND,
               TIKU_TRK_PN_OR,       TIKU_TRK_PN_NOT } tiku_trk_pnkind_t;

/* Evaluation cost class; the device values are tiku_vfs_ecost_t + 2 so a
 * descriptor's ecost passes straight through and orders the plan. */
typedef enum { TIKU_TRK_COST_META = 0, TIKU_TRK_COST_STAT, TIKU_TRK_COST_FREE,
               TIKU_TRK_COST_CHEAP,    TIKU_TRK_COST_PERIPH,
               TIKU_TRK_COST_BUS } tiku_trk_cost_t;

typedef struct {
    uint8_t  kind;                 /* tiku_trk_pnkind_t                  */
    uint8_t  op;                   /* tiku_trk_op_t (terms only)         */
    uint8_t  vtype;                /* RHS type: TIKU_TRK_V_*             */
    uint8_t  cost;                 /* tiku_trk_cost_t of the LHS attr    */
    int16_t  lhs, rhs;             /* child indices; -1 on a term        */
    uint16_t attr;                 /* offset into pool[] (attribute name)*/
    uint8_t  icase;                /* Q-003/Q-008 case-insensitive push  */
    uint8_t  _pad;
    union { int64_t i; uint64_t u; double f; int64_t t; uint16_t s; } val;
    void    *prog;                 /* compiled regex/glob; freed by _free*/
} tiku_trk_pnode_t;

typedef struct {
    tiku_trk_pnode_t node[TIKU_TRK_PRED_MAX_NODES];
    uint8_t  count;
    int16_t  root;
    char     pool[TIKU_TRK_PRED_POOL];
    uint16_t pool_len;
    uint8_t  max_cost;             /* highest cost class any term needs  */
    uint8_t  dynamic_date;         /* Q-009                              */
} tiku_trk_pred_t;

typedef struct { char attr[32]; uint8_t op; char text[128];
                 uint8_t combine_or; } tiku_trk_attrrow_t;   /* Q-005 row */

int  tiku_trk_pred_from_name (tiku_trk_pred_t *p, const char *text,
                              const char *class_sel);            /* Q-003/Q-004 */
int  tiku_trk_pred_from_rows (tiku_trk_pred_t *p,
                              const tiku_trk_attrrow_t *rows, int n,
                              const char *class_sel);            /* Q-005..Q-008 */
int  tiku_trk_pred_compile   (tiku_trk_pred_t *p, const char *formula,
                              char *err, size_t errmax);         /* Q-010/Q-048 */
int  tiku_trk_pred_print     (const tiku_trk_pred_t *p, char *buf, size_t max);
                                                                 /* Q-011 */
void tiku_trk_pred_free      (tiku_trk_pred_t *p);

/* Three-valued so cheap terms can decide before an expensive one is paid. */
typedef enum { TIKU_TRK_MATCH_NO = 0, TIKU_TRK_MATCH_YES,
               TIKU_TRK_MATCH_UNKNOWN } tiku_trk_match_t;

typedef struct { uint8_t kind;      /* MISSING/STR/INT/UINT/TIME/FLOAT/DEFER */
                 const char *s; int64_t i; double f; } tiku_trk_aval_t;

typedef struct tiku_trk_entry_src {
    int      (*get)(void *ctx, const char *attr, unsigned max_cost,
                    tiku_trk_aval_t *out);   /* DEFER when over max_cost */
    unsigned (*cost_of)(void *ctx, const char *attr);
    void      *ctx;
} tiku_trk_entry_src_t;

tiku_trk_match_t tiku_trk_pred_eval(const tiku_trk_pred_t *p,
                                    const tiku_trk_entry_src_t *e,
                                    unsigned max_cost);

/* Ported verbatim from TrackerString.cpp / RegExp.cpp (Q-049/Q-050/Q-051). */
#define TIKU_TRK_GLOB_MAX_BACKTRACK 100
int tiku_trk_glob_match (const char *name, const char *pat, int icase);
int tiku_trk_regex_match(const char *name, const void *prog, int icase);

/* ---- tiku_trk_query.h : the saved query and the live session ---- */
#define TIKU_TRK_QUERY_SCOPE_MAX 8

typedef enum { TIKU_TRK_SCOPE_EVERYWHERE = 0, TIKU_TRK_SCOPE_LOCAL_TREE,
               TIKU_TRK_SCOPE_DEVICE,        TIKU_TRK_SCOPE_DEVICE_TREE
} tiku_trk_scopekind_t;

typedef struct {
    uint8_t kind;
    char    path[512];             /* subtree root, "/" = whole store   */
    char    dev_uid[40];           /* /sys/device/uid   (Q-027)         */
    char    dev_name[40];          /* fallback scoring + display label  */
} tiku_trk_scope_t;

typedef struct {                   /* the on-disk record, parsed        */
    char             file[512];    /* absolute path of the .query file  */
    uint8_t          is_template;  /* Q-028                             */
    uint8_t          temporary;    /* Q-037 / MA-053                    */
    uint8_t          mode;         /* by-name / by-attr / by-formula    */
    uint8_t          search_trash; /* Q-036                             */
    uint8_t          dynamic_date; /* Q-009 / Q-040                     */
    int64_t          lastchange;   /* Q-033                             */
    char             pred[1024];   /* the EXECUTABLE predicate (Q-010)  */
    char             initstr[256]; /* by-name/formula text  (Q-026)     */
    char             class_sel[64];/* MIME label / node class           */
    tiku_trk_attrrow_t rows[8]; uint8_t nrows;
    tiku_trk_scope_t scope[TIKU_TRK_QUERY_SCOPE_MAX];   uint8_t nscope;
    char             exclude[TIKU_TRK_QUERY_SCOPE_MAX][512]; uint8_t nexclude;
} tiku_trk_qfile_t;

int tiku_trk_qfile_read (tiku_trk_qfile_t *qf, const char *path);
int tiku_trk_qfile_write(const tiku_trk_qfile_t *qf, const char *path);
int tiku_trk_qfile_default_name(const tiku_trk_qfile_t *qf,
                                char *out, size_t max);          /* Q-024 */
const char *tiku_trk_queries_dir(void);   /* XDG dir, created on demand  */

/* Result identity across two backe
```
</details>
