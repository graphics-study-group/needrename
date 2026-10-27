// Model matrices initialization order (fix-model-matrices-init-order).
//
// Two writers reach the model matrices buffer before the first frame:
//
//   1. the creation-time identity fill in `SceneDataManager::Create`, and
//   2. any producer that writes the buffer after render-system creation and
//      before a frame is submitted - `PhysicsApp::CommitScene`'s seed is the
//      engine's own example.
//
// The buffer is initialized once, so the only thing that decides who owns the
// contents is which write executes last on the queue. Enqueued into the frame
// manager's deferred upload batch, the fill is recorded by the first
// `SubmitFrame`, i.e. after the producer, and wins:
//
//   SceneDataManager::Create     first SubmitFrame
//        |                              |
//        |  fill enqueued (deferred)    |
//        |                              +-- batch (identity copy) --> main CB
//        v                              v
//   [producer write] ------------> [identity copy] -> [frames read identity]
//
// The requirement places the fill earlier: it is an initialization submission
// of its own, submitted and waited for inside `Create`, so it precedes every
// producer and no producer can be overwritten afterwards:
//
//   SceneDataManager::Create           [producer write] --> [frames read the pose]
//        |
//        +-- fill submitted and waited for (buffer = identity)
//
// This test pins both halves of that contract:
//
//   - "Identity is readable before any frame is submitted": immediately after
//     render-system creation the buffer holds identity matrices, with no frame
//     submission required to make them visible;
//   - "Initialization precedes any producer": a producer writing the buffer
//     before any frame has been submitted still owns the contents after the
//     frame manager's staged uploads are flushed.
//
// Reading back requires a copy into a staging buffer, because the model
// matrices buffer is device-local.
//
// Fidelity note: the test flushes with `ExecuteSubmissionImmediately`, while the
// frame path uses `ExecuteSubmission`. Both record every currently pending
// operation into a one-time command buffer and submit it; only the wait differs
// (fence versus timeline semaphore). With the fix in place the flush has no
// model matrices work pending at all, which is exactly the property asserted.

