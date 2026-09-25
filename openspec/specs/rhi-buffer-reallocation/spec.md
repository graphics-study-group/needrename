# Rhi Buffer Reallocation

## Purpose

Defines how a device buffer's storage is replaced while the buffer object itself stays at the same address, so that a long-lived reference to a buffer — a render graph's imported resource, or any consumer that outlives the component that created it — cannot be invalidated by a resize.

## Requirements

### Requirement: Buffer storage is replaced in place

A buffer's storage SHALL be replaceable without moving the buffer object: after a reallocation the buffer object SHALL remain at the same address and SHALL remain usable through any reference taken before the reallocation.

A reallocation SHALL allocate the replacement storage before releasing the previous storage. The released allocation SHALL take the same retirement path as any other buffer allocation destruction, and SHALL NOT be freed while a submission that may reference it is outstanding.

The capability SHALL be exposed by the buffer type whose descriptor bindings are re-established on every use, as an exact-size entry point (`Reallocate(allocator, bytes)`, which may grow or shrink) and a grow-only entry point (`EnsureCapacity(allocator, bytes)`). A caller chooses the entry point that expresses its intent.

#### Scenario: Object address survives reallocation

- **WHEN** a reference to a buffer is taken, the buffer is reallocated to a different size, and the reference is used afterwards
- **THEN** the reference is still valid and denotes the same buffer
- **AND** the buffer reports its new size

#### Scenario: The replaced allocation is retired, not freed

- **WHEN** a buffer is reallocated while a submission that may reference its previous storage may still be in flight
- **THEN** the previous allocation is parked under the submission epoch instead of being destroyed
- **AND** it is released only once every submission that may reference it has completed

#### Scenario: Reallocation without a retirement facility frees immediately

- **WHEN** a buffer is reallocated through an allocator with no retirement facility installed
- **THEN** the previous allocation is destroyed immediately, exactly as an allocation destroyed at that point would be

### Requirement: Reallocation invalidates the previous handle and mapping

A reallocation SHALL discard the buffer's contents and SHALL invalidate the Vulkan buffer handle and any host-visible mapping obtained before it. A caller MUST NOT retain a buffer handle or mapped pointer across a reallocation.

The buffer's debug name SHALL NOT be invalidated: the buffer object SHALL remember the name it was created with and SHALL use it for every replacement storage, so a reallocation reproduces the name without the caller repeating it. The name is a property of the buffer, not of the allocation that happens to back it at the moment.

#### Scenario: Handle changes, name does not

- **WHEN** a named buffer is reallocated
- **THEN** its Vulkan buffer handle differs from the handle before the reallocation
- **AND** the replacement storage carries the same debug name, taken from the name the buffer remembered

#### Scenario: Contents are not preserved

- **WHEN** a buffer is reallocated to a larger size
- **THEN** the new storage's contents are undefined
- **AND** re-establishing them is the caller's responsibility

### Requirement: Reallocation is exposed only where bindings are re-established

Only a buffer type whose descriptor bindings are re-established on every use SHALL expose reallocation. A buffer type that writes its handle into a descriptor set once and reuses that set MUST NOT expose a reallocation entry point.

#### Scenario: A compute buffer can be reallocated

- **WHEN** a caller needs a compute buffer of a different size
- **THEN** the buffer can be reallocated in place
- **AND** the next binding of that buffer uses the new handle

#### Scenario: A buffer with a persistent descriptor cannot be reallocated

- **WHEN** a buffer type binds its handle into a descriptor set that is not rewritten afterwards
- **THEN** that buffer type does not expose a reallocation entry point

### Requirement: A grow-only request within capacity reallocates nothing

A grow-only reallocation request at or below the buffer's current size SHALL NOT reallocate the buffer, SHALL NOT change its handle, and SHALL NOT discard its contents.

#### Scenario: A request within capacity is a no-op

- **WHEN** a grow-only request is made for a size at or below the buffer's current size
- **THEN** the buffer's storage, contents and handle are unchanged

#### Scenario: A request above capacity reallocates

- **WHEN** a grow-only request is made for a size above the buffer's current size
- **THEN** the buffer is reallocated to at least the requested size
- **AND** the buffer object stays at the same address
