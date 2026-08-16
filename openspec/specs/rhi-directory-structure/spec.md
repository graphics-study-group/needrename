# rhi-directory-structure Specification

## Purpose

Defines the `Rhi` module's source organization: responsibility-based subdirectories (`Device/`, `Buffer/`, `Texture/`, `Pipeline/`, `Submission/`, `Resource/`) with full-path includes repo-wide, leaving only module-wide infrastructure files at the module root.

## Requirements

### Requirement: Rhi sources organized into subdirectories

The `Rhi` module SHALL organize its source files under `engine/Rhi/` into the responsibility-based subdirectories `Device/`, `Buffer/`, `Texture/`, `Pipeline/`, `Submission/`, and `Resource/`, with only module-wide infrastructure files (`rhi_export.h`, `RhiReflectionRegistration.cpp`, `CMakeLists.txt`) remaining at the module root.

#### Scenario: Device sources live in Device/

- **WHEN** the `Rhi` module is checked out
- **THEN** `DeviceInterface.*`, `DeviceContext.*`, `Structs.h`, `DebugUtils.h`, `Hasher.hpp`, `AllocatorState.*`, `MemoryAllocation.*`, `MemoryTypes.h`, and `MemoryAccessTypes.h` SHALL reside in `engine/Rhi/Device/`

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
- **THEN** `SubmissionHelper.*` SHALL reside in `engine/Rhi/Submission/` and `ImmutableResourceCache.*` SHALL reside in `engine/Rhi/Resource/`

### Requirement: Rhi headers are included by full path

All includes of Rhi headers, both inside the `Rhi` module and from other modules (Render, Physics, Asset, Framework), tests, examples, and the editor, SHALL use the full path from the engine source root including the group directory (e.g. `#include "Rhi/Device/DeviceInterface.h"`).

#### Scenario: No group-less Rhi includes remain

- **WHEN** the repository is searched for `#include "Rhi/` patterns after the restructure
- **THEN** every match SHALL contain a group directory between `Rhi/` and the file name

### Requirement: Restructure preserves module behavior

The directory restructure SHALL be a pure file move: class names, the `Engine::Rhi` namespace, public APIs, and runtime behavior SHALL remain unchanged, and the Rhi CMake source list SHALL continue to compile all moved sources (via the existing recursive glob) without reordering.

#### Scenario: Build and tests stay green after restructure

- **WHEN** `cmake --build --preset debug` and `ctest --preset debug` are run after the restructure
- **THEN** the build SHALL succeed and all tests SHALL pass with the same count as before the restructure
