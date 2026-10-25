# Device control resources

TikuDesktop selects the native board provider described in
[the shared runtime contract](../../foundation/docs/contracts/desktop-vfs-runtime.md#device-and-development-owners).
The existing `/devices/<board>` filesystem mount remains the path for board
files. A resource-service collection at `/devices/<opaque-id>` links to that
mount and its generation. These two namespaces have different roots.

Discovery reads the native candidate cache. Explicit scan and connect actions
use the existing probe workers; observations do not start a port scan. The
current-device text states follow the desktop's native selection, including a
saved selection whose board is disconnected.

Name and CPU-target proposals use the native configuration owner and recovery
outbox. Clients supply the published context token and the descriptor's current
durability. Pending recovery, unsupported nodes and stale UID/revision contexts
refuse writes. Device Preferences provides enrollment and recovery controls.
Its other settings remain available through their native UI and board nodes.

Bench and the source IDEs publish their own development observations. Per-device
build/debug collections reference those application providers and their existing
commands. Opening the development view does not install or flash firmware.

Run the native and simulated resource workflow with an exported SDK:

```bash
make -j6 core sdk apps shells build/tests/test_device_resources_fixture
python3 foundation/tests/integration/run_device_resources_sdk.py \
  --sdk build/sdk --desktop build/runtime/system/TikuDesktop \
  --apps build/runtime/apps --boards \
  --fixture build/tests/test_device_resources_fixture \
  --fixture-client tests/integration/device_resources_client.c \
  --evidence build/device-resources-sdk
```

The simulated producer supplies a RAM backend and a native resource endpoint.
The consumer compiles against a relocated SDK. Its workflow checks saves,
managed receipts, cancellation, UID mismatch and mount replacement without
opening a physical device. Physical board qualification is a separate lane.
