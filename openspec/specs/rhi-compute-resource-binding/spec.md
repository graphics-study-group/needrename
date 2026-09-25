# Rhi Compute Resource Binding

## Purpose

Defines `ComputeResourceBinding` as a caller-declared rotation-depth resource binding: the caller commits to its own submission cadence via a `slot_count` parameter, and the API exposes a neutral `slot` index with bounds assertion — no render-frame vocabulary leaks into the Rhi compute API.

## Requirements

### Requirement: ComputeResourceBinding declares rotation depth at construction

`ComputeResourceBinding` SHALL accept a `slot_count` parameter at construction that declares the caller's rotation depth, defaulting to 1. The class SHALL NOT contain any hard-coded rotation depth constant.

Rotation depth governs the per-slot uniform-buffer slices the binding owns and the range of accepted `slot` values. Descriptor sets SHALL NOT be pre-allocated per slot, and the binding SHALL NOT store a descriptor-set handle: sets are obtained from the device descriptor arena when the binding is asked for one, and are returned to the caller with the data needed to bind them.

#### Scenario: Default construction uses a single slot

- **WHEN** a `ComputeResourceBinding` is created without an explicit `slot_count`
- **THEN** the binding supports exactly one rotation slot

#### Scenario: Caller declares a custom rotation depth

- **WHEN** a caller creates a binding with `slot_count = 3`
- **THEN** the binding allocates three uniform-buffer slices and accepts `slot` values 0 through 2
- **AND** no descriptor set is allocated at construction time
- **AND** no per-slot descriptor-set storage exists

#### Scenario: Descriptor sets are obtained on demand

- **WHEN** a binding is asked for the descriptor set of a slot
- **THEN** the set comes from the device descriptor arena
- **AND** the binding does not own a descriptor pool
- **AND** the returned set is not retained by the binding for a later call

#### Scenario: Declared depth exceeds the supported maximum

- **WHEN** a caller creates a binding with `slot_count > 8`
- **THEN** the binding asserts (debug builds) and does not proceed

### Requirement: ComputeResourceBinding exposes neutral slot indices

`UpdateGPUInfo` SHALL take a `slot` index (not a backbuffer/frame index) and SHALL assert that the slot is within the declared depth. It SHALL return the slot's descriptor set together with the dynamic offsets needed to bind it, so that a caller never receives a handle it can store for a later epoch. There SHALL be no separate descriptor-set accessor. The public API and its documentation SHALL contain no render-frame vocabulary (`backbuffer`, `frame_index`, "frames-in-flight", "back-buffer count").

#### Scenario: Updating GPU info for an in-range slot

- **WHEN** `UpdateGPUInfo(slot)` is called with `slot < slot_count`
- **THEN** the descriptor set for that slot's content is acquired from the arena and returned
- **AND** the slot's UBO slice is written if dirty
- **AND** its dynamic offset is returned with the set

#### Scenario: Updating GPU info with an out-of-range slot

- **WHEN** `UpdateGPUInfo(slot)` is called with `slot >= slot_count`
- **THEN** the call asserts (debug builds)

#### Scenario: No render-frame vocabulary remains in the Rhi API

- **WHEN** the Rhi module source is scanned for `BACK_BUFFERS`, `backbuffer`, `frame_index`, and "frames-in-flight" comments
- **THEN** no matches are found in `ComputeResourceBinding` or `ComputeHelpers`

### Requirement: ComputeStage forwards slot_count to allocated bindings

`ComputeStage::AllocateResourceBinding` SHALL accept a `slot_count` parameter (default 1) and SHALL forward it to the created `ComputeResourceBinding`.

#### Scenario: Allocating a binding with explicit depth

- **WHEN** `AllocateResourceBinding(3)` is called
- **THEN** the returned binding is constructed with `slot_count = 3`

#### Scenario: Allocating a binding with default depth

- **WHEN** `AllocateResourceBinding()` is called
- **THEN** the returned binding is constructed with `slot_count = 1`

### Requirement: Existing callers pass their rotation depth explicitly

All callers SHALL declare the rotation depth their own submission cadence needs. Physics components take the single-slot default, because a physics step waits for its own submission before recording the next and therefore rotates nothing. Render, editor and test callers pass `FrameManager::FRAMES_IN_FLIGHT`.

Rotation depth sizes the uniform-buffer slices a binding owns. It no longer sizes any descriptor-set storage, because the arena keys sets on content and returns one set for the content all slots share.

#### Scenario: Physics bindings keep their 3-slot rotation

- **WHEN** physics components allocate their compute bindings
- **THEN** no physics call site passes a depth, so their UBO slices are indexed by the same slot value as before the descriptor-set storage was removed
- **AND** the rotation depth no longer sizes any descriptor-set storage, because the arena keys sets on content and returns one set for the content every slot shares

#### Scenario: Render and test bindings derive depth from FRAMES_IN_FLIGHT

- **WHEN** render graph builders (bloom), editor builders, and compute tests allocate bindings
- **THEN** each allocation passes `FrameManager::FRAMES_IN_FLIGHT` and the literal `3` does not appear at those call sites
