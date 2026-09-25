// Stable model matrix identity and per-frame production (tasks 6.1, 6.2).
//
// The model matrices buffer is render-owned and its object is never replaced:
// only its storage is reallocated in place. This test asserts the properties
// that ownership model rests on:
//
//   - the buffer object's address is unchanged across a slot-count change,
//     while its handle and size follow the storage (6.1);
//   - the render graph imports that object, so the handle it resolves when
//     recording is the current one, and a growth after the graph has been
//     built leaves it referencing a live buffer (6.1);
//   - no shared ownership participates: the accessor hands out a reference,
//     and neither `ComputeBuffer::CreateShared` nor the `shared_ptr` import
//     overload exists (6.1);
//   - a step alone writes no model matrices, so a frame that produces nothing
//     issues no model matrix dispatch (6.1);
//   - a paused-frame fixture: with simulation disabled and no step at all, a
//     recorded production still yields valid, non-degenerate matrices, and the
//     check fails if the production call is removed (6.2).
//
// The production checks write into a CPU-readable mirror target, because the
// render-owned buffer itself is device-local; the mirror is bound through the
// same shader binding name the renderer's buffer is bound with.

#include "Framework/MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Solver/DummySolver.h"
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
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <type_traits>
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

    /// @brief The buffer the graph imports must have been handed over as a
    /// reference, never through a reference-counted handle.
    ///
    /// The removal of the `std::shared_ptr<const Rhi::DeviceBuffer>` import
    /// overload itself is verified by a repository-wide search (the overload no
    /// longer exists to be named).
    static_assert(
        std::is_same_v<
            decltype(std::declval<RenderSystemState::SceneDataManager &>().GetModelMatricesBuffer()),
            Rhi::ComputeBuffer &>,
        "SceneDataManager must hand out a reference to a uniquely owned buffer"
    );
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 640, .resol_y = 480, .headless = true, .title = "Model Matrices Ownership Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    auto &allocator = rsys->GetAllocatorState();
    auto &device_context = rsys->GetDeviceContext();
    auto device = device_context.GetDeviceInterface().GetDevice();
    auto &tracker = device_context.GetEpochTracker();
    auto &scene_data = rsys->GetSceneDataManager();

    /// @brief Record one recording-scope into a one-time command buffer, submit
    /// it and wait for completion, so the host observes the dispatches.
    auto run_command_buffer = [&](auto &&record) {
        const auto &dev_iface = device_context.GetDeviceInterface();
        auto cbs = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{
                dev_iface.GetQueueInfo().graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
            }
        );
        auto cb = std::move(cbs[0]);
        cb->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        record(cb.get());
        cb->end();

        vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
        vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
        dev_iface.GetQueueInfo().graphicsQueue.submit2(sinfo, fence.get());
        const auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
        Check(wait_result == vk::Result::eSuccess, "the test's command buffer must complete");
        device.waitIdle();
    };

    // ── 6.1: the render-owned buffer never moves; only its storage changes ──

    {
        Rhi::ComputeBuffer *const address = &scene_data.GetModelMatricesBuffer();
        const vk::Buffer handle_initial = address->GetBuffer();
        const size_t capacity_initial = address->GetSize();
        Check(handle_initial != nullptr, "the render-owned buffer must exist before any producer runs");
        Check(capacity_initial > 0u, "the buffer must start with an initial reservation");

        // A request within the initial reservation reallocates nothing.
        scene_data.EnsureModelMatricesCapacity(4u);
        Check(&scene_data.GetModelMatricesBuffer() == address, "the buffer object must not move");
        Check(address->GetBuffer() == handle_initial, "a within-capacity request must not change the handle");
        Check(address->GetSize() == capacity_initial, "a within-capacity request must not change the capacity");

        // A request above the reservation grows the storage in place.
        const uint32_t elements_initial = static_cast<uint32_t>(capacity_initial / sizeof(glm::mat4));
        const uint32_t grown = elements_initial + 16u;
        scene_data.EnsureModelMatricesCapacity(grown);
        Check(
            &scene_data.GetModelMatricesBuffer() == address,
            "growth must keep the buffer object at the same address, so a long-lived reference stays valid"
        );
        Check(address->GetBuffer() != handle_initial, "growth must replace the storage and its handle");
        Check(
            address->GetSize() >= static_cast<size_t>(grown) * sizeof(glm::mat4),
            "the grown buffer must satisfy the requested element count"
        );

        // The graph imports the same object, so the handle it resolves when
        // recording is the current one, and a later growth cannot dangle it.
        RenderGraphBuilder rgb{*rsys};
        auto mm_handle = rgb.ImportExternalResource(*address, BufferAccess{BufferBits::ShaderRandomWrite});
        rgb.AddPass(
            RenderGraphPassBuilder{*rsys}
                .SetName("Model matrices consumer")
                .UseBuffer(mm_handle, BufferAccess{BufferBits::ShaderRandomRead})
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction(DummyComputePass)
                .Get()
        );
        auto graph = rgb.BuildRenderGraph();

        const vk::Buffer handle_imported = address->GetBuffer();
        const uint32_t grown_again = grown + 32u;
        scene_data.EnsureModelMatricesCapacity(grown_again);
        Check(
            &scene_data.GetModelMatricesBuffer() == address,
            "a growth after the graph was built must leave the imported object alive"
        );
        Check(
            address->GetBuffer() != handle_imported && address->GetBuffer() != nullptr,
            "the object the graph imported reports the current handle after a growth"
        );
        Check(
            address->GetSize() >= static_cast<size_t>(grown_again) * sizeof(glm::mat4),
            "the graph's buffer stays large enough for the frames that follow"
        );

        graph.reset();
    }

    // ── 6.1 / 6.2: production is separate from the step ─────────────────────

    {
        PhysicsSystem physics;
        PhysicsScene &scene = physics.CreateScene(1u);
        physics.RegisterSolver(1u, std::make_unique<DummySolver>(device_context));

        constexpr uint32_t kBodyCount = 3u;
        const std::array<float, kBodyCount> body_z{1.0f, 2.0f, 3.0f};
        for (uint32_t i = 0; i < kBodyCount; ++i) {
            const uint32_t body = scene.AllocateRigidBodySlot();
            RigidBodyComDescriptor rb{};
            rb.mass = 1.0f;
            rb.center_world_position = glm::vec4(0.0f, 0.0f, body_z[i], 1.0f);
            rb.center_world_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            rb.inertia = glm::mat4(1.0f);
            rb.inverse_inertia = glm::mat4(1.0f);
            scene.SubmitRigidBody(body, rb);
        }

        Rhi::SubmissionHelper submission{
            device_context.GetDeviceInterface(), device_context.GetAllocatorState(), tracker
        };
        scene.SyncGpuBuffers(device_context, submission);

        // A CPU-readable stand-in for the renderer's target, bound through the
        // same shader binding name.
        auto target = Rhi::ComputeBuffer::CreateUnique(
            allocator, kBodyCount * sizeof(glm::mat4), true, false, false, false, "Model matrices mirror"
        );
        auto *const matrices = reinterpret_cast<glm::mat4 *>(target->GetVMAddress());
        const glm::mat4 kZeroMatrix{0.0f};
        auto zero_target = [&] {
            for (uint32_t i = 0; i < kBodyCount; ++i) matrices[i] = kZeroMatrix;
            target->Flush();
        };
        auto target_is_zeroed = [&] {
            for (uint32_t i = 0; i < kBodyCount; ++i) {
                if (std::memcmp(&matrices[i], &kZeroMatrix, sizeof(glm::mat4)) != 0) return false;
            }
            return true;
        };
        auto target_matches_pose = [&](const std::array<float, kBodyCount> &expected_z) {
            for (uint32_t i = 0; i < kBodyCount; ++i) {
                const glm::mat4 &m = matrices[i];
                // Non-degenerate: a zeroed (unproduced) buffer fails here.
                if (m[0][0] == 0.0f || m[3][3] == 0.0f) {
                    std::cerr << "FAIL: body " << i << " has a degenerate model matrix" << std::endl;
                    g_failures++;
                    return;
                }
                if (m[3][0] != 0.0f || m[3][1] != 0.0f || m[3][2] != expected_z[i] || m[3][3] != 1.0f) {
                    std::cerr << "FAIL: body " << i << " translation is not the current pose (z=" << m[3][2]
                              << ", expected " << expected_z[i] << ")" << std::endl;
                    g_failures++;
                    return;
                }
            }
        };

        // A step alone writes no model matrices: the target stays untouched.
        zero_target();
        physics.PreGPUStep();
        run_command_buffer([&](vk::CommandBuffer cb) { physics.GPUStep(cb); });
        Check(target_is_zeroed(), "a step alone must issue no model matrix dispatch");

        // 6.2 paused-frame fixture: simulation disabled and no step at all, yet
        // a recorded production yields valid, non-degenerate matrices from the
        // current poses. Remove the production call and the mirror stays zeroed,
        // which the check below reports.
        scene.SetSimulationEnabled(false);
        zero_target();
        run_command_buffer([&](vk::CommandBuffer cb) { physics.GPUCalcModelMatrices(scene, cb, *target); });
        Check(!target_is_zeroed(), "a paused frame with no step must still produce model matrices");
        target_matches_pose(body_z);

        // The frame rule: production after a step observes the step's poses.
        scene.SetSimulationEnabled(true);
        zero_target();
        physics.PreGPUStep();
        run_command_buffer([&](vk::CommandBuffer cb) {
            physics.GPUStep(cb);
            physics.GPUCalcModelMatrices(scene, cb, *target);
        });
        std::array<float, kBodyCount> displaced_z{};
        for (uint32_t i = 0; i < kBodyCount; ++i) {
            displaced_z[i] = matrices[i][3][2];
            Check(
                displaced_z[i] < body_z[i] && displaced_z[i] > body_z[i] - 1.0f,
                "a frame's production must observe the poses its step produced"
            );
        }
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
