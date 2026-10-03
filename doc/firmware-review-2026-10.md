# Firmware readability and performance review

This review covers the entry point, effect lifecycle and calculation, axis
filtering, packed HID reports/descriptors, USB compatibility/control buffers,
timing counters, UART/CAN motor adapters, diagnostics and board configuration.
The independent EmbeddedComm library remains a separate transport layer.

## Findings and changes

### Released effects could be restarted without allocation

The handler previously checked the ID range but not the allocation bit before
accepting configuration or Start/Start Solo. Freeing a slot leaves its old
parameters in storage. A subsequent Start could therefore revive stale force;
Start Solo on that free slot could also stop an unrelated live effect.

Configuration and playback now use an allocated-slot lookup. Release remains
idempotent, and a new allocation still clears the entire runtime block. Unsupported
Create New Effect types return Block Load error status 3 with ID zero and consume
no pool capacity. Tests exercise these cases for all supported axis counts.

### Repeated condition normalization in the force loop

Each condition effect previously divided motion metrics by the same axis maxima.
Three local, lazy caches now normalize position, speed and acceleration on first
use during a calculation. Their lifetime ends with that call, so subsequent input
updates cannot leave stale normalized values. Friction and damper share the speed
cache. Conditions write the sampled-force array directly instead of using a
second temporary array and copying it.

For fifteen full-XY spring effects, source-level normalization divisions fall
from thirty to two per calculation. This counts algorithmic work, not elapsed
CPU time: compiler optimizations and real hardware scheduling determine the
actual improvement. Playback/trigger advancement, sample-period holds, gains,
direction projection and the documented independent-axis compatibility path
retain their behavior. Tests verify a full condition pool against analytical
forces and then change input polarity to detect cross-call stale caches.

### Debug logging bypassed the bounded UART writer

The legacy per-command debug path could wait for UART space while holding the
effect mutex, delaying USB callbacks and force computation. Following the user's
cleanup request, its switch, command log branches and unused HAL debug API were
removed. All remaining application console UART writes
belong to one low-priority consumer in `diagnostic_console.cpp`; producers use
zero-wait enqueue and drop oversized/full-queue messages. Formatting still has a
bounded CPU cost, so enabling timing diagnostics requires renewed timing tests.

Builds without timing logs or CAN initialization diagnostics omit
console initialization. This avoids requesting a 4128-byte queue payload and a
2048-byte task stack, plus their kernel overhead. This is an allocation reduction,
not a measured runtime free-heap delta. CAN and diagnostic configurations retain
the queue/task. UART1 motor traffic and HIL telemetry keep their existing owner.

### Shared LCD durations and Feature snapshots

Two cross-task LCD duration values previously relied on `volatile`. They now
use a short TimingLock-protected snapshot, compiled only with LCD enabled.
Rendering holds no timing/model lock. GET_REPORT copies handler-owned data to a
local typed snapshot before releasing the handler mutex; subsequent copying and
debug formatting do not retain an unlocked handler pointer. Returned lengths
derive from report types instead of duplicate numeric sizes.

### Optional math helper undefined behavior

The approximate square root read a float through a `long*`, violating aliasing
and potentially reading eight bytes from four-byte storage on some hosts. It now
copies fixed-width bits and handles zero, negative and non-finite values. The
table sine reduces finite angles before unsigned conversion; negative angles no
longer depend on out-of-range float-to-unsigned behavior, and non-finite angles
return NaN. Its immutable table is const. These helpers remain approximations;
the default effect path continues to use standard trigonometric functions.

### Entry-point organization and style

Console ownership moved out of main.cpp. Setup now names transport initialization,
input calibration, display initialization, model/queue creation and USB startup
as separate functions. Resource creation precedes USB exposure and task creation;
the force startup gate still opens only after all peer tasks exist. Relevant
implementation files follow the repository formatter, and obsolete worker-task
comments and abandoned allocation code were removed.

## Validation and remaining measurements

- 215 native tests passed across `native`, `native-axis1` and `native-axis3`,
  including five new boundary/full-pool/math cases in each axis configuration.
- `esp32-s3`, `esp32-s2`, `esp32-s3-hil` and `esp32-s3-can` compiled successfully.
- `esp32-s3-upgrade` and `esp32-s3-upgrade-hil` compiled successfully using the
  existing short `E:/pio-upgrade` cache, Arduino 3.3.12 and TinyUSB 0.21.0.

This source revision has not been flashed or hardware-timed. The previous
509-us maximum and passing hardware captures belong to the earlier image and
must not be attributed to this revision. Repeat the existing DirectInput HIL
suite after flashing, with motors disconnected, comparing lifetime misses/skips,
worst-cycle timestamp tuples and force-task stack headroom. The lazy metric
caches add local stack storage whose hardware high-water mark should be observed.

External-position mode retains its current behavior when feedback stops: no new
sample is published, and the last input metrics remain available. A feedback-age
shutdown policy needs an explicit motor/control requirement; no new timeout or
actuator policy is inferred by this optimization. Historical USB/deadline failure
records and the axis-condition compatibility exception remain applicable.
