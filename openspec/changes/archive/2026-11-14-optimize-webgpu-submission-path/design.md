## Context

See proposal.md - Why. The current WebGPU backend ends a compute pass in every `ComputeKernel::DispatchAndRecord`, resolves the same named resources several times, copies binding names into an alias-check map, and immediately calls `wgpuQueueWriteBuffer` for every 256-byte constant window. `CommandBuffer::End` also assumes that every compute pass was closed by the kernel. Native fence waits poll with `sleep_for(50us)`, and native presentation takes the first advertised mode, normally FIFO.

The measured reference scene records about 1220 physics dispatches per step. The original windowed frame time was 18.61 ms; the combined prototype for shared passes, native waits, allocation-free bookkeeping, and Mailbox measured 12.71 ms. Per-dispatch recording fell from about 4.55 us to 2.33 us in that prototype. These are native Dawn/D3D12 reference measurements, not portable performance guarantees.

The public RHI interface and Vulkan implementation must remain unchanged. Browser behavior is a correctness constraint, while native Release timings are the optimization signal.

## Goals / Non-Goals

**Goals:**

- Keep backend-private pass and constant state coherent from recording through submission.
- Remove avoidable per-dispatch allocation, repeated lookup, queue writes, and sleep quantization without weakening validation.
- Make native presentation and explicit waits deterministic and comparable with the Vulkan path.
- Make structural overhead observable through write/pass counts and phase timings.

**Non-Goals:**

- No public flush, compute-pass, or backend-specific call in RHI consumers.
- No change to solver dispatch counts, numerical behavior, asynchronous frame architecture, or render-graph structure in this change.
- No cross-machine absolute frame-time gate and no permanent performance-analysis scripting framework.

## Decisions

### D1. One backend-private recording state owns pass and constant lifetime

Associate each native `WGPUCommandEncoder` with a backend-private recording state owned by its `CommandBuffer`. The state contains the open compute pass, the constant cursor, host constant staging, and the association between the encoder and its finished command buffer. `CommandEncoder` instances attach to this state through the existing native-handle boundary; callers continue to see only the portable handle.

`CommandBuffer::End` closes any open compute pass before `wgpuCommandEncoderFinish`. `Reset` clears the pass and constant recording state. Destruction releases the pass, staging association, and native objects in an order that leaves no registry entry or pass pointing at a destroyed encoder. The encoder destructor closes a pass it still owns but does not destroy the command buffer's recording state.

This extends the existing pimpl/backend-private implementation rather than changing public handles. A shared ownership or backend registry is acceptable internally, provided native encoder and finished-buffer lifetime remain singular and single-threaded like the current backend.

*Alternative: independent pass and staging registries in each function.* Rejected because End, Submit, Reset, and destruction would each have a different partial view of recording state.

### D2. Compute scopes cover a contiguous compute region

The first pipeline bind opens a compute pass. Later pipeline binds, descriptor binds, constants, and dispatches reuse it. Copy buffer/image operations, `FillBuffer`, `BeginRenderPass`, `CommandBuffer::End`, and encoder destruction close it. A later compute operation opens a fresh pass.

Synchronization marks remain no-ops at the API recording level. Consecutive WebGPU dispatches execute in recording order; the backend does not use a scope split as a substitute for every caller mark. Cross-backend dependency tests must prove that a producer dispatch followed by a consumer dispatch observes the producer's writes in both native and browser builds.

*Alternative: one pass per dispatch.* Retain only as the rollback if browser validation disproves the shared-pass visibility assumption; it costs measurable recording time without reducing the measured GPU workload.

### D3. Constant publication is deferred to submission

Each command buffer owns a grow-on-demand host byte vector. Recording a constant block:

1. Checks that another 256-byte window fits the existing 1 MiB device ring.
2. Appends one zero-initialized 256-byte window and copies the supplied prefix into it.
3. Binds the existing dynamic-offset bind group at the window's device offset.
4. Records no queue operation.

Immediately before a non-empty `wgpuQueueSubmit`, `DeviceContext::Submit` finds the command buffer's staging and issues one `wgpuQueueWriteBuffer` for its contiguous used range. The host vector can then be recycled for that command buffer's next recording cycle; queue write copies the source bytes before returning.

