# Rhi Compute Resource Binding

## Purpose

Defines `ComputeResourceBinding` as a caller-declared rotation-depth resource binding: the caller commits to its own submission cadence via a `slot_count` parameter, and the API exposes a neutral `slot` index with bounds assertion — no render-frame vocabulary leaks into the Rhi compute API.

## Requirements

### Requirement: ComputeResourceBinding declares rotation depth at construction

`ComputeResourceBinding` SHALL accept a `slot_count` parameter at construction that declares the caller's rotation depth, defaulting to 1. The class SHALL NOT contain any hard-coded rotation depth constant.

#### Scenario: Default construction uses a single slot

- **WHEN** a `ComputeResourceBinding` is created without an explicit `slot_count`
- **THEN** the binding supports exactly one rotation slot

#### Scenario: Caller declares a custom rotation depth

- **WHEN** a caller creates a binding with `slot_count = 3`
- **THEN** the binding allocates three UBO slices and three descriptor-set slots, and accepts `slot` values 0 through 2

#### Scenario: Declared depth exceeds the supported maximum

- **WHEN** a caller creates a binding with `slot_count > 8`
- **THEN** the binding asserts (debug builds) and does not proceed

### Requirement: ComputeResourceBinding exposes neutral slot indices

`UpdateGPUInfo` and `GetDescriptorSet` SHALL take a `slot` index (not a backbuffer/frame index) and SHALL assert that the slot is within the declared depth. The public API and its documentation SHALL contain no render-frame vocabulary (`backbuffer`, `frame_index`, "frames-in-flight", "back-buffer count").

#### Scenario: Updating GPU info for an in-range slot

- **WHEN** `UpdateGPUInfo(slot)` is called with `slot < slot_count`
- **THEN** the descriptor set for that slot is refreshed and the slot's UBO slice is written if dirty

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

All existing callers SHALL pass their rotation depth explicitly: physics components pass `3` (transitional, preserving current behavior); render, editor, and test callers pass `FrameManager::FRAMES_IN_FLIGHT`.

#### Scenario: Physics bindings keep their 3-slot rotation

- **WHEN** physics components allocate their compute bindings
- **THEN** each allocation passes `3`, so descriptor sets and UBO slices are indexed exactly as before the change

#### Scenario: Render and test bindings derive depth from FRAMES_IN_FLIGHT

- **WHEN** render graph builders (bloom), editor builders, and compute tests allocate bindings
- **THEN** each allocation passes `FrameManager::FRAMES_IN_FLIGHT` and the literal `3` does not appear at those call sites
