# rhi-descriptor-arena

## Purpose

Defines the device-scoped descriptor arena: the single facility that owns descriptor pools and hands out descriptor sets through two services — a **pool layer** that allocates a set for a layout without knowing its contents, and a **content-keyed cache layer** built on it that resolves, writes, keeps sets resident and reuses them across submission epochs. The cache layer reclaims only under a soft resident budget and only among entries the completed prefix has passed, so a set an outstanding submission may reference is never released.

## ADDED Requirements

### Requirement: Descriptor sets come from a device-scoped arena

Descriptor sets SHALL be obtained from a single device-scoped arena reachable from the device facilities. No consumer — compute pipeline object, material, scene data, camera data, or render system — SHALL create, own, or reset a descriptor pool **for the sets the engine acquires for its own bindings**.

The pool an ImGui backend creates and hands to the GUI library is deliberately outside this rule: the GUI library allocates and frees those sets itself, the engine never acquires one of them from the arena, and the arena cannot key or reclaim them.

When the pool a request would be served from cannot satisfy it, the arena SHALL create an additional pool rather than fail the request. The arena MUST NOT impose a fixed maximum number of descriptor sets per consumer.

#### Scenario: Acquiring beyond the previous per-consumer budget

- **WHEN** a single consumer acquires more descriptor sets with distinct binding content than one pool was created for
- **THEN** each acquisition succeeds
- **AND** the arena has created the additional pools it needed

#### Scenario: No engine-owned pool remains

- **WHEN** the repository is searched for descriptor-pool creation outside the arena
- **THEN** no compute, material, scene-data or camera consumer creates a descriptor pool
- **AND** no consumer exposes a pool to its callers
- **AND** the only remaining pool is the GUI backend's, which the GUI library allocates and frees from itself

#### Scenario: Arena available without a render system

- **WHEN** a headless program creates the device facilities with no render system
- **THEN** descriptor sets can be acquired from the arena

### Requirement: Sets are resident and reused across epochs

The arena SHALL keep acquired sets in a store keyed by the descriptor-set layout it resolved through the device's immutable resource cache, the dynamic-offset flags, and the resolved binding content, and SHALL return the same set for a repeated request of that key, **including across epoch boundaries**. Re-acquiring a set SHALL refresh the epoch recorded for it; it MUST NOT cause a new set to be created.

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

### Requirement: A set is re-acquired in every epoch that uses it

A set acquired from the cache layer SHALL be re-acquired in every epoch whose command buffers use it, and a handle to such a set MUST NOT be held across epochs by the caller.

This contract is what makes the recorded epoch an upper bound on the epochs that can reference the set. The API SHALL make the obligation structural rather than documentary: an acquisition SHALL return the set together with the data needed to bind it, so that no caller is handed a handle it can store for a later epoch.

#### Scenario: A dispatch refreshes its sets

- **WHEN** a dispatch binds its compute resources
- **THEN** every set it uses is re-acquired and its recorded epoch is refreshed

#### Scenario: A material bind refreshes its set

- **WHEN** a material is bound for drawing
- **THEN** its set is re-acquired for that bind rather than read from a handle stored by an earlier frame

#### Scenario: A set that stops being requested stops being refreshed

- **WHEN** no command buffer requests a set's content in an epoch
- **THEN** the set keeps the epoch recorded at its last acquisition
- **AND** it becomes eligible for reclamation once the completed prefix reaches that epoch

### Requirement: Reclamation is pressure-driven and the budget is soft

The arena SHALL reclaim entries only when it is over a configured resident budget, and only among entries that are **eligible** — claimed by an epoch and recorded at or below the completed prefix. Among eligible entries it SHALL evict the least recently acquired first.

An entry that is not eligible MUST NOT be evicted, even when the arena is over budget: exceeding the budget is preferable to releasing a set an outstanding submission may reference. The budget is therefore a soft cap.

The arena SHALL read the completed prefix from the retirement facility when it evaluates reclamation. It SHALL NOT require, and SHALL NOT depend on, any notification of prefix advancement.

#### Scenario: Eviction under pressure

- **WHEN** the resident budget is exceeded and eligible entries exist
- **THEN** the least recently acquired eligible entries are released
- **AND** the resident count returns to at or below the budget

#### Scenario: Ineligible entries are never evicted

- **WHEN** the resident budget is exceeded and every entry is still referenced by an outstanding epoch
- **THEN** no entry is released
- **AND** the arena exceeds the budget until the completed prefix advances past those entries' recorded epochs

#### Scenario: Completion is driven by the completed prefix, not by a single report

- **WHEN** an epoch is reported complete while an earlier epoch is still outstanding
- **THEN** no set recorded against the earlier epoch is released
- **AND** sets become eligible only as the completed prefix advances past the epochs recorded for them

#### Scenario: A fully serialised submitter still reuses its sets

