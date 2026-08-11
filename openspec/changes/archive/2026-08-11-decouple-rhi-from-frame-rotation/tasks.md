# Tasks

Single-phase mechanical rework: parameterize rotation depth in `ComputeResourceBinding`, remove render-frame vocabulary from the Rhi compute API, adapt all 45 call sites. Ends with a mandatory review stop: build + tests green, then STOP and hand the diff to the user for review and manual commit.

## 1. Rhi core rework

- [x] 1.1 `ComputeResourceBinding` constructor: add `uint32_t slot_count = 1` parameter (after `ComputeStage&`); store it; add `assert(slot_count <= 8)`; delete `BACK_BUFFERS` constant
- [x] 1.2 Internal storage: `descriptor_sets` → `std::array<vk::DescriptorSet, 8>`; `ubo_dirty` stays `std::bitset<8>`; UBO `IndexedBuffer::CreateUnique` passes the actual `slot_count`
- [x] 1.3 API rename + assertions: `UpdateGPUInfo(uint32_t slot)` and `GetDescriptorSet(uint32_t slot)` with `assert(slot < slot_count)` in both; update header docs (drop "frames-in-flight"/backbuffer language, document caller-side slot-advancement contract)
- [x] 1.4 `ComputeHelpers`: `BindComputeResource(..., uint32_t slot)` rename of `frame_index`; update comments (remove "Frame-in-flight index", "back-buffer count", "modulo the binding's back-buffer count")
- [x] 1.5 `ComputeStage::AllocateResourceBinding(uint32_t slot_count = 1)` forwards the depth to `ComputeResourceBinding` construction; header doc update

## 2. Call-site adaptation (mechanical, zero behavior change)

- [x] 2.1 Physics (literal 3): `XPBDGpuSolver` (14 sites), `DummySolver` (1), `SpatialHashBroadDetector` (10), `ConvexCollisionDetector` (2), `RadixSort` (4), `ParallelScan` (2), `CompactUnique` (4) — pass `3` to every `AllocateResourceBinding` call
- [x] 2.2 Render/editor (semantic constant): `ComplexRenderGraphBuilder.cpp` (1), `EditorRenderGraphBuilder.cpp` (2) — pass `FrameManager::FRAMES_IN_FLIGHT`, add `#include "Render/RenderSystem/FrameManager.h"` where not transitively visible
- [x] 2.3 Tests (semantic constant): `headless_compute_test` (1), `pbr_test` (1), `new_material_test` (1), `compute_shader_test` (1), `compute_buffer_test` (1) — pass `FrameManager::FRAMES_IN_FLIGHT`, add the include if missing

## 3. Verification

- [x] 3.1 `cmake --build --preset debug` succeeds
- [x] 3.2 `ctest --preset debug` passes (48/48)
- [x] 3.3 Grep acceptance under `engine/Rhi/`: `BACK_BUFFERS` = 0 hits; `backbuffer` = 0 hits; `frame_index` = 0 hits; "frames-in-flight" / "back-buffer count" comments = 0 hits (SubmissionHelper batch-semantics identifiers excluded by scope)
- [x] 3.4 Confirm physics behavior unchanged: physics call sites pass 3, render/editor/test sites pass `FRAMES_IN_FLIGHT` (spot-check the 45 sites)
- [x] 3.5 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before archiving

## 4. Follow-up notes (documented, not implemented)

- [x] 4.1 Record known issues in the change: physics `m_frame_counter` desync from render fif (e.g. `ParallelScan` advances twice per frame) → resolved by the future physics push-constants change; physics literal-3 call sites are the transitional marker for that change to search-and-replace
