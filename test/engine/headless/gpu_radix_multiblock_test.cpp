#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"

#include <Physics/gpu_algorithm/RadixSort.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

using namespace Engine;
using namespace Engine::Rhi;

// gpu_radix_multiblock_test — the radix sort's contract, exercised at the
// solver's element counts rather than over a handful of elements.
//
// LSD radix sort is only correct if **every** pass is stable, and the earlier
// implementation was not: it claimed output positions with an atomic, so a pass
// over a byte that is constant across the whole array (exactly what the solver's
// body-index keys produce in their upper bytes) re-permuted the array in
// atomic-grant order and left it only approximately ascending.  These cases
// therefore assert stability directly — every element carries its input index as
// payload, so equal keys must come out with strictly increasing payloads — and
// they use capacities that span several histogram/scatter blocks.
//
// They also cover the contract's edges: a keys-only sort, a one-pass and a
// multi-pass key bound (the pass count is derived, so the result can land in
// either ping-pong array and `Record` returns which), a count below the capacity
// with whole blocks beyond it, and a zero capacity.

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
        rsys.WaitForIdle();
    }

    std::unique_ptr<ComputeBuffer> MakeHostBuffer(RenderSystem &rsys, size_t bytes, const char *name) {
        return ComputeBuffer::CreateUnique(rsys.GetAllocatorState(), bytes, true, false, false, false, name);
    }

    /// What one `Record` call produced, plus the buffers it reported.
    struct SortOutcome {
        std::vector<uint32_t> keys;     // the returned key array, `count` entries
        std::vector<uint32_t> payloads; // the returned payload array, when bound
        Rhi::ComputeBuffer *result_keys = nullptr;
        Rhi::ComputeBuffer *keys_a = nullptr;
        Rhi::ComputeBuffer *keys_b = nullptr;
        bool initialized_after = false;
    };

    /// Sorts `count` elements of `input_keys` / `input_payloads` on capacity-sized
    /// buffers and returns what the sort's own result buffers hold.  Both input
    /// vectors are capacity-sized; the tail beyond `count` is garbage that must
    /// not influence the sorted prefix.
    SortOutcome RunSort(
        RenderSystem &rsys,
        RadixSort &sorter,
        const std::vector<uint32_t> &input_keys,
        const std::vector<uint32_t> &input_payloads,
        uint32_t capacity,
        uint32_t count,
        uint32_t max_key_value
    ) {
        const bool has_payload = !input_payloads.empty();
        const size_t bytes = static_cast<size_t>(capacity) * sizeof(uint32_t);

        auto keys_a = MakeHostBuffer(rsys, bytes, "Radix keys a");
        auto keys_b = MakeHostBuffer(rsys, bytes, "Radix keys b");
        auto scratch = MakeHostBuffer(rsys, RadixSort::GetRequiredScratchBytes(capacity), "Radix scratch");
        auto count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Radix count");
        std::unique_ptr<ComputeBuffer> payload_a{};
        std::unique_ptr<ComputeBuffer> payload_b{};
        if (has_payload) {
            payload_a = MakeHostBuffer(rsys, bytes, "Radix payload a");
            payload_b = MakeHostBuffer(rsys, bytes, "Radix payload b");
        }

        auto fill = [capacity](ComputeBuffer &buf, const std::vector<uint32_t> &src) {
            std::memset(buf.GetVMAddress(), 0, buf.GetSize());
            std::memcpy(buf.GetVMAddress(), src.data(), static_cast<size_t>(capacity) * sizeof(uint32_t));
        };
        fill(*keys_a, input_keys);
        std::memset(keys_b->GetVMAddress(), 0, keys_b->GetSize());
        if (has_payload) {
            fill(*payload_a, input_payloads);
            std::memset(payload_b->GetVMAddress(), 0, payload_b->GetSize());
        }
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = count;
        keys_a->Flush();
        keys_b->Flush();
        scratch->Flush();
        count_buf->Flush();
        if (has_payload) {
            payload_a->Flush();
            payload_b->Flush();
        }

        const RadixSortBuffers buffers{
            .keys_a = keys_a.get(),
            .keys_b = keys_b.get(),
            .payload_a = has_payload ? payload_a.get() : nullptr,
            .payload_b = has_payload ? payload_b.get() : nullptr,
            .scratch = scratch.get(),
            .count = count_buf.get(),
        };

        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        const RadixSortOutput result = sorter.Record(cb, buffers, capacity, max_key_value);
        cb.end();
        Submit(rsys, cb);

        SortOutcome outcome{};
        outcome.result_keys = result.keys;
        outcome.keys_a = keys_a.get();
        outcome.keys_b = keys_b.get();
        outcome.initialized_after = sorter.IsInitialized();

        result.keys->Invalidate();
        const auto *keys = reinterpret_cast<const uint32_t *>(result.keys->GetVMAddress());
        outcome.keys.assign(keys, keys + count);
        if (result.payload != nullptr) {
            result.payload->Invalidate();
            const auto *payloads = reinterpret_cast<const uint32_t *>(result.payload->GetVMAddress());
            outcome.payloads.assign(payloads, payloads + count);
        }
        return outcome;
    }

    /// Every key must be at or above its predecessor.
    void CheckAscending(const std::vector<uint32_t> &keys, const char *what) {
        for (size_t i = 1u; i < keys.size(); ++i) {
            if (keys[i - 1u] > keys[i]) {
                std::cerr << "FAIL: " << what << ": descending step at " << i << " (" << keys[i - 1u] << " -> "
                          << keys[i] << ")" << std::endl;
                g_failures++;
                return;
            }
        }
    }

    /// Stability, checked through the payload: with a payload equal to the input
    /// index, equal keys must keep their input order, so the payloads inside every
    /// run of equal keys must be strictly increasing.  An unstable scatter (one
    /// that lets an atomic decide the order) breaks this immediately.
    void CheckStable(const std::vector<uint32_t> &keys, const std::vector<uint32_t> &payloads, const char *what) {
        for (size_t i = 1u; i < keys.size(); ++i) {
            if (keys[i - 1u] == keys[i] && payloads[i - 1u] >= payloads[i]) {
                std::cerr << "FAIL: " << what << ": equal keys " << keys[i] << " at " << (i - 1u) << " and " << i
                          << " came out with payloads " << payloads[i - 1u] << " then " << payloads[i]
                          << " (not stable)" << std::endl;
                g_failures++;
                return;
            }
        }
    }

    /// The payloads must be a permutation of [0, count).
    void CheckPayloadBijection(const std::vector<uint32_t> &payloads, uint32_t count, const char *what) {
        std::vector<bool> seen(count, false);
        for (uint32_t p : payloads) {
            if (p >= count || seen[p]) {
                std::cerr << "FAIL: " << what << ": payload " << p << " is not a bijection of [0," << count << ")"
                          << std::endl;
                g_failures++;
                return;
            }
            seen[p] = true;
        }
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Radix Multi-Block Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    // ── The spec's stability scenario, verbatim ───────────────────────────
    // keys [3,1,3,1,3,1] with payloads equal to their input indices, over a
    // capacity that spans more than one 256-element block.
    {
        constexpr uint32_t kCapacity = 512u;
        const std::vector<uint32_t> keys{3u, 1u, 3u, 1u, 3u, 1u};
        std::vector<uint32_t> input_keys(kCapacity, 0u);
        std::vector<uint32_t> input_payloads(kCapacity, 0u);
        for (uint32_t i = 0u; i < 6u; ++i) {
            input_keys[i] = keys[i];
            input_payloads[i] = i;
        }

        RadixSort sorter{rsys->GetDeviceContext()};
        Check(!sorter.IsInitialized(), "no shader is loaded before the first Record");
        const SortOutcome outcome =
            RunSort(*rsys, sorter, input_keys, input_payloads, kCapacity, 6u, /*max_key_value=*/7u);
        Check(sorter.IsInitialized(), "the first Record loads the shaders");

        const std::vector<uint32_t> want_keys{1u, 1u, 1u, 3u, 3u, 3u};
        Check(outcome.keys == want_keys, "equal keys sort ascending");
        Check(
            outcome.payloads == std::vector<uint32_t>({1u, 3u, 5u, 0u, 2u, 4u}),
            "equal keys keep their input order (payloads 1,3,5 then 0,2,4)"
        );
    }

    // ── Stability and bijection at a multi-block, solver-scale capacity ───
    // `kCapacity` spans several blocks and the keys come from a small domain, so
    // every run is long and every block boundary falls inside a run.
    {
        constexpr uint32_t kCapacity = 2310u; // 10 blocks of 256
        for (uint32_t trial = 0u; trial < 3u; ++trial) {
            std::mt19937 rng(20260919u + trial);
            std::uniform_int_distribution<uint32_t> key(0u, 21u);
            std::vector<uint32_t> input_keys(kCapacity);
            std::vector<uint32_t> input_payloads(kCapacity);
            for (uint32_t i = 0u; i < kCapacity; ++i) {
                input_keys[i] = key(rng);
                input_payloads[i] = i;
            }

            RadixSort sorter{rsys->GetDeviceContext()};
            const SortOutcome outcome =
                RunSort(*rsys, sorter, input_keys, input_payloads, kCapacity, kCapacity, /*max_key_value=*/22u);
            CheckAscending(outcome.keys, "2310 elements, 22 distinct small keys");
            CheckStable(outcome.keys, outcome.payloads, "2310 elements, 22 distinct small keys");
            CheckPayloadBijection(outcome.payloads, kCapacity, "2310 elements, 22 distinct small keys");
        }
    }

    // ── A keys-only sort permutes nothing else ────────────────────────────
    {
        constexpr uint32_t kCapacity = 600u;
        std::vector<uint32_t> input_keys(kCapacity);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            input_keys[i] = (kCapacity - i) % 251u;
        }
        RadixSort sorter{rsys->GetDeviceContext()};
        const SortOutcome outcome =
            RunSort(*rsys, sorter, input_keys, {}, kCapacity, kCapacity, /*max_key_value=*/255u);
        CheckAscending(outcome.keys, "keys-only sort");
        Check(outcome.payloads.empty(), "a keys-only sort returns no payload buffer");
        Check(outcome.result_keys != nullptr, "a keys-only sort still returns a key buffer");
    }

    // ── A count below the capacity: whole blocks lie beyond it ────────────
    // The tail carries keys that are *lower* than the real ones, so an
    // implementation that ignored the count would move them into the prefix.
    {
        constexpr uint32_t kCapacity = 1000u; // 4 blocks; only block 0 is fully real
        constexpr uint32_t kCount = 300u;
        std::vector<uint32_t> input_keys(kCapacity);
        std::vector<uint32_t> input_payloads(kCapacity);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            if (i < kCount) {
                input_keys[i] = 40u - (i % 41u); // descending-ish, keys in [0,40]
                input_payloads[i] = i;
            } else {
                input_keys[i] = 0u; // a lower key than every real one
                input_payloads[i] = i;
            }
        }

        RadixSort sorter{rsys->GetDeviceContext()};
        const SortOutcome outcome =
            RunSort(*rsys, sorter, input_keys, input_payloads, kCapacity, kCount, /*max_key_value=*/64u);

        std::vector<uint32_t> prefix(input_keys.begin(), input_keys.begin() + kCount);
        std::sort(prefix.begin(), prefix.end());
        Check(outcome.keys == prefix, "a count below the capacity sorts the real prefix and only it");
        CheckStable(outcome.keys, outcome.payloads, "a count below the capacity");
        CheckPayloadBijection(outcome.payloads, kCount, "a count below the capacity");
        // Every real element's payload must still be below the count: a sort that
        // had read the tail would have carried tail payloads into the prefix.
        for (uint32_t p : outcome.payloads) {
            Check(p < kCount, "no tail element reached the sorted prefix");
        }
    }

    // ── The derived pass count decides which array holds the result ───────
    // 200 needs one byte (1 pass, odd -> the pong array); 1000 needs two bytes
    // (2 passes, even -> the ping array).  `Record` must report the buffer the
    // last pass actually wrote in both cases.
    {
        constexpr uint32_t kCapacity = 512u;
        std::vector<uint32_t> input_keys(kCapacity);
        std::vector<uint32_t> input_payloads(kCapacity);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            input_keys[i] = (i * 13u) % 200u;
            input_payloads[i] = i;
        }

        RadixSort sorter{rsys->GetDeviceContext()};
        const SortOutcome one_pass =
            RunSort(*rsys, sorter, input_keys, input_payloads, kCapacity, kCapacity, /*max_key_value=*/200u);
        CheckAscending(one_pass.keys, "one-byte key bound");
        CheckStable(one_pass.keys, one_pass.payloads, "one-byte key bound");
        Check(one_pass.result_keys == one_pass.keys_b, "a one-pass sort returns the pong array it wrote");

        // Two bytes: the same instance, another capacity bound and another parity.
        std::vector<uint32_t> wide_keys(kCapacity);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            wide_keys[i] = 1000u - (i % 1000u);
        }
        const SortOutcome two_pass =
            RunSort(*rsys, sorter, wide_keys, input_payloads, kCapacity, kCapacity, /*max_key_value=*/1000u);
        CheckAscending(two_pass.keys, "two-byte key bound");
        CheckStable(two_pass.keys, two_pass.payloads, "two-byte key bound");
        Check(two_pass.result_keys == two_pass.keys_a, "a two-pass sort returns the ping array it wrote");

        // A key exactly equal to the bound stays valid: 256 needs two bytes.
        std::vector<uint32_t> bound_keys(kCapacity, 0u);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            bound_keys[i] = (i < 128u) ? 256u : 7u;
        }
        const SortOutcome on_bound =
            RunSort(*rsys, sorter, bound_keys, input_payloads, kCapacity, kCapacity, /*max_key_value=*/256u);
        CheckAscending(on_bound.keys, "a key equal to the bound is still sorted");
        Check(on_bound.keys.front() == 7u && on_bound.keys.back() == 256u, "a key equal to the bound sorts last");
    }

    // ── A zero capacity records nothing and leaves the input alone ────────
    {
        constexpr uint32_t kCapacity = 4u;
        RadixSort sorter{rsys->GetDeviceContext()};
        const std::vector<uint32_t> input_keys{4u, 2u, 3u, 1u};
        auto keys_a = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "zero keys a");
        auto keys_b = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "zero keys b");
        auto scratch = MakeHostBuffer(*rsys, RadixSort::GetRequiredScratchBytes(kCapacity), "zero scratch");
        auto count_buf = MakeHostBuffer(*rsys, sizeof(uint32_t), "zero count");
        std::memcpy(keys_a->GetVMAddress(), input_keys.data(), input_keys.size() * sizeof(uint32_t));
        std::memset(keys_b->GetVMAddress(), 0, keys_b->GetSize());
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = 0u;
        keys_a->Flush();
        keys_b->Flush();
        scratch->Flush();
        count_buf->Flush();

        const RadixSortBuffers buffers{
            .keys_a = keys_a.get(),
            .keys_b = keys_b.get(),
            .scratch = scratch.get(),
            .count = count_buf.get(),
        };

        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        bool threw = false;
        cb.begin(vk::CommandBufferBeginInfo{});
        RadixSortOutput result{};
        try {
            result = sorter.Record(cb, buffers, 0u, 4096u);
        } catch (...) {
            threw = true;
        }
        cb.end();
        Check(!threw, "a zero capacity does not throw");
        Check(!sorter.IsInitialized(), "a zero capacity records no dispatch at all");
        Check(result.keys == keys_a.get(), "a zero capacity returns the caller's input key array");
        Submit(*rsys, cb);

        keys_a->Invalidate();
        const auto *base = reinterpret_cast<const uint32_t *>(keys_a->GetVMAddress());
        Check(
            base[0] == 4u && base[1] == 2u && base[2] == 3u && base[3] == 1u,
            "a zero capacity leaves the caller's keys untouched"
        );
    }

    // ── Per-call validation ───────────────────────────────────────────────
    {
        constexpr uint32_t kCapacity = 8u;
        RadixSort sorter{rsys->GetDeviceContext()};
        auto keys_a = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "cap keys a");
        auto keys_b = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "cap keys b");
        auto scratch = MakeHostBuffer(*rsys, RadixSort::GetRequiredScratchBytes(kCapacity), "cap scratch");
        auto count_buf = MakeHostBuffer(*rsys, sizeof(uint32_t), "cap count");
        std::memset(keys_a->GetVMAddress(), 0, keys_a->GetSize());
        std::memset(keys_b->GetVMAddress(), 0, keys_b->GetSize());
        std::memset(scratch->GetVMAddress(), 0, scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = kCapacity;
        keys_a->Flush();
        keys_b->Flush();
        scratch->Flush();
        count_buf->Flush();

        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});

        bool zero_bound_threw = false;
        try {
            const RadixSortBuffers buffers{
                .keys_a = keys_a.get(), .keys_b = keys_b.get(), .scratch = scratch.get(), .count = count_buf.get()
            };
            (void)sorter.Record(cb, buffers, kCapacity, 0u);
        } catch (const std::invalid_argument &) {
            zero_bound_threw = true;
        } catch (...) {
        }
        Check(zero_bound_threw, "Record rejects a zero key bound");

        bool oversize_threw = false;
        try {
            const RadixSortBuffers buffers{
                .keys_a = keys_a.get(), .keys_b = keys_b.get(), .scratch = scratch.get(), .count = count_buf.get()
            };
            (void)sorter.Record(cb, buffers, kCapacity * 2u, 4096u);
        } catch (const std::runtime_error &) {
            oversize_threw = true;
        } catch (...) {
        }
        Check(oversize_threw, "Record rejects a capacity larger than the bound key arrays");

        bool half_payload_threw = false;
        try {
            auto payload = MakeHostBuffer(*rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "cap payload");
            const RadixSortBuffers buffers{
                .keys_a = keys_a.get(),
                .keys_b = keys_b.get(),
                .payload_a = payload.get(),
                .payload_b = nullptr,
                .scratch = scratch.get(),
                .count = count_buf.get(),
            };
            (void)sorter.Record(cb, buffers, kCapacity, 4096u);
        } catch (const std::invalid_argument &) {
            half_payload_threw = true;
        } catch (...) {
        }
        Check(half_payload_threw, "Record rejects a payload array supplied for only one ping-pong buffer");

        cb.end();
        Submit(*rsys, cb);
    }

    rsys->WaitForIdle();
    if (g_failures == 0) {
        std::cout << "gpu_radix_multiblock_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_radix_multiblock_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
