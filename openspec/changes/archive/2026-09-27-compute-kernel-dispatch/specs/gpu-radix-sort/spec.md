# gpu-radix-sort

## MODIFIED Requirements

### Requirement: RadixSort class construction

The `RadixSort` class SHALL be constructible with a `Rhi::DeviceContext &` alone. Element geometry SHALL NOT be a construction-time parameter and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources. Shader loading and kernel acquisition SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to `Rhi::DeviceContext`, `ComputeBuffer`, the Rhi compute kernel facility, `ParallelScan` and `vk::CommandBuffer`.

Because the element capacity and the key bound are supplied per call, one instance SHALL be reusable for any capacity and any bound, including several different ones within a single frame, and SHALL NOT require rebuilding when the caller's geometry changes.

The class SHALL own an internal `ParallelScan` instance for its prefix-sum step and SHALL rebuild it internally when a call's element capacity exceeds the capacity it was built for. That instance SHALL NOT appear in the class's interface, and the class SHALL NOT allocate any buffer: every buffer it binds SHALL be caller-provided.

#### Scenario: Construction with valid parameters

- **WHEN** `RadixSort` is constructed with a device context
- **THEN** no GPU resources are allocated and no shaders are loaded
- **AND** `IsInitialized()` returns false

#### Scenario: Construction rejects zero max_elem_count

The element count is no longer a construction parameter, so the former construction-time rejection is replaced by per-call behaviour: a zero capacity is a no-op at `Record`, while a capacity whose implied byte size exceeds the bound buffers is rejected there.

- **WHEN** `Record` is called with a capacity of zero
- **THEN** no dispatch is recorded and no exception is thrown

#### Scenario: One instance serves several capacities

- **WHEN** the same instance records two sorts with different element capacities in one frame
- **THEN** each call dispatches for its own capacity
- **AND** no rebuild is required at the call site

#### Scenario: No shader-module bookkeeping is exposed

- **WHEN** the `RadixSort` class is inspected for the shader pipeline it uses
- **THEN** it holds no per-instance shader stage or binding object
- **AND** its shaders are obtained from the device-level kernel facility on demand
