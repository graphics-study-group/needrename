## Purpose

Defines how CPU code touches GPU memory: host writes and device readback go through a mapped window with explicit open/close semantics, so the same call sites work on a backend whose memory is permanently mapped and on one whose memory may only be mapped while no GPU work uses it.

## ADDED Requirements

### Requirement: CPU access happens inside a mapped window

A host-accessible buffer SHALL expose its CPU address as a mapped window. The window is open while the buffer is not participating in device work and closes when the buffer is next used by the device. An address obtained while the window is open SHALL stay readable until the buffer next participates in device work, and a caller MUST NOT use an address obtained before that point afterwards.

#### Scenario: Address is stable while the window is open

- **WHEN** a caller obtains a buffer's CPU address and the buffer is not used by the device before the caller finishes reading
- **THEN** the address stays valid for the whole read

#### Scenario: Address is invalidated by device use

- **WHEN** a caller obtains a CPU address, the buffer is then used by the device, and the caller obtains the address again after the resulting readback completes
- **THEN** the second address names the refreshed contents and the first address must not be used

### Requirement: Host writes close the window and become device-visible

A write through the window SHALL be accounted as dirty, and `Flush` SHALL close the window and make the written ranges visible to subsequent device work. The backend SHALL flush every dirty window before the submission that consumes it even when the caller does not call `Flush` explicitly.

#### Scenario: Explicit flush publishes writes

- **WHEN** a caller writes through the window and calls `Flush`
- **THEN** device work submitted afterwards observes the written bytes

#### Scenario: Implicit flush covers forgotten calls

- **WHEN** a caller writes through the window and never calls `Flush`, and a submission consuming the buffer is recorded
- **THEN** the submission still observes the written bytes

#### Scenario: Vulkan keeps the window open

- **WHEN** the Vulkan backend serves a window
- **THEN** obtaining the address performs no mapping or unmapping and the address remains the same pointer across flushes

### Requirement: Device writes are observed through a refreshed window

Reading device-produced contents SHALL go through `Invalidate`, which refreshes the window to the contents produced by completed device work. A consumer that reads the window without refreshing SHALL observe the last completed contents rather than a partially written state.

#### Scenario: Refresh exposes completed device writes

- **WHEN** a dispatch writes a buffer, the readback completes, and the caller invalidates and reads the window
- **THEN** the caller observes the dispatched values

#### Scenario: No torn state is observable

- **WHEN** the caller reads the window while a readback is still in flight
- **THEN** it observes the previous completed contents rather than a mix of old and new bytes

### Requirement: Readback completion is asynchronous by default

Registering a readback SHALL be non-blocking and SHALL deliver the filled buffer to a caller-supplied callback when the copy into it has completed. The callback is the product-path consumption mechanism.

#### Scenario: Callback fires on completion

- **WHEN** a caller registers a readback callback and the device completes the copy
- **THEN** the callback runs exactly once with the buffer whose contents are complete

#### Scenario: Registration does not block

- **WHEN** a caller registers a readback while device work is still in flight
- **THEN** the call returns immediately

### Requirement: Blocking readback exists only as a pump

A blocking form SHALL exist for test and load-time code: it waits until a registered readback completes by pumping the device's completion events. Product-frame paths MUST NOT use it. On a build whose host cannot pump device events without yielding to the host event loop, the blocking form SHALL be unavailable unless the test-only yield support is compiled in.

#### Scenario: Test reads results synchronously

- **WHEN** a test registers a readback and calls the blocking form
- **THEN** it returns with the readback completed and the window readable

#### Scenario: Product path never blocks

- **WHEN** the per-frame rendering and simulation paths need device-produced data
- **THEN** they consume the last completed snapshot through a callback and do not wait

### Requirement: Snapshot consumers tolerate completion latency

A consumer of device-produced state that runs every frame SHALL be served the most recent completed snapshot and SHALL NOT require same-frame data. Consumers requiring same-frame data SHALL use the blocking pump.

#### Scenario: Actuator reads previous step state

- **WHEN** a per-step control loop reads body state produced by the previous step and writes commands for the next step
- **THEN** it operates correctly on the last completed snapshot without waiting
