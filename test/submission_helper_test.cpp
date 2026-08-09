#include "Rhi/AllocatorState.h"
#include "Rhi/DeviceInterface.h"

#include "Rhi/DeviceBuffer.h"
#include "Rhi/MemoryTypes.h"
#include "Rhi/Structs.h"
#include "Rhi/SubmissionHelper.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.hpp>

using namespace Engine;
using namespace Engine::Rhi;

static bool g_pass = true;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                                        \
            g_pass = false;                                                                                            \
        }                                                                                                              \
    } while (0)

static bool ExpectRuntimeError(std::function<void()> fn) {
    try {
        fn();
    } catch (const std::runtime_error &) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

int main() {
    // Requires the SDL video subsystem for SDL_Vulkan_LoadLibrary(nullptr).
    SDL_Init(SDL_INIT_VIDEO);

    // Standalone headless facilities: no window, no RenderSystem, no FrameManager.
    Rhi::DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "Rhi::SubmissionHelper Standalone Test",
        .application_version = 0,
        .dynamic_dispatcher = nullptr,
    };
    Rhi::DeviceInterface gpu_device{cfg};
    Rhi::AllocatorState allocator_state{gpu_device};
    const auto device = gpu_device.GetDevice();
    const auto &allocator = allocator_state;
    CHECK(device && "Rhi must create a Vulkan device headlessly.");

    // Initialize this module's copy of the dynamic dispatch loader (instance
    // first, then device). Same pattern as RenderSystem::Create.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(gpu_device.GetInstance(), ::vkGetInstanceProcAddr);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

    // Timeline semaphore for the caller-provided submission signal.
    vk::SemaphoreTypeCreateInfo stci{vk::SemaphoreType::eTimeline, 0};
    vk::SemaphoreCreateInfo sci{};
    sci.pNext = &stci;
    auto timeline = device.createSemaphoreUnique(sci);

    Rhi::SubmissionHelper helper{gpu_device, allocator_state};
    auto target = Rhi::DeviceBuffer::CreateUnique(allocator, {Rhi::BufferTypeBits::CopyTo}, 64, "Target buffer");
    std::vector<std::byte> data(64, std::byte{0xAB});

    // Case 1: empty-batch ExecuteSubmission advances the signal CPU-side;
    // idle OnBatchComplete is idempotent.
    helper.ExecuteSubmission({timeline.get(), 1});
    uint64_t value = 0;
    CHECK(device.getSemaphoreCounterValue(timeline.get(), &value) == vk::Result::eSuccess && value == 1);
    helper.OnBatchComplete();

    // Case 2: normal submit -> reap -> re-submit cycle.
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmission({timeline.get(), 2});
    helper.OnBatchComplete();
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmission({timeline.get(), 3});
    helper.OnBatchComplete();

    // Case 3: consecutive deferred submissions are rejected.
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmission({timeline.get(), 4});
    CHECK(ExpectRuntimeError([&] { helper.ExecuteSubmission({timeline.get(), 5}); }));
    helper.OnBatchComplete();

    // Case 4: immediate submission while a deferred batch is pending is rejected.
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmission({timeline.get(), 6});
    CHECK(ExpectRuntimeError([&] { helper.ExecuteSubmissionImmediately(); }));
    helper.OnBatchComplete();

    // Case 5: frame end with unsubmitted operations is rejected; the state is
    // left untouched (Reset with pending operations), so submission recovers.
    helper.EnqueueBufferSubmission(*target, data);
    CHECK(ExpectRuntimeError([&] { helper.OnBatchComplete(); }));
    helper.ExecuteSubmission({timeline.get(), 7});
    helper.OnBatchComplete();

    // Case 6: consecutive immediate submissions are legal (self-contained).
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmissionImmediately();
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmissionImmediately();

    // Case 7: mixing immediate and deferred submissions.
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmission({timeline.get(), 8});
    helper.OnBatchComplete();
    helper.EnqueueBufferSubmission(*target, data);
    helper.ExecuteSubmissionImmediately();

    // Case 8: destruction with a pending deferred batch does not crash
    // (the destructor waits on the batch fence).
    {
        Rhi::SubmissionHelper destructing_helper{gpu_device, allocator_state};
        auto destructing_target =
            Rhi::DeviceBuffer::CreateUnique(allocator, {Rhi::BufferTypeBits::CopyTo}, 64, "Destructing target");
        std::vector<std::byte> destructing_data(64, std::byte{0xCD});
        destructing_helper.EnqueueBufferSubmission(*destructing_target, destructing_data);
        destructing_helper.ExecuteSubmission({timeline.get(), 9});
    }

    device.waitIdle();
    if (!g_pass) {
        std::cerr << "Rhi::SubmissionHelper standalone test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Rhi::SubmissionHelper standalone test PASSED." << std::endl;
    return 0;
}
