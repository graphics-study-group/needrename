## Context

The current GPU physics pipeline lives inside the same `RenderGraph` as rendering. `XPBDGpuSolver::AddStepPasses()` is called once during graph construction (before the main loop), and per-frame dispatch happens via lambda functions captured in `RenderGraphPassBuilder::SetPassFunction()`. This design was expedient but has fundamental issues:

1. **No per-frame physics entry point**: Physics cannot be driven by a natural imperative `GPUStep()` — everything must be pre-declared as graph passes
2. **Tight coupling**: `PhysicsExampleRenderGraphBuilder` must orchestrate both physics and rendering, knowing implementation details of both
3. **Inflexible**: Adding a new solver type or running physics conditionally requires graph-level changes

The render graph already supports importing external resources with `prev_access` to declare pre-graph buffer state. The command buffer lifecycle (`reset` in `StartFrame`, single `begin`/`end`/`submit`) supports recording passes from multiple sources into one CB. We can leverage both to give physics its own `RenderGraph` while sharing the main command buffer.

### Current Frame Lifecycle (simplified)

```
RunOneFrame()
  ├─ Scene updates, events, input
  ├─ StartFrame()             ← resets main CB, acquires swapchain image
  ├─ render_graph->Execute()  ← cb.begin(), RecordAllPasses(cb), cb.end(), SubmitMainCommandBuffer()
  └─ CompleteFrame()          ← present
```

### Key Constraints

- `vk::CommandBuffer` goes through `Initial → Recording → Executable → (submit) → Invalid → (reset) → Initial`. Multiple `begin()`/`end()` cycles per frame are **not allowed** — a CB is recorded once, submitted once, then reset.
- `FrameManager::StartFrame()` calls `command_buffer->reset()` and `device.resetFences()`, making the CB ready for a single recording cycle.
- `SubmitMainCommandBuffer()` submits to the graphics queue with timeline semaphore synchronization.
- `ImportExternalResource` with non-None `prev_access` injects a Virtual Source pass, enabling the builder to compute correct barriers FROM an external state.

## Goals / Non-Goals

**Goals:**
- Give physics its own `RenderGraph`, constructed lazily on first `GPUStep()` and cached
- Share a single `vk::CommandBuffer` across multiple `RenderGraph` instances within one frame
- Use `prev_access` on `ImportExternalResource` to propagate buffer state across RenderGraph boundaries for correct Vulkan barriers
- Define a minimal `ISolver` base class and `DummySolver` to validate the architecture
- Add solver registration and per-frame `PreGPUStep`/`GPUStep`/`PostGPUStep` to `PhysicsSystem`
- Modify `ComplexRenderGraphBuilder` to optionally render physics-driven model matrices
- Refactor the physics example to use the new architecture

**Non-Goals:**
- Remove or refactor `XPBDGpuSolver` — it stays as-is; adapting it to `ISolver` is future work
- Implement multi-queue async compute — all work remains on the graphics queue
- Create a separate `vk::CommandPool` or `vk::CommandBuffer` — the main CB is reused
- Actual GPU→CPU readback — `PostGPUStep` is declared as a hook but left empty for now

## Decisions

### Decision 1: Single CommandBuffer, multiple RenderGraphs via modified RecordAllPasses

`RenderGraph::RecordAllPasses` currently calls `cb.begin()` and `cb.end()` internally. We move these calls out so the caller controls CB lifecycle. `PreGPUStep`/`PostGPUStep` happen outside the CB scope; `GPUStep` receives the CB and solvers record their passes into it directly:

```
StartFrame()                  ← resets main CB

physics->PreGPUStep(renderer) ← CPU-side prep (no CB needed)

cb.begin()
  physics->GPUStep(renderer, cb)          ← each solver records its RG to cb
  rendering_rg->RecordAllPasses(cb)       ← rendering records its RG to cb
cb.end()
SubmitMainCommandBuffer()     ← one submission

physics->PostGPUStep(renderer) ← readback hook (CB already submitted)
```

