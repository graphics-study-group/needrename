#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeHelpers.h"

#include <Physics/gpu_algorithm/SumByKey.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
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

    void CloseF(float a, float b, float eps, const char *what) {
        if (!(std::fabs(a - b) <= eps)) {
            std::cerr << "FAIL: " << what << " expected " << b << " got " << a << std::endl;
            g_failures++;
        }
    }

    // Raw command buffer submission helper (mirrors headless_compute_test).
    void Submit(RenderSystem &rsys, vk::CommandBuffer cb) {
        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto s = vk::SubmitInfo{{}, {}, {cb}, {}};
        queues.graphicsQueue.submit(s);
        queues.graphicsQueue.waitIdle();
    }

    // Host-visible compute buffer of a given byte size.
    std::unique_ptr<ComputeBuffer> MakeHostBuffer(RenderSystem &rsys, size_t bytes, const char *name) {
        return ComputeBuffer::CreateUnique(rsys.GetAllocatorState(), bytes, true, false, false, false, name);
    }

    struct RunCtx {
        RenderSystem &rsys;
        std::unique_ptr<ComputeBuffer> pairs; // sorted (key, slot) entries
        std::unique_ptr<ComputeBuffer> values;
        std::unique_ptr<ComputeBuffer> records;
        std::unique_ptr<ComputeBuffer> out;

        uint32_t num_channels = 0u;
        uint32_t max_entries = 0u;
        uint32_t max_key_value = 0u;

        // Entry i = (key, slot).  The slot is the index this entry's values are
        // gathered from, so it is unrelated to i.
        void SetPair(uint32_t i, uint32_t key, uint32_t slot) const {
            auto *p = reinterpret_cast<uint32_t *>(pairs->GetVMAddress());
            p[2u * i] = key;
            p[2u * i + 1u] = slot;
        }

        void SetValue(uint32_t channel, uint32_t slot, float value) const {
            auto *v = reinterpret_cast<float *>(values->GetVMAddress());
            v[static_cast<size_t>(channel) * max_entries + slot] = value;
        }
    };

    // Build a RunCtx sized for the given geometry and zero the output + records.
    RunCtx MakeCtx(RenderSystem &rsys, uint32_t max_entries, uint32_t max_key_value, uint32_t num_channels) {
        RunCtx ctx{rsys, {}, {}, {}, {}, num_channels, max_entries, max_key_value};
        ctx.pairs = MakeHostBuffer(
            rsys, static_cast<size_t>(max_entries) * 2u * sizeof(uint32_t), "SumByKey pairs"
        );
        ctx.values = MakeHostBuffer(
            rsys, static_cast<size_t>(num_channels) * max_entries * sizeof(uint32_t), "SumByKey values"
        );
        size_t rec_bytes = SumByKey::GetRequiredRecordsBytes(max_entries, num_channels);
        if (rec_bytes == 0u) rec_bytes = 1u; // ensure a non-empty allocation
        ctx.records = MakeHostBuffer(rsys, rec_bytes, "SumByKey records");
        ctx.out = MakeHostBuffer(
            rsys, static_cast<size_t>(num_channels) * max_key_value * sizeof(uint32_t), "SumByKey out"
        );

        std::memset(ctx.pairs->GetVMAddress(), 0, ctx.pairs->GetSize());
        std::memset(ctx.values->GetVMAddress(), 0, ctx.values->GetSize());
        std::memset(ctx.out->GetVMAddress(), 0, ctx.out->GetSize());
        std::memset(ctx.records->GetVMAddress(), 0, ctx.records->GetSize());
        return ctx;
    }

    void FlushAll(RunCtx &ctx) {
        ctx.pairs->Flush();
        ctx.values->Flush();
        ctx.records->Flush();
        ctx.out->Flush();
    }

    void RunSumByKey(RenderSystem &rsys, RunCtx &ctx) {
        FlushAll(ctx);
        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];

        SumByKey reducer{rsys.GetDeviceContext(), ctx.max_entries, ctx.max_key_value, ctx.num_channels};

        cb.begin(vk::CommandBufferBeginInfo{});
        reducer.Record(cb, *ctx.pairs, *ctx.values, *ctx.records, *ctx.out);
        cb.end();
        Submit(rsys, cb);

        ctx.out->Invalidate();
        ctx.records->Invalidate();
    }

    const uint32_t *OutRow(const RunCtx &ctx, uint32_t channel) {
        auto *base = reinterpret_cast<const uint32_t *>(ctx.out->GetVMAddress());
        return base + static_cast<size_t>(channel) * ctx.max_key_value;
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "SumByKey Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    // ── Scenario A: single-level, many keys in one block (k == 1) ─────────
    {
        constexpr uint32_t kEntries = 100; // <= 256 -> single level
        constexpr uint32_t kMaxKey = 200;
        constexpr uint32_t kChannels = 1;
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);
        // keys 0..99 each appearing once, identity payload, value = 2.0
        for (uint32_t i = 0; i < kEntries; ++i) {
            ctx.SetPair(i, i, i);
            ctx.SetValue(0u, i, 2.0f);
        }
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kEntries; ++b) {
            CloseF(out[b], 2.0f, 1e-4f, "many-keys-in-one-block per-key sum");
        }
        // keys absent (100..199) remain untouched (zero).
        for (uint32_t b = kEntries; b < kMaxKey; ++b) {
            if (out[b] != 0.0f) {
                std::cerr << "FAIL: absent key slot " << b << " should be untouched" << std::endl;
                g_failures++;
            }
        }
    }

    // ── Scenario B: single key spanning many blocks (multi-level k) ───────
    {
        constexpr uint32_t kEntries = 10000; // R_0=10000 -> R_1=80 -> single final block (k=3)
        constexpr uint32_t kMaxKey = 100;
        constexpr uint32_t kChannels = 1;
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);
        for (uint32_t i = 0; i < kEntries; ++i) {
            ctx.SetPair(i, 5u, i); // all key 5
            ctx.SetValue(0u, i, 1.0f);
        }
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        CloseF(out[5], static_cast<float>(kEntries), 1e-2f, "single key spanning many blocks");
        if (out[0] != 0.0f || out[99] != 0.0f) {
            std::cerr << "FAIL: single-key run must leave other slots untouched" << std::endl;
            g_failures++;
        }
    }

    // ── Scenario C: INVALID sentinel entries are ignored ──────────────────
    {
        constexpr uint32_t kEntries = 300; // multi-block at level 0
        constexpr uint32_t kMaxKey = 100;
        constexpr uint32_t kChannels = 1;
        constexpr uint32_t kInvalid = 0xFFFFFu;
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);
        for (uint32_t i = 0; i < kEntries; ++i) {
            if (i < 200u) {
                // sorted ascending: bodies 0..99 each appearing twice (contiguous
                // per body), so every body has a two-element run.
                ctx.SetPair(i, i / 2u, i);
                ctx.SetValue(0u, i, 1.0f);
            } else {
                ctx.SetPair(i, kInvalid, i); // trailing invalid run, must be ignored
                ctx.SetValue(0u, i, 99.0f);
            }
        }
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            const float expected = (b < 100u) ? 2.0f : 0.0f;
            CloseF(out[b], expected, 1e-4f, "INVALID sentinel ignored");
        }
    }

    // ── Scenario D: seven channels in one Record call ─────────────────────
    {
        constexpr uint32_t kEntries = 5000;
        constexpr uint32_t kMaxKey = 64;
        constexpr uint32_t kChannels = 7;
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);

        // keys 0..63 in a pattern spanning many blocks; each body b appears a
        // known number of times with distinct per-channel contributions.
        uint32_t pos = 0u;
        // Use counts: count(b) = 1 + (b % 7). Sum < kEntries.
        std::vector<float> expected(kMaxKey * kChannels, 0.0f);
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            uint32_t count = 1u + (b % 7u);
            for (uint32_t t = 0; t < count && pos < kEntries; ++t) {
                ctx.SetPair(pos, b, pos);
                for (uint32_t c = 0; c < kChannels; ++c) {
                    float val = static_cast<float>(c) + static_cast<float>(t);
                    ctx.SetValue(c, pos, val);
                    expected[c * kMaxKey + b] += val;
                }
                ++pos;
            }
        }
        // Fill the remainder with INVALID so no slot is left stale.
        constexpr uint32_t kInvalid = 0xFFFFFu;
        for (; pos < kEntries; ++pos) {
            ctx.SetPair(pos, kInvalid, pos);
            for (uint32_t c = 0; c < kChannels; ++c) {
                ctx.SetValue(c, pos, 0.0f);
            }
        }

        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            for (uint32_t c = 0; c < kChannels; ++c) {
                const float got = out[c * kMaxKey + b];
                const float want = expected[c * kMaxKey + b];
                if (!(std::fabs(got - want) <= 1e-2f)) {
                    std::cerr << "FAIL: 7-channel body " << b << " ch " << c << " expected " << want << " got " << got
                              << std::endl;
                    g_failures++;
                }
            }
        }
    }

    // ── Scenario E: construction geometry is reported back to the caller ───
    // XPBDGpuSolver uses these accessors to decide whether a SumByKey instance is
    // still valid for the current entry geometry and must be rebuilt, so they must
    // report exactly what the instance was constructed with.
    {
        constexpr uint32_t kEntries = 300u; // > kBlockSize, so the geometry is multi-level
        constexpr uint32_t kMaxKey = 32u;
        constexpr uint32_t kChannels = 7u;
        SumByKey reducer{rsys->GetDeviceContext(), kEntries, kMaxKey, kChannels};
        Check(reducer.GetMaxEntries() == kEntries, "GetMaxEntries reports construction max_entries");
        Check(reducer.GetMaxKeyValue() == kMaxKey, "GetMaxKeyValue reports construction max_key_value");
        Check(reducer.GetNumChannels() == kChannels, "GetNumChannels reports construction num_channels");
        Check(
            reducer.GetNumLevels() == SumByKey::GetNumLevels(kEntries),
            "GetNumLevels matches the static geometry for max_entries"
        );
        Check(!reducer.IsInitialized(), "no shader is loaded before the first Record");
        Check(SumByKey::GetNumLevels(kEntries) > 1u, "300 entries need more than one level");
    }

    // ── Scenario F: level-0 gather reads values BY SLOT, not by position ───
    // 600 entries across 3 level-0 blocks with 10 keys, so the boundary-record
    // chain (levels >= 1) is exercised too.  Payload slots are a scrambled
    // bijection of [0, max_entries), and every value is distinct per slot, so a
    // reducer that read values by position would produce different sums.
    {
        constexpr uint32_t kEntries = 600; // R_0=600 -> R_1=6 (k == 2), 3 blocks
        constexpr uint32_t kMaxKey = 10;
        constexpr uint32_t kChannels = 1;
        constexpr uint32_t kPerBody = kEntries / kMaxKey; // 60
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);

        // value(slot) = 1 + (slot % 13): distinct per slot, repeating so no
        // single-value shortcut can pass by accident.
        std::vector<float> value(kEntries);
        for (uint32_t s = 0; s < kEntries; ++s) {
            value[s] = 1.0f + static_cast<float>(s % 13u);
            ctx.SetValue(0u, s, value[s]);
        }

        // Keys ascending (required): body b occupies pairs [60b, 60b+60).
        // Slots are a bijection of [0,600): slot(i) = (i * 7) % 600 (gcd(7,600)=1).
        std::vector<float> expected(kMaxKey, 0.0f);
        for (uint32_t i = 0; i < kEntries; ++i) {
            const uint32_t body = i / kPerBody;
            const uint32_t slot = (i * 7u) % kEntries;
            ctx.SetPair(i, body, slot);
            expected[body] += value[slot];
        }

        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());

        float total_got = 0.0f;
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            CloseF(out[b], expected[b], 1e-2f, "gather-by-slot per-key sum");
            total_got += out[b];
        }
        float total_want = 0.0f;
        for (uint32_t s = 0; s < kEntries; ++s) {
            total_want += value[s];
        }
        // Conservation: every slot is consumed exactly once and none is skipped.
        CloseF(total_got, total_want, 1e-1f, "gather consumes every slot exactly once");
    }

    // ── Scenario G: an out-of-range payload slot is dropped, not read ──────
    // 300 entries all keyed to body 7 (a run spanning two blocks); one entry
    // carries a slot beyond max_entries.  It must contribute 0.0 while its key is
    // left unchanged, so body 7 loses exactly that one contribution.
    {
        constexpr uint32_t kEntries = 300;
        constexpr uint32_t kMaxKey = 100;
        constexpr uint32_t kChannels = 1;
        constexpr uint32_t kBadSlot = 5000u; // >= max_entries
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);
        for (uint32_t i = 0; i < kEntries; ++i) {
            const bool bad = (i == 100u);
            ctx.SetPair(i, 7u, bad ? kBadSlot : i);
            ctx.SetValue(0u, i, bad ? 999.0f : 1.0f);
        }
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        CloseF(out[7], static_cast<float>(kEntries - 1u), 1e-3f, "out-of-range payload slot contributes nothing");
        if (out[0] != 0.0f) {
            std::cerr << "FAIL: dropping a bad slot must not move its key elsewhere" << std::endl;
            g_failures++;
        }
    }

    rsys->WaitForIdle();

    if (g_failures == 0) {
        std::cout << "gpu_sum_by_key_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_sum_by_key_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
