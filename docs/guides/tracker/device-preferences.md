# Devices Preferences

Open **Settings → Devices Preferences…** in TikuDesktop's Deskbar. Choose a
connected device by name/icon, choose a setting, and press Apply (or Sync now).
Refresh devices reloads the mount list and the selected device's values.
Device connection and the default execution target remain in the Devices menu.

The classic-style panel separates the inset device list, current-value table,
and an etched editor for the selected setting. Read-only settings are labelled
explicitly. **Done** closes the panel without applying pending input; changes
still require **Apply**, **Sync now**, or **Save for restart**. The layout uses
shared font-scaled rectangles for painting and hit testing.
Current values now use the shared two-column property-table control. Unavailable
values use subdued ink; long labels and values show `...` independently without
crossing columns. Group interiors and footer actions share the same layout
helpers as Startup Preferences. Explanations wrap through the shared text kit.

The panel initially selects **CPU clock**. Its row distinguishes the running
rate from a different saved rate. Choose a frequency from the dropdown, then
press **Save for restart**; an unchanged selection disables Save. The dropdown
labels unsaved choices explicitly. Space opens the list, arrow keys choose,
and Enter closes the open list without writing. Restart the device manually
when ready. Older firmware displays an update/reconnect hint instead of a
nonfunctional control; fixed clocks and read-only connections are explained.

CPU and cryptography use the shared dropdown control: Escape cancels an open
list, Tab dismisses it and moves focus, and clicking outside dismisses without
changing the selection. Selecting an option never writes to the device.
Tab/Shift-Tab visits enabled devices, settings, editors and buttons. Space
activates the focused row or button. Text fields support pointer selection,
Shift-arrow selection, Home/End, Command-A/C/X/V, and Command-Z/Shift-Z undo/redo.
The X11 host bridges text to other desktop applications. Transfers are bounded
to 4095 UTF-8 bytes; failed or empty pastes leave the selection unchanged.
Text insertion is atomic and UTF-8 codepoint-safe, not full IME or
grapheme editing. Buttons activate on release, so dragging out cancels a press.

## VFS audit

Audited the current kernel's `kernel/vfs/tree` node tables and write handlers.
The panel probes the selected device's actual nodes and advertised write
permissions; missing features remain unavailable. This is a curated settings
interface, not a generic writer for every VFS command or file.

| Setting | VFS node | Behavior |
| --- | --- | --- |
| Device name | `/sys/device/name` | Editable, 1–31 bytes. Stored persistently. Existing mount names/identity caches update on reconnect. |
| CPU clock | `/sys/cpu/freq`, `freq_target`, `freq_available`, `freq_change_mode` | Current rate in MHz plus a capability-driven next-restart selector. Choices come from the device, not a desktop board-name table. Older firmware without capability nodes remains read-only. |
| Date and time | `/sys/time` | Syncs Unix seconds from the host clock; displayed in UTC. Uses the firmware RTC persistence implementation. Requires `sys` capability. |
| Cryptography | `/sys/crypto/mode` | `auto` or `sw`, when the Nordic crypto surface exists. Runtime setting. |
| Radio transmit power | `/sys/radio/txpower` | Signed integer dBm. Firmware validates exact silicon-supported steps and radio ownership. Runtime setting. |
| Idle sleep mode | `/sys/power/mode` | Read-only here. Firmware with `/sys/power/policy` sets the mode through that node or the shell's `sleep` command; a mode that stops console input needs `allow-console-loss`. |

Other writable surfaces have different operational semantics and are not exposed
as ordinary preferences:

- `/sys/watchdog/{mode,clock,interval,enabled,kick}` reconfigure or feed the
  watchdog. These require coordinated supervision and platform-specific clock
  support; writing an interval is not changing the CPU frequency.
- `/sys/radio/beacon` starts/stops a radio operation; it is not a persistent
  connection preference and can compete with a Bluetooth connection.
- `/sys/coproc/{run,echo}`, `/sys/flpr/{run,spin,spinbench,echo,pulse}`, and
  `/sys/npu/{model,run}` launch programs or diagnostics.
- `/sys/psram/state` and `/sys/emmc/state` control memory/storage power; they
  can invalidate active users of those resources.
- `/sys/init/<slot>/enable` changes the persisted boot sequence. It belongs
  with boot-service management rather than the general device settings page.
- `/dev/led*`, writable GPIO values, `/dev/console`, `/data`, and dynamically
  registered driver nodes are application I/O, file operations, or controls
  with driver-specific contracts. They are not automatically made preferences.
- `/sys/clock`, boot-clock reports, GPU, NOR-flash reports, persistent-cell
  reports, timers, and VFS watch/cache statistics are read-only in these tables.

