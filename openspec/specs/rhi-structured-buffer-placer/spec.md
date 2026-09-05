# Rhi Structured Buffer Placer

## Purpose

Defines the size contract of `Rhi`'s `StructuredBufferPlacer`: the placer exposes a computed required size for staging writes and a caller-declared block size that includes trailing padding, so GPU uniform-buffer allocations and descriptor ranges always cover the full std140 block regardless of C++ struct layout.

## Requirements

### Requirement: Placer exposes the required size as the staging footprint

`StructuredBufferPlacer` SHALL provide `GetRequiredSize()`, computed as the maximum of `offset + size` over all registered variables. For a nested structured buffer the element size SHALL be the child placer's required size. `WriteBuffer(data, vector)` SHALL resize the staging vector to exactly `GetRequiredSize()` before writing members. The required size SHALL NOT include trailing padding.

#### Scenario: Padded struct reports the last member end

- **WHEN** a placer registers `uint32_t` at offset 0, `double` at offset 8, and `float[3]` at offset 16
- **THEN** `GetRequiredSize()` equals 28 (the end of the last member), not the struct's `sizeof`

#### Scenario: Staging vector is sized by the required size

- **WHEN** `WriteBuffer(data, std::vector<std::byte>&)` is called
- **THEN** the vector is resized to `GetRequiredSize()` and every member write stays within the vector

#### Scenario: Nested buffer contributes its required size

- **WHEN** a parent placer registers a nested structured buffer at an offset
- **THEN** the parent's required size accounts for the child's required size at that offset

### Requirement: Caller declares the block size including trailing padding

`StructuredBufferPlacer` SHALL provide `SetBlockSize(size_t)` to inject the target-layout block size (the CPU mirror struct's `sizeof`, or the std140/std430 block size), including trailing padding, and `GetBlockSize()` to read it. `GetBlockSize()` SHALL return the injected value when set and SHALL fall back to `GetRequiredSize()` when unset, preserving the historical packed-layout behavior for callers that do not declare a block size.

#### Scenario: Padded struct declares its full size

- **WHEN** a placer for a struct with trailing padding calls `SetBlockSize(sizeof(T))`
- **THEN** `GetBlockSize()` equals `sizeof(T)`, which is larger than `GetRequiredSize()`

#### Scenario: Undeclared block size falls back to the required size

- **WHEN** a placer never calls `SetBlockSize`
- **THEN** `GetBlockSize()` equals `GetRequiredSize()`

#### Scenario: Undersized block size is rejected in debug builds

- **WHEN** `SetBlockSize` is called with a value smaller than the current required size
- **THEN** a debug-build assertion fires, and no release-build write is performed beyond the declared block size

### Requirement: Reflected UBO interfaces declare the std140 block size

`SPLayout::Reflect` SHALL attach, to each reflected uniform-buffer interface, a placer whose block size is the shader block's std140 size: the declared struct size rounded up to the structure's base alignment, which is 16 when any vec4/matrix member is present and 4 otherwise. The required size SHALL remain the last member end. The push-constant size (`push_constant_size`) SHALL be unchanged by this rule.

#### Scenario: Block with trailing padding reflects its padded size

- **WHEN** a shader declares `layout(std140) uniform Block { vec4 a; float b; }`
- **THEN** the reflected placer reports block size 32 and required size 20

#### Scenario: Scalar-only block reflects without rounding

- **WHEN** a shader declares `layout(std140) uniform Block { uint count; }`
- **THEN** the reflected placer reports block size 4 and required size 4

#### Scenario: Vector-and-matrix block reflects an aligned size

- **WHEN** a shader declares `layout(std140) uniform Block { vec4 a; mat4 m; }`
- **THEN** the reflected placer reports block size 80 and required size 80

### Requirement: GPU UBO allocations use the block size

`ComputeResourceBinding` and the material `MaterialInstance` SHALL allocate uniform-buffer slices sized by the placer's block size, so the descriptor range bound for the buffer covers the full std140 block including trailing padding.

#### Scenario: Padded block allocates a slice covering the whole block

- **WHEN** a compute or material binding prepares its UBO for a reflected block with trailing padding
- **THEN** the allocated slice size is at least the block size, and the bound descriptor range covers the block

#### Scenario: Existing aligned blocks allocate unchanged sizes

- **WHEN** a reflected block ends exactly on its 16-byte alignment
- **THEN** the allocated slice size equals the pre-change allocation

### Requirement: Tests pin exact sizes for padded, aligned, and nested cases

The unit tests SHALL verify the size contract with exact numeric assertions: a padded struct (required size equals the last member end, block size equals `sizeof`), an aligned struct (required size equals block size equals `sizeof`), and a nested struct pair (parent block size equals `sizeof`, parent required size uses the child's required size). Byte-level round-trips SHALL be performed against a buffer sized exactly by the block size. Reflected-block sizes SHALL be verified by compiling GLSL to SPIR-V at test time and reflecting it.

#### Scenario: Padded struct test pins both sizes

- **WHEN** the structured buffer unit test registers a struct whose members end 4 bytes short of its alignment
- **THEN** it asserts the required size equals the last member end and the block size equals `sizeof`

#### Scenario: Round-trip against a block-sized buffer

- **WHEN** members are written into a buffer of exactly `GetBlockSize()` bytes and read back into the mirror struct
- **THEN** all member values round-trip and no write exceeds the buffer

#### Scenario: Reflection test pins std140 rounding

- **WHEN** a test compiles a GLSL uniform block with trailing padding and reflects it
- **THEN** it asserts the reflected block size and required size match the std140 layout
