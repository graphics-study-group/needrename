# rhi-gpu-resource-retirement

## Purpose

Defines how GPU resources are released safely while work is in flight: every resource retirement is parked under a submission epoch, and device memory is freed only once every submission that may reference the resource has completed.

## ADDED Requirements

### Requirement: Submission epochs identify a completion promise

A submission epoch SHALL be identified by a monotonically increasing watermark issued by the device-scoped retirement facility. An epoch represents the GPU work covered by one completion signal; it MUST NOT be defined in terms of command buffers submitted or presentation frames.

Every submission that can reference GPU-allocated resources SHALL be associated with exactly one epoch, and a submitter SHALL be able to open more than one epoch between two presentations.

The facility MUST NOT expose presentation or render-frame vocabulary (no back-buffer index, frame index, or frames-in-flight counter) in its public interface.

#### Scenario: Two epochs per presentation

- **WHEN** a frame performs a staged-upload submission before the frame's command batch is submitted
- **THEN** the upload submission and the frame batch are each associated with their own epoch
- **AND** the two epochs carry distinct, increasing watermarks

#### Scenario: No presentation vocabulary

- **WHEN** the retirement facility's public interface and documentation are inspected
- **THEN** they contain no back-buffer, frame-index, or frames-in-flight concept

### Requirement: A report proves one epoch; the prefix is derived from many

A completion report SHALL be taken as proof of exactly the epoch it names and MUST NOT be taken as proof that any other epoch has completed. The facility has no way to observe a queue submission by itself, so the only fact a report carries is "the submission carrying this epoch has finished".

The facility SHALL maintain the set of individually reported epochs and derive from it a **completed prefix**: the largest watermark such that every watermark at or below it has been reported. The prefix advances only through that contiguous run, so a report for a later epoch MUST NOT advance it past an earlier, unreported epoch.

The prefix is therefore a conservative statement about a contiguous range, not a synonym for "completion". Both facts below hold at once and the distinction is load-bearing:

- an epoch can be reported while the prefix is far below it;
- the prefix can be far below an epoch whose GPU work finished long ago.

#### Scenario: A report for a later epoch does not advance the prefix

- **WHEN** an epoch is reported complete while an earlier epoch is still unreported
- **THEN** the completed prefix does not advance past the earlier epoch
- **AND** resources whose release depends on the prefix are retained

#### Scenario: The prefix advances through a contiguous run

- **WHEN** every epoch up to and including some watermark has been reported
- **THEN** the prefix advances to that watermark in one step
- **AND** resources waiting on the prefix up to that watermark are released

### Requirement: Retired resources are parked under one of two modes

A retired resource SHALL be parked under exactly one of two modes. Its release condition SHALL be evaluated when it is parked and again whenever the facility's knowledge changes — that is, after every report. A resource whose condition already holds when it is parked SHALL be released immediately; there is no separate immediate-release rule.

**Upper-bound mode — for resources whose referencing epochs are unknown.** The caller supplies an upper bound: the referencing epochs are a subset of the epochs at or below it. The resource is released when the completed prefix reaches that bound. A buffer allocation destroyed through the device allocator takes this mode by default, with the newest watermark issued at the moment of release as its bound.

The bound is sound because a resource released at time T cannot be bound by an epoch whose recording begins after T: binding a released resource is not a legitimate operation. This holds regardless of how epochs overlap in time, and it does not require epochs to be non-nested.

**Exact mode — for resources whose referencing epoch the caller knows.** The caller names the single epoch whose command buffers reference the resource. The resource is released as soon as that epoch is reported; the prefix is not consulted, because the report for that epoch is itself the proof that the submission carrying it has completed.

A caller that names an epoch which has not been reported SHALL have its resource retained until that epoch is reported. An incorrect exact claim therefore costs retention, never safety: the facility still waits for a report it can verify rather than trusting the claim.

#### Scenario: Upper-bound parking waits for the prefix

- **WHEN** a buffer is destroyed while several earlier epochs are still outstanding
- **THEN** it is parked under the newest watermark issued at that moment
- **AND** it is released only once the completed prefix reaches that watermark

#### Scenario: The bound is the release moment, not the last use

- **WHEN** an allocation is released while several newer epochs are outstanding, even though it was last bound many epochs earlier
- **THEN** it is parked under the watermark in effect at the release moment
- **AND** it is released only once the completed prefix reaches that value

#### Scenario: An allocation bound into a recorded but unsubmitted command buffer is not released early

