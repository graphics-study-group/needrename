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
        std::unique_ptr<ComputeBuffer> keys;
        std::unique_ptr<ComputeBuffer> values;
        std::unique_ptr<ComputeBuffer> records;
        std::unique_ptr<ComputeBuffer> out;

        uint32_t num_channels = 0u;
        uint32_t max_entries = 0u;
        uint32_t max_key_value = 0u;
    };

    // Build a RunCtx sized for the given geometry and zero the output + records.
    RunCtx MakeCtx(RenderSystem &rsys, uint32_t max_entries, uint32_t max_key_value, uint32_t num_channels) {
        RunCtx ctx{rsys, {}, {}, {}, {}, num_channels, max_entries, max_key_value};
        ctx.keys = MakeHostBuffer(
            rsys, static_cast<size_t>(max_entries) * sizeof(uint32_t), "SumByKey keys"
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

        std::memset(ctx.values->GetVMAddress(), 0, ctx.values->GetSize());
        std::memset(ctx.out->GetVMAddress(), 0, ctx.out->GetSize());
        std::memset(ctx.records->GetVMAddress(), 0, ctx.records->GetSize());
        return ctx;
    }

    void FlushAll(RunCtx &ctx) {
        ctx.keys->Flush();
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
        reducer.Record(cb, *ctx.keys, *ctx.values, *ctx.records, *ctx.out);
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
        auto *k = reinterpret_cast<uint32_t *>(ctx.keys->GetVMAddress());
        auto *v = reinterpret_cast<float *>(ctx.values->GetVMAddress());
        // keys 0..99 each appearing once, value channel0 = 2.0
        for (uint32_t i = 0; i < kEntries; ++i) {
            k[i] = i;
            v[i] = 2.0f;
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
        auto *k = reinterpret_cast<uint32_t *>(ctx.keys->GetVMAddress());
        auto *v = reinterpret_cast<float *>(ctx.values->GetVMAddress());
        for (uint32_t i = 0; i < kEntries; ++i) {
            k[i] = 5u; // all key 5
            v[i] = 1.0f;
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
        auto *k = reinterpret_cast<uint32_t *>(ctx.keys->GetVMAddress());
        auto *v = reinterpret_cast<float *>(ctx.values->GetVMAddress());
        for (uint32_t i = 0; i < kEntries; ++i) {
            if (i < 200u) {
                // sorted ascending: bodies 0..99 each appearing twice (contiguous
                // per body), so every body has a two-element run.
                k[i] = i / 2u;
                v[i] = 1.0f;
            } else {
                k[i] = kInvalid; // trailing invalid run, must be ignored
                v[i] = 99.0f;
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
        auto *k = reinterpret_cast<uint32_t *>(ctx.keys->GetVMAddress());
        auto *v = reinterpret_cast<float *>(ctx.values->GetVMAddress());

        // keys 0..63 in a pattern spanning many blocks; each body b appears a
        // known number of times with distinct per-channel contributions.
        // Build: body b appears (b+1) times, keys appended in blocks so body 0..63
        // repeat; values[c] for occurrence t of body b = c + t.
        uint32_t pos = 0u;
        // body 63 appears 64 times and dominates; keep total <= kEntries.
        // Use counts: count(b) = 1 + (b % 7). Sum < kEntries.
        std::vector<float> expected(kMaxKey * kChannels, 0.0f);
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            uint32_t count = 1u + (b % 7u);
            for (uint32_t t = 0; t < count && pos < kEntries; ++t) {
                k[pos] = b;
                for (uint32_t c = 0; c < kChannels; ++c) {
                    float val = static_cast<float>(c) + static_cast<float>(t);
                    v[c * kEntries + pos] = val;
                    expected[c * kMaxKey + b] += val;
                }
                ++pos;
            }
        }
        // Fill the remainder with INVALID so no slot is left stale.
        constexpr uint32_t kInvalid = 0xFFFFFu;
        for (; pos < kEntries; ++pos) {
            k[pos] = kInvalid;
            for (uint32_t c = 0; c < kChannels; ++c) {
                v[c * kEntries + pos] = 0.0f;
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

    rsys->WaitForIdle();

    if (g_failures == 0) {
        std::cout << "gpu_sum_by_key_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_sum_by_key_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
