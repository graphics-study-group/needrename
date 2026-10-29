# rhi-resource-identity Specification

## Purpose

Gives every descriptor-relevant Vulkan object a never-reused identity carried by typed binding references, so that caches and binding flows distinguish resources by identity instead of by driver-assigned handle values.

## Requirements

### Requirement: Every descriptor-relevant object bears a never-reused identity

Every descriptor-relevant Vulkan object — a buffer allocation, an image view, a sampler, a descriptor-set layout — SHALL bear an identity issued when the object is created. An identity MUST NOT be issued again after the object it names is destroyed, and no two live objects SHALL share an identity.

Replacing a buffer's storage in place SHALL issue a new identity for the replacement allocation: identity follows the underlying Vulkan object, not the wrapper object. Moving a wrapper object SHALL NOT change the identity of the object it holds.

#### Scenario: A destroyed object's identity is never seen again

- **WHEN** a buffer allocation is destroyed and many further allocations are created
- **THEN** none of the new allocations bears the destroyed allocation's identity

#### Scenario: Replacing storage changes identity

- **WHEN** a buffer's storage is replaced in place (exact-size reallocation or geometric growth)
- **THEN** the replacement allocation bears a new identity
- **AND** the wrapper object keeps its own address and interface

#### Scenario: Moving a wrapper keeps identity

- **WHEN** a buffer or texture wrapper is moved from one owner to another
- **THEN** the held Vulkan object keeps the identity it was issued at creation

### Requirement: Binding references carry identity and come only from owning wrappers

A binding reference to a buffer, image view, sampler or descriptor-set layout SHALL carry both the Vulkan handle and the object's identity. Such a reference SHALL be constructible only from the RHI facility that owns the object; there MUST NOT be a construction path from a bare handle.

Two binding references SHALL be equal exactly when they name the same object identity. Hashing or comparing a binding MUST use identity, never the raw handle value.

#### Scenario: A bare handle cannot become a binding identity

- **WHEN** code attempts to construct a binding reference from a bare Vulkan handle
- **THEN** the attempt does not compile

#### Scenario: Equal references name one object

- **WHEN** two references are obtained from the same wrapper without an intervening storage replacement
- **THEN** they are equal and hash equally

#### Scenario: Distinct objects are distinct even when handles collide

- **WHEN** two live objects hold different identities but the driver has issued their handles the same value at different times
- **THEN** references to them are not equal

### Requirement: Binding windows are distinct from identity

The offset and range that restrict a binding to a window of a buffer SHALL be part of a binding's content but MUST NOT be part of its reference identity. Two bindings of the same object at different windows SHALL be distinct contents.

#### Scenario: One buffer at two windows

- **WHEN** one buffer is bound at two different offset/range windows
- **THEN** the two bindings are distinct contents and are not conflated

### Requirement: Bindings resolve live at acquisition

A binding that names a resource object SHALL resolve to that object's current handle and identity at acquisition time, not at the time the binding was declared. A resource whose storage was replaced after it was bound SHALL therefore resolve to its replacement.

The caller carries one obligation: a resource SHALL neither be destroyed nor moved while a declared binding may still name it. A binding that names a destroyed or moved-from resource is undefined behaviour; the facility documents this obligation and provides no release-mode mitigation for violating it.

#### Scenario: Storage replacement self-heals

- **WHEN** a resource is bound by name, its storage is then replaced in place, and the binding is acquired
- **THEN** the acquired content names the replacement allocation's handle and identity

#### Scenario: A declared binding outlives nothing

- **WHEN** a resource is destroyed while a declared binding still names it
- **THEN** behaviour is undefined and the obligation is recorded in the interface documentation

### Requirement: Raw handles are record-time currency only

Raw Vulkan handle accessors on the RHI wrappers SHALL remain available for command recording and interop. Such a handle MUST NOT serve as identity anywhere: any store that keys, caches or compares bound resources SHALL use binding references instead.

#### Scenario: Recording keeps raw handles

- **WHEN** a command buffer records copies, barriers or index/vertex binds
- **THEN** it uses raw handles from the wrappers without any identity bookkeeping

#### Scenario: A cache never keys on handle values

- **WHEN** the repository is searched for cached or keyed binding content
- **THEN** every comparison and hash over bound resources uses object identity
- **AND** no keyed store compares a buffer, view or sampler by raw handle value
