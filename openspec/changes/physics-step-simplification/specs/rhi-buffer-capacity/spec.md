# rhi-buffer-capacity

## Purpose

Defines the growth *policy* of the reallocation entry points that `stable-buffer-identity`'s `rhi-buffer-reallocation` capability introduces. That capability establishes the mechanism — a buffer's storage is replaced in place, the object stays at the same address, and `ComputeBuffer` exposes an exact-size `Reallocate(allocator, bytes)` alongside a grow-only `EnsureCapacity(allocator, bytes)`. This capability does not add a second mechanism or a rival entry point: it modifies the policy those two entry points follow.

Two policy changes, and one consequence:

- capacity is only ever increased — a grow-only request never shrinks, and an exact-size `Reallocate` remains the way to shrink;
- growth through `EnsureCapacity` is geometric rather than exact, so capacity tracks capacity steps instead of every value change;
- because capacity and logical element count are therefore no longer the same thing, `GetSize()` reports **capacity**, and every consumer obtains its logical bound explicitly.

This capability depends on `stable-buffer-identity`: it restates the growth policy of a contract that change introduces, and must land after it.

## ADDED Requirements

### Requirement: Buffer capacity only grows

The grow-only entry point `EnsureCapacity(allocator, bytes)` SHALL NOT reallocate when the requested size is at or below the buffer's current capacity, and SHALL NOT discard the buffer's contents in that case. Only a request above the current capacity SHALL cause a new allocation, and the replacement SHALL be large enough to satisfy the request.

The exact-size entry point `Reallocate(allocator, bytes)` retains its exact semantics and remains the way to reduce a buffer's size; this capability does not make `Reallocate` grow-only.

Because `EnsureCapacity` never reduces capacity, a workload that oscillates between a large and a small size reallocates only on the transitions upward.

#### Scenario: A growth request within capacity reallocates nothing

- **WHEN** a buffer currently holds 4096 elements and is resized to 3000 elements
- **THEN** no new allocation is made
- **AND** the buffer's contents are unchanged

#### Scenario: Repeated shrink and grow crosses no allocation

- **WHEN** a buffer is resized between a large and a small element count several times, never exceeding the large count
- **THEN** exactly one allocation exists for the whole sequence
- **AND** the buffer's reported size stays at the capacity established by the large count

### Requirement: Growth is geometric

When a resize request exceeds the current capacity, the new capacity SHALL be the larger of the requested size and a geometric multiple of the current capacity, rounded up as needed. A buffer that grows by a small increment SHALL therefore cross an allocation boundary only when a capacity step is passed, rather than on every increment.

The growth factor SHALL be a fixed constant greater than one, and the resulting capacity SHALL be permitted to exceed the requested size.

#### Scenario: A small increment over capacity doubles rather than fits exactly

- **WHEN** a buffer holding 1000 elements is resized to 1001 elements
- **THEN** the new capacity is a geometric multiple of the old capacity, not 1001
- **AND** a subsequent resize to any size at or below that capacity reallocates nothing

#### Scenario: Capacity steps bound the allocation count

- **WHEN** a buffer is grown one element at a time from 1 to 100000 elements
- **THEN** the number of allocations is logarithmic in the final size
- **AND** it does not grow with the number of resize calls

### Requirement: Reported size is capacity, not element count

A buffer's reported size SHALL be its allocated capacity in bytes. It SHALL NOT be interpreted as a logical element count, a valid-input bound, or a dispatch bound.

Every consumer that needs a logical bound SHALL obtain it explicitly from the caller rather than deriving it from the buffer's size. Where a consumer derives dispatch geometry, it SHALL derive it from the logical count, not from capacity.

#### Scenario: Dispatch geometry follows the logical count

- **WHEN** a pass is dispatched over a buffer whose capacity exceeds the logical element count it is processing
- **THEN** the number of workgroups is derived from the logical element count
- **AND** no workgroup is launched for elements beyond it

#### Scenario: A logical count is passed explicitly

- **WHEN** a caller requests work bounded by an element count
- **THEN** the count is supplied as an argument or a per-dispatch constant
- **AND** it is not read back from the buffer's size

### Requirement: Consumers accept oversized buffers

A consumer that validates a buffer against a required size SHALL accept any buffer whose capacity is at least that size, and SHALL operate only on the region it was asked to process. An oversized buffer SHALL NOT change a consumer's result.

#### Scenario: An oversized scratch buffer is accepted

- **WHEN** an algorithm is given a scratch buffer larger than the size its geometry requires
- **THEN** the call succeeds
- **AND** the result is identical to the same call with an exactly sized buffer

#### Scenario: Trailing capacity is not processed

- **WHEN** a pass runs over a buffer whose capacity exceeds the region it was asked to process
- **THEN** no element beyond the requested region is read or written
- **AND** the trailing capacity keeps its previous contents
