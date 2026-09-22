# rhi-compute-resource-binding

## MODIFIED Requirements

### Requirement: ComputeResourceBinding declares rotation depth at construction

`ComputeResourceBinding` SHALL accept a `slot_count` parameter at construction that declares the caller's rotation depth, defaulting to 1. The class SHALL NOT contain any hard-coded rotation depth constant.

Rotation depth governs the per-slot uniform-buffer slices the binding owns and the range of accepted `slot` values. Descriptor sets SHALL NOT be pre-allocated per slot: the binding obtains them from the device descriptor arena when it is asked for one, and the epoch recorded against them by the arena governs when they may be reclaimed.

#### Scenario: Default construction uses a single slot

- **WHEN** a `ComputeResourceBinding` is created without an explicit `slot_count`
- **THEN** the binding supports exactly one rotation slot

#### Scenario: Caller declares a custom rotation depth

- **WHEN** a caller creates a binding with `slot_count = 3`
- **THEN** the binding allocates three uniform-buffer slices and accepts `slot` values 0 through 2
- **AND** no descriptor set is allocated at construction time

#### Scenario: Descriptor sets are obtained on demand

- **WHEN** a binding is asked for the descriptor set of a slot
- **THEN** the set comes from the device descriptor arena
- **AND** the binding does not own a descriptor pool

#### Scenario: Declared depth exceeds the supported maximum

- **WHEN** a caller creates a binding with `slot_count > 8`
- **THEN** the binding asserts (debug builds) and does not proceed