CPU clock reads now report the live HAL rate instead of the build constant.
`freq_available` lists space-separated integer Hz choices; `freq_change_mode`
is `reboot` when at least two safe choices exist, otherwise `fixed`.
`freq_target` reads the saved next-boot rate and accepts a strict integer Hz write
(requires the filesystem capability, like persistent device naming).

The selector shows the saved target separately from the current clock. Save
confirms the target's readback, not a live clock change. Restart is never
automatic. Nordic reads its magic-gated RRAM cell before starting HF peripherals;
missing/invalid state falls back to `TIKU_NORDIC_CPU_MHZ`. The early read performs
no storage initialization or writes. Saved choices survive ordinary resets and
power cycles, but not necessarily firmware updates that relocate persistent
cells or erase that partition. Saving the original rate restores that choice.

| Platform | Advertised MHz choices | Boot handling |
| --- | --- | --- |
| Nordic nRF54L | 64, 128 | Read directly mapped RRAM before HF peripherals start. |
| RP2350 | 12, 48, 100, 125, 133, 150 | Apply after flash-mirror restore, before peripherals. SysTick reload uses the actual core rate. |
| Apollo4 Lite / Plus | 96, 192 | Apply LP/HP mode after MRAM-mirror restore. |
| Apollo510 / 510B | 96, 250 | Uses Apollo510's actual HP rate, not Apollo4's 192 MHz. |
| RA8P1 | 240, 480, 1000 | Apply the existing operating-point sequence before external flash enters OPI mode. |
| MSP430 FR5994 / FR6989 | 1, 2, 4, 8, for an 8 MHz SMCLK build | Change only MCLK's divider; UART, SMCLK timers and ACLK are unchanged. Other SMCLK configurations stay fixed. Busy delays scale with the core rate but remain approximate. |
| STM32N6 | Exact supported divisors among 100, 150, 200, 300, 400, 600, 800 | Change only IC1 after NOR-mirror restore; preserve PLL, bus/XSPI clocks and voltage. The existing PLL and voltage determine the list (800 requires the high-voltage range). |

The portable preference is applied once, after memory restoration and before
normal peripheral initialization. A missing gate, corrupt complementary value,
unsupported target, or failed V2 mirror restore leaves the boot default intact.
Mirror-backed saves verify the persistent image and CRC, not just SRAM readback;
a failed save reports failure and restores the previous working-copy value.
Low-level hardware refusal leaves the actual frequency visible separately from
the saved target. There is no automatic reboot or runtime frequency change.

Live retuning remains disabled. Enabling it on another port needs a validated
transition that preserves timer accounting, peripheral baud rates, and memory
timing. Hardware verification must cover save/reboot/readback at every offered
rate, console communication, timing, storage I/O and interrupted saves before
release. Build and host-test coverage do not replace those board tests.

Cross-builds pass for RP2350, Apollo4 Lite, Apollo510, STM32N6, RA8P1,
MSP430FR5994, nRF54LM20B and nRF54L15. FR6989 clock sources compile, but the
complete image currently fails to link within its RAM/FRAM limits, including
with BASIC disabled. No on-device save/reboot tests have been performed for
this feature. FR2433 and FR5969 are reference-only ports rejected by Makefile.

## Applying changes

One setting is written per Apply. Writes recheck the node's permissions and
read back the device's response. Rejections and unverified writes remain visible
as errors/status messages. Reading or opening the window never writes settings.
Transport requests run on workers using the same key as the selected device's
mount, so disconnect cancels pending requests and closes the backend after any
request already in progress. Pending requests retain panel state through window
and desktop teardown. The default device does not redirect an open preferences
panel; settings always target the device explicitly selected there.

`test_deviceprefs` covers capability gating, explicit writes, changed permissions,
rejection, readback mismatch, and unavailable nodes with a mock device. No test
changes a user's physical device settings. It also checks cancellation and a
late completion after the panel's desktop ownership has been released.
It also covers clock capability discovery, pending versus current frequency,
read-only policies, save readback, and dropdown selection without a write.

`tools/cpu_clock_test.c` in the firmware repository tests the production Nordic
preference policy with a mock persistent store. Run with a host C compiler:

```sh
cc -I. tools/cpu_clock_test.c -o /tmp/tiku-cpu-clock-test
/tmp/tiku-cpu-clock-test
cc -I. -DTIKU_NORDIC_CPU_MHZ=64 tools/cpu_clock_test.c -o /tmp/tiku-cpu-clock-test64
/tmp/tiku-cpu-clock-test64
cc -I. tools/cpu_settings_test.c -o /tmp/tiku-cpu-settings-test
/tmp/tiku-cpu-settings-test
```
