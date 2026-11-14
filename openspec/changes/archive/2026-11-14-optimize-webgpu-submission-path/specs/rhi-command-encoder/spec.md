## MODIFIED Requirements

### Requirement: Synchronization marks are the caller's declared sync points

The encoder SHALL offer a general synchronization mark and a texture-state synchronization mark. Both SHALL mean "the work recorded before this mark is visible to the work recorded after it". Their placement is the caller's knowledge of its own data flow; a backend MUST NOT add, remove or move their semantic synchronization points.

A backend SHALL satisfy each mark through its own ordering mechanism. A mark need not produce a distinct command or compute-scope boundary when the backend already guarantees the required visibility within the current command sequence.

#### Scenario: Vulkan backend records a real barrier

- **WHEN** a caller records a synchronization mark between two dispatches
- **THEN** the Vulkan backend records a pipeline barrier at that point and nothing else between the two dispatches

#### Scenario: WebGPU backend carries ordering by scope boundary

- **WHEN** the WebGPU backend receives a synchronization mark between two dispatches
- **THEN** the mark itself records no command, and the second dispatch observes the first dispatch's writes whether the backend keeps them in one compute scope or separates them

#### Scenario: The kernel adds no synchronization

- **WHEN** two dispatches follow each other with no mark between them
- **THEN** neither backend inserts an explicit barrier or a mandatory scope boundary between them

### Requirement: Per-dispatch constants are recorded as kernel parameters

The encoder SHALL record a per-dispatch constant block with the semantics of kernel parameters: a small, CPU-supplied value that must reach the shader for exactly that dispatch. The block size contract SHALL come from the shader's reflected constant block, and debug builds SHALL assert that a recorded block does not exceed it. Each backend SHALL map the value to its own mechanism: the Vulkan backend to real push constants when the block fits its device limit, the WebGPU backend to a slice of a per-command constant ring bound as a uniform.

Recording a constant block and publishing its bytes to the device MAY be separate operations. A backend that defers publication SHALL preserve each command's distinct bytes, zero-fill the unused portion of its declared window, and publish the data before the consuming submission.

#### Scenario: Constants reach the shader on both backends

- **WHEN** a caller records a constant block and then a dispatch
- **THEN** the dispatched shader observes exactly those bytes as its constant block, on both backends

#### Scenario: Oversized constant block is caught

- **WHEN** a caller records a constant block larger than the reflected block size
- **THEN** the recording asserts in debug builds

#### Scenario: Backend maps constants to its own mechanism

- **WHEN** the WebGPU backend records a dispatch that carries a constant block
- **THEN** the value is carried by a uniform-buffer slice rather than by any backend-specific immediate-data feature

#### Scenario: Deferred publication preserves every command's value

- **WHEN** one command buffer records multiple dispatches or draws with different constant blocks and is then submitted
- **THEN** every command observes its own recorded bytes and no command observes a later command's values