Every command buffer's device cursor starts at zero as it does today. For A then B submission, the queue sequence is write A, submit A, write B, submit B, so B's reuse of device windows cannot affect A. Interleaved recording is safe because A and B have separate host vectors. A command buffer with no constants performs no constant queue write. Overflow remains an explicit recording error; there is no mid-command-buffer flush, wrap, or dynamic ring growth.

*Alternative: flush per pass or expose `FlushConstants`.* Rejected because it retains unnecessary writes or leaks backend lifetime into every caller. Per-pass flush also complicates reuse of device windows before earlier work has submitted.

### D4. Binding resolution is linear after one dictionary pass

Replace allocating `std::map::find(std::string{key})` calls with heterogeneous string-view lookup in the kernel's immutable interface table. Resolve each supplied resource once into a record containing its declared interface and canonical binding content. Validate the resource kind during that same pass.

Sort the resolved records by `(set, binding)`, then walk them once per declared set to build canonical arena entries. This replaces the nested dictionary/content scans and repeated per-set sorting. Arena acquisition still occurs for every dispatch, preserving epoch and cache semantics.

For alias checking, scan a small local array of storage-use records containing buffer identity and pointers to immutable interface metadata. Construct names and formatted diagnostics only on a conflict. Do not copy strings into a per-dispatch map. Interface, kind, missing-resource, and aliasing checks remain enabled in all configurations.

*Alternative: disable validation in Release.* Rejected: after this optimization the measured alias scan is about 0.07 us per dispatch, so disabling it saves less than 0.1 ms in the reference step while removing the most useful browser-portability diagnostic.

### D5. Native waits and presentation use explicit platform policies

On native builds, request `WGPUInstanceFeatureName_TimedWaitAny` at instance creation. `WaitForFence` registers queue completion with `WGPUCallbackMode_WaitAnyOnly` and waits through `wgpuInstanceWaitAny`, translating the platform result into the existing completion/timeout return. The no-outstanding-submission fast path remains. Emscripten retains the existing event-pump implementation and never enters the native blocking path.

Native surface setup selects Mailbox when advertised and FIFO otherwise. Browser setup selects FIFO. Immediate is not part of the default preference chain.

*Alternative: portable sleep polling or Immediate fallback.* Rejected because Windows sleep quantizes waits to milliseconds and Immediate changes presentation behavior by allowing tearing.

## Risks / Trade-offs

- [Shared compute passes expose an incorrect ordering assumption] → Test producer/consumer dispatches on native and browser; if the browser disproves visibility, roll back to per-dispatch scopes without changing the public contract.
- [Deferred constants reuse a device window too early] → Keep staging per command buffer, publish only at Submit, and test interleaved recording with A/B submission order.
- [Open-pass lifetime leaks across End or destruction] → Centralize pass ownership in recording state and exercise End, Reset, destructor, copy, clear, and render transitions.
- [Binding rewrites change diagnostics or omit validation] → Run the same invalid-dictionary, wrong-kind, and aliasing tests in Release and Debug.
- [Native WaitAny support differs from browser support] → Compile the blocking path only for native and retain the browser pump; request the timed-wait feature explicitly.
- [Mailbox changes power usage or presentation latency relative to FIFO] → Limit the policy change to native and keep browser FIFO; Immediate remains opt-in work outside this change.
- [Absolute timings vary across GPUs and Dawn versions] → Gate structural counts and correctness, record phase timing as evidence rather than a universal millisecond threshold.

## Migration Plan

1. Add focused regression coverage for pass closure, constant values/padding/overflow, interleaved command buffers, validation, waits, and present-mode selection.
2. Land native present and wait changes, then shared compute scopes, then binding bookkeeping, keeping the Vulkan build green after each step.
3. Introduce per-command-buffer constant staging and submission-time publication last; compare queue-write counts and phase timings against the previous step.
4. Run native Vulkan and WebGPU test suites plus the browser correctness path. Record Release native record/submit/wait/GPU timings and constant-write counts without retaining the temporary analysis scripts in the repository.
5. Roll back individual steps independently if a regression appears. The public API and Vulkan path need no migration.
