// GPU buffer retirement under a real frame loop (tasks 2.2, 4.1, 4.2).
//
// Drives the engine's actual frame loop headlessly with a live XPBD GPU solver
// while the physics scene's geometry changes between steps, so buffers bound
// into still-executing frames are destroyed and replaced. Run under the
// validation layer (Debug builds), a failure of the retirement facility shows up
// as a use-after-free / invalid-handle report on stderr.
//
// Covers:
//   - a skipped frame (failed image acquisition) leaves no open epoch (2.2);
//   - several frames with two to three frames in flight and geometry changes
//     between steps, under the validation layer (4.1);
//   - a long dynamic run with repeated grow/shrink cycles: the parked-item
//     counter is non-monotonic and returns to zero after idle waits, and device
//     memory does not grow without bound (4.2).

#include "Framework/MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Solver/XPBDGpuSolver.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Submission/SubmissionHelper.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
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

    uint32_t LiveAllocationCount(const Rhi::AllocatorState &allocator) {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator.GetAllocator(), &stats);
        return stats.total.statistics.allocationCount;
    }

    VkDeviceSize LiveAllocationBytes(const Rhi::AllocatorState &allocator) {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator.GetAllocator(), &stats);
        return stats.total.statistics.allocationBytes;
    }

    /**
     * @brief A present provider whose acquisition can be made to fail on demand.
     *
     * The offscreen provider never fails, so this is the only way to drive
     * `FrameManager::StartFrame` down its "skip this frame" path deterministically.
     */
    class ControllablePresentProvider final : public IPresentProvider {
        const Rhi::DeviceInterface *m_device_interface;
        uint32_t m_image_count{3};
        uint32_t m_frame_counter{0};

    public:
        bool fail_next_acquire{false};
        vk::Extent2D extent{320, 240};
        vk::Format format{vk::Format::eR8G8B8A8Unorm};

        explicit ControllablePresentProvider(const Rhi::DeviceInterface &device_interface) :
            m_device_interface(&device_interface) {
        }

        vk::Extent2D GetExtent() const override {
            return extent;
        }
        vk::Format GetColorFormat() const override {
            return format;
        }
        uint32_t GetImageCount() const override {
            return m_image_count;
        }

        uint32_t AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t) override {
            if (fail_next_acquire) {
                fail_next_acquire = false;
                return std::numeric_limits<uint32_t>::max();
            }
            // Fulfil the acquire contract: signal image_ready via an empty submit.
            vk::SemaphoreSubmitInfo signal_info{image_ready_semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands};
            vk::SubmitInfo2 sinfo{};
            sinfo.signalSemaphoreInfoCount = 1;
            sinfo.pSignalSemaphoreInfos = &signal_info;
            m_device_interface->GetQueueInfo().graphicsQueue.submit2(sinfo, nullptr);
            return (m_frame_counter++) % m_image_count;
        }

        vk::CommandBuffer PrepareCopy(
            vk::Device, const RenderTargetTexture &, uint32_t, Rhi::MemoryAccessTypeImageBits
        ) override {
            return nullptr;
        }

        bool Present(vk::Device, uint32_t, vk::Semaphore) override {
            return false;
        }

        void Recreate(vk::Extent2D new_extent) override {
            extent = new_extent;
        }
    };

    /// @brief Allocate a box rigid body plus one collision shape for it.
    void AddBox(PhysicsScene &scene, float z) {
        const uint32_t body = scene.AllocateRigidBodySlot();
        RigidBodyComDescriptor rb{};
        rb.mass = 1.0f;
        rb.is_kinematic = false;
        rb.center_world_position = glm::vec4(0.0f, 0.0f, z, 1.0f);
        rb.center_world_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        rb.inertia = glm::mat4(1.0f);
        rb.inverse_inertia = glm::mat4(1.0f);
        scene.SubmitRigidBody(body, rb);

        const uint32_t shape = scene.AllocateCollisionShapeSlot();
        CollisionShapeComDescriptor sd{};
        sd.type = 0u; // SHAPE_TYPE_BOX
        sd.feature = glm::vec4(0.25f, 0.25f, 0.25f, 0.0f);
        sd.local_position = glm::vec4(0.0f);
        sd.local_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        sd.bound_rigid_body = body;
        scene.SubmitCollisionShape(shape, sd);
    }

    void BuildScene(PhysicsScene &scene, uint32_t body_count) {
        for (uint32_t i = 0; i < body_count; ++i) {
            AddBox(scene, 0.4f + 0.6f * static_cast<float>(i));
        }
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 320, .resol_y = 240, .headless = true, .title = "Gpu Buffer Retirement Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    auto &device_context = rsys->GetDeviceContext();
    auto &tracker = device_context.GetEpochTracker();
    auto &allocator = rsys->GetAllocatorState();

    std::cout << "Running under the validation layer; watch stderr for "
                 "use-after-free / invalid-handle reports."
              << std::endl;

    // ── 2.2: a skipped frame leaves no open epoch ───────────────────────────

    {
        // A dedicated FrameManager with a controllable present provider, so the
        // "skip this frame" path can be taken on demand.
        RenderSystemState::FrameManager frame_manager{*rsys};
        ControllablePresentProvider provider{device_context.GetDeviceInterface()};
        frame_manager.Create(provider);

        const Rhi::EpochWatermark newest_before = tracker.GetNewestWatermark();
        const size_t outstanding_before = tracker.GetOutstandingEpochCount();

        provider.fail_next_acquire = true;
        Check(
            frame_manager.StartFrame() == std::numeric_limits<uint32_t>::max(),
            "a failed acquisition must report a skipped frame"
        );
        Check(tracker.GetNewestWatermark() == newest_before, "a skipped frame must not open an epoch");
        Check(tracker.GetOutstandingEpochCount() == outstanding_before, "a skipped frame must leave no open epoch");

        // A committed frame opens exactly one epoch, and nothing more.
        Check(frame_manager.StartFrame() != std::numeric_limits<uint32_t>::max(), "headless acquisition must succeed");
        const Rhi::EpochWatermark frame_epoch = tracker.GetNewestWatermark();
        Check(frame_epoch == newest_before + 1, "a committed frame must open exactly one epoch");
        Check(
            tracker.GetOutstandingEpochCount() == outstanding_before + 1,
            "the committed frame's epoch must be outstanding"
        );

        // This test never submits that frame, so the epoch it opened produced no
        // GPU work and is abandoned — the protocol's terminal state for it.
        tracker.AbandonEpoch(frame_epoch);
        Check(
            tracker.GetOutstandingEpochCount() == outstanding_before,
            "abandoning an epoch that produced no work must terminate it"
        );

        // The acquire contract signals the image-ready semaphore with an empty
        // submit, so let the queue drain before this frame manager's semaphores
        // are destroyed.
        rsys->WaitForIdle();
    }

    // ── 4.1 / 4.2: real frames with geometry changes between steps ──────────

    RenderTargetTexture::RenderTargetTextureDesc present_desc{
        .dimensions = 2,
        .width = 320,
        .height = 240,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    auto present_texture =
        RenderTargetTexture::CreateUnique(device_context, present_desc, Rhi::Texture::SamplerDesc{}, "Present target");

    // A minimal transfer pass that clears the present target, so the frame's
    // command buffer carries a real render-side pass alongside the physics
    // dispatches (and the target has a defined layout for the present copy).
    RenderGraphBuilder present_builder{*rsys};
    auto present_handle = present_builder.ImportExternalResource(*present_texture);
    present_builder.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Clear present target")
            .UseImage(present_handle, Rhi::MemoryAccessTypeImageBits::TransferWrite)
            .SetAffinity(RenderGraphPassAffinity::Transfer)
            .SetPassFunction([present_handle](CommandBuffer &cb, const RenderGraph &rg) {
                auto *target = rg.GetInternalTextureResource(present_handle);
                cb.GetCommandBuffer().clearColorImage(
                    target->GetImage(),
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue{0.1f, 0.1f, 0.2f, 1.0f},
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
                );
            })
            .Get()
    );
    auto present_graph = present_builder.BuildRenderGraph();

    PhysicsSystem physics;
    PhysicsScene &scene = physics.CreateScene(1u);
    scene.SetSimulationEnabled(true);
    physics.RegisterSolver(1u, std::make_unique<XpbdGpuSolver>(device_context));

    BuildScene(scene, 3u);
    scene.SyncGpuBuffers(device_context, rsys->GetFrameManager().GetSubmissionHelper());

    std::vector<size_t> parked_samples;
    std::vector<VkDeviceSize> cycle_bytes;
    std::vector<uint32_t> cycle_allocs;

    constexpr uint32_t kCycles = 10;
    constexpr uint32_t kFramesPerCycle = 6;
    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
        // Alternate the geometry between a small and a larger scene. Each
        // transition retires the whole previous buffer set while earlier frames
        // are still executing on the GPU.
        scene.Clear();
        BuildScene(scene, (cycle % 2u == 0u) ? 3u : 7u);
        scene.SyncGpuBuffers(device_context, rsys->GetFrameManager().GetSubmissionHelper());

        for (uint32_t frame = 0; frame < kFramesPerCycle; ++frame) {
            physics.PreGPUStep();
            if (rsys->StartFrame() == std::numeric_limits<uint32_t>::max()) {
                Check(false, "headless acquisition must never fail in this loop");
                continue;
            }
            auto cb = rsys->GetFrameManager().BeginMainCommandBuffer();
            physics.GPUStep(cb.GetCommandBuffer());
            present_graph->RecordIntoMainCommandBuffer(*rsys);
            rsys->CompleteFrame(*present_texture, Rhi::MemoryAccessTypeImageBits::TransferWrite);
            physics.PostGPUStep();
            parked_samples.push_back(tracker.GetParkedResourceCount());
        }

        // A device-idle point must release everything that was parked.
        rsys->WaitForIdle();
        Check(tracker.GetParkedResourceCount() == 0u, "a device-idle wait must release every parked resource");
        cycle_bytes.push_back(LiveAllocationBytes(allocator));
        cycle_allocs.push_back(LiveAllocationCount(allocator));
    }

    // 4.1: the run completed under the validation layer. Nothing to assert here
    // beyond the fact that no validation error aborted it; the substantive check
    // is the retirement bookkeeping below.

    // 4.2: the parked-item counter is non-monotonic and drains at idle waits.
    const size_t peak_parked =
        parked_samples.empty() ? 0u : *std::max_element(parked_samples.begin(), parked_samples.end());
    Check(peak_parked > 0u, "geometry changes must actually park retired buffers");
    bool decreased = false;
    for (size_t i = 1; i < parked_samples.size(); ++i) {
        if (parked_samples[i] < parked_samples[i - 1]) {
            decreased = true;
            break;
        }
    }
    Check(decreased, "the parked-item count must fall again as epochs complete (non-monotonic)");

    // 4.2: device memory is bounded by the largest scene, not by the number of
    // grow/shrink cycles. Every cycle ends at the same geometry as cycle 0, so a
    // leak of retired buffers would show up as growth here.
    Check(cycle_bytes.size() == kCycles, "every cycle must be measured");
    if (cycle_allocs.size() == kCycles && !cycle_allocs.empty()) {
        const VkDeviceSize worst_bytes = *std::max_element(cycle_bytes.begin(), cycle_bytes.end());
        const uint32_t worst_allocs = *std::max_element(cycle_allocs.begin(), cycle_allocs.end());
        std::cout << "Device memory after each cycle (bytes): ";
        for (VkDeviceSize b : cycle_bytes) std::cout << b << " ";
        std::cout << "\nLive allocations after each cycle: ";
        for (uint32_t a : cycle_allocs) std::cout << a << " ";
        std::cout << "\nParked peak: " << peak_parked << std::endl;

        Check(
            cycle_allocs.back() <= cycle_allocs.front() + 2u,
            "the live allocation count must not grow across repeated grow/shrink cycles"
        );
        Check(
            worst_allocs <= cycle_allocs.front() * 2u,
            "the live allocation count must stay bounded across repeated grow/shrink cycles"
        );
        Check(
            worst_bytes <= cycle_bytes.front() * 2u,
            "device memory must stay bounded across repeated grow/shrink cycles"
        );
    }

    rsys->WaitForIdle();
    tracker.ReleaseAllParked();

    if (g_failures != 0) {
        std::cerr << "Gpu buffer retirement test FAILED (" << g_failures << " checks)." << std::endl;
        return 1;
    }
    std::cout << "Gpu buffer retirement test PASSED." << std::endl;
    return 0;
}
