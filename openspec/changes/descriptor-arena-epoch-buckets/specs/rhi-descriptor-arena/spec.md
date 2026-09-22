# rhi-descriptor-arena

## Purpose

Defines the device-scoped descriptor arena: the single facility that owns descriptor pools and hands out descriptor sets, keeping them resident and reusable across epochs and releasing them under one of two triggers — cache pressure for per-dispatch compute bindings, and an explicit owner release for long-lived material, scene and camera state — with on-demand pool growth instead of a fixed set budget.

## ADDED Requirements

### Requirement: Descriptor sets come from a device-scoped arena

Descriptor sets SHALL be obtained from a single device-scoped arena reachable from the device facilities. No consumer — compute pipeline object, material, scene data, camera data, or render system — SHALL create, own, or reset a descriptor pool for its own sets.

When the pool a request would be served from cannot satisfy it, the arena SHALL create an additional pool rather than fail the request. The arena MUST NOT impose a fixed maximum number of descriptor sets per consumer.

#### Scenario: Acquiring beyond the previous per-consumer budget

- **WHEN** a single consumer acquires more descriptor sets with distinct binding content than one pool was created for
- **THEN** each acquisition succeeds
- **AND** the arena has created the additional pools it needed

#### Scenario: No consumer-owned pool remains

- **WHEN** the repository is searched for descriptor-pool creation outside the arena
- **THEN** no compute, material, scene-data or camera consumer creates a descriptor pool
- **AND** no consumer exposes a pool to its callers

#### Scenario: Arena available without a render system

- **WHEN** a headless program creates the device facilities with no render system
- **THEN** descriptor sets can be acquired from the arena

### Requirement: Sets are resident and reused across epochs

The arena SHALL keep acquired sets in a content-keyed store and SHALL return the same set for a repeated request of the same descriptor-set layout and binding content, **including across epoch boundaries**. Re-acquiring a set SHALL refresh the epoch recorded for it; it MUST NOT cause a new set to be created.

A set SHALL be released only when the epoch recorded for it is at or below the completed prefix, so that a set referenced by an outstanding epoch is never released. A report for an entry's own epoch is NOT sufficient: earlier epochs may still be executing command buffers that bind the same set, so only the prefix makes an entry eligible. The arena MUST NOT rewrite a set's descriptors in place while it is resident.

#### Scenario: Steady-state reuse mints nothing

- **WHEN** the same dispatch is recorded in consecutive epochs with identical binding content
- **THEN** each epoch obtains the same descriptor set
- **AND** no new set is created after the first epoch

#### Scenario: Outstanding epoch retains its sets

- **WHEN** an epoch is submitted but not yet reported complete
- **THEN** the sets acquired in that epoch are retained
- **AND** they are released only after completion is reported and only when the arena is under pressure to do so

#### Scenario: Interning within one epoch

- **WHEN** two passes in the same epoch request the same descriptor set layout and the same binding content
- **THEN** both receive the same descriptor set

#### Scenario: Completion is driven by the completed prefix, not by a single report

- **WHEN** an epoch is reported complete while an earlier epoch is still outstanding
- **THEN** no set recorded against the earlier epoch is released
- **AND** sets become releasable only as the completed prefix advances past the epochs recorded for them

### Requirement: Pressure-driven sets carry a re-acquisition contract

A set acquired through the pressure-driven trigger SHALL be re-acquired in every epoch whose command buffers use it, and a handle to such a set MUST NOT be held across epochs by the caller.

This contract is what makes the recorded epoch an upper bound on the epochs that can reference the set. A caller that needs to hold a set handle across epochs SHALL use the owner-driven trigger instead.

#### Scenario: A dispatching kernel refreshes its sets

- **WHEN** a kernel dispatches in an epoch
- **THEN** every set it binds is re-acquired and its recorded epoch is refreshed

#### Scenario: A kernel that stops dispatching stops refreshing

- **WHEN** a kernel does not dispatch in an epoch
- **THEN** its sets keep the epoch recorded at their last acquisition
- **AND** they become releasable once the completed prefix reaches that epoch

#### Scenario: A caller that holds a handle across epochs uses the owner-driven trigger

- **WHEN** a caller needs a descriptor set to remain valid across frames without re-acquiring it
- **THEN** it obtains the set through the owner-driven trigger
- **AND** the set remains valid until the caller releases it

### Requirement: Eviction is epoch-guarded and the budget is soft

The arena SHALL reclaim pressure-driven entries only when it is over a configured resident budget, and only among entries that are **eligible** — claimed by an epoch and recorded at or below the completed prefix. Among eligible entries it SHALL evict the least recently acquired first.

An entry that is not eligible MUST NOT be evicted, even when the arena is over budget: exceeding the budget is preferable to releasing a set an outstanding submission may reference. The budget is therefore a soft cap.

#### Scenario: Eviction under pressure

- **WHEN** the resident budget is exceeded and eligible entries exist
- **THEN** the least recently acquired eligible entries are released
- **AND** the resident count returns to at or below the budget

#### Scenario: Ineligible entries are never evicted

- **WHEN** the resident budget is exceeded and every entry is still referenced by an outstanding epoch
- **THEN** no entry is released
- **AND** the arena exceeds the budget until the completed prefix advances past those entries' recorded epochs

#### Scenario: A fully serialised submitter still reuses its sets

- **WHEN** a caller completes each epoch before opening the next, so that no set is ever re-acquired before its epoch completes
- **THEN** its sets are retained across epochs as long as the arena is within budget
- **AND** they are not released merely because their epoch completed

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

### Requirement: Owner-driven sets are valid until released

A set acquired through the owner-driven trigger SHALL remain valid until its owner releases it or the cache that owns it evicts it. The arena SHALL return released sets to its pools so their descriptors are reusable.

The release SHALL be deferred when the epoch recorded for the set is still outstanding; a set MUST NOT be freed while a command buffer that binds it may still be executing.

#### Scenario: Owner-driven set reused across frames

- **WHEN** a material instance is drawn for many frames with unchanged binding content
- **THEN** it reuses the same descriptor set
- **AND** no new set is acquired for it during that time

#### Scenario: Release on owner destruction

- **WHEN** the object owning a set is destroyed
- **THEN** the set is released back to the arena
- **AND** the arena's live set count decreases

#### Scenario: Release does not free a set still in use

- **WHEN** a release is requested for a set whose recorded epoch is still outstanding
- **THEN** the release is deferred
- **AND** the set is reclaimed only once the completed prefix reaches that epoch

### Requirement: Descriptor-set layouts are shared

The arena SHALL obtain descriptor-set layouts from the device's immutable resource cache, so that equal layout descriptions resolve to one layout object.

#### Scenario: Equal layouts resolve to one object

- **WHEN** two acquisitions request the same descriptor set layout description
- **THEN** both are allocated against the same layout object

### Requirement: Headless operation without a frame loop

The arena SHALL operate with no presentation frame loop. A device-idle wait SHALL NOT by itself release resident sets: it advances the completed prefix, which makes entries eligible, but reclamation still requires budget pressure or an owner release.

#### Scenario: Standalone compute workload

- **WHEN** a headless program records and submits compute work across several epochs with no frame loop
- **THEN** its sets are reused across those epochs while the arena is within budget
- **AND** every set and pool is released when the arena is destroyed

#### Scenario: Device-idle does not discard a live cache

- **WHEN** a caller waits for the device to be idle between two epochs and then records the same binding content again
- **THEN** the resident entries survive the idle wait
- **AND** the second epoch reuses them rather than minting new sets
