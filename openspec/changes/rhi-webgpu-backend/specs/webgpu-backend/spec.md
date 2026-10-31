## Purpose

Implements the backend-neutral contract on WebGPU and provides the two build shapes that use it: the browser build that is the product, and a desktop build kept only as a validation and A/B environment.

## ADDED Requirements

### Requirement: The backend implements the contract against the standard C interface

The WebGPU backend SHALL be written against the standard WebGPU C interface so that the same implementation compiles for the browser target and for a desktop WebGPU implementation. No browser-only or desktop-only API SHALL leak into the shared backend code.

#### Scenario: One implementation serves both builds

- **WHEN** the browser build and the desktop validation build are produced
- **THEN** they compile the same backend sources and differ only in platform glue and linked library

### Requirement: Dispatches run in their own compute scope and marks are no-ops

Each dispatch SHALL be recorded into its own compute scope. A synchronization mark SHALL record nothing: the scope boundaries around every dispatch carry the ordering the marks declare.

#### Scenario: Marked dispatches are ordered

- **WHEN** a recording contains dispatch, mark, dispatch
- **THEN** the two dispatches run in separate scopes and the second observes the first's writes

#### Scenario: Unmarked dispatches are still ordered

- **WHEN** a recording contains two dispatches with no mark between them
- **THEN** they run in separate scopes and are ordered as well; the caller's choice of mark placement does not change correctness on this backend

#### Scenario: Synchronization cost matches the caller's barrier count

- **WHEN** a recording contains N dispatches and M marks
- **THEN** the backend creates N scopes and records no barrier commands of its own

### Requirement: Bindings come from the interface description and are cached by content

Bind groups SHALL be built from the interface description's binding table, expanding combined samplers into separate texture and sampler entries and binding per-dispatch constants at the reserved location the description names. A bind group SHALL be reused for any later request with the same binding content, where content is the bound resource identity together with each entry's window offset and size.

#### Scenario: Same content reuses one bind group

- **WHEN** two dispatches bind the same resources with the same windows
- **THEN** one bind group object serves both

#### Scenario: Different window mints a distinct bind group

- **WHEN** two dispatches bind the same buffer at different window offsets
- **THEN** each receives its own bind group and sees its own window

#### Scenario: Name binding is unchanged for callers

- **WHEN** a caller binds a texture by the shader's interface name
- **THEN** the backend resolves it through the description without the caller knowing about group or binding numbers

### Requirement: Host access uses the mapped window with queue-ordered uploads

Host writes SHALL reach the device through queue-ordered uploads issued before the submission that consumes them, and readback SHALL open the window when the copy into it completes. No buffer that is mapped SHALL be consumed by device work.

#### Scenario: Writes are visible to the consuming submission

- **WHEN** a caller writes a buffer's window and a submission consuming it is recorded
- **THEN** the submission observes the written bytes

#### Scenario: Readback opens the window on completion

- **WHEN** a readback copy completes
- **THEN** the window opens and the registered callback runs

### Requirement: Frame completion advances through callbacks

Submission SHALL return without waiting. Completion SHALL be observed through callbacks that advance the epoch watermark used for resource retirement. A frame slot whose previous work is not yet complete SHALL be skipped rather than waited on. Product paths MUST NOT block on device completion.

#### Scenario: Retirement advances from callbacks

- **WHEN** a submission completes
- **THEN** its completion callback advances the completed prefix and retired resources become reclaimable

#### Scenario: Busy frame slot is skipped

- **WHEN** the frame loop finds its next slot still busy
- **THEN** the frame is skipped and retried on the next tick instead of blocking

### Requirement: Presentation uses the host surface

The backend SHALL present through the host's surface abstraction, taking one surface image per frame and presenting it at the end of the frame. The CPU-side frame ring and its per-frame windows SHALL remain valid regardless of how many surface images the host exposes.

#### Scenario: Frame renders to the surface

- **WHEN** a frame is recorded and presented in the browser build
- **THEN** the rendered image reaches the canvas

#### Scenario: Frame ring is independent of surface image count

- **WHEN** the host exposes one surface image while the frame ring has three slots
- **THEN** per-frame windows stay distinct and no slot's data is overwritten while in flight

### Requirement: Errors and device loss are reported

Invalid usage, uncaptured errors and device loss SHALL be reported through the engine's logging and debug path with enough context to identify the failing operation. Device loss SHALL NOT be silent.

#### Scenario: Validation error is surfaced

- **WHEN** a recorded operation violates the API's validation rules
- **THEN** the error is reported through the logging path and names the failing operation

#### Scenario: Device loss is surfaced

- **WHEN** the device is lost
- **THEN** the loss is reported and the engine does not continue rendering silently

### Requirement: The desktop build is a validation environment only

A desktop build of the WebGPU backend SHALL exist to validate behavior outside a browser and to run headless cross-backend comparisons. It SHALL NOT grow platform features such as its own window management, input handling or editor support; the browser remains the behavioral reference.

#### Scenario: Headless cross-backend comparison runs

- **WHEN** the same scene is rendered by the desktop WebGPU build and by the Vulkan build
- **THEN** both produce an image and the two can be compared pixel by pixel

#### Scenario: No platform feature creep

- **WHEN** the desktop build's surface is inspected
- **THEN** it provides rendering and readback only, with no window, input or editor integration
