# rhi-directory-structure

## MODIFIED Requirements

### Requirement: Rhi sources organized into subdirectories

The `Rhi` module SHALL organize its source files under `engine/Rhi/` into the responsibility-based subdirectories `Device/`, `Buffer/`, `Texture/`, `Pipeline/`, `Submission/`, and `Resource/`, with only module-wide infrastructure files (`rhi_export.h`, `RhiReflectionRegistration.cpp`, `CMakeLists.txt`) remaining at the module root.

#### Scenario: Device sources live in Device/

- **WHEN** the `Rhi` module is checked out
- **THEN** `DeviceInterface.*`, `DeviceContext.*`, `Structs.h`, `DebugUtils.h`, `Hasher.hpp`, `AllocatorState.*`, `MemoryAllocation.*`, `MemoryTypes.h`, `MemoryAccessTypes.h`, and the allocation retire sink interface SHALL reside in `engine/Rhi/Device/`, with the sink alongside `MemoryAllocation.*` where the buffer allocation it serves is defined

#### Scenario: Buffer sources live in Buffer/

- **WHEN** the `Rhi` module is checked out
- **THEN** `DeviceBuffer.*`, `ComputeBuffer.*`, `IndexedBuffer.*`, `StructuredBuffer.*`, and `StructuredBufferPlacer.*` SHALL reside in `engine/Rhi/Buffer/`

#### Scenario: Texture sources live in Texture/

- **WHEN** the `Rhi` module is checked out
- **THEN** `Texture.*`, `ImageTexture.*`, `TextureSubresourceView.*`, `ImageUtils.*`, and `ImageUtilsFunc.h` SHALL reside in `engine/Rhi/Texture/`

#### Scenario: Pipeline sources live in Pipeline/

- **WHEN** the `Rhi` module is checked out
- **THEN** `PipelineEnums.h` (with its `_reflection` companion), `PipelineInfo.*`, `ShaderInterface.h`, `ShaderParameterLayout.*`, `ShaderResourceBinding.*`, `ComputeResourceBinding.*`, `ComputeStage.*`, and `ComputeHelpers.h` SHALL reside in `engine/Rhi/Pipeline/`

#### Scenario: Submission and Resource groups

- **WHEN** the `Rhi` module is checked out
- **THEN** `SubmissionHelper.*` and the submission epoch tracker SHALL reside in `engine/Rhi/Submission/`
- **AND** `ImmutableResourceCache.*` SHALL reside in `engine/Rhi/Resource/`
