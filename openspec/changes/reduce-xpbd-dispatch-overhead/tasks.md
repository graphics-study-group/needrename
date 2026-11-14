## 1. Contact Scratch Initialization

- [ ] 1.1 Add contact regression coverage with scratch pre-filled to non-zero values for invalid shapes/bodies, dead and kinematic bodies, zero inverse mass, invalid normals, separated contacts, and zero impulses; verify the tests define zero delta and zero flag expectations before the shader change.
- [ ] 1.2 Add the seven-channel slot initializer beside `scatter_slot` and invoke it for both owned slots immediately after the count/capacity guards in position and velocity accumulation; verify the new early-return tests and existing contact, friction, restitution, and multi-substep suites pass on Vulkan and desktop WebGPU.
- [ ] 1.3 Remove the counted-clear kernel, push structure, loader, dispatch helper, call sites, and `clear_entry_values.comp`; verify physics shader builds no longer produce or load that artifact and the reference scene records 120 fewer dispatches without changing multiplier lifetimes.

## 2. Shared Joint Color Metadata

- [ ] 2.1 Replace the four scene-owned GPU color buffers with shared IDs and offsets buffers, concatenating complete fixed/hinge CPU ranges and uploading once per table on each color rebuild; verify empty, fixed-only, hinge-only, mixed, topology-change, and buffer-reallocation cases produce the exact expected table contents.
- [ ] 2.2 Migrate per-color fixed and hinge dispatches to the shared IDs buffer, deriving hinge global offsets from the final fixed range while leaving per-color shaders unchanged; verify large-color fallback and dead-fixed-descriptor tests pass on Vulkan and desktop WebGPU before sweep is enabled.

## 3. Browser-Compatible Joint Sweep

- [ ] 3.1 Change sweep bindings 14/15 to the shared tables, implement derived hinge IDs/offsets bases, and preserve the 32-byte push block with a compile-time size check; verify interface inspection shows exactly 16 storage buffers and per-color fallback remains functional.
- [ ] 3.2 Replace device-scope memory barriers with workgroup-scope `controlBarrier` plus `barrier`, restore the sweep to the normal physics shader build, and remove obsolete deferred handling; verify SPIR-V/WGSL/interface outputs build successfully and both color loops in WGSL contain `storageBarrier` followed by `workgroupBarrier`.
- [ ] 3.3 Load and dispatch the sweep under the existing width policy while preserving fixed-then-hinge order and the large-color fallback; verify small mixed-joint scenes use one joint dispatch per position iteration and oversized colors still use per-color dispatches.
- [ ] 3.4 Audit every enabled physics shader interface for the browser storage-buffer budget; verify none exceeds 16 and no higher device limit is requested.

## 4. Equivalence and Browser Coverage

- [ ] 4.1 Port the deferred sweep equivalence test to portable command recording, copy, submit/wait, and readback interfaces; verify at least 30 steps of sweep/per-color comparisons pass on Vulkan and desktop WebGPU for mixed joints, dead fixed descriptors, single-type scenes, no joints, and large-color fallback, with an assertion that the scene actually moves.
- [ ] 4.2 Add a finite browser joint smoke using the mixed fixed/hinge fixture; verify it executes the sweep, reports an explicit numerical pass/fail, produces no WebGPU validation error, and checks the 16-buffer pipeline contract rather than relying on a screenshot.

## 5. Integration and Performance Verification

- [ ] 5.1 Update solver, shader, and CMake comments that describe deferred sweep support, four color buffers, or a standalone entry clear; verify all comments match the restored two-mode implementation.
- [ ] 5.2 Run the standard Vulkan and desktop WebGPU Debug/Release builds and relevant physics suites, including the actuator stand regression; verify no solver-quality or cross-backend regression beyond accepted floating-point tolerance.
- [ ] 5.3 Measure the native Release reference scene after `optimize-webgpu-submission-path`; verify the dispatch count is approximately 740 rather than 1220, record the remaining phase timings, and confirm no permanent performance-report files were added to the repository.