**What changes in RecordAllPasses**: Remove the internal `cb.begin(vk::CommandBufferBeginInfo{})` and `cb.end()` calls. The method becomes a pure recording function that assumes the CB is already in the Recording state. Callers (`RenderGraph::Execute`, `test/compute_buffer_test.cpp`) add their own `begin`/`end`.

**Why not separate CBs**: Separate CBs would require managing an additional `vk::CommandPool`, fences, and timeline semaphore synchronization between the physics CB submission and the rendering CB submission. The existing `FrameManager` assumes exactly one main CB per frame-in-flight. Multiple CBs would require deep changes to `FrameManager` or a parallel submission infrastructure, both of which are unnecessary since all work targets the same graphics queue.

**Why not just merge into one RenderGraph**: That's the current design that we're trying to move away from. Merging requires all passes to be known at graph construction time, which prevents the imperative `GPUStep()` pattern.

**`vk::CommandBuffer` lifecycle with this approach**:

```
Frame N:
  StartFrame() → waitForFences(prev frame) → cb.reset() → acquireNextImage

  physics->PreGPUStep(renderer)                     ← no CB

  cb.begin(vk::CommandBufferBeginInfo{})            ← one begin
  physics->GPUStep(renderer, cb)                    ← solvers record to CB
  rendering_rg->RecordAllPasses(cb)                  ← rendering records to CB
  cb.end()                                           ← one end
  SubmitMainCommandBuffer()                          ← one submit

  physics->PostGPUStep(renderer)                    ← no CB

  ... GPU executes cb ...

  CompleteFrame() → present → advance FIF index

Frame N+1:
  StartFrame() → waitForFences(cb's fence from submit in CompleteFrame) → cb.reset()
  ...
```

### Decision 2: Cross-RenderGraph synchronization via `prev_access`

The physics RG writes `model_matrices` (and potentially position/rotation buffers). The rendering RG reads `model_matrices` for shadow map and lit passes. Since `BuildRenderGraph()` of each RG runs independently, the rendering RG builder has no knowledge that physics wrote to `model_matrices` in preceding passes.

**Solution**: When building the rendering RG, import the shared buffer with `prev_access` set to the access type the physics RG leaves it with:

```cpp
// Building the RENDERING RG
auto mm_handle = rgb.ImportExternalResource(
    *gpu.model_matrices,
    MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::ShaderRandomWrite)
    //                      ↑ tells the builder: before our first pass,
    //                        this buffer was written by a compute shader
);
```

This causes `RenderGraphBuilder::ImportExternalResource` to inject a Virtual Source pass (if no passes exist yet) with the declared access. When the builder later computes barriers for the first rendering pass that reads `model_matrices`, it sees the transition:

```
Virtual Source (Compute | ShaderRandomWrite)
    → barrier (COMPUTE_SHADER|SHADER_WRITE → ALL_GRAPHICS|SHADER_READ)
        → Shadowmap Pass (Graphics | ShaderRandomRead)
```

The `prev_access` is set at **graph build time** (once, before the main loop). The physics RG always writes `model_matrices` with `ShaderRandomWrite`, so the rendering RG can hardcode this.

**For buffers that physics reads and rendering also reads** (e.g., `rigid_body_is_kinematic`): both sides import with `prev_access = None` and declare `ShaderRandomRead`. No barrier is needed between read-after-read, which the builder correctly skips.

**Queue ordering complement**: Since both RGs record to the same CB on the same graphics queue, GPU execution order is guaranteed to match recording order. The barriers within the CB handle cache coherence between the two logical "graphs".

### Decision 3: Lazy RenderGraph creation in DummySolver

```cpp
class DummySolver : public ISolver {
    std::unique_ptr<RenderGraph> m_rg;
    XpbdConfig m_config{};

    void GPUStep(RenderSystem &system, PhysicsScene &scene, vk::CommandBuffer cb) override {
        auto gpu = scene.GetGpuBuffers();
        if (gpu.rigid_body_slot_count == 0) return;

        if (!m_rg || body_count_changed) {
            m_rg = BuildRenderGraph(system, scene);
        }
        // Update uniforms, then record
        m_rg->RecordAllPasses(cb);
    }

    std::unique_ptr<RenderGraph> BuildRenderGraph(RenderSystem &system, PhysicsScene &scene);
};
```

