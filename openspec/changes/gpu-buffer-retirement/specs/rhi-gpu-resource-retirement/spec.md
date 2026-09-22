# rhi-gpu-resource-retirement

## Purpose

Defines how GPU resources are released safely while work is in flight: every resource retirement is associated with a submission epoch, and device memory is freed only once every submission that may reference the resource has completed.

## ADDED Requirements

### Requirement: Submission epochs identify a completion promise

A submission epoch SHALL be identified by a monotonically increasing watermark issued by the device-scoped retirement facility. An epoch represents the GPU work covered by one completion signal; it MUST NOT be defined in terms of command buffers submitted or presentation frames.

Every submission that can reference GPU-allocated resources SHALL be associated with exactly one epoch, and a submitter SHALL be able to open more than one epoch between two presentations.

The facility MUST NOT expose presentation or render-frame vocabulary (no back-buffer index, frame index, or frames-in-flight counter) in its public interface.

#### Scenario: Two epochs per presentation

- **WHEN** a frame performs a blocking upload submission before the frame's command batch is submitted
- **THEN** the upload submission and the frame batch are each associated with their own epoch
- **AND** the two epochs carry distinct, increasing watermarks

#### Scenario: No presentation vocabulary

- **WHEN** the retirement facility's public interface and documentation are inspected
- **THEN** they contain no back-buffer, frame-index, or frames-in-flight concept

### Requirement: Retired allocations are parked, not freed

Destroying a buffer allocation SHALL NOT free the underlying device memory while a submission that may reference it is outstanding. The allocation SHALL instead be parked under **the watermark in effect at the moment it was released** — the newest outstanding epoch watermark — and released once the completed watermark reaches it.

The parking watermark is a property of *when the allocation was released*, not of when it was created. Release-time is sufficient because the epochs that can reference an allocation are exactly those at or below the release watermark: an epoch whose recording begins after the release cannot bind the allocation, since binding a released allocation is not a legitimate operation. The argument rests only on that, and not on epochs being non-nested.

The facility does not track which epoch last bound an allocation, so an allocation released long after its last use is still parked under the release-time watermark. This is deliberate: the release moment is the caller's decision, and the extra retention is bounded by the in-flight depth.

This guarantee SHALL apply to every allocation made through the device allocator, without opt-in by the allocation site.

#### Scenario: Resize during in-flight frames

- **WHEN** a buffer is resized while two earlier frame batches are still executing on the GPU
- **THEN** the replaced allocation's device memory is not freed at that moment
- **AND** it is released only after the newest outstanding epoch has been reported complete

#### Scenario: Release-time watermark governs, not last use

- **WHEN** an allocation is released while several newer epochs are outstanding, even though it was last bound many epochs earlier
- **THEN** it is parked under the watermark in effect at the release moment
- **AND** it is released only once the completed watermark reaches that value

#### Scenario: No opt-in required

- **WHEN** a buffer is allocated and destroyed through the existing allocator path without any retirement-specific call
- **THEN** its destruction still obeys the parking rule

### Requirement: Completion is reported in order

A submitter SHALL report an epoch complete only after observing the completion signal of the submission that carries that epoch. Reported epochs SHALL advance a completed watermark only through the contiguous prefix of reported epochs, so that a completion reported out of order MUST NOT release resources parked under an earlier, still-outstanding epoch.

#### Scenario: Out-of-order completion does not release early

- **WHEN** a blocking submission's epoch is reported complete while an earlier epoch remains outstanding
- **THEN** resources parked under the earlier epoch are retained
- **AND** they are released once that earlier epoch is reported complete

#### Scenario: In-order completions release everything below the watermark

- **WHEN** consecutive epochs are reported complete
- **THEN** all resources parked under watermarks at or below the advanced completed watermark are released

### Requirement: Immediate release when the parking watermark is already complete

The facility SHALL release a retired allocation immediately, without parking it, when the watermark it would be parked under is already at or below the completed watermark.

This is the **only** immediate-release condition. In particular, an allocation that has been bound into a command buffer that is recorded but not yet submitted MUST NOT be released early: the command buffer references it and will be submitted later, so releasing it would leave that submission referencing destroyed memory. "Not yet submitted" does not mean "not yet referenced".

The consequence for callers is that buffers bound into a frame's recording are retained until that frame's epoch completes. Hot loops are expected to reuse a buffer they already own (grow-only capacity) rather than allocate a fresh one per call; a per-call temporary is retained until its epoch completes, which is correct rather than wasteful.

#### Scenario: Staging released after its submission is proven complete

- **WHEN** a submitter waits on its own submission's fence, reports that epoch complete, and only then releases the batch's staging allocations
- **THEN** those allocations are released immediately rather than being parked for later epochs

#### Scenario: Recorded-but-unsubmitted work keeps its allocations alive

- **WHEN** an allocation is bound into a command buffer being recorded and released again before that command buffer is submitted
- **THEN** the allocation is parked under the watermark of the epoch that recorded it
- **AND** it is released only once that epoch has completed
- **AND** it is NOT released merely because no submission has been issued yet

### Requirement: Submitter protocol

A submitter of GPU work SHALL:

1. open an epoch before recording work that can allocate or retire device resources;
2. report the epoch complete only after the completion signal for every submission issued within it has been observed;
3. abandon an epoch whose submission was skipped or failed, so that it does not retain resources indefinitely;
4. release all parked resources at any point where the device is known to be idle.

The release in step 4 applies only to resources that have already been retired and are waiting for their parking watermark. It MUST NOT be read as authority to discard resources that are still live — a device-idle wait proves that submitted work has finished, not that a live resource has no owner.

An epoch MAY contain more than one submission. The facility does not need to observe individual submissions: the parking rule depends only on the watermark in effect when an allocation is released, and a single report covers every submission made within the epoch.

The protocol MUST be usable from a single thread without introducing thread synchronization primitives.

#### Scenario: Skipped frame does not strand an epoch

- **WHEN** a frame is abandoned before its command batch is submitted (for example because the presentation surface is unavailable)
- **THEN** no epoch is left open for that frame
- **AND** no resources are stranded under a watermark that can never complete

#### Scenario: Device-idle points release parked resources

- **WHEN** the caller establishes that all GPU work has finished (a device-wide idle wait)
- **THEN** all parked resources are released before the call returns
- **AND** live resources are left untouched

#### Scenario: Failed submission abandons its epoch

- **WHEN** a submission fails or throws after its epoch was opened
- **THEN** the epoch is abandoned and the resources parked under it are released

### Requirement: Headless operation without a frame loop

The retirement facility SHALL operate when no presentation frame loop exists, including when every submission is a blocking batch that is waited on immediately.

#### Scenario: Standalone compute workload

- **WHEN** a headless program allocates, resizes and destroys buffers across several blocking submissions with no frame loop
- **THEN** every retired allocation is released within the same submission cycle
- **AND** no device memory is retained after the program's device-idle teardown
