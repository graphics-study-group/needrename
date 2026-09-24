// Stable ownership of the physics model matrices buffer (tasks 3.1 - 3.4).
//
// Covers:
//   - the shared-ownership buffer factory, bound through the existing binding
//     path and kept alive by the holder (3.1);
//   - `PhysicsScene` holding `model_matrices` by shared ownership, so a resize
//     leaves the previously exposed handle valid (3.2);
//   - the render side holding a stable reference rather than a borrowed raw
//     pointer (3.3);
//   - the reallocation scenario: change the rigid body slot count after the
//     render graph has been built, and observe that the graph still references a
//     live buffer and that the previously forwarded buffer is released only once
//     it is unreferenced (3.4).

#include "Framework/MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"
#include "Render/RenderSystem/SceneDataManager.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Submission/SubmissionHelper.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

using namespace Engine;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    void DummyComputePass(CommandBuffer &, const RenderGraph &) {
    }

    /// @brief Number of live VMA allocations, used to observe buffer lifetimes.
    uint32_t LiveAllocationCount(const Rhi::AllocatorState &allocator) {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator.GetAllocator(), &stats);
        return stats.total.statistics.allocationCount;
    }

    using BufferAccess = Rhi::MemoryAccessTypeBuffer;
    using BufferBits = Rhi::MemoryAccessTypeBufferBits;
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 640, .resol_y = 480, .headless = true, .title = "Model Matrices Ownership Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    auto &allocator = rsys->GetAllocatorState();
    auto &device_context = rsys->GetDeviceContext();
    auto &tracker = device_context.GetEpochTracker();

    // ── 3.1: the shared-ownership factory, through the existing binding path ──

    {
        const uint32_t before = LiveAllocationCount(allocator);
        auto buffer =
            Rhi::ComputeBuffer::CreateShared(allocator, 4096, false, false, false, false, "Shared-ownership buffer");
        Check(buffer != nullptr, "CreateShared must return a buffer");
        std::weak_ptr<const Rhi::ComputeBuffer> weak = buffer;
        const vk::Buffer handle = buffer->GetBuffer();
        Check(handle != nullptr, "the shared buffer must hold a valid VkBuffer");

        // Bind it through the existing external-resource binding path.
        RenderGraphBuilder rgb{*rsys};
        auto mm_handle = rgb.ImportExternalResource(buffer, BufferAccess{BufferBits::ShaderRandomWrite});
        rgb.AddPass(
            RenderGraphPassBuilder{*rsys}
                .SetName("Shared buffer consumer")
                .UseBuffer(mm_handle, BufferAccess{BufferBits::ShaderRandomRead})
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction(DummyComputePass)
                .Get()
        );

        // Drop the caller's own reference: the graph still holds a share.
        buffer.reset();
        Check(!weak.expired(), "the graph must keep the imported buffer alive");

        auto graph = rgb.BuildRenderGraph();
        Check(!weak.expired(), "the built graph must keep the imported buffer alive");
        Check(
            LiveAllocationCount(allocator) == before + 1u, "the allocation must stay alive while a reference is held"
        );

        graph.reset();
        Check(weak.expired(), "the buffer must be released once it is unreferenced");
        tracker.ReleaseAllParked();
        Check(
            LiveAllocationCount(allocator) == before, "the device memory must be freed once the buffer is unreferenced"
        );
    }

    // ── 3.2 / 3.3 / 3.4: physics reallocation under a live render graph ─────

    {
        PhysicsSystem physics;
        PhysicsScene &scene = physics.CreateScene(1u);
        scene.SetSimulationEnabled(true);

        Rhi::SubmissionHelper submission{
            device_context.GetDeviceInterface(), device_context.GetAllocatorState(), tracker
        };

        for (uint32_t i = 0; i < 2u; ++i) {
            scene.AllocateRigidBodySlot();
        }
        scene.SyncGpuBuffers(device_context, submission);

        auto mm_old = scene.GetGpuBuffers().model_matrices;
        Check(mm_old != nullptr, "PhysicsScene must expose its model matrices buffer");
        const vk::Buffer old_handle = mm_old->GetBuffer();
        std::weak_ptr<const Rhi::ComputeBuffer> weak_old = mm_old;

        // Forward it to the render side exactly as MainClass does.
        rsys->GetSceneDataManager().SetModelMatricesBuffer(mm_old);
        Check(
            rsys->GetSceneDataManager().GetModelMatricesBuffer() == mm_old,
            "SceneDataManager must hold the forwarded buffer"
        );

        // Build a render graph that imports and reads it.
        RenderGraphBuilder rgb{*rsys};
        auto mm_handle = rgb.ImportExternalResource(mm_old, BufferAccess{BufferBits::ShaderRandomWrite});
        rgb.AddPass(
            RenderGraphPassBuilder{*rsys}
                .SetName("Model matrices consumer")
                .UseBuffer(mm_handle, BufferAccess{BufferBits::ShaderRandomRead})
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction(DummyComputePass)
                .Get()
        );
        auto graph = rgb.BuildRenderGraph();

        // The reallocation: grow the rigid body slot count, which makes
        // PhysicsScene replace its model matrices buffer.
        for (uint32_t i = 0; i < 6u; ++i) {
            scene.AllocateRigidBodySlot();
        }
        scene.SyncGpuBuffers(device_context, submission);

        auto mm_new = scene.GetGpuBuffers().model_matrices;
        Check(mm_new != nullptr, "the resized scene must expose a model matrices buffer");
        Check(mm_new != mm_old, "growing the slot count must replace the model matrices buffer");
        Check(
            mm_old->GetBuffer() == old_handle,
            "the previously exposed handle must remain valid while a reference is held"
        );

        // The render side moves on to the new buffer, dropping its share of the
        // old one; the graph's share is what keeps it alive now.
        rsys->GetSceneDataManager().SetModelMatricesBuffer(mm_new);
        mm_old.reset();
        Check(!weak_old.expired(), "the graph must still reference a live buffer after the reallocation");
        Check(
            mm_new->GetBuffer() != old_handle && mm_new->GetBuffer() != nullptr,
            "the next forwarding call hands over the new buffer"
        );

        graph.reset();
        Check(weak_old.expired(), "the previously forwarded buffer is released once it is unreferenced");
    }

    rsys->WaitForIdle();
    tracker.ReleaseAllParked();

    if (g_failures != 0) {
        std::cerr << "Model matrices ownership test FAILED (" << g_failures << " checks)." << std::endl;
        return 1;
    }
    std::cout << "Model matrices ownership test PASSED." << std::endl;
    return 0;
}
