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

    // Sorts the first `count` pairs of `input` on pair buffers sized for
    // `capacity` pairs, dispatching for `elem_capacity`, and returns what the
    // ping buffer holds afterwards.  The geometry is a property of the call now,
    // not of the instance, so every value the sort needs is a parameter.
    std::vector<uint32_t> RunSort(
        RenderSystem &rsys,
        RadixSort &sorter,
        RadixSortMode mode,
        const std::vector<uint32_t> &input,
        uint32_t capacity,
        uint32_t elem_capacity,
        uint32_t count
    ) {
        const size_t pair_bytes = static_cast<size_t>(capacity) * 2u * sizeof(uint32_t);

        // ping = caller input (doubles as output); pong = temp.
        auto ping = MakeHostBuffer(rsys, pair_bytes, "Radix ping");
        auto pong = MakeHostBuffer(rsys, pair_bytes, "Radix pong");
        auto scratch = MakeHostBuffer(rsys, RadixSort::GetRequiredScratchBytes(), "Radix scratch");
        auto count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Radix count");

        std::memcpy(ping->GetVMAddress(), input.data(), input.size() * sizeof(uint32_t));
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = count;

        ping->Flush();
        pong->Flush();
        scratch->Flush();
        count_buf->Flush();

        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        sorter.Record(cb, *ping, *pong, *scratch, elem_capacity, *count_buf, 4096u, mode);
        cb.end();
        Submit(rsys, cb);

        ping->Invalidate();
        auto *base = reinterpret_cast<const uint32_t *>(ping->GetVMAddress());
        return std::vector<uint32_t>(base, base + input.size());
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
        RadixSort sorter{rsys->GetDeviceContext()};
        Check(!sorter.IsInitialized(), "no shader is loaded before the first Record");
        std::vector<uint32_t> result = RunSort(*rsys, sorter, RadixSortMode::ePrimaryOnly, input, 64u, 64u, 4u);

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
        // capacity larger than count; only the first 3 pairs participate.
        std::vector<uint32_t> full{9u, 99u, 3u, 30u, 7u, 70u, 1u, 11u, 2u, 22u, 5u, 55u, 6u, 66u, 8u, 88u};
        RadixSort sorter{rsys->GetDeviceContext()};
        std::vector<uint32_t> result = RunSort(*rsys, sorter, RadixSortMode::ePrimaryOnly, full, 8u, 8u, 3u);
        // Sorted among the first 3 only: {3,30},{7,70},{9,99}.
        Check(result[0] == 3u && result[1] == 30u, "guard: lowest key first");
        Check(result[2] == 7u && result[3] == 70u, "guard: middle key");
        Check(result[4] == 9u && result[5] == 99u, "guard: highest key first");
    }

    // ── Per-call capacity contract ────────────────────────────────────────
    // The element capacity is a call parameter now: a capacity whose implied byte
    // size exceeds the bound pair buffer must be rejected for that call, and a
    // zero capacity must be a no-op rather than an error.
    {
        constexpr uint32_t kCapacity = 8u; // 64 bytes per pair buffer
        RadixSort sorter{rsys->GetDeviceContext()};
        auto ping = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * 2u * sizeof(uint32_t), "cap ping");
        auto pong = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * 2u * sizeof(uint32_t), "cap pong");
        auto scratch = MakeHostBuffer(*rsys, RadixSort::GetRequiredScratchBytes(), "cap scratch");
        auto count_buf = MakeHostBuffer(*rsys, sizeof(uint32_t), "cap count");
        std::memset(ping->GetVMAddress(), 0, ping->GetSize());
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = kCapacity;
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
            sorter.Record(cb, *ping, *pong, *scratch, kCapacity * 2u, *count_buf, 4096u, RadixSortMode::ePrimaryOnly);
        } catch (const std::runtime_error &) {
            threw = true;
        } catch (...) {
        }
        cb.end();
        Check(threw, "Record rejects a capacity larger than the bound pair buffer");
        Submit(*rsys, cb);
    }

    // ── A zero capacity records nothing and does not throw ────────────────
    {
        RadixSort sorter{rsys->GetDeviceContext()};
        std::vector<uint32_t> input{4u, 44u, 2u, 22u};
        auto ping = MakeHostBuffer(*rsys, static_cast<size_t>(2u) * 2u * sizeof(uint32_t), "zero ping");
        auto pong = MakeHostBuffer(*rsys, static_cast<size_t>(2u) * 2u * sizeof(uint32_t), "zero pong");
        auto scratch = MakeHostBuffer(*rsys, RadixSort::GetRequiredScratchBytes(), "zero scratch");
        auto count_buf = MakeHostBuffer(*rsys, sizeof(uint32_t), "zero count");
        std::memcpy(ping->GetVMAddress(), input.data(), input.size() * sizeof(uint32_t));
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = 2u;
        ping->Flush();
        pong->Flush();
        scratch->Flush();
        count_buf->Flush();

        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        bool threw = false;
        cb.begin(vk::CommandBufferBeginInfo{});
        try {
            sorter.Record(cb, *ping, *pong, *scratch, 0u, *count_buf, 4096u, RadixSortMode::ePrimaryOnly);
        } catch (...) {
            threw = true;
        }
        cb.end();
        Check(!threw, "a zero capacity does not throw");
        Check(!sorter.IsInitialized(), "a zero capacity records no dispatch at all");
        Submit(*rsys, cb);

        ping->Invalidate();
        const auto *base = reinterpret_cast<const uint32_t *>(ping->GetVMAddress());
        Check(
            base[0] == 4u && base[1] == 44u && base[2] == 2u && base[3] == 22u,
            "a zero capacity leaves the caller's pairs untouched"
        );
    }

    // ── One instance sorts several capacities in one frame ────────────────
    // The instance stores no geometry, so consecutive calls with different
    // capacities must each dispatch for their own call's capacity.
    {
        RadixSort sorter{rsys->GetDeviceContext()};

        std::vector<uint32_t> small{5u, 50u, 1u, 10u, 3u, 30u, 2u, 20u};
        std::vector<uint32_t> r1 = RunSort(*rsys, sorter, RadixSortMode::ePrimaryOnly, small, 4u, 4u, 4u);
        bool small_sorted = r1[0] == 1u && r1[2] == 2u && r1[4] == 3u && r1[6] == 5u;
        Check(small_sorted, "first capacity sorts correctly");

        // A larger capacity with a different block count, same instance.
        std::vector<uint32_t> large;
        for (uint32_t i = 0; i < 12u; ++i) {
            large.push_back(11u - i);        // keys descending
            large.push_back(100u + (11u - i)); // payload rides along
        }
        std::vector<uint32_t> r2 = RunSort(*rsys, sorter, RadixSortMode::ePrimaryOnly, large, 12u, 12u, 12u);
        bool large_sorted = true;
        for (uint32_t i = 0; i < 12u; ++i) {
            if (r2[i * 2u] != i || r2[i * 2u + 1u] != 100u + i) large_sorted = false;
        }
        Check(large_sorted, "second capacity sorts correctly through the same instance");
    }

    rsys->WaitForIdle();
    if (g_failures == 0) {
        std::cout << "gpu_radix_primary_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_radix_primary_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
