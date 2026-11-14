## 1. Native Platform Policies

- [x] 1.1 Add present-mode selection coverage for Mailbox availability, FIFO-only surfaces, browser FIFO, and the absence of a default Immediate choice; verify the focused tests pass before changing surface configuration.
- [x] 1.2 Implement native Mailbox-to-FIFO selection while retaining browser FIFO; verify the same selection tests pass and the native surface log reports the chosen mode.
- [x] 1.3 Request timed-wait support at native instance creation and replace sleep polling with `WaitAnyOnly` plus `wgpuInstanceWaitAny`, retaining the no-work fast path and Emscripten event pump; verify completion, finite-timeout, and idle-wait tests pass on desktop WebGPU and the browser build still compiles without the native wait path.

## 2. Recording State and Compute Scopes

- [x] 2.1 Introduce backend-private per-command-buffer recording state for open-pass lifetime and later constant staging, including End, Reset, encoder destruction, and command-buffer destruction; verify lifecycle tests finish valid command buffers without leaked or dangling pass state.
- [x] 2.2 Keep consecutive compute commands in one pass and close it before copies, clears, render scopes, End, and destruction; verify pass-transition tests and a producer/consumer dispatch readback test pass on desktop WebGPU while Vulkan results remain unchanged.
- [x] 2.3 Exercise shared-pass producer/consumer visibility in the browser correctness path; verify no validation error occurs and the consumer reads the producer's bytes, or restore per-dispatch scopes and record the compatibility reason in the design before proceeding.

## 3. Dispatch Binding and Validation

- [x] 3.1 Replace allocating name lookups with heterogeneous string-view lookup and resolve each supplied resource once; verify valid dispatch, undeclared-name, omitted-interface, and wrong-resource-type tests pass with the same diagnostics.
- [x] 3.2 Sort resolved records by set and binding and build per-set arena entries with a single scan while retaining per-dispatch arena acquisition; verify dictionary-order canonicalization and descriptor-reuse tests pass.
- [x] 3.3 Replace the string-copying alias map with a bounded local identity scan and failure-only message construction; verify writable-aliasing and read-only-aliasing cases pass in both Release and Debug, and confirm the optimized recording profile retains alias checking.

## 4. Batched Constant Publication

- [x] 4.1 Add per-command-buffer host staging for zero-filled 256-byte constant windows while recording dynamic-offset binds without queue writes; verify short-block padding, multiple distinct values, no-constant submissions, and ring-overflow tests pass.
- [x] 4.2 Publish each submitted command buffer's used staging range with one queue write immediately before submission and recycle staging safely afterward; verify an instrumented multi-constant submission performs exactly one constant write and each shader command reads its own value.
- [x] 4.3 Cover two command buffers recorded interleaved with different constants and submitted in a known order; verify A/B readback proves that device-window reuse follows write-A/submit-A/write-B/submit-B ordering and no earlier values are overwritten.

## 5. Integration and Performance Verification

- [x] 5.1 Update comments that still describe per-dispatch passes, immediate constant writes, or polling waits; verify the revised comments match the implementation and no public RHI call-site change was introduced.
- [x] 5.2 Build and test the standard Vulkan and desktop WebGPU configurations, including the relevant Debug and Release suites; verify all existing portable RHI, physics, and presentation regressions pass.
- [x] 5.3 Run the browser WebGPU correctness path and the native Release PhysicsApp comparison; verify no browser validation error occurs, record record/submit/wait/GPU phases, and confirm constant writes drop from approximately one per dispatch to at most one per constant-bearing command buffer without treating absolute frame time as a cross-machine gate.
