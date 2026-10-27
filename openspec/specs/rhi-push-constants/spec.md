# rhi-push-constants

## Purpose

Provide push-constant reflection in the Rhi layer: `SPLayout` reports the size of a shader's push-constant block from SPIR-V, which lets a consumer declare a matching pipeline-layout range and pass small per-dispatch parameters without descriptor-set rotation. The compute kernel facility is the consumer that declares the range and records the value.

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
