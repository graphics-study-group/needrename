#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeHelpers.h"

#include <Physics/gpu_algorithm/RadixSort.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace Engine;
using namespace Engine::Rhi;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    void Submit(RenderSystem &rsys, vk::CommandBuffer cb) {
        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto s = vk::SubmitInfo{{}, {}, {cb}, {}};
        queues.graphicsQueue.submit(s);
        queues.graphicsQueue.waitIdle();
    }

    std::unique_ptr<ComputeBuffer> MakeHostBuffer(RenderSystem &rsys, size_t bytes, const char *name) {
        return ComputeBuffer::CreateUnique(rsys.GetAllocatorState(), bytes, true, false, false, false, name);
    }

    void RunSort(RenderSystem &rsys, RadixSort &sorter, RadixSortMode mode, std::vector<uint32_t> &input,
                 std::vector<uint32_t> &ping_out) {
        const uint32_t cap = static_cast<uint32_t>(input.size() / 2u);
        const uint32_t max_elem = sorter.GetMaxElemCount();

        // ping = caller input (doubles as output); pong = temp.
        auto ping = MakeHostBuffer(rsys, static_cast<size_t>(max_elem) * 2u * sizeof(uint32_t), "Radix ping");
        auto pong = MakeHostBuffer(rsys, static_cast<size_t>(max_elem) * 2u * sizeof(uint32_t), "Radix pong");
        auto scratch = MakeHostBuffer(rsys, RadixSort::GetRequiredScratchBytes(), "Radix scratch");
        auto count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Radix count");

        std::memcpy(ping->GetVMAddress(), input.data(), input.size() * sizeof(uint32_t));
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = cap;

        ping->Flush();
        pong->Flush();
        scratch->Flush();
        count_buf->Flush();

        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        sorter.Record(cb, *ping, *pong, *scratch, max_elem, *count_buf, 4096u, mode);
        cb.end();
        Submit(rsys, cb);

        ping->Invalidate();
        auto *base = reinterpret_cast<const uint32_t *>(ping->GetVMAddress());
        ping_out.assign(base, base + input.size());
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Radix Primary Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    // ── Primary-only mode: [(5,10),(2,20),(2,30),(1,40)] ─────────────────
    {
        std::vector<uint32_t> input{5u, 10u, 2u, 20u, 2u, 30u, 1u, 40u};
        const uint32_t max_elem = 64u;
        RadixSort sorter{rsys->GetDeviceContext(), max_elem};
        std::vector<uint32_t> result;
        RunSort(*rsys, sorter, RadixSortMode::ePrimaryOnly, input, result);

        // Keys must be ascending. Payloads are a permutation of {10,20,30,40}.
        std::vector<uint32_t> keys;
        std::vector<uint32_t> payloads;
        for (size_t i = 0; i < result.size() / 2u; ++i) {
            keys.push_back(result[i * 2u]);
            payloads.push_back(result[i * 2u + 1u]);
        }
        bool ascend = true;
        for (size_t i = 1; i < keys.size(); ++i) {
            if (keys[i - 1u] > keys[i]) ascend = false;
        }
        Check(ascend, "primary-only keys ascend");
        std::sort(payloads.begin(), payloads.end());
        bool each_once =
            payloads.size() == 4u && payloads[0] == 10u && payloads[1] == 20u && payloads[2] == 30u && payloads[3] == 40u;
        Check(each_once, "every payload appears exactly once");
    }

    // ── Count-guard semantics: only the first pair_count pairs sort ────────
    {
        // capacity larger than count; only 3 pairs participate.
        std::vector<uint32_t> input{9u, 99u, 3u, 30u, 7u, 70u}; // only first 3 used as count=3
        const uint32_t max_elem = 8u;
        RadixSort sorter{rsys->GetDeviceContext(), max_elem};
        // We must test with a separate path since RunSort uses full capacity as
        // the count. Use the Record directly here.
        auto ping = MakeHostBuffer(*rsys, static_cast<size_t>(max_elem) * 2u * sizeof(uint32_t), "ping");
        auto pong = MakeHostBuffer(*rsys, static_cast<size_t>(max_elem) * 2u * sizeof(uint32_t), "pong");
        auto scratch = MakeHostBuffer(*rsys, RadixSort::GetRequiredScratchBytes(), "scratch");
        auto count_buf = MakeHostBuffer(*rsys, sizeof(uint32_t), "count");

        // full input: put 4 extra junk pairs that must not participate.
        std::vector<uint32_t> full{9u, 99u, 3u, 30u, 7u, 70u, 1u, 11u, 2u, 22u, 5u, 55u, 6u, 66u, 8u, 88u};
        std::memcpy(ping->GetVMAddress(), full.data(), full.size() * sizeof(uint32_t));
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = 3u;
        ping->Flush();
        pong->Flush();
        scratch->Flush();
        count_buf->Flush();

        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        sorter.Record(cb, *ping, *pong, *scratch, max_elem, *count_buf, 4096u, RadixSortMode::ePrimaryOnly);
        cb.end();
        Submit(*rsys, cb);
        ping->Invalidate();
        const auto *base = reinterpret_cast<const uint32_t *>(ping->GetVMAddress());
        // Sorted among the first 3 only: {3,30},{7,70},{9,99}.
        Check(base[0] == 3u && base[1] == 30u, "guard: lowest key first");
        Check(base[2] == 7u && base[3] == 70u, "guard: middle key");
        Check(base[4] == 9u && base[5] == 99u, "guard: highest key first");
    }

    // ── Capacity contract: the accessors the solver uses to detect staleness ──
    // XPBDGpuSolver compares GetMaxElemCount() with the capacity it now needs and
    // rebuilds the sorter when they differ, so the accessor must report the
    // construction-time value and Record must refuse an over-capacity call.
    {
        constexpr uint32_t kMaxElem = 8u;
        RadixSort sorter{rsys->GetDeviceContext(), kMaxElem};
        Check(sorter.GetMaxElemCount() == kMaxElem, "GetMaxElemCount reports construction max_elem_count");

        auto ping = MakeHostBuffer(*rsys, static_cast<size_t>(kMaxElem) * 2u * sizeof(uint32_t), "cap ping");
        auto pong = MakeHostBuffer(*rsys, static_cast<size_t>(kMaxElem) * 2u * sizeof(uint32_t), "cap pong");
        auto scratch = MakeHostBuffer(*rsys, RadixSort::GetRequiredScratchBytes(), "cap scratch");
        auto count_buf = MakeHostBuffer(*rsys, sizeof(uint32_t), "cap count");
        std::memset(ping->GetVMAddress(), 0, ping->GetSize());
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = kMaxElem;
        ping->Flush();
        pong->Flush();
        scratch->Flush();
        count_buf->Flush();

        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        bool threw = false;
        try {
            sorter.Record(cb, *ping, *pong, *scratch, kMaxElem * 2u, *count_buf, 4096u, RadixSortMode::ePrimaryOnly);
        } catch (const std::exception &) {
            threw = true;
        }
        cb.end();
        Check(threw, "Record rejects elem_capacity above max_elem_count");
        Submit(*rsys, cb);
    }

    rsys->WaitForIdle();
    if (g_failures == 0) {
        std::cout << "gpu_radix_primary_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_radix_primary_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
