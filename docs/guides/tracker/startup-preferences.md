# Startup Preferences

Open **Settings > Startup Preferences…** in TikuDesktop's main menu.

- **Reopen my previous folders** restores the folders and
  their minimized state from the previous session.
- **Open default folders if none reopen** opens the
  host home folder, or `/sys/mem` and `/dev` with a direct device backend.
  Turn both folder options off to start with no folder windows.
- **Applications** lists installed applications, including Programming tools
  and trusted in-process apps. Check only those you want to launch.
- **Open nothing** clears both folder options and every application checkbox.
  Click **Save** to keep this choice.

Changes take effect on the next desktop launch. Save never starts or closes an
application now. Cancel, Escape, or closing the dialog discards pending changes.
Tab/Shift-Tab moves through enabled checkboxes and buttons. Space toggles the
focused checkbox or activates a button. Enter activates a focused button, or
saves when a checkbox is focused; Command-S always saves. The arrow buttons
page through longer application lists. Mouse buttons activate on release
inside the same control; dragging out cancels the click.
The inset app list shows six rows at a time; its footer counts selected apps
across all pages. Keyboard focus appears only while navigating with keys.
Group interiors and footer actions use shared font-aware preferences helpers.
Long application labels and status text show `...` when they do not fit,
without splitting UTF-8 characters or drawing into adjacent controls.

Until configured, the previous behavior is preserved: folders are restored,
default folders are opened if needed, and trusted apps start automatically.
Other installed apps default to off. Safe mode skips saved folder restoration
and automatic application launches; the default-folder checkbox still applies.
Explicit folder arguments on the command line always take precedence.

Choices are stored with the desktop's session settings in its existing sidecar
store. Application identities use their installation group and filename, not
discovery order or an absolute build-directory path. Only currently discovered
apps can launch; temporarily missing apps keep their saved choices. There is a
32-application limit. A full saved list or a failed save produces an error
instead of silently discarding choices. App-specific recovery windows remain
the responsibility of each selected application. Programs launched by external
scripts and the explicit `TIKU_EMBED_DEMO` development override are outside this
startup list.

Verification (from `applications`):

```sh
make build/tests/test_startup
build/tests/test_startup
```

The test uses real window controls and an in-memory settings store, with app
launches and folder opening mocked. It checks selection, cancellation, saving,
stable identities, absent apps, invalid records, paging, safe mode, folder
restoration, and a completely empty startup. It renders the dialog to
`/tmp/tiku-startup-preferences.png` without launching another desktop instance.
