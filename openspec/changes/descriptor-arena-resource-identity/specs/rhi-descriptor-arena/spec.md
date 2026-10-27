# rhi-descriptor-arena (delta)

## MODIFIED Requirements

### Requirement: Sets are resident and reused across epochs

The arena SHALL keep acquired sets in a store keyed by the descriptor-set layout it resolved through the device's immutable resource cache, the dynamic-offset flags, and the resolved binding content, and SHALL return the same set for a repeated request of that key, **including across epoch boundaries**. Re-acquiring a set SHALL refresh the epoch recorded for it; it MUST NOT cause a new set to be created.

Binding content is identified by the **object identity** of every bound resource together with its binding window (offset and range), never by a raw handle value. Two requests whose resources hold different identities SHALL NOT share an entry even when the driver has issued their handles equal values at different times, and a request whose resource was destroyed and replaced SHALL NOT match an entry written for the destroyed object.

The arena itself SHALL resolve the bound resources into descriptor writes and write them when it creates an entry. It MUST NOT rewrite a resident entry's descriptors, because the entry's identity is its content.

#### Scenario: Steady-state reuse mints nothing

- **WHEN** the same dispatch or the same material bind is recorded in consecutive epochs with identical binding content
- **THEN** each epoch obtains the same descriptor set
- **AND** no new set is created after the first epoch

#### Scenario: Interning within one epoch

- **WHEN** two requests in the same epoch name the same descriptor set layout and the same binding content
- **THEN** both receive the same descriptor set

#### Scenario: Equal layouts resolve to one key

- **WHEN** two requests describe equal layout bindings
- **THEN** both are keyed on the same descriptor-set layout object, because the arena resolves layouts through the immutable resource cache rather than from a reflected-layout object's address

#### Scenario: A resident entry is never rewritten

- **WHEN** a caller requests a set whose content differs from that of a resident entry
- **THEN** the arena creates a distinct entry rather than rewriting the resident one

#### Scenario: Recycled handle values do not alias distinct resources

- **WHEN** a resource bound in a resident entry is destroyed, a later resource is created and the driver issues its handle the value of the destroyed one, and the later resource is bound at the same window
- **THEN** the request does not hit the resident entry
- **AND** the arena mints a distinct entry whose descriptors name the new object

#### Scenario: A replaced buffer is distinct from its predecessor

- **WHEN** a buffer's storage is replaced in place and the same binding is acquired afterwards
- **THEN** the acquisition is a miss against the entry written for the old storage
- **AND** the new entry's descriptors name the replacement allocation

## ADDED Requirements

### Requirement: Distinct contents are never conflated

Two distinct binding contents that produce the same content hash SHALL remain distinct resident entries. A request SHALL be answered only with a set whose stored content is exactly the requested content; a hash collision MUST NOT cause a request to receive a set written for different content, and MUST NOT discard either content's resident entry.

#### Scenario: A hash collision keeps both contents

- **WHEN** two distinct binding contents hash to the same content hash
- **THEN** both remain resident as distinct entries
- **AND** each request receives the set written for its exact content

#### Scenario: A colliding request is never answered with the other content

- **WHEN** a request's content hash matches a resident entry whose content differs
- **THEN** no set written for the differing content is ever returned for that request
