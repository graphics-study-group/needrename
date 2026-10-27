# rhi-structured-buffer-placer

## MODIFIED Requirements

### Requirement: GPU UBO allocations use the block size

The material `MaterialInstance` SHALL allocate uniform-buffer slices sized by the placer's block size, so the descriptor range bound for the buffer covers the full std140 block including trailing padding.

The compute side no longer allocates uniform-buffer slices at all: a compute kernel's resources are supplied per dispatch, and a shader's uniform-buffer interfaces are bound from buffers the caller provides. The placer's block size therefore constrains only the material path.

#### Scenario: Padded block allocates a slice covering the whole block

- **WHEN** the material path prepares its UBO for a reflected block with trailing padding
- **THEN** the allocated slice size is at least the block size, and the bound descriptor range covers the block

#### Scenario: Existing aligned blocks allocate unchanged sizes

- **WHEN** a reflected block ends exactly on its 16-byte alignment
- **THEN** the allocated slice size equals the pre-change allocation

#### Scenario: Compute path allocates no UBO slices

- **WHEN** a compute kernel is dispatched with a shader that declares a uniform-buffer interface
- **THEN** the buffer bound for that interface is the one the caller supplied
- **AND** no per-dispatch uniform-buffer slice is allocated from the placer's block size