#include "Framework/MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Solver/DummySolver.h"
#include "Render/FullRenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
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
#include <vector>
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

    /// @brief The identity matrix the creation-time fill writes.
    const glm::mat4 kIdentity{1.0f};

    bool IsIdentity(const glm::mat4 &matrix) {
        return std::memcmp(&matrix, &kIdentity, sizeof(glm::mat4)) == 0;
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 640, .resol_y = 480, .headless = true, .title = "Model Matrices Initialization Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    auto &allocator = rsys->GetAllocatorState();
    auto &device_context = rsys->GetDeviceContext();
    auto &scene_data = rsys->GetSceneDataManager();
    auto &tracker = device_context.GetEpochTracker();
    const auto &dev_iface = device_context.GetDeviceInterface();
    auto device = dev_iface.GetDevice();
    const auto &queue_info = dev_iface.GetQueueInfo();

    constexpr uint32_t kInitCount = RenderSystemState::SceneDataManager::MAX_MODEL_MATRICES;

    /// @brief Copy the head of the model matrices buffer into a CPU-visible
    /// staging buffer and read it back.
    ///
    /// The render-owned buffer is device-local, so the copy is the only way to
    /// observe its contents. Each read is its own submission, waited for on the
    /// host; none of them is a frame submission.
    Rhi::ComputeBuffer &model_matrices = scene_data.GetModelMatricesBuffer();
    auto staging = Rhi::ComputeBuffer::CreateUnique(
        allocator, kInitCount * sizeof(glm::mat4), true, false, false, false, "Model matrices readback"
    );
    auto read_back = [&](uint32_t count) {
        const size_t bytes = static_cast<size_t>(count) * sizeof(glm::mat4);
        Check(bytes <= staging->GetSize(), "the readback staging buffer must cover the requested range");

        auto cbs = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        );
        auto cb = std::move(cbs[0]);
        cb->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        // Every earlier write to the buffer is a device write - the creation-time
        // transfer, or a producer's shader write - so this barrier is what makes
        // it visible to the copy.
        const std::array before_copy{vk::MemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead
        }};
        cb->pipelineBarrier2(vk::DependencyInfo{{}, before_copy, {}, {}});
        cb->copyBuffer(model_matrices.GetBuffer(), staging->GetBuffer(), vk::BufferCopy{0, 0, bytes});
        // Make the copy's result visible to the host read below.
        const std::array before_host_read{vk::MemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead
        }};
        cb->pipelineBarrier2(vk::DependencyInfo{{}, before_host_read, {}, {}});
        cb->end();

        vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
        vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
        queue_info.graphicsQueue.submit2(sinfo, fence.get());
        const auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
        Check(wait_result == vk::Result::eSuccess, "the readback copy must complete");

        staging->Invalidate(0, bytes);
        std::vector<glm::mat4> result(count);
        std::memcpy(result.data(), staging->GetVMAddress(), bytes);
        return result;
    };

    /// @brief Record one scope into a one-time command buffer, submit it and wait.
    auto run_command_buffer = [&](auto &&record) {
        auto cbs = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        );
        auto cb = std::move(cbs[0]);
        cb->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        record(cb.get());
        cb->end();

        vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
        vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
        queue_info.graphicsQueue.submit2(sinfo, fence.get());
        const auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
        Check(wait_result == vk::Result::eSuccess, "the test's command buffer must complete");
    };

    // -- Identity is readable before any frame is submitted -------------------

    {
        // Render-system creation has returned and no frame has been submitted:
        // `MainClass::Initialize` creates the render system but never calls
        // `RunOneFrame`, so nothing has reached `FrameManager::SubmitFrame`.
        const auto matrices = read_back(kInitCount);
        Check(
            scene_data.GetModelMatricesBuffer().GetBuffer() == model_matrices.GetBuffer(),
            "the model matrices buffer must be created by render-system creation"
        );

        uint32_t wrong = kInitCount;
        for (uint32_t i = 0; i < kInitCount; ++i) {
            if (!IsIdentity(matrices[i])) {
                wrong = i;
                break;
            }
        }
        Check(
            wrong == kInitCount,
            "the buffer must hold identity matrices immediately after render-system creation, "
            "without any frame submission"
        );
        if (wrong != kInitCount) {
            std::cerr << "  first non-identity matrix is at index " << wrong << std::endl;
        }
    }

    // -- Initialization precedes any producer ---------------------------------

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

        // Upload and wait for the poses the producer reads.
        Rhi::SubmissionHelper submission{dev_iface, allocator, tracker};
        scene.SyncGpuBuffers(device_context, submission);

        // Produce through the engine's model matrix entry point, into the
        // render-owned buffer - the same call `PhysicsApp::CommitScene` seeds
        // with. No frame is submitted before or after it.
        run_command_buffer([&](vk::CommandBuffer cb) { physics.GPUCalcModelMatrices(scene, cb, model_matrices); });

        // Flush the frame manager's staged uploads: what the first
        // `SubmitFrame` would have done. A creation-time fill left in that batch
        // would be recorded here, after the producer, and overwrite it.
        rsys->GetFrameManager().GetSubmissionHelper().ExecuteSubmissionImmediately();

        const auto matrices = read_back(kBodyCount);
        for (uint32_t i = 0; i < kBodyCount; ++i) {
            const glm::mat4 &m = matrices[i];
            Check(
                !IsIdentity(m),
                "the producer's write must survive the frame manager's staged upload flush "
                "(identity here means the creation-time fill overwrote it)"
            );
            // Non-degenerate: an unproduced or zeroed matrix fails these.
            Check(m[0][0] != 0.0f && m[3][3] == 1.0f, "the producer must leave a valid model matrix");
            Check(
                m[3][0] == 0.0f && m[3][1] == 0.0f && m[3][2] == body_z[i],
                "the buffer must hold the producer's pose after the flush"
            );
        }
    }

    rsys->WaitForIdle();
    tracker.ReleaseAllParked();

    if (g_failures != 0) {
        std::cerr << "Model matrices initialization test FAILED (" << g_failures << " checks)." << std::endl;
        return 1;
    }
    std::cout << "Model matrices initialization test PASSED." << std::endl;
    return 0;
}
