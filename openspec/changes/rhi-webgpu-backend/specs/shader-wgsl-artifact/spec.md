## Purpose

Makes shader artifacts carry a WebGPU-usable form next to the existing SPIR-V form: baked WGSL code plus an interface description that is the single source of CPU-side layout truth, so a runtime that cannot compile GLSL or reflect SPIR-V can still bind resources and place CPU-side values correctly.

## ADDED Requirements

### Requirement: Artifacts carry WGSL and an interface description alongside SPIR-V

A baked shader artifact SHALL keep its existing SPIR-V form and MAY additionally carry a WGSL code variant and an interface description. The variants are additive, not mutually exclusive: one baked artifact set SHALL serve both a Vulkan runtime and a WebGPU runtime without a second copy of the assets.

#### Scenario: One artifact set serves both backends

- **WHEN** baked artifacts are loaded by a Vulkan runtime and by a WebGPU runtime
- **THEN** the Vulkan runtime reads the SPIR-V variant and the WebGPU runtime reads the WGSL variant and the description, from the same artifact set

#### Scenario: Vulkan form is unchanged

- **WHEN** artifacts without a WGSL variant are loaded by a Vulkan runtime
- **THEN** loading and reflection behave exactly as before this change

### Requirement: The interface description is the layout source of truth

The interface description SHALL carry, for each shader entry point: the binding table mapping each named interface to its final group and binding numbers in the WGSL; the byte offset, size and alignment of every variable placed in a uniform or storage buffer; the size and binding location of the per-dispatch constant block; and the mapping by which a combined sampler binding expands into a texture binding and a sampler binding. A consumer placing CPU-side values SHALL derive their offsets from the description and MUST NOT re-derive them.

#### Scenario: CPU placement matches WGSL layout

- **WHEN** a consumer places a set of named variables into a uniform buffer using the description's offsets and the shader reads them
- **THEN** every variable is observed at the value written for its name

#### Scenario: Combined sampler expands by name

- **WHEN** a shader declares one combined sampler and a consumer binds a texture by that interface's name
- **THEN** the binding resolves to both the texture entry and the sampler entry the description names

#### Scenario: Constant block location is declared

- **WHEN** a consumer records per-dispatch constants for a shader
- **THEN** the description names the reserved binding the constants must be bound at, and the shader observes them there

### Requirement: The bake validates layout agreement

The bake SHALL convert SPIR-V to WGSL and SHALL verify that the WGSL binding table and buffer layout implied by the produced code agree with the interface description it writes. A disagreement SHALL fail the bake and name the shader and the disagreeing entry.

#### Scenario: Layout disagreement fails the bake

- **WHEN** the converter produces WGSL whose buffer offsets differ from the description's offsets
- **THEN** the bake fails and reports the shader and the mismatching variable

#### Scenario: Conversion failure fails the bake

- **WHEN** the converter cannot express a shader's SPIR-V in WGSL
- **THEN** the bake fails and names the shader rather than emitting an artifact a runtime cannot use

### Requirement: Runtimes that cannot reflect load from the description

A runtime built without a SPIR-V reflection facility SHALL construct its binding and layout information from the interface description alone. Constructing a pipeline layout, resolving a named binding, and placing CPU-side values SHALL all be possible without inspecting shader code.

#### Scenario: Web runtime builds layouts without reflection

- **WHEN** a WebGPU runtime creates a pipeline for a baked shader
- **THEN** it derives every binding layout and window offset from the description and links no reflection library

### Requirement: The physics shader tree carries the same artifacts

Precompiled physics compute shaders SHALL carry the WGSL variant and the interface description next to their SPIR-V module, with the same content rules as shader artifacts.

#### Scenario: Physics kernel loads without reflection

- **WHEN** a WebGPU runtime creates a physics compute kernel
- **THEN** it loads the module's WGSL and description from beside the SPIR-V module and needs no reflection
