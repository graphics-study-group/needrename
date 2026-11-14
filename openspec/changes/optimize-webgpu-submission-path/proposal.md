## Why

The desktop Dawn/D3D12 PhysicsApp spends a substantial part of every frame on per-dispatch recording, constant uploads, submission replay, polling waits, and FIFO presentation rather than on shader arithmetic. The existing prototype reduced the measured windowed frame time from 18.61 ms to 12.71 ms before constant batching, so these costs are the first optimization target; browser behavior and the public RHI call shape must remain intact.

## What Changes

- Select Mailbox with FIFO fallback for native surface presentation while retaining FIFO for the browser; do not enable Immediate by default.
- Replace native polling sleeps with `wgpuInstanceWaitAny`-based blocking waits, while retaining the browser event pump and existing caller-visible completion semantics.
- Keep a WebGPU compute scope open across consecutive compute commands, closing it before non-compute commands, render scopes, command-buffer completion, and encoder destruction.
- Resolve dispatch bindings without temporary string allocation or quadratic entry assembly while preserving interface, resource-type, and writable-storage-aliasing validation in all build configurations.
- Stage per-command constant windows during recording and publish each command buffer's contiguous constant data with at most one queue write immediately before its submission.
- Preserve the existing flat encoder, named-resource dispatch, constant-window, timeout, overflow, and error-reporting contracts.
- Validate Release performance through dispatch/write counts and phase timings, with native Dawn/D3D12 as the primary performance environment and the browser as a correctness target.
- Leave XPBD algorithm changes, indirect dispatch, asynchronous physics pipelines, WGSL instruction tuning, and render-graph optimization to separate work.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `rhi-command-encoder`: WebGPU may derive shared compute scopes for consecutive compute commands instead of requiring a scope boundary at every dispatch or synchronization mark; per-command constants retain their semantics when publication is deferred to submission.
- `webgpu-backend`: Define shared compute-scope lifetime, native blocking waits versus browser event pumping, native present-mode selection, and queue-ordered batched publication of per-command constant windows.

## Impact

- WebGPU command recording and completion: `engine/Rhi/Backend/WebGPU/Command/CommandBuffer.cpp`, `engine/Rhi/Backend/WebGPU/Command/CommandEncoder.cpp`, and `engine/Rhi/Backend/WebGPU/Device/DeviceContext.cpp`.
- WebGPU kernel dispatch and binding validation: `engine/Rhi/Backend/WebGPU/Pipeline/ComputeKernel.cpp`.
- Native surface configuration: `engine/Render/Backend/WebGPU/RenderSystem/SurfacePresentProvider.cpp` and the instance feature setup needed by timed waits.
- Portable command-buffer, constant, validation, synchronization, presentation, and physics regression tests in the existing test tree.
- No public RHI call-site migration, new third-party dependency, physics solver change, or permanent performance-reporting framework is introduced.
