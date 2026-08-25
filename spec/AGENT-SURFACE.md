# The agent surface — plan, objective, and goalpost

Status: adopted 2026-08-24.  This document states an OBJECTIVE the
architecture has been converging on without saying so, and turns it into
staged, testable work.  It sits beside ARCHITECTURE.md and DECISIONS.md;
where they describe what is, this describes what TikuDesktop is FOR, one
level up.

## The objective, in one sentence

TikuDesktop is operable by programs on the same terms as people: every
gesture can be injected, every visible fact can be asked for by name,
over one small, stated, gated surface — and that surface is the same one
the tests, the tools, and the assistive software use, because a surface
each of them had privately would be three surfaces that drift.

## Why this is the goalpost and not a feature

The thesis of the whole project is that the desktop IS the device's
interface — applications project into it, people reach the device
through it.  An agent is simply another party at that interface, and it
arrives with the same needs a test has: address the thing, not the
pixels it occupied on one machine; ask for facts, not screenshots to
interpret; a small surface whose whole contract can be read.

The evidence that this works is not hypothetical.  The product is driven
through this surface thousands of times per suite run, and it was built
and debugged BY an agent operating it: injecting gestures, querying
titles and menus, reading screenshots only where a pixel was the actual
claim.  Every honesty discipline the harness learned — name-addressed
targets over coordinates, semantic queries over pixel-reading, mutation
tests over trust — is exactly the discipline that makes an agent
interface robust.  Tests and agents fail for the same reasons.

## Principles

These bind every stage below.  A change that violates one is wrong even
if it works.

1. **Two verbs, growing nouns.**  The conductor surface stays INJECT
   (one event, as the display would have delivered it) and QUERY (one
   named fact).  Capability grows by adding NOUNS the shell can already
   answer, never by adding verbs; everything else — waits, retries,
   expectations, plans — is composed by the driver, outside the product.

2. **Semantic before pixels.**  A fact the shell knows is answered as a
   fact: a title, a menu's rows, a selection's name.  A pixel is
   answerable too, but an agent that needs pixels to learn a fact the
   shell knew is evidence of a missing noun, and the fix is the noun.

3. **One surface.**  Tests, agents, accessibility readers and remote
   tooling share it.  The moment one of them gets a private door, the
   doors disagree; the suite is the proof the surface works, so the
   surface the suite uses must be the surface everything uses.

4. **Off by default; identity before reach.**  A device that can be
   puppeted by anything that reaches its socket is a device with a hole
   in it.  The channel is opt-in today and stays so; before it is ever
   reachable over a non-local transport it carries a named principal,
   and QUERY-only is a different grant from INJECT.

5. **The shipping binary carries no test vocabulary.**  Already true and
   enforced (tools/check_shipping.py); stays true.  The interpreter,
   the planner, the agent — all of it lives on the far end of the
   stream.

6. **Every noun lands proven.**  A new answerable fact arrives with a
   script that uses it and a mutation that shows the script would notice
   it lying.  An agent surface that can drift from the truth is worse
   than none, for the same reason a silent miss was worse than a frame.

## Where it stands (stage 0 — done)

- The conductor channel: INJECT + QUERY over the session framing, local
  socket or serial line, off unless asked for (-conduct / -conduct-tty),
  driving the SHIPPING binary from outside.
- QUERY nouns today: pixel, title, titles, status, count, menu.
- The script engine (dev builds): pointer at three levels of address
  (raw x/y, row, name), keyboard with modifiers, menu-pick, ~40
  expect- verbs; run.sh proves every script in both front-ends.
- The capability handshake: HELLO answered by WELCOME, features claimed
  before they are used, unknown types stepped over in both directions.
- The command stream: windows travel as what was DRAWN, icons as their
  definitions — the structured screen exists on the wire already.

## The stages

Each stage is a set of claims that can be written as spec rows and
proven the way everything else here is proven.  Order matters: nothing
in a later stage may be started to avoid the hard part of an earlier
one.

### Stage 1 — Nouns: the desktop answers what it knows

The poseview, the workspace and the menus already know everything an
agent asks pixels for.  Expose it:

- rows: names, count, which is selected, which columns say what;
- controls: "the control labelled X in window Y is at rect R, enabled
  or not" — the shell lays these out, it can say them;
- windows: the list, their titles, their frames, which is focused;
- alerts and errors as facts: an agent must never learn about a dialog
  by staring at pixels.

Done when: a canonical set of tasks (open a folder, select a named
entry, copy it elsewhere, change a setting, read a device value) can be
scripted with ZERO expect-pixel lines, and the scripts hold.

### Stage 2 — Events out: from polling to being told

The driver polls today.  The device session already has the idiom for
the other direction — the `~<path>` subscription line.  Carry it up:
a driver may subscribe to desktop facts (a window opened, an operation
finished, an alert appeared, a mount arrived) and is told, on the same
stream, when they change.

