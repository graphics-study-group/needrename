#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"

#include <Physics/gpu_algorithm/RadixSort.h>

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

using namespace Engine;
using namespace Engine::Rhi;

// gpu_radix_multiblock_test — the radix sort must return a key-ascending array
// for the *solver's* element counts, not only for a handful of pairs.
//
// The XPBD entry list is sorted with RadixSortMode::ePrimaryOnly over keys that
// are body/slot indices, i.e. small integers whose upper three bytes are always
// zero, and an LSD pass over a byte that is constant across the whole array
// moves every element into a single bin.  A scatter that claims its output slot
// with an atomic (rather than ranking elements stably within their bin) loses
// the ordering established by the earlier passes in exactly that case, so the
// result stops being sorted as soon as the element count spans more than one
// warp.  These cases use the solver's shape: capacity above the entry count, a
// constant high byte, and hundreds of entries.

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

    // Sorts the first `count` pairs of `input` (capacity-sized buffers) and
    // returns the full ping buffer contents afterwards.
    std::vector<uint32_t> RunSort(
        RenderSystem &rsys,
        RadixSort &sorter,
        const std::vector<uint32_t> &input,
        uint32_t capacity,
        uint32_t count
    ) {
        const size_t pair_bytes = static_cast<size_t>(capacity) * 2u * sizeof(uint32_t);
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
        sorter.Record(cb, *ping, *pong, *scratch, capacity, *count_buf, 1u << 20u, RadixSortMode::ePrimaryOnly);
        cb.end();
        Submit(rsys, cb);

        ping->Invalidate();
        auto *base = reinterpret_cast<const uint32_t *>(ping->GetVMAddress());
        return std::vector<uint32_t>(base, base + input.size());
    }

    // Counts descending key steps in the first `count` pairs and reports the
    // first few, naming the indices so a failure points at the shape that broke.
    void CheckAscending(const std::vector<uint32_t> &pairs, uint32_t count, const char *what) {
        uint32_t violations = 0u;
        for (uint32_t i = 1u; i < count; ++i) {
            const uint32_t prev = pairs[2u * (i - 1u)];
            const uint32_t cur = pairs[2u * i];
            if (prev > cur) {
                if (violations < 4u) {
                    std::cerr << "  " << what << ": descending step at e=" << i << " prev_key=" << prev
                              << " cur_key=" << cur << std::endl;
                }
                violations++;
            }
        }
        if (violations != 0u) {
            std::cerr << "FAIL: " << what << " (" << violations << " descending steps)" << std::endl;
            g_failures++;
        }
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Radix Multi-Block Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    // ── One key byte, one warp: the case the existing tests cover ─────────
    {
        std::vector<uint32_t> input{5u, 10u, 2u, 20u, 2u, 30u, 1u, 40u};
        RadixSort sorter{rsys->GetDeviceContext()};
        const std::vector<uint32_t> result = RunSort(*rsys, sorter, input, 64u, 4u);
        CheckAscending(result, 4u, "4 pairs, keys below 256");
    }

    // ── The solver's shape: 22 keys, hundreds of entries, pad tail ────────
    // `count` real entries carry body indices in [0,22) in the entry pass's own
    // order (slot == index, exactly what entries/*.comp writes); everything from
    // `count` to the capacity carries the INVALID sentinel run the entry passes
    // leave behind, which is ascending by construction.
    {
        constexpr uint32_t kCapacity = 2310u;
        constexpr uint32_t kCount = 400u;
        constexpr uint32_t kInvalidKey = 0xFFFFFu;

        for (uint32_t trial = 0u; trial < 3u; ++trial) {
            std::mt19937 rng(20260919u + trial);
            std::uniform_int_distribution<uint32_t> body(0u, 21u);
            std::vector<uint32_t> input(static_cast<size_t>(kCapacity) * 2u);
            for (uint32_t i = 0u; i < kCapacity; ++i) {
                input[2u * i] = (i < kCount) ? body(rng) : kInvalidKey;
                input[2u * i + 1u] = i;
            }

            RadixSort sorter{rsys->GetDeviceContext()};
            const std::vector<uint32_t> result = RunSort(*rsys, sorter, input, kCapacity, kCount);
            CheckAscending(result, kCount, "400 entries, 22 distinct small keys");

            // Every payload must survive the sort exactly once.
            std::vector<bool> seen(kCapacity, false);
            bool bijection = true;
            for (uint32_t i = 0u; i < kCount; ++i) {
                const uint32_t slot = result[2u * i + 1u];
                if (slot >= kCount || seen[slot]) {
                    bijection = false;
                } else {
                    seen[slot] = true;
                }
            }
            Check(bijection, "payload slots stay a bijection of the sorted prefix");
        }
    }

    rsys->WaitForIdle();
    if (g_failures == 0) {
        std::cout << "gpu_radix_multiblock_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_radix_multiblock_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