**Why lazy**: The RenderGraph can only be built after the physics scene has its GPU buffers allocated (which happens during `InitializePendingRigidBodies`, called from `SceneBuilder::Finalize`). At solver registration time, the scene may not be ready.

**Why cache**: `BuildRenderGraph` allocates transient resources and compiles passes — it's expensive. The solver configuration (body count, shape count) determines buffer sizes; if these change, the RG must be rebuilt. For the dummy solver, body count changes only during scene setup, so caching is safe.

### Decision 4: ISolver base class — GPUStep takes CommandBuffer, no GetRenderGraph

```cpp
// engine/Physics/Solver/ISolver.h
namespace vk { struct CommandBuffer; }
namespace Engine {
    class RenderSystem;
    class PhysicsScene;

    class ISolver {
    public:
        virtual ~ISolver() = default;

        // CPU-side preparation (e.g., uploading new data to GPU).
        // Called BEFORE cb.begin(). Default: no-op.
        virtual void PreGPUStep(RenderSystem &system, PhysicsScene &scene) {}

        // GPU step — lazily builds RG, updates uniforms, records passes to cb.
        // Called BETWEEN cb.begin() and cb.end().
        virtual void GPUStep(RenderSystem &system, PhysicsScene &scene, vk::CommandBuffer cb) = 0;

        // GPU→CPU readback or post-processing.
        // Called AFTER cb.end() + submit. Default: no-op.
        virtual void PostGPUStep(RenderSystem &system, PhysicsScene &scene) {}

        // Returns true if the solver has been fully initialized.
        [[nodiscard]]
        virtual bool IsInitialized() const noexcept = 0;
    };
}
```

**Why `GPUStep` takes `vk::CommandBuffer`**: The solver owns its RenderGraph and is responsible for recording it. Passing the CB into `GPUStep` lets the solver call `m_rg->RecordAllPasses(cb)` internally, keeping RenderGraph ownership private.

**Why no `GetRenderGraph()`**: It was a leaky abstraction — callers shouldn't need to know about the solver's internal RG. The solver records its passes itself.

**Why `PreGPUStep` / `PostGPUStep` have no CB**: They're for CPU work before/after the CB recording window. `PreGPUStep` uploads data to host-visible buffers (no CB needed). `PostGPUStep` handles readback after CB submission (too late to record).

**Why forward-declare `vk::CommandBuffer` with `namespace vk { struct CommandBuffer; }`**: Same pattern used by `RenderGraph.h`. Avoids pulling `<vulkan/vulkan.hpp>` into every translation unit that includes `ISolver.h`.

### Decision 5: PhysicsSystem splits into PreGPUStep / GPUStep / PostGPUStep

```cpp
// PhysicsSystem.h additions
class ISolver;

class PhysicsSystem {
    // ... existing API ...

    void RegisterSolver(std::unique_ptr<ISolver> solver);

    // Called BEFORE cb.begin(). Iterates solvers calling PreGPUStep.
    void PreGPUStep(RenderSystem &render_system);

    // Called BETWEEN cb.begin() and cb.end(). Iterates solvers passing cb.
    void GPUStep(RenderSystem &render_system, vk::CommandBuffer cb);

    // Called AFTER cb.end() + submit. Iterates solvers calling PostGPUStep.
    void PostGPUStep(RenderSystem &render_system);

private:
    std::vector<std::unique_ptr<ISolver>> m_solvers{};
};
```

Each method iterates all registered solvers in order and calls the corresponding `ISolver` method on the main scene (`scene_id=0`).

**Why three separate methods instead of one**: `PreGPUStep` must run before `cb.begin()`, `GPUStep` needs the CB, and `PostGPUStep` must run after submit. The caller (`MainClass::RunOneFrame`) controls the CB lifecycle and calls each at the right time.

