# gpu-sum-by-key

## MODIFIED Requirements

### Requirement: SumByKey class construction

The `SumByKey` class SHALL be constructible with `(Rhi::DeviceContext &device_context)` alone. Element geometry SHALL NOT be a construction-time parameter and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources; shader loading and kernel acquisition SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to the Rhi compute infrastructure (`ComputeBuffer`, the compute kernel facility, `DeviceContext`) plus `vk::CommandBuffer`.

Because geometry is supplied per call, one instance SHALL be reusable for any geometry, including several different geometries within a single frame, and SHALL NOT require rebuilding when the caller's geometry changes.

#### Scenario: Construction allocates nothing and stores no geometry

- **WHEN** `SumByKey` is constructed with a device context
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** no geometry is stored on the instance

#### Scenario: One instance serves several geometries in one frame

- **WHEN** a single instance is called for reductions with different capacities, key bounds and entry counts
- **THEN** each call uses the geometry passed to that call
- **AND** no call is affected by a previous call's geometry

#### Scenario: No shader-module bookkeeping is exposed

- **WHEN** the `SumByKey` class is inspected for the shader pipeline it uses
- **THEN** it holds no per-instance shader stage or binding object
- **AND** its shader is obtained from the device-level kernel facility on demand
