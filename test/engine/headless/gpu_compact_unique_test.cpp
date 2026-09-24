#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeHelpers.h"

#include <Physics/gpu_algorithm/CompactUnique.h>
#include <Physics/gpu_algorithm/ParallelScan.h>
#include <Physics/gpu_algorithm/RadixSort.h>

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeResourceBinding.h>
#include <Rhi/Pipeline/ComputeStage.h>
#include <Rhi/Pipeline/ShaderResourceBinding.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm.hpp>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace Engine;
using namespace Engine::Rhi;

// gpu_compact_unique_test �?the broad phase's dedup record, end to end.
//
// The dedup carries a candidate pair as the single packed key `a * shape_count +
// b`: the sort orders the keys, CompactUnique removes the adjacent duplicates,
// and `unpack_pairs.comp` restores the canonical `uvec2(a, b)` pair and publishes
// the pair count.  This fixture exercises each stage:
//
//   - CompactUnique over already-sorted keys, for the shapes the algorithm's
//     contract has to pin down (all-duplicate, partly-duplicate, unique, single
//     element, zero elements, and a size spanning several workgroups);
//   - the whole pack �?sort �?compact �?unpack round trip, which is what the
//     detector records, including the canonical `pair.x < pair.y` order and the
//     published counts.
//
// The round trip is also what makes the sort's *stability* visible here: two
// candidate pairs with the same packed key must collapse to exactly one unique
// key, and a scatter that re-permuted the array would still produce a
// plausible-looking (but wrong) compacted set.