- **WHEN** an allocation is bound into a command buffer being recorded and released again before that command buffer is submitted
- **THEN** it is parked under the watermark of the epoch that recorded it
- **AND** it is released only once the completed prefix reaches that watermark
- **AND** it is NOT released merely because no submission has been issued yet

#### Scenario: Exact parking releases on that epoch's own report

- **WHEN** a submitter releases the staging of a batch after waiting the fence of the submission that carried it, naming that submission's epoch
- **THEN** the staging is released as soon as that epoch is reported
- **AND** it is not retained until the prefix reaches that epoch

#### Scenario: An exact claim naming an unreported epoch is retained, not trusted

- **WHEN** a caller releases a resource under the exact mode naming an epoch that has not been reported
- **THEN** the resource is retained
- **AND** it is released when that epoch is reported, not before

#### Scenario: No opt-in required

- **WHEN** a buffer is allocated and destroyed through the existing allocator path without any retirement-specific call
- **THEN** its destruction is parked under the upper-bound mode

### Requirement: Submitter protocol

A submitter of GPU work SHALL:

1. open an epoch before recording work that can allocate or retire device resources;
2. report an epoch complete only after observing the completion signal of a submission **it issued itself** for that epoch;
3. abandon an epoch that produced no GPU work;
4. release all parked resources at any point where the device is known to be idle.

A submitter MUST NOT report an epoch opened by another submitter. A report asserts that the submission carrying that epoch has completed, and only the submitter that issued that submission can observe its completion signal; reporting on another's behalf would assert a completion that has not been observed.

Abandoning an epoch SHALL mark it as reported without a completion signal, so that the completed prefix can advance past it. Abandonment MUST NOT release resources directly: a resource parked under an abandoned epoch may still be referenced by an earlier, unreported epoch, so it is released only when the completed prefix reaches its bound. Abandonment is valid only for an epoch for which no submission was issued; if a submission was issued and its completion cannot be observed, that failure MUST be surfaced rather than masked by abandonment.

The release in step 4 applies only to resources that have already been retired and are waiting for their release condition. It MUST NOT be read as authority to discard resources that are still live — a device-idle wait proves that submitted work has finished, not that a live resource has no owner.

An epoch MAY contain more than one submission, and a submitter MAY open several epochs per presentation. The facility does not need to observe individual submissions.

The protocol MUST be usable from a single thread without introducing thread synchronization primitives.

#### Scenario: A submitter does not report an epoch it did not open

- **WHEN** a submitter completes its own work while an epoch opened by a different submitter is still in flight
- **THEN** it reports only the epochs it opened
- **AND** the other epoch remains outstanding until its own submitter observes its completion signal

#### Scenario: An abandoned epoch does not release its parked resources

- **WHEN** an epoch is abandoned while earlier epochs are still unreported
- **THEN** the abandoned epoch is treated as reported
- **AND** resources parked under it are retained until the completed prefix reaches them
- **AND** no resource is released merely because its epoch was abandoned

#### Scenario: Skipped frame does not strand an epoch

- **WHEN** a frame is abandoned before its command batch is submitted (for example because the presentation surface is unavailable)
- **THEN** no epoch is left open for that frame
- **AND** no resources are stranded under a watermark that can never be reported

#### Scenario: Device-idle points release parked resources

- **WHEN** the caller establishes that all GPU work has finished (a device-wide idle wait)
- **THEN** all parked resources are released before the call returns
- **AND** live resources are left untouched

### Requirement: Prefix advancement depends on every submitter reporting

The completed prefix advances only when submitters report. The facility SHALL therefore document, and the submitters SHALL honour, that every epoch eventually reaches exactly one of the two terminal states: reported (with or without a completion signal) or abandoned.

A submitter that stops reporting strands every resource parked under its epochs and every resource parked under any later epoch, because the prefix cannot advance past it. The render side's frame-to-frame serialisation only bounds how long a lag lasts; it is not what makes the mechanism correct.

#### Scenario: An unreported epoch delays everything above it

- **WHEN** a submitter opens an epoch and never reports or abandons it
- **THEN** the prefix stops at the epoch before it
- **AND** resources parked under that epoch and every later one are retained
- **AND** the condition is observable through the outstanding-epoch diagnostic

### Requirement: Headless operation without a frame loop

The retirement facility SHALL operate when no presentation frame loop exists, including when every submission is a blocking batch that is waited on immediately.

#### Scenario: Standalone compute workload

- **WHEN** a headless program allocates, resizes and destroys buffers across several blocking submissions with no frame loop
- **THEN** every retired allocation is released within the same submission cycle
- **AND** no device memory is retained after the program's device-idle teardown
