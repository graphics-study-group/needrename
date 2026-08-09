# rhi-push-constants

## Purpose

Provide push-constant support in the Rhi layer: `SPLayout` reflects push-constant blocks from SPIR-V, `ComputeStage` declares the matching pipeline-layout range, and a `PushConstants` helper records values at command-buffer time. This lets any Rhi consumer pass small per-dispatch parameters without descriptor-set rotation.

## Requirements

### Requirement: SPLayout reflects push-constant block size

`SPLayout::Reflect` SHALL inspect the reflected push-constant blocks of the SPIR-V code and expose the total block size as `push_constant_size` (bytes), using `get_declared_struct_size`. When the shader declares no push-constant block, `push_constant_size` SHALL be 0. The field SHALL be an unsigned 32-bit integer defaulting to 0 and SHALL NOT affect the reflection of descriptor-set interfaces.

#### Scenario: Shader with a push-constant block reflects its size
- **WHEN** `SPLayout::Reflect` processes SPIR-V compiled from GLSL containing `layout(push_constant) uniform Params { vec4 value; } params;`
- **THEN** `layout.push_constant_size` equals 16 (the std430 block size)

#### Scenario: Shader with a scalar-only push-constant block reflects its size
- **WHEN** `SPLayout::Reflect` processes SPIR-V compiled from GLSL containing `layout(push_constant) uniform Params { uint count; } params;`
- **THEN** `layout.push_constant_size` equals 4

#### Scenario: Shader without push constants reports zero
- **WHEN** `SPLayout::Reflect` processes a shader declaring only descriptor-set buffers
- **THEN** `layout.push_constant_size` equals 0

### Requirement: ComputeStage declares the push-constant range in its pipeline layout

`ComputeStage::CreatePipeline` SHALL add a `VkPushConstantRange` with stage flags `eCompute`, offset 0 and size equal to the reflected `push_constant_size` when that size is greater than 0. When the size is 0, the pipeline layout SHALL contain no push-constant range. `ComputeStage::GetPushConstantSize()` SHALL return the reflected size in bytes (0 when none).

#### Scenario: Push-constant shader gets a pipeline-layout range
- **WHEN** a `ComputeStage` is instantiated from a shader with a 16-byte push-constant block
- **THEN** `GetPushConstantSize()` returns 16
- **AND** the pipeline layout's push-constant ranges cover `{eCompute, 0, 16}`

#### Scenario: Non-push-constant shader gets no range
- **WHEN** a `ComputeStage` is instantiated from a shader without push constants
- **THEN** `GetPushConstantSize()` returns 0
- **AND** the pipeline layout contains no push-constant range

### Requirement: PushConstants helper records values

`Rhi::PushConstants` SHALL be a template helper taking a command buffer, a `ComputeStage` and a value of type `T`, recording `sizeof(T)` bytes at offset 0 for the compute stage. It SHALL assert that `sizeof(T)` does not exceed `GetPushConstantSize()`. `Rhi::BindComputeResource` SHALL default its `slot` parameter to 0 so callers without rotation can omit it.

#### Scenario: Recording a scalar parameter
- **WHEN** a caller records `PushConstants(cb, stage, uint32_t{256})` on a stage whose push-constant block is `{ uint count; }`
- **THEN** the command buffer receives 4 bytes containing the value 256 at offset 0

#### Scenario: Recording a struct parameter
- **WHEN** a caller records `PushConstants(cb, stage, Params{...})` on a stage whose push-constant block matches the struct layout
- **THEN** the command buffer receives `sizeof(Params)` bytes at offset 0

#### Scenario: Oversized push is caught in debug builds
- **WHEN** a caller records a value whose size exceeds the stage's `GetPushConstantSize()`
- **THEN** the assertion in `PushConstants` fires in debug builds