Done when: a script/agent can wait on "the copy finished" without a
wait-loop, and a mutation that drops the notice is caught by a test
that would otherwise hang (bounded, so it FAILS rather than hangs —
the harness's own rule).

### Stage 3 — Identity and grants

Prerequisite for any non-local reach, and for calling this an interface
rather than a harness:

- a named principal per conductor session (SO_PEERCRED locally, a token
  in the HELLO for serial/remote — the protocol slot exists);
- QUERY-only versus INJECT grants; per-session quotas beside the
  session's existing counters;
- the audit's session-identity work lands here, once, for both the
  window session and the conductor — they share the framing and should
  share the principal.

Done when: a QUERY-only credential provably cannot inject (a test that
tries), and an unnamed peer gets exactly what an unanswered HELLO gets:
nothing beyond version 1.

### Stage 4 — The structured screen as a feed

**2026-08-25: the vocabulary is now worth reading.**  Fourteen controls
record themselves as what they ARE rather than as the rectangles they are
drawn from -- button, checkbox, radio, list row, panel, raised, sunken,
icon, and now gauge, tip, text field, scrollbar, slider, tab strip, alert
icon and menu field.  An alert crosses in ten commands where it took
seventy-three, a scrollbar in one where it took twenty-five, and what a
reader gets is the noun: a field and what is typed in it, a bar and how
full it is, which tab of which names is current.

Two of those were not merely verbose but WRONG, and the measurement is
what found them: the gauge recorded a single fill and suppressed the
calls that draw its frame and its bar, so it crossed as an empty box; the
menu field's marker is stroked pixel by pixel, which the list cannot
carry, and it recorded no miss -- a window that arrived looking finished
with its marker absent.  test_dlcontrols now draws every recording
control, sends it, plays it back and compares the surfaces byte for byte,
which is the only form this stage's claim can be checked in.

**Later the same day: the reader exists.**  tiku_dl_read() walks a
flattened stream and hands back each command as the facts it carries;
tiku_dl_narrate() turns a stream into plain lines -- 'button "Discard",
the default', 'tabs: "Info" "Permissions" (current)', 'gauge at 42%' --
with the paint skipped and no surface anywhere in the call.  The proof is
test_dlcontrols' closing section: a window with an alert, tabs, a gauge,
a field and a slider is narrated from the wire alone, and a label that
never terminates stops the reader rather than sending it past the
payload's end, which is the difference between a reader and a hazard the
moment the stream comes from a wire.

What remains is COMPOSITION, and it belongs to the earlier stages:
plumbing narration to a conductor noun (stage 1's vocabulary), and the
subscription that pushes a changed window to a remote reader (stage 2).
The library half of this stage is done.

**2026-08-25, later: the noun exists, and the stage's claim is met.**
QUERY "narrate" answers with the focused window as tiku_dl_narrate()'s
plain lines; expect-narrate spells it identically in both drivers, and
narrate.script proves it three ways -- both in-process front-ends, and
the conducted lane, where the driver has never seen a pixel of the
window it hears rows, a scrollbar and a count from.  The kit owns the
composition: tiku_workspace_narrate() draws the window's content ONCE
onto a recording surface of its own -- never the shown one, the pixel
query's own rule -- and tiku_dl_say() flattens, narrates, and closes
with a confession line when the list holds marks the wire could not
carry.  The answer's text grew to 2048 for this, which is a framing
change, and so the channel version moved to 2 -- a mismatched pairing
is refused at HELLO rather than half-served.

Narrating found one more silent wrong, the same shape stage 4 keeps
finding: the folder window's kept-pixels economy handed a reader its
cache as one blit -- headers and chrome as prose, every row missing,
one mark confessed.  The rule that fixes it is now in the window and
stated here: **a recording surface is a reader asking, not a frame
being shown** -- an incremental painter paints ALL of what it shows
through drawing calls, and writes NOTHING back (no row signatures, no
damage drained, no economy counters), because a narration that updated
the bookkeeping would tell the next real frame its stale pixels are
current.  Mutations for the noun, the bypass and the confession each
fail a named check.

Still open from this stage: the stage-2 changed-window subscription
(being told, rather than asking again).



The display list is already a semantic description of a window — "a
button, here, labelled this; this art, there".  Let a permitted reader
subscribe to it as a UI TREE rather than replaying it as pixels.  This
is the accessibility story and the agent story converging on one
mechanism, which is the point of principle 3: a screen reader and an
agent want the same feed, and the wire already carries it.

Done when: an assistive reader (or an agent in reader mode) can narrate
a window it has never seen pixels of, from the stream alone.

### Stage 5 — Fleet: one agent, many devices

The namespace made devices places (/devices/board1); the conductor
makes the desktop drivable; non-blocking sessions (already the stated
next step in the storage plan) make many devices safe in one loop.
Compose them: an agent addresses a board's UI and files through one
desktop, or drives N desktops through one plan — provisioning,
diagnostics, field debugging, with the UI as the protocol and no
separate management plane to build or secure.

Done when: the canonical tasks of stage 1 run against a device mount,
unchanged, and a board that stops answering costs its own mount only.

## Non-goals, so they do not creep

- No general RPC.  If a capability wants to exist that the UI cannot
  express, the question is why the UI cannot express it.
- No pixel-first automation API.  Pixels remain answerable and remain
  last resort.
- No in-process scripting in shipping binaries, ever.
- This is the DESKTOP's surface.  Application-level scripting (the
  BMessage suite, PVK-052) stays what it is; the two meet only in that
  an agent may use both.

## Measures

Ways to know it is working, each observable:

- pixel-probe ratio in the suite falls as stage 1 lands: expect-pixel
  reserved for claims about pixels (themes, marks, fades);
- a reported bug becomes a failing script in one sitting, routinely;
- the release walkthroughs are scripts with shots, regenerated and
  therefore true;
- an agent completes the canonical tasks over the serial lane against
  real hardware — the demo that is also the product claim.
