// A host write that never calls `Flush` must still reach the device (tasks 6.2 and 8.5).
//
// The mapped window is the only place a caller writes a buffer from the host, and the contract these
// two tasks state is that a submission which reads that buffer sees the bytes - whether or not the
// caller remembered to flush. On a backend where the window is a host copy rather than device memory
// that is not free: the copy has to be published before the command that reads it, which means the
// submission has to do it.
//
// The test writes a recognisable pattern through the window, **never flushes**, copies the buffer
// into a readback buffer on the device, and compares what comes back.

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
using namespace Engine::Rhi;

namespace {
    constexpr size_t PATTERN_WORDS = 16;
    constexpr uint32_t PATTERN_SEED = 0x9E3779B9u;
}

int main(int, char **) {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{
        .resol_x = 320, .resol_y = 240, .headless = true, .title = "Host write window test"
    };
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_ERROR);
    auto rsys = cmc->GetRenderSystem();
    if (rsys == nullptr) {
        std::cerr << "Host write window test FAILED: no render system." << std::endl;
        return 1;
    }

    const auto &alloc = rsys->GetAllocatorState();
    auto &dc = rsys->GetDeviceContext();
    const size_t bytes = PATTERN_WORDS * sizeof(uint32_t);

    const BufferType source_type{BufferTypeBits::StagingToDevice};
    auto source = DeviceBuffer::CreateUnique(alloc, source_type, bytes, "Host write source");
    const BufferType readback_type{BufferTypeBits::ReadbackFromDevice};
    auto readback = DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Host write readback");
    if (!source || !readback) {
        std::cerr << "Host write window test FAILED: buffer creation failed." << std::endl;
        return 1;
    }

    // The pattern, written where the contract says a caller writes it - and deliberately not
    // flushed. A backend that needs the flush to publish the window has to do it at submit.
    std::array<uint32_t, PATTERN_WORDS> expected{};
    auto *window = reinterpret_cast<uint32_t *>(source->GetVMAddress());
    if (window == nullptr) {
        std::cerr << "Host write window test FAILED: the mapped window is null." << std::endl;
        return 1;
    }
    for (size_t i = 0; i < PATTERN_WORDS; ++i) {
        expected[i] = PATTERN_SEED + static_cast<uint32_t>(i) * 7919u;
        window[i] = expected[i];
    }

    auto buffers = Engine::Rhi::CommandBuffer::Create(dc, Engine::Rhi::CommandBufferPool::GraphicsOneTime, 1);
    Engine::Rhi::CommandBuffer &command_buffer = buffers[0];
    Engine::Rhi::CommandEncoder encoder{command_buffer.GetHandle()};
    encoder.CopyBuffer(source->GetBuffer(), readback->GetBuffer(), bytes);
    command_buffer.End();

    const Engine::Rhi::Fence fence = Engine::Rhi::Fence::Create(dc.GetDeviceInterface(), false);
    Engine::Rhi::Submission submission{};
    submission.command_buffers.push_back(command_buffer.GetHandle());
    dc.Submit(submission, fence.GetHandle());
    if (!dc.WaitForFence(fence.GetHandle(), std::numeric_limits<uint64_t>::max())) {
        std::cerr << "Host write window test FAILED: the submission did not complete." << std::endl;
        return 1;
    }

    // The readback buffer's memory is host-visible but not necessarily coherent, so the device's
    // write is made visible before the host reads it. That is the read side of the rule the write
    // side follows.
    readback->Invalidate(0, bytes);
    const auto *got = reinterpret_cast<const uint32_t *>(readback->GetVMAddress());
    if (got == nullptr) {
        std::cerr << "Host write window test FAILED: the readback window is null." << std::endl;
        return 1;
    }

    size_t mismatches = 0;
    size_t first_bad = PATTERN_WORDS;
    for (size_t i = 0; i < PATTERN_WORDS; ++i) {
        if (got[i] != expected[i]) {
            if (first_bad == PATTERN_WORDS) {
                first_bad = i;
            }
            ++mismatches;
        }
    }
    if (mismatches != 0) {
        std::cerr << "Host write window test FAILED: " << mismatches << " of " << PATTERN_WORDS
                  << " words differ; first at " << first_bad << " (" << got[first_bad] << " vs "
                  << expected[first_bad] << "). The window was never published before the copy."
                  << std::endl;
        return 1;
    }

    std::cout << "Host write window test PASSED: " << PATTERN_WORDS
              << " words written through the window reached the device without a flush." << std::endl;
    return 0;
}