- **WHEN** a caller completes each epoch before opening the next, so that no set is ever re-acquired before its epoch completes
- **THEN** its sets are retained across epochs as long as the arena is within budget
- **AND** they are not released merely because their epoch completed

#### Scenario: No release entry point exists

- **WHEN** a caller wants a set it no longer needs to be reclaimed
- **THEN** it simply stops requesting that content
- **AND** the arena reclaims the entry after the completed prefix has passed its recorded epoch and the arena is over budget

### Requirement: Sets acquired outside an epoch are pinned until claimed

Descriptor sets acquired while no epoch is open SHALL be marked unclaimed and MUST NOT be evicted while unclaimed, because the command buffer that bound them has not been submitted and will be submitted under a later epoch.

An epoch SHALL claim every unclaimed set when it opens, recording that epoch against them. An unclaimed set for which no further epoch is ever opened SHALL be released only when the arena is destroyed.

#### Scenario: Recording precedes the epoch

- **WHEN** a caller records compute work without an open epoch, then submits it under a newly opened epoch
- **THEN** the sets recorded in that work remain valid until that epoch completes
- **AND** they are not evicted in the interval before that epoch opens

#### Scenario: Consecutive recording-then-submitting rounds

- **WHEN** a caller records and submits one batch, then records and submits another batch, without an intervening epoch open at recording time
- **THEN** each batch's sets are recorded against the epoch that claims them
- **AND** neither batch's sets are released before their own epoch completes

#### Scenario: Recorded but never submitted

- **WHEN** compute work is recorded and never submitted, and no epoch is ever opened afterwards
- **THEN** its sets are retained for the arena's lifetime
- **AND** they are released when the arena is destroyed

### Requirement: The arena allocates sets whose descriptors the caller writes

The arena SHALL allocate a descriptor set for a requested layout and hand it to the caller without recording, keying, interning or rewriting its contents, for consumers whose descriptor content the arena cannot express — long-lived state whose content changes per frame, and descriptor arrays.

Such a set SHALL NOT be reclaimed by the arena: the arena cannot observe whether its owner still holds the handle, nor the last epoch that bound it. It SHALL remain valid until the arena is destroyed. The arena SHALL NOT require an owner identity for it, because it is never shared.

The caller carries one obligation: a set whose descriptors the caller writes MUST NOT be rewritten while a command buffer that binds it may still be executing.

#### Scenario: Long-lived state keeps its sets

- **WHEN** a consumer obtains a set for a layout through this service
- **THEN** the set remains valid until the arena is destroyed
- **AND** no eviction pressure releases it

#### Scenario: The same layout yields distinct sets

- **WHEN** two requests name the same descriptor set layout
- **THEN** each receives a distinct descriptor set, because these sets are not interned

#### Scenario: The caller must not rewrite a set an in-flight submission binds

- **WHEN** a caller wants to rewrite the descriptors of a set it wrote
- **THEN** it must first establish that no command buffer binding that set is still executing
- **AND** an in-flight rewrite leaves the arena nothing to detect it with, because the arena never sees these contents

### Requirement: Descriptor-set layouts are shared

The arena SHALL obtain descriptor-set layouts from the device's immutable resource cache, so that equal layout descriptions resolve to one layout object. Consumers that build a pipeline layout over a descriptor set layout SHALL obtain that layout from the same cache, so that the layout used for a pipeline and the layout a set is allocated against are the same object.

#### Scenario: Equal layouts resolve to one object

- **WHEN** two acquisitions request the same descriptor set layout description
- **THEN** both are allocated against the same layout object

#### Scenario: A compute stage's allocation layout is its pipeline layout's layout

- **WHEN** a compute pipeline object is created and a set is later allocated for the same bindings
- **THEN** both name the object the resource cache returned for that description
- **AND** no separate layout is created outside the cache

### Requirement: Headless operation without a frame loop

The arena SHALL operate with no presentation frame loop. A device-idle wait SHALL NOT by itself release resident sets: it makes entries eligible by recording that everything issued so far is complete, but reclamation still requires budget pressure.

#### Scenario: Standalone compute workload

- **WHEN** a headless program records and submits compute work across several epochs with no frame loop
- **THEN** its sets are reused across those epochs while the arena is within budget
- **AND** every set and pool is released when the arena is destroyed

#### Scenario: Device-idle does not discard a live cache

- **WHEN** a caller waits for the device to be idle between two epochs and then records the same binding content again
- **THEN** the resident entries survive the idle wait
- **AND** the second epoch reuses them rather than minting new sets

#### Scenario: Device-idle makes entries eligible without releasing them

- **WHEN** a device-idle wait is broadcast to the arena and a later acquisition exceeds the budget
- **THEN** entries that were ineligible only because their epochs had not been reported are reclaimable
- **AND** no entry was released by the idle wait itself

#### Scenario: Sets acquired after the idle wait stay ineligible

- **WHEN** a set is acquired after a device-idle wait and its epoch is not yet reported
- **THEN** the idle wait does not make that set eligible
