# rhi-backend-selection Specification

## Purpose

Makes the `Rhi` layer backend-neutral: one GPU backend is selected when the build is configured, and every interface the engine compiles against speaks only portable value types and opaque handles, so a second backend is an added implementation rather than a rewritten seam.

## Requirements

### Requirement: Exactly one backend per build

The build SHALL select exactly one GPU backend at configure time through a single `ENGINE_RHI_BACKEND` cache variable whose accepted values are `vulkan` and `webgpu`, and SHALL fail configuration on any other value. A build SHALL compile exactly one backend's implementation units; no runtime backend switching and no process hosting two backends is required.

#### Scenario: Configure selects the Vulkan backend

- **WHEN** the project is configured with `ENGINE_RHI_BACKEND=vulkan`
- **THEN** configuration succeeds and the produced libraries contain the Vulkan backend only

#### Scenario: Configure selects the WebGPU backend

- **WHEN** the project is configured with `ENGINE_RHI_BACKEND=webgpu`
- **THEN** configuration succeeds and the produced libraries contain the WebGPU backend only

#### Scenario: Unknown backend value fails configuration

- **WHEN** the project is configured with `ENGINE_RHI_BACKEND=d3d12`
- **THEN** configuration fails with a message naming the accepted values

### Requirement: Public interfaces carry no backend type

No header under `engine/Rhi/`, and no signature of a `Rhi` consumer (`engine/Render`, `engine/Physics`, `engine/Framework`, `engine/Core`), SHALL name a backend type such as a Vulkan handle, a VMA type, a WebGPU handle, or a backend enum. Backend types SHALL appear only inside one backend's implementation units. Portable value types (image format, extent, rectangle, sample count, usage bits) SHALL replace them in interfaces.

#### Scenario: Rhi headers compile without backend headers

- **WHEN** a translation unit includes every public header under `engine/Rhi/` and compiles
- **THEN** no backend header (for example `vulkan/vulkan.hpp`, a VMA header, `webgpu.h`) is required to be on the include path of that translation unit

#### Scenario: Consumer signatures are backend-neutral

- **WHEN** the declarations of the solver interface, the render command recorder and the presentation provider are inspected
- **THEN** none of their parameters, return types or members names a backend type

#### Scenario: Backend handles are opaque across the seam

- **WHEN** a consumer asks a resource for its identity or binding reference
- **THEN** it receives an opaque handle and a never-reused identity, and no backend object handle

### Requirement: Capability and limits are queried, not assumed

`Rhi` SHALL expose a capability and limits query covering at least queue count, maximum uniform-buffer offset alignment, maximum storage-buffer binding size, maximum buffer size, maximum bind-group count, and whether the backend natively provides per-dispatch constants and dynamic uniform slices. Consumers SHALL NOT test for a backend by name.

#### Scenario: Consumer adapts to a single-queue backend

- **WHEN** the capability query reports one queue
- **THEN** a consumer can detect it and schedule its work on the single queue without naming a backend

#### Scenario: Consumer adapts to missing per-dispatch constants

- **WHEN** the capability query reports that per-dispatch constants are emulated
- **THEN** a consumer can observe that fact without inspecting which backend is compiled in

### Requirement: The WebGPU build carries no native GPU toolchain

A build configured with `ENGINE_RHI_BACKEND=webgpu` SHALL NOT require the Vulkan SDK, VMA, glslang or SPIRV-Cross to configure, build or run. Shader code in such a build SHALL arrive as baked artifacts, and no runtime shader compilation or runtime SPIR-V reflection SHALL exist in it.

#### Scenario: Web build configures without a Vulkan SDK

- **WHEN** a machine without a Vulkan SDK installed configures the project with `ENGINE_RHI_BACKEND=webgpu`
- **THEN** configuration succeeds

#### Scenario: Web build contains no runtime shader compiler

- **WHEN** the link inputs of a `webgpu` build are inspected
- **THEN** neither glslang nor SPIRV-Cross appears among them
