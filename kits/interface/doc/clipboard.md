# Host text clipboard

The X11 host connects the kit's shared text clipboard to the surrounding
desktop's CLIPBOARD selection. Copy/cut in a shared field or an application
using the desktop clipboard service publishes text; paste imports external
text. This includes TikuBasic's existing clipboard service. Applications that
implement a separate private clipboard are not automatically migrated.

The bridge lives entirely in `tiku_x11_clipboard.c` behind `tiku_host_poll`;
there are no X11 includes in portable field models or desktop shell code.
Board applications still use the existing desktop service/protocol. No board
firmware change is needed, and no device VFS data is written by this bridge.

## Input and failure handling

Imports are asynchronous. Activation and explicit Ctrl-V request fresh text;
primary-button clicks refresh with a short 250 ms cache so a menu-driven paste
has current text too. The event that initiated the request and later native
input stay queued while selection/property replies are processed. Timers and
the desktop loop keep running. A one-second absolute deadline releases input
even if another application never replies; this is not a blocking wait loop.

Only an explicit paste or an activation holds input back; the refresh a
plain click arms runs in the background, so a selection owner that never
answers cannot still the desk. A refresh nobody asked for keeps a failure
to itself: the text that was there stands and no error is raised.

`TIKU_EVENT_CLIPBOARD` precedes the deferred input. Its `key` contains a
`TIKU_CLIPBOARD_*` result. The desktop mirrors imported text to remote sessions
before forwarding their paste event; embedded clients read the shared store.
TikuDesktop and TikuTracker show transfer errors in their status area.

Imports never take host ownership. Only a new local copy does that, with a real
X server timestamp. A local copy also cancels an outstanding import, preventing
a late response from overwriting it. Each import has its own temporary hidden
requestor window so stale replies cannot complete a later transfer. Transfer
resources are released on completion, timeout, replacement, and host shutdown.

The store remains bounded to 4095 bytes of UTF-8 plus a terminator. Complete
imports are validated atomically; oversized, malformed or embedded-NUL text is
refused. Latin-1 conversion also checks the expanded UTF-8 size. On transfer
failure the local text cache becomes empty instead of exposing stale text;
the external owner's clipboard is unchanged. Empty text pastes are no-ops in
both field APIs and TikuBasic's service path, preserving the user's selection.

The legacy field API also inserts nonempty Paste text atomically as UTF-8.
Forbidden characters or capacity overflow reject the paste without deleting the
selection; they are no longer silently filtered or truncated into another name.
`tiku_clipboard_import` itself leaves the store unchanged on failure; it is the
host bridge that deliberately invalidates stale external-cache contents.

## Protocol and limits

The bridge follows the X.Org [selection transfer conventions](https://www.x.org/releases/current/doc/xorg-docs/icccm/icccm.html#Peer-to-Peer_Communication_by_Means_of_Selections):

- Requests UTF8_STRING, falling back to STRING (ISO Latin-1).
- Receives direct properties and INCR chunks, validating type and capacity.
- Serves TARGETS, TIMESTAMP, UTF8_STRING, TEXT and representable STRING text.
- Refuses unsupported targets and unrepresentable Latin-1 conversions.
- Catches X errors from vanished requestors instead of terminating the host.

This is text-only. PRIMARY/middle-click selection, file-copy operations, images,
MULTIPLE conversions, and CLIPBOARD_MANAGER/SAVE_TARGETS persistence are not
implemented. Tracker's file clipboard remains independent. Without a clipboard
manager that has already saved the text, a local copy can disappear when its
owning desktop process exits. This is an X11 bridge, not a native Wayland,
browser, IME or accessibility implementation. The bridge runs on the UI thread;
its short X error-handler traps are not a general multithreaded Xlib facility.

## Verification

From `applications`:

```sh
make build/tests/test_clipboard_text build/tests/test_client_wake
build/tests/test_clipboard_text
build/tests/test_client_wake
make test-host-clipboard
```

The last target starts a private Xvfb display if installed, otherwise a nested
Xephyr window, and terminates only that test server afterwards. It does not use
the user's actual clipboard. Xephyr requires an existing DISPLAY; Xvfb permits
headless testing. Running `test_host_clipboard` without an explicit
`TIKU_CLIPBOARD_TEST_DISPLAY` reports SKIP, so ordinary test runs cannot silently
overwrite a user's clipboard.

Coverage includes direct UTF-8, Latin-1, multibyte characters split across INCR
chunks, exact size boundaries, conversion expansion, empty/invalid text,
unsupported formats, silent/dead peers, stale responses, no ownership echo,
independence from PRIMARY, and clipboard-before-input ordering. The client
runtime test verifies that a remote application's first paste sees the new
UTF-8 clipboard through its existing service.