namespace {
    int g_failures = 0;

    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };

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

    vk::CommandBuffer BeginCommandBuffer(RenderSystem &rsys) {
        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        return cb;
    }

    std::vector<uint32_t> LoadPhysicsSpirvBytes(const char *relative_path) {
        std::filesystem::path full = std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / relative_path;
        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        }
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u) {
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        }
        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }

    // The broad phase's packing: `a * shape_count + b`, injective over
    // `a, b < shape_count` and order-preserving.
    constexpr uint32_t kShapeCount = 200u;
    constexpr uint32_t kCapacity = 4096u;

    uint32_t Pack(uint32_t a, uint32_t b) {
        return a * kShapeCount + b;
    }

    // ── CompactUnique over an already-sorted key array ─────────────────────
    struct CompactOutcome {
        std::vector<uint32_t> keys; // the first `count` keys after compaction
        uint32_t count = 0u;
    };

    CompactOutcome RunCompactUnique(RenderSystem &rsys, const std::vector<uint32_t> &sorted_keys, uint32_t count) {
        auto keys_buf = MakeHostBuffer(rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "CU keys");
        auto flags_buf = MakeHostBuffer(rsys, CompactUnique::GetRequiredFlagBytes(kCapacity), "CU flags");
        auto offsets_buf = MakeHostBuffer(rsys, CompactUnique::GetRequiredFlagBytes(kCapacity), "CU offsets");
        auto count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "CU count");
        auto elem_count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "CU elem count");
        auto scan_scratch = MakeHostBuffer(rsys, ParallelScan::GetRequiredBlockSumsBytes(kCapacity), "CU scan scratch");

        std::memset(keys_buf->GetVMAddress(), 0, keys_buf->GetSize());
        std::memcpy(keys_buf->GetVMAddress(), sorted_keys.data(), sorted_keys.size() * sizeof(uint32_t));
        std::memset(flags_buf->GetVMAddress(), 0, flags_buf->GetSize());
        std::memset(offsets_buf->GetVMAddress(), 0, offsets_buf->GetSize());
        std::memset(scan_scratch->GetVMAddress(), 0, scan_scratch->GetSize());
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = 0u;
        *reinterpret_cast<uint32_t *>(elem_count_buf->GetVMAddress()) = count;
        keys_buf->Flush();
        flags_buf->Flush();
        offsets_buf->Flush();
        count_buf->Flush();
        elem_count_buf->Flush();
        scan_scratch->Flush();

        CompactUnique compact{rsys.GetDeviceContext(), kCapacity};
        ParallelScan scan{rsys.GetDeviceContext(), kCapacity};

        auto cb = BeginCommandBuffer(rsys);
        compact.Record(
            cb, *keys_buf, *flags_buf, *offsets_buf, *count_buf, *scan_scratch, scan, *elem_count_buf, kCapacity
        );
        cb.end();
        Submit(rsys, cb);

        keys_buf->Invalidate();
        count_buf->Invalidate();

        CompactOutcome outcome{};
        outcome.count = *reinterpret_cast<const uint32_t *>(count_buf->GetVMAddress());
        const auto *base = reinterpret_cast<const uint32_t *>(keys_buf->GetVMAddress());
        outcome.keys.assign(base, base + outcome.count);
        return outcome;
    }

    // ── The whole dedup: pack �?sort �?compact �?unpack ───────────────────
    struct DedupOutcome {
        std::vector<glm::uvec2> pairs; // the canonical pairs, `pair_count` of them
        uint32_t pair_count = 0u;
        uint32_t unique_count = 0u;
    };

    DedupOutcome RunDedup(
        RenderSystem &rsys, const std::vector<glm::uvec2> &candidates, std::vector<uint32_t> &candidate_keys_out
    ) {
        auto keys_a = MakeHostBuffer(rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "Dedup keys a");
        auto keys_b = MakeHostBuffer(rsys, static_cast<size_t>(kCapacity) * sizeof(uint32_t), "Dedup keys b");
        auto pairs_buf = MakeHostBuffer(rsys, static_cast<size_t>(kCapacity) * sizeof(glm::uvec2), "Dedup pairs");
        auto pair_count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Dedup pair count");
        auto unique_count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Dedup unique count");
        auto flags_buf = MakeHostBuffer(rsys, CompactUnique::GetRequiredFlagBytes(kCapacity), "Dedup flags");
        auto offsets_buf = MakeHostBuffer(rsys, CompactUnique::GetRequiredFlagBytes(kCapacity), "Dedup offsets");
        auto radix_scratch = MakeHostBuffer(rsys, RadixSort::GetRequiredScratchBytes(kCapacity), "Dedup radix scratch");
        auto scan_scratch =
            MakeHostBuffer(rsys, ParallelScan::GetRequiredBlockSumsBytes(kCapacity), "Dedup scan scratch");

        // The pair generators write only `candidate_count` keys; the tail is left
        // as the sort's guards expect it (never read).
        std::vector<uint32_t> candidate_keys(kCapacity, 0u);
        for (size_t i = 0u; i < candidates.size(); ++i) {
            candidate_keys[i] = Pack(candidates[i].x, candidates[i].y);
        }
        candidate_keys_out = candidate_keys;

        std::memset(keys_a->GetVMAddress(), 0, keys_a->GetSize());
        std::memcpy(keys_a->GetVMAddress(), candidate_keys.data(), candidate_keys.size() * sizeof(uint32_t));
        std::memset(keys_b->GetVMAddress(), 0, keys_b->GetSize());
        std::memset(pairs_buf->GetVMAddress(), 0, pairs_buf->GetSize());
        std::memset(flags_buf->GetVMAddress(), 0, flags_buf->GetSize());
        std::memset(offsets_buf->GetVMAddress(), 0, offsets_buf->GetSize());
        std::memset(radix_scratch->GetVMAddress(), 0, radix_scratch->GetSize());
        std::memset(scan_scratch->GetVMAddress(), 0, scan_scratch->GetSize());
        *reinterpret_cast<uint32_t *>(pair_count_buf->GetVMAddress()) = static_cast<uint32_t>(candidates.size());
        *reinterpret_cast<uint32_t *>(unique_count_buf->GetVMAddress()) = 0u;
        keys_a->Flush();
        keys_b->Flush();
        pairs_buf->Flush();
        pair_count_buf->Flush();
        unique_count_buf->Flush();
        flags_buf->Flush();
        offsets_buf->Flush();
        radix_scratch->Flush();
        scan_scratch->Flush();

        RadixSort radix_sort{rsys.GetDeviceContext()};
        CompactUnique compact{rsys.GetDeviceContext(), kCapacity};
        ParallelScan scan{rsys.GetDeviceContext(), kCapacity};

        // The detector's unpack pass, loaded directly: it is a collision-layer
        // shader, not part of any gpu_algorithm module.
        const std::vector<uint32_t> unpack_spirv =
            LoadPhysicsSpirvBytes("collision/SpatialHashBroadDetector/unpack_pairs.comp.spv");
        Rhi::ComputeStage unpack_stage{rsys.GetDeviceContext()};
        unpack_stage.Instantiate(unpack_spirv, "Test UnpackPairs");
        auto &unpack_binding = unpack_stage.AllocateResourceBinding();

        const RadixSortBuffers sort_buffers{
            .keys_a = keys_a.get(),
            .keys_b = keys_b.get(),
            .scratch = radix_scratch.get(),
            .count = pair_count_buf.get(),
        };

        auto cb = BeginCommandBuffer(rsys);
        const RadixSortOutput sorted = radix_sort.Record(cb, sort_buffers, kCapacity, kShapeCount * kShapeCount - 1u);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        compact.Record(
            cb,
            *sorted.keys,
            *flags_buf,
            *offsets_buf,
            *unique_count_buf,
            *scan_scratch,
            scan,
            *pair_count_buf,
            kCapacity
        );
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        auto &srb = unpack_binding.GetShaderResourceBinding();
        srb.BindBuffer("CompactedKeys", *sorted.keys);
        srb.BindBuffer("CollisionPairs", *pairs_buf);
        srb.BindBuffer("UniqueCount", *unique_count_buf);
        srb.BindBuffer("PairCount", *pair_count_buf);
        Rhi::PushConstants(cb, unpack_stage, kShapeCount);
        Rhi::BindComputeStage(cb, unpack_stage);
        Rhi::BindComputeResource(cb, unpack_stage, unpack_binding);
        Rhi::DispatchCompute(cb, (kCapacity + 63u) / 64u, 1, 1);
        cb.end();
        Submit(rsys, cb);

        pairs_buf->Invalidate();
        pair_count_buf->Invalidate();
        unique_count_buf->Invalidate();

        DedupOutcome outcome{};
        outcome.pair_count = *reinterpret_cast<const uint32_t *>(pair_count_buf->GetVMAddress());
        outcome.unique_count = *reinterpret_cast<const uint32_t *>(unique_count_buf->GetVMAddress());
        const auto *base = reinterpret_cast<const glm::uvec2 *>(pairs_buf->GetVMAddress());
        outcome.pairs.assign(base, base + outcome.pair_count);
        return outcome;
    }
} // namespace

