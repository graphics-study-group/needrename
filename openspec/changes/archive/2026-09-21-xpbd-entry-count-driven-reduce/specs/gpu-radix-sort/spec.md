# gpu-radix-sort

## MODIFIED Requirements

### Requirement: RadixSort class construction

The `RadixSort` class SHALL be constructible with a `Rhi::DeviceContext &` alone. Element geometry SHALL NOT be a construction-time parameter and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources. Shader loading and `ComputeStage` instantiation SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to `Rhi::DeviceContext`, `ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding` and `vk::CommandBuffer`.

Because the element capacity is supplied per call, one instance SHALL be reusable for any capacity, including several different capacities within a single frame, and SHALL NOT require rebuilding when the caller's capacity changes.

The former construction-time element-count bound SHALL be replaced by a per-call check: `Record` SHALL reject a capacity whose implied byte size exceeds the buffer bound for that call, and SHALL treat a capacity of zero as a no-op that records nothing.

#### Scenario: Construction with valid parameters

- **WHEN** `RadixSort` is constructed with a device context
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** no element geometry is stored on the instance

#### Scenario: Construction rejects zero max_elem_count

The element count is no longer a construction parameter, so the former construction-time rejection is replaced by the per-call behaviour below.

- **WHEN** `Record` is called with a capacity of zero
- **THEN** no dispatch is recorded
- **AND** no exception is thrown

#### Scenario: One instance sorts several capacities in one frame

- **WHEN** a single instance records sorts with different element capacities
- **THEN** each sort dispatches for the capacity passed to that call
- **AND** no sort is affected by a previous call's capacity

#### Scenario: A capacity larger than the bound buffers is rejected

- **WHEN** `Record` is called with an element capacity whose implied byte size exceeds the pair buffer bound for that call
- **THEN** a `std::runtime_error` is thrown
