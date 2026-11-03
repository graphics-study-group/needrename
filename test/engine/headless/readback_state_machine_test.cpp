// The mapped window's state machine across submissions (task 8.5).
//
// A mapped buffer cannot be consumed by device work, so a readback target that a caller has mapped
// has to be released before the next copy targets it, and re-mapped before it is read again. Two
// frames of that cycle are what this test drives:
//
//   write through the window -> submit a copy -> read the readback -> release it -> do it again.
//
// A state machine that only works for the first submission passes nothing here: the second copy
// would target a buffer that is still mapped, and the device says so at submit.

#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Command/CommandBuffer.h"
#include "Rhi/Command/CommandEncoder.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/Fence.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

using namespace Engine;

namespace {
    constexpr size_t WORDS = 8;

    /// One frame of the cycle: write, copy on the device, read the result, release the map.
    bool RunFrame(
        Engine::Rhi::DeviceContext &dc, Engine::Rhi::DeviceBuffer &source, Engine::Rhi::DeviceBuffer &readback,
        uint32_t seed, size_t frame, std::array<uint32_t, WORDS> &expected
    ) {
        for (size_t i = 0; i < WORDS; ++i) {
            expected[i] = seed + static_cast<uint32_t>(i) * 31u;
        }
        auto *window = reinterpret_cast<uint32_t *>(source.GetVMAddress());
        if (window == nullptr) {
            std::cerr << "Readback state machine: frame " << frame << " has no source window." << std::endl;
            return false;
        }
        std::memcpy(window, expected.data(), WORDS * sizeof(uint32_t));

        auto buffers = Engine::Rhi::CommandBuffer::Create(dc, Engine::Rhi::CommandBufferPool::GraphicsOneTime, 1);
        Engine::Rhi::CommandBuffer &command_buffer = buffers[0];
        command_buffer.Begin();
        Engine::Rhi::CommandEncoder encoder{command_buffer.GetHandle()};
        encoder.CopyBuffer(source.GetBuffer(), readback.GetBuffer(), WORDS * sizeof(uint32_t));
        command_buffer.End();

        const Engine::Rhi::Fence fence = Engine::Rhi::Fence::Create(dc.GetDeviceInterface(), false);
        Engine::Rhi::Submission submission{};
        submission.command_buffers.push_back(command_buffer.GetHandle());
        dc.Submit(submission, fence.GetHandle());
        if (!dc.WaitForFence(fence.GetHandle(), std::numeric_limits<uint64_t>::max())) {
            std::cerr << "Readback state machine: frame " << frame << " did not complete." << std::endl;
            return false;
        }

        readback.PrepareReadback();
        const auto *got = reinterpret_cast<const uint32_t *>(readback.GetVMAddress());
        if (got == nullptr) {
            std::cerr << "Readback state machine: frame " << frame << " has no readback window." << std::endl;
            return false;
        }
        for (size_t i = 0; i < WORDS; ++i) {
            if (got[i] != expected[i]) {
                std::cerr << "Readback state machine: frame " << frame << " word " << i << " is " << got[i]
                          << ", expected " << expected[i] << "." << std::endl;
                return false;
            }
        }
        // Given back before the next frame's copy targets it: a mapped buffer cannot be a copy
        // destination, and this release is what makes the second submission legal.
        readback.ReleaseReadback();
        return true;
    }
} // namespace

int main(int, char **) {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{
        .resol_x = 320, .resol_y = 240, .headless = true, .title = "Readback state machine test"
    };
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_ERROR);
    auto rsys = cmc->GetRenderSystem();
    if (rsys == nullptr) {
        std::cerr << "Readback state machine FAILED: no render system." << std::endl;
        return 1;
    }

    const auto &alloc = rsys->GetAllocatorState();
    auto &dc = rsys->GetDeviceContext();
    const size_t bytes = WORDS * sizeof(uint32_t);

    const Engine::Rhi::BufferType source_type{Engine::Rhi::BufferTypeBits::StagingToDevice};
    auto source = Engine::Rhi::DeviceBuffer::CreateUnique(alloc, source_type, bytes, "State machine source");
    const Engine::Rhi::BufferType readback_type{Engine::Rhi::BufferTypeBits::ReadbackFromDevice};
    auto readback = Engine::Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "State machine readback");
    if (!source || !readback) {
        std::cerr << "Readback state machine FAILED: buffer creation failed." << std::endl;
        return 1;
    }

    std::array<uint32_t, WORDS> expected{};
    for (size_t frame = 0; frame < 3; ++frame) {
        const uint32_t seed = 0x1000u + static_cast<uint32_t>(frame) * 0x111u;
        if (!RunFrame(dc, *source, *readback, seed, frame, expected)) {
            std::cerr << "Readback state machine FAILED at frame " << frame << "." << std::endl;
            return 1;
        }
    }

    std::cout << "Readback state machine test PASSED: three submissions mapped, read and released "
                 "the readback window in turn." << std::endl;
    return 0;
}