int main() try {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Compact Unique Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    // ── All duplicates collapse to a single key ───────────────────────────
    {
        const CompactOutcome outcome = RunCompactUnique(*rsys, {42u, 42u, 42u, 42u}, 4u);
        Check(outcome.count == 1u, "an all-duplicate input has one unique key");
        Check(outcome.keys == std::vector<uint32_t>({42u}), "the surviving key is the repeated one");
    }

    // ── A partly-duplicate input keeps one of each, in order ──────────────
    {
        const CompactOutcome outcome = RunCompactUnique(*rsys, {10u, 10u, 11u, 12u, 12u, 12u}, 6u);
        Check(outcome.count == 3u, "a partly-duplicate input has three unique keys");
        Check(outcome.keys == std::vector<uint32_t>({10u, 11u, 12u}), "the compacted keys keep their order");
    }

    // ── An already-unique input is unchanged ──────────────────────────────
    {
        const CompactOutcome outcome = RunCompactUnique(*rsys, {10u, 11u, 12u}, 3u);
        Check(outcome.count == 3u, "an already-unique input keeps its count");
        Check(outcome.keys == std::vector<uint32_t>({10u, 11u, 12u}), "an already-unique input is unchanged");
    }

    // ── A single element is one unique key ────────────────────────────────
    {
        const CompactOutcome outcome = RunCompactUnique(*rsys, {7u}, 1u);
        Check(outcome.count == 1u, "a single element has one unique key");
        Check(outcome.keys == std::vector<uint32_t>({7u}), "the single element survives compaction");
    }

    // ── A zero element count produces no unique key ───────────────────────
    {
        const CompactOutcome outcome = RunCompactUnique(*rsys, {}, 0u);
        Check(outcome.count == 0u, "a zero element count has no unique key");
        Check(outcome.keys.empty(), "a zero element count compacts to nothing");
    }

    // ── A size spanning several workgroups ────────────────────────────────
    // 300 keys, every third one distinct: the flag, copy, scan and scatter passes
    // all see more than one 64-invocation workgroup (and the scan more than one
    // 512-element block).
    {
        std::vector<uint32_t> sorted_keys;
        for (uint32_t i = 0u; i < 300u; ++i) {
            sorted_keys.push_back(1000u + (i / 3u));
        }
        const CompactOutcome outcome = RunCompactUnique(*rsys, sorted_keys, 300u);
        Check(outcome.count == 100u, "300 keys with runs of three have 100 unique keys");
        bool ordered = true;
        for (size_t i = 0u; i < outcome.keys.size(); ++i) {
            if (outcome.keys[i] != 1000u + static_cast<uint32_t>(i)) ordered = false;
        }
        Check(ordered, "the multi-workgroup compaction keeps every unique key, in order");
    }

    // ── The pack �?sort �?compact �?unpack round trip ─────────────────────
    // Candidate pairs in the generators' order (interleaved, with duplicates
    // spanning workgroups), packed exactly as the broad phase packs them.
    {
        std::mt19937 rng(20260920u);
        std::uniform_int_distribution<uint32_t> shape(0u, kShapeCount - 1u);
        std::vector<glm::uvec2> candidates;
        // A few hand-written pairs first, so the canonical-order scenario is
        // visible, then a randomized candidate stream with duplicates.
        candidates.push_back({1u, 4u});
        candidates.push_back({2u, 3u});
        candidates.push_back({1u, 4u});
        for (uint32_t i = 0u; i < 400u; ++i) {
            uint32_t a = shape(rng);
            uint32_t b = shape(rng);
            if (a == b) continue;
            candidates.push_back({std::min(a, b), std::max(a, b)});
        }
        // Duplicate the whole randomized set once, so every key appears at least
        // twice and the compaction has real work to do.
        const size_t half = candidates.size();
        for (size_t i = 0u; i < half; ++i) {
            candidates.push_back(candidates[i]);
        }

        std::vector<uint32_t> candidate_keys;
        const DedupOutcome outcome = RunDedup(*rsys, candidates, candidate_keys);

        // The unique set: sort the packed keys and collapse adjacent duplicates.
        std::vector<uint32_t> expect_keys(candidate_keys.begin(), candidate_keys.begin() + candidates.size());
        std::sort(expect_keys.begin(), expect_keys.end());
        expect_keys.erase(std::unique(expect_keys.begin(), expect_keys.end()), expect_keys.end());

        Check(
            outcome.unique_count == static_cast<uint32_t>(expect_keys.size()),
            "the compaction's unique count matches an independent host-side dedup"
        );
        Check(outcome.pair_count == outcome.unique_count, "unpack_pairs publishes pair_count from the unique count");
        Check(outcome.pairs.size() == expect_keys.size(), "the pair buffer holds exactly the unique pair count");

        std::vector<glm::uvec2> expect_pairs;
        expect_pairs.reserve(expect_keys.size());
        for (uint32_t key : expect_keys) {
            expect_pairs.push_back({key / kShapeCount, key % kShapeCount});
        }
        Check(outcome.pairs == expect_pairs, "the unpacked pairs equal the packed pairs, in packed-key order");

        for (const glm::uvec2 &pair : outcome.pairs) {
            Check(pair.x < pair.y, "the unpacked pairs keep the canonical pair.x < pair.y order");
            Check(pair.x < kShapeCount && pair.y < kShapeCount, "the unpacked pairs stay inside the shape domain");
        }
    }

    // ── The spec's duplicate-removal scenario ─────────────────────────────
    // (1,4), (2,3) and (1,4) again: two unique pairs, and the count published.
    {
        std::vector<uint32_t> candidate_keys;
        const std::vector<glm::uvec2> candidates{{1u, 4u}, {2u, 3u}, {1u, 4u}};
        const DedupOutcome outcome = RunDedup(*rsys, candidates, candidate_keys);
        Check(outcome.pair_count == 2u, "a duplicated candidate set publishes two pairs");
        Check(outcome.unique_count == 2u, "a duplicated candidate set has two unique keys");
        Check(
            outcome.pairs == std::vector<glm::uvec2>({{1u, 4u}, {2u, 3u}}),
            "the surviving pairs are (1,4) and (2,3), in packed-key order"
        );
    }

    rsys->WaitForIdle();
    if (g_failures == 0) {
        std::cout << "gpu_compact_unique_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_compact_unique_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
} catch (const std::exception &e) {
    std::cerr << "gpu_compact_unique_test ABORTED: " << e.what() << std::endl;
    return 1;
} catch (...) {
    std::cerr << "gpu_compact_unique_test ABORTED: unknown exception" << std::endl;
    return 1;
}