**Why no `GetSolvers()`**: Not needed — all solver iteration is now internal to `PhysicsSystem`.

### Decision 6: RunOneFrame — three-phase physics call

```cpp
void MainClass::RunOneFrame() {
    // ... existing: events, input, scene updates ...
    this->world->UpdateRendererData(*this->renderer);

    this->renderer->StartFrame();   // reset main CB

    // Phase 1: CPU-side physics prep (no CB)
    this->physics->PreGPUStep(*this->renderer);

    // Phase 2: GPU recording (CB open)
    auto cb = this->renderer->GetFrameManager().GetRawMainCommandBuffer();
    cb.begin(vk::CommandBufferBeginInfo{});

    this->physics->GPUStep(*this->renderer, cb);    // solvers record their RGs

    if (this->render_graph && this->render_graph->GetNumPasses() > 0) {
        this->render_graph->RecordAllPasses(cb);
    }

    cb.end();
    this->renderer->GetFrameManager().SubmitMainCommandBuffer();

    // Phase 3: Physics readback (CB already submitted)
    this->physics->PostGPUStep(*this->renderer);

    // ... existing CompleteFrame ...
}
```

**Why physics before rendering in recording order**: The physics RG writes model matrices; the rendering RG reads them. Recording order on the same queue guarantees execution order, and the barriers in the rendering RG's first read-pass transition from `ComputeShader/ShaderWrite` (from `prev_access`) to `Graphics/ShaderRead`.

**Why PreGPUStep before cb.begin() and PostGPUStep after submit**: `PreGPUStep` does CPU→GPU data upload (writing to host-visible buffers) — no CB needed. `PostGPUStep` does GPU→CPU readback — requires GPU work to be submitted first, so it runs after `SubmitMainCommandBuffer()`.

### Decision 7: ComplexRenderGraphBuilder model matrices support

Add a new `BuildDefaultRenderGraph` overload that accepts an optional model matrices buffer:

```cpp
// ComplexRenderGraphBuilder.h
std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
    uint32_t texture_width,
    uint32_t texture_height,
    RGTextureHandle &final_color_target_id,
    const ComputeBuffer *model_matrices_buffer = nullptr
);
```

When `model_matrices_buffer != nullptr`:
1. Import the buffer with `prev_access = ShaderRandomWrite` (left by physics)
2. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on both shadow map and lit passes

The existing overload (without the parameter) calls the new overload with `nullptr`, preserving backward compatibility for non-physics examples.

### Decision 8: DummySolver compute shader

A single compute shader (`dummy_solver.comp`) performs two operations per rigid body — see `engine/Physics/shader/solver/DummySolver/dummy_solver.comp`. This shader reads `rigid_body_alive`, `rigid_body_center_world_position`, `rigid_body_center_world_rotation` and writes `rigid_body_center_world_position`, `model_matrices`. All are scene-owned buffers imported as external resources.

## Risks / Trade-offs

- **Risk**: If physics RG has zero passes (e.g., zero rigid bodies), `GPUStep` should skip recording. → **Mitigation**: Each solver checks body count in `GPUStep` and returns early if zero. Empty graphs never call `RecordAllPasses`.

- **Risk**: Modifying `RecordAllPasses` to remove internal `begin()`/`end()` breaks the existing `compute_buffer_test.cpp` which calls it directly. → **Mitigation**: Update `compute_buffer_test.cpp` to add `cb.begin()` and `cb.end()` around `RecordAllPasses`. Only 2 call sites: `RenderGraph::Execute` and the test.

- **Risk**: `prev_access` on the rendering RG's `ImportExternalResource` uses `ShaderRandomWrite` as the assumed physics output state. If the physics RG later writes with a different access type, the barrier will be incorrect. → **Mitigation**: The dummy solver exclusively uses `ShaderRandomWrite` for output buffers. Document this contract.

- **Trade-off**: Recording physics before rendering means physics compute work cannot overlap with rendering graphics work. → **Accepted**: Async compute overlap requires multi-queue support which is explicitly a non-goal.

## Open Questions

None.
