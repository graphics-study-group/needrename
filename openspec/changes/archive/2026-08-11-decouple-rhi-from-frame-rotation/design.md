# Design: Decouple Rhi from render frame rotation

## Context

`ComputeResourceBinding` (engine/Rhi/) manages compute shader bindings: a descriptor set (via `ShaderResourceBinding`, content-hash-cached) plus a variable-Ubo mechanism (`IndexedBuffer` slices + dynamic offsets). It originated in the render pipeline, where the caller always had a frames-in-flight index from `FrameManager`. The design hard-codes that reality: `BACK_BUFFERS = 3`, `descriptor_sets[3]`, tri-sliced UBO, `ubo_dirty` bitset, and `UpdateGPUInfo(backbuffer)` / `GetDescriptorSet(backbuffer)`.

Audit result: this is the **only** rotation/backbuffer concept in `engine/Rhi/`. `ShaderResourceBinding` is content-hash-cached (content change → new descriptor set, no rotation needed); `IndexedBuffer` is a generic parameterized slice facility; `SubmissionHelper` is a single-batch state machine (its `OnFrameComplete` naming is batch semantics, not rotation, and stays untouched).

Because the API forces callers to supply a frame index, the 8 physics components each maintain `m_frame_counter++ % 3` — a duplicate of the render fif with no mechanism keeping them in sync (`ParallelScan` already advances twice per frame). Rotation depth is a caller-side commitment about its own submission cadence; `Rhi` must not hard-code it.

## Goals / Non-Goals

**Goals:**
- Remove the hard-coded 3 and all render-frame vocabulary (`backbuffer`, `frame_index`, "frames-in-flight") from `ComputeResourceBinding` and `ComputeHelpers`.
- Let callers declare rotation depth (`slot_count`), defaulting to 1 for the common "no rotation needed" case.
- Keep behavior byte-identical for all current callers (physics 3-slot rotation preserved; render bloom keeps the fif-driven 3-slot path).
- Guard misuse with bounds assertions.

**Non-Goals:**
- Physics push-constants rework (removing physics counters / parameter pools, dropping physics bindings to `slot_count = 1`) — separate follow-up change.
- Renaming `SubmissionHelper`'s batch-semantics identifiers (`OnFrameComplete`, `m_inflight_staging`) — not rotation, out of scope.
- Fixing the unbounded `ShaderResourceBinding` hash cache (pre-existing, unrelated).
- Touching `MaterialInstance`/`CameraManager`/`SceneDataManager` (render-internal rotation, legitimately fif-driven; their duplicate `BACK_BUFFERS = 3` magic number is a cleanup item for a future change).

## Decisions

### D1: `slot_count` parameterized with default 1, enforced by assertions

`ComputeResourceBinding(DeviceContext&, ComputeStage&, uint32_t slot_count = 1)` and `ComputeStage::AllocateResourceBinding(uint32_t slot_count = 1)`.

- `assert(slot_count <= 8)` — keeps the existing `std::bitset<8>` ubo-dirty storage untouched (no dynamic container), with headroom for any realistic depth.
- `assert(slot < slot_count)` in both `UpdateGPUInfo` and `GetDescriptorSet`.

Alternatives considered:
- Default `3` — rejected: re-embeds the render magic number in Rhi's default.
- Mandatory (no default) — rejected by user decision: default 1 is the correct future-facing convenience (physics post-push-constants and any non-rotating caller), and the assertions turn forgotten depths into debug-time failures instead of UB.

### D2: Neutral `slot` naming

`backbuffer` → `slot` in `UpdateGPUInfo(uint32_t slot)` / `GetDescriptorSet(uint32_t slot)`; `BindComputeResource(..., uint32_t slot)`. Header comments rewritten to "rotation slot" semantics: callers advance the slot in lockstep with their own submission cadence and must pass `slot_count >=` their max in-flight batches.

### D3: Call-site adaptation rule

- **Physics (37 sites, 8 components): literal `3`.** Deliberate: physics is a transitional state (counters + rotation get removed by the follow-up change), literals make the later search-and-replace trivial; half-measures (a per-component constant) would split the same "3" across a constant and the untouched `m_frame_counter % 3` literal.
- **Render / editor / tests (8 sites): `FrameManager::FRAMES_IN_FLIGHT`.** Semantic constant, no magic 3. Requires adding `#include "Render/RenderSystem/FrameManager.h"` where not already transitively visible.

### D4: Internal storage

- `descriptor_sets`: `std::array<vk::DescriptorSet, 8>` (fixed, matches the 8-slot assertion; only slots `< slot_count` are touched).
- `ubo_dirty`: `std::bitset<8>` unchanged.
- UBO `IndexedBuffer::CreateUnique(..., slot_count, ...)`: created with the actual declared depth (only allocation that scales with `slot_count`).

## Risks / Trade-offs

- [Default 1 + forgotten explicit depth at a rotating caller → assert fires only in debug builds (NDEBUG skips asserts)] → The 45 current call sites are all adapted explicitly in this change; `ctest` (debug preset) exercises all of them across multiple frames, so any miss is caught by tests.
- [Behavior regression while keeping semantics identical] → Mechanical rename + parameter plumbing only; verification is `ctest --preset debug` 48/48 green plus the grep acceptance list (`BACK_BUFFERS`, `backbuffer`, `frame_index`, "frames-in-flight" comments = 0 hits under `engine/Rhi/`).
- [Physics 3-slot rotation remains desynced from render fif (pre-existing `ParallelScan` issue)] → Known problem, documented in this change, resolved by the follow-up physics change; this change neither worsens nor fixes it.
- [`slot_count <= 8` cap could bite an exotic caller] → 8 in-flight batches is far beyond current usage (3); raising it later means switching `ubo_dirty` to a dynamic container (local, API-compatible change).

## Migration Plan

1. Rework `ComputeResourceBinding` (constructor, storage, assertions, comments).
2. Rework `ComputeHelpers` (rename + comments).
3. `ComputeStage::AllocateResourceBinding` forwarding parameter.
4. Adapt all 45 call sites (physics literal 3; render/editor/tests `FRAMES_IN_FLIGHT` + includes).
5. Build + full test suite; grep acceptance.
6. Review stop before archiving; follow-up change (physics push-constants) is pre-planned but independent.
