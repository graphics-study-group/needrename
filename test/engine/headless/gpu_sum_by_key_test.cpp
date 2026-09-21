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
#include <random>
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
        std::unique_ptr<ComputeBuffer> keys;      // sorted key array
        std::unique_ptr<ComputeBuffer> payloads;  // payload-index array (level-0 gather)
        std::unique_ptr<ComputeBuffer> values;
        std::unique_ptr<ComputeBuffer> records;
        std::unique_ptr<ComputeBuffer> out;
        std::unique_ptr<ComputeBuffer> count;   // entry count (GPU-read, host-written here)

        uint32_t num_channels = 0u;
        uint32_t capacity = 0u;
        uint32_t max_key_value = 0u;

        // Entry i = (key, payload).  The payload is the index this entry's values
        // are gathered from, so it is unrelated to i.
        void SetEntry(uint32_t i, uint32_t key, uint32_t payload) const {
            reinterpret_cast<uint32_t *>(keys->GetVMAddress())[i] = key;
            reinterpret_cast<uint32_t *>(payloads->GetVMAddress())[i] = payload;
        }

        void SetValue(uint32_t channel, uint32_t slot, float value) const {
            auto *v = reinterpret_cast<float *>(values->GetVMAddress());
            v[static_cast<size_t>(channel) * capacity + slot] = value;
        }

        void SetCount(uint32_t entry_count) const {
            *reinterpret_cast<uint32_t *>(count->GetVMAddress()) = entry_count;
        }

        void ClearOut() const {
            std::memset(out->GetVMAddress(), 0, out->GetSize());
        }

        void FillOut(float value) const {
            auto *o = reinterpret_cast<float *>(out->GetVMAddress());
            for (size_t i = 0; i < out->GetSize() / sizeof(float); ++i) {
                o[i] = value;
            }
        }
    };

    // Build a RunCtx sized for the given geometry and zero the output + records.
    RunCtx MakeCtx(RenderSystem &rsys, uint32_t capacity, uint32_t max_key_value, uint32_t num_channels) {
        RunCtx ctx{rsys, {}, {}, {}, {}, {}, {}, num_channels, capacity, max_key_value};
        ctx.keys = MakeHostBuffer(rsys, static_cast<size_t>(capacity) * sizeof(uint32_t), "SumByKey keys");
        ctx.payloads = MakeHostBuffer(rsys, static_cast<size_t>(capacity) * sizeof(uint32_t), "SumByKey payloads");
        ctx.values =
            MakeHostBuffer(rsys, static_cast<size_t>(num_channels) * capacity * sizeof(uint32_t), "SumByKey values");
        size_t rec_bytes = SumByKey::GetRequiredRecordsBytes(capacity, num_channels);
        if (rec_bytes == 0u) rec_bytes = 1u; // ensure a non-empty allocation
        ctx.records = MakeHostBuffer(rsys, rec_bytes, "SumByKey records");
        ctx.out =
            MakeHostBuffer(rsys, static_cast<size_t>(num_channels) * max_key_value * sizeof(uint32_t), "SumByKey out");
        ctx.count = MakeHostBuffer(rsys, sizeof(uint32_t), "SumByKey count");

        std::memset(ctx.keys->GetVMAddress(), 0, ctx.keys->GetSize());
        std::memset(ctx.payloads->GetVMAddress(), 0, ctx.payloads->GetSize());
        std::memset(ctx.values->GetVMAddress(), 0, ctx.values->GetSize());
        std::memset(ctx.out->GetVMAddress(), 0, ctx.out->GetSize());
        std::memset(ctx.records->GetVMAddress(), 0, ctx.records->GetSize());
        ctx.SetCount(capacity);
        return ctx;
    }

    void FlushAll(RunCtx &ctx) {
        ctx.keys->Flush();
        ctx.payloads->Flush();
        ctx.values->Flush();
        ctx.records->Flush();
        ctx.out->Flush();
        ctx.count->Flush();
    }

    void RunSumByKeyWith(RenderSystem &rsys, RunCtx &ctx, SumByKey &reducer) {
        FlushAll(ctx);
        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];

        cb.begin(vk::CommandBufferBeginInfo{});
        reducer.Record(
            cb,
            *ctx.keys,
            *ctx.payloads,
            *ctx.values,
            *ctx.records,
            *ctx.out,
            *ctx.count,
            ctx.capacity,
            ctx.num_channels,
            ctx.max_key_value
        );
        cb.end();
        Submit(rsys, cb);

        ctx.out->Invalidate();
        ctx.records->Invalidate();
    }

    void RunSumByKey(RenderSystem &rsys, RunCtx &ctx) {
        SumByKey reducer{rsys.GetDeviceContext()};
        if (reducer.IsInitialized()) {
            std::cerr << "FAIL: no shader may be loaded before the first Record" << std::endl;
            g_failures++;
        }
        RunSumByKeyWith(rsys, ctx, reducer);
        if (!reducer.IsInitialized()) {
            std::cerr << "FAIL: the first Record must load the shader" << std::endl;
            g_failures++;
        }
    }

    // ── Independent host-side expectation ──────────────────────────────────
    //
    // Computes the per-key sums straight from the source buffers, so it shares
    // no structure with the reduction (record regions, level chain or the
    // block-internal merge).  Keys at or above max_key_value and payload indices
    // at or above the capacity contribute nothing.
    std::vector<float> HostExpected(const RunCtx &ctx, uint32_t count) {
        std::vector<float> expect(static_cast<size_t>(ctx.num_channels) * ctx.max_key_value, 0.0f);
        const auto *keys = reinterpret_cast<const uint32_t *>(ctx.keys->GetVMAddress());
        const auto *payloads = reinterpret_cast<const uint32_t *>(ctx.payloads->GetVMAddress());
        const auto *values = reinterpret_cast<const float *>(ctx.values->GetVMAddress());
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t key = keys[i];
            const uint32_t payload = payloads[i];
            if (key >= ctx.max_key_value || payload >= ctx.capacity) continue;
            for (uint32_t c = 0u; c < ctx.num_channels; ++c) {
                expect[static_cast<size_t>(c) * ctx.max_key_value + key] +=
                    values[static_cast<size_t>(c) * ctx.capacity + payload];
            }
        }
        return expect;
    }

    // Compares every channel of every key against `expect`, naming the key and
    // the channel on a mismatch.
    void CheckExpected(const RunCtx &ctx, const std::vector<float> &expect, float eps, const char *what) {
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t c = 0u; c < ctx.num_channels; ++c) {
            for (uint32_t b = 0u; b < ctx.max_key_value; ++b) {
                const float want = expect[static_cast<size_t>(c) * ctx.max_key_value + b];
                const float got = out[static_cast<size_t>(c) * ctx.max_key_value + b];
                if (!(std::fabs(got - want) <= eps)) {
                    std::cerr << "FAIL: " << what << " key " << b << " channel " << c << " expected " << want
                              << " got " << got << std::endl;
                    g_failures++;
                }
            }
        }
    }

    // ── Counted-tail helpers ───────────────────────────────────────────────
    //
    // Each counted-tail case fills the first `count` entries with real data (two
    // entries per key, value 1.0, so key b sums to 2.0) and every entry from
    // `count` up to the capacity with **garbage that is indistinguishable from
    // real data**: a *valid* key (below max_key_value) carried by entries whose
    // payload slots are *valid* too, with a value large enough that reading it
    // would be obvious.  A reduction that ignored the entry count would therefore
    // write garbage into the output.
    constexpr float kGarbageValue = 1000.0f;

    void FillCountedTail(RunCtx &ctx, uint32_t count, uint32_t garbage_key) {
        for (uint32_t i = 0; i < ctx.capacity; ++i) {
            if (i < count) {
                ctx.SetEntry(i, i / 2u, i);
                ctx.SetValue(0u, i, 1.0f);
            } else {
                ctx.SetEntry(i, garbage_key, i); // valid key, valid payload slot
                ctx.SetValue(0u, i, kGarbageValue);
            }
        }
    }

    // Asserts the expectation of FillCountedTail: out[b] == 2.0 for the real keys
    // and 0.0 everywhere else (in particular at the garbage key).
    void CheckCountedTail(const RunCtx &ctx, uint32_t count, uint32_t garbage_key, const char *what) {
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < count / 2u; ++b) {
            CloseF(out[b], 2.0f, 1e-4f, what);
        }
        for (uint32_t b = count / 2u; b < ctx.max_key_value; ++b) {
            if (out[b] != 0.0f) {
                std::cerr << "FAIL: " << what << " key " << b << " must be untouched, got " << out[b] << std::endl;
                g_failures++;
            }
        }
        if (garbage_key < ctx.max_key_value && out[garbage_key] != 0.0f) {
            std::cerr << "FAIL: " << what << " garbage key " << garbage_key << " was read" << std::endl;
            g_failures++;
        }
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
            ctx.SetEntry(i, i, i);
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
            ctx.SetEntry(i, 5u, i); // all key 5
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
                ctx.SetEntry(i, i / 2u, i);
                ctx.SetValue(0u, i, 1.0f);
            } else {
                ctx.SetEntry(i, kInvalid, i); // trailing invalid run, must be ignored
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
                ctx.SetEntry(pos, b, pos);
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
            ctx.SetEntry(pos, kInvalid, pos);
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

    // ── Scenario E: no geometry is stored, and one instance serves several ─
    // geometries in one frame.  Geometry is a call parameter now, so the same
    // instance must reduce two different capacities without being rebuilt, and a
    // freshly constructed instance must have loaded nothing.
    {
        SumByKey reducer{rsys->GetDeviceContext()};
        Check(!reducer.IsInitialized(), "no shader is loaded before the first Record");

        // Geometry 1: 600 entries over 10 keys (k == 2, 3 level-0 blocks).
        constexpr uint32_t kEntries1 = 600;
        constexpr uint32_t kMaxKey1 = 10;
        auto ctx1 = MakeCtx(*rsys, kEntries1, kMaxKey1, 1u);
        for (uint32_t i = 0; i < kEntries1; ++i) {
            ctx1.SetEntry(i, i / 60u, i);
            ctx1.SetValue(0u, i, 2.0f);
        }
        RunSumByKeyWith(*rsys, ctx1, reducer);
        {
            const auto *out = reinterpret_cast<const float *>(ctx1.out->GetVMAddress());
            for (uint32_t b = 0; b < kMaxKey1; ++b) {
                CloseF(out[b], 120.0f, 1e-2f, "first geometry through a shared instance");
            }
        }

        // Geometry 2: a different capacity, key bound and channel count, same
        // instance.  Nothing from the first call may leak into it.
        constexpr uint32_t kEntries2 = 300;
        constexpr uint32_t kMaxKey2 = 20;
        constexpr uint32_t kPerKey2 = kEntries2 / kMaxKey2; // 15
        auto ctx2 = MakeCtx(*rsys, kEntries2, kMaxKey2, 2u);
        for (uint32_t i = 0; i < kEntries2; ++i) {
            ctx2.SetEntry(i, i / kPerKey2, i);
            ctx2.SetValue(0u, i, 1.0f);
            ctx2.SetValue(1u, i, 3.0f);
        }
        RunSumByKeyWith(*rsys, ctx2, reducer);
        {
            const auto *out = reinterpret_cast<const float *>(ctx2.out->GetVMAddress());
            for (uint32_t b = 0; b < kMaxKey2; ++b) {
                CloseF(out[b], static_cast<float>(kPerKey2), 1e-2f, "second geometry channel 0 through a shared instance");
                CloseF(
                    out[kMaxKey2 + b],
                    3.0f * static_cast<float>(kPerKey2),
                    1e-2f,
                    "second geometry channel 1 through a shared instance"
                );
            }
        }
        Check(reducer.IsInitialized(), "the shared instance stays initialized");
    }

    // ── Scenario F: level-0 gather reads values BY SLOT, not by position ───
    // 600 entries across 3 level-0 blocks with 10 keys, so the boundary-record
    // chain (levels >= 1) is exercised too.  Payload slots are a scrambled
    // bijection of [0, capacity), and every value is distinct per slot, so a
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
            ctx.SetEntry(i, body, slot);
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
    // carries a slot beyond the capacity.  It must contribute 0.0 while its key is
    // left unchanged, so body 7 loses exactly that one contribution.
    {
        constexpr uint32_t kEntries = 300;
        constexpr uint32_t kMaxKey = 100;
        constexpr uint32_t kChannels = 1;
        constexpr uint32_t kBadSlot = 5000u; // >= capacity
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, kChannels);
        for (uint32_t i = 0; i < kEntries; ++i) {
            const bool bad = (i == 100u);
            ctx.SetEntry(i, 7u, bad ? kBadSlot : i);
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

    // ── Scenario H: per-call argument and buffer validation ───────────────
    // Geometry is per call now, so the checks the constructor used to own are the
    // caller's contract at every Record.
    {
        constexpr uint32_t kEntries = 300;
        constexpr uint32_t kMaxKey = 32;
        auto ctx = MakeCtx(*rsys, kEntries, kMaxKey, 1u);
        FlushAll(ctx);
        SumByKey reducer{rsys->GetDeviceContext()};
        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});

        auto throws_invalid = [&](uint32_t capacity, uint32_t channels, uint32_t max_key) {
            try {
                reducer.Record(
                    cb,
                    *ctx.keys,
                    *ctx.payloads,
                    *ctx.values,
                    *ctx.records,
                    *ctx.out,
                    *ctx.count,
                    capacity,
                    channels,
                    max_key
                );
            } catch (const std::invalid_argument &) {
                return true;
            } catch (...) {
                return false;
            }
            return false;
        };
        Check(throws_invalid(kEntries, 0u, kMaxKey), "zero num_channels is rejected per call");
        Check(throws_invalid(kEntries, 9u, kMaxKey), "num_channels above kMaxChannels is rejected per call");
        Check(throws_invalid(0u, 1u, kMaxKey), "zero capacity is rejected per call");
        Check(throws_invalid(kEntries, 1u, 0u), "zero max_key_value is rejected per call");

        // A capacity whose implied value-buffer size exceeds the bound buffer.
        bool threw_runtime = false;
        try {
            reducer.Record(
                cb,
                *ctx.keys,
                *ctx.payloads,
                *ctx.values,
                *ctx.records,
                *ctx.out,
                *ctx.count,
                kEntries * 4u,
                1u,
                kMaxKey
            );
        } catch (const std::runtime_error &) {
            threw_runtime = true;
        } catch (...) {
        }
        Check(threw_runtime, "a capacity larger than the bound value buffer is rejected per call");
        cb.end();
        Submit(*rsys, cb);
    }

    // ── Scenario I: a count far below the capacity ignores the garbage tail ─
    // capacity 4096 -> R_0=4096, R_1=32 (k == 2, 16 level-0 blocks).  The count
    // (300) is not a multiple of 256, so the last real block is partially filled.
    {
        constexpr uint32_t kCapacity = 4096u;
        constexpr uint32_t kCount = 300u;    // off a 256-boundary
        constexpr uint32_t kMaxKey = 512u;
        constexpr uint32_t kGarbageKey = 150u; // a valid key that only the tail carries
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        FillCountedTail(ctx, kCount, kGarbageKey);
        ctx.SetCount(kCount);
        RunSumByKey(*rsys, ctx);
        CheckCountedTail(ctx, kCount, kGarbageKey, "count off a 256-boundary ignores the garbage tail");
    }

    // ── Scenario J: the same, with the count exactly on a 256-boundary ─────
    {
        constexpr uint32_t kCapacity = 4096u;
        constexpr uint32_t kCount = 512u;    // a multiple of 256
        constexpr uint32_t kMaxKey = 512u;
        constexpr uint32_t kGarbageKey = 256u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        FillCountedTail(ctx, kCount, kGarbageKey);
        ctx.SetCount(kCount);
        RunSumByKey(*rsys, ctx);
        CheckCountedTail(ctx, kCount, kGarbageKey, "count on a 256-boundary ignores the garbage tail");
    }

    // ── Scenario K: a large capacity with the count at or below 256 ────────
    // The whole real data sits in level-0 block 0 while the capacity spans eight
    // blocks.  A count-derived *array* bound would make every block treat itself
    // as the last level and drop the partial sums that cross a block boundary.
    {
        constexpr uint32_t kCapacity = 2048u;
        constexpr uint32_t kCount = 200u; // <= 256 while the capacity is far above
        constexpr uint32_t kMaxKey = 256u;
        constexpr uint32_t kGarbageKey = 100u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        FillCountedTail(ctx, kCount, kGarbageKey);
        ctx.SetCount(kCount);
        RunSumByKey(*rsys, ctx);
        CheckCountedTail(ctx, kCount, kGarbageKey, "a count at or below 256 does not shrink the array bound");
    }

    // ── Scenario L: a zero entry count reads nothing ──────────────────────
    // The tail still carries a *valid* key, so a reduction that read it would
    // write a large sum.  Every output must stay at zero.
    {
        constexpr uint32_t kCapacity = 1024u;
        constexpr uint32_t kMaxKey = 64u;
        constexpr uint32_t kGarbageKey = 5u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        FillCountedTail(ctx, 0u, kGarbageKey);
        ctx.SetCount(0u);
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            CloseF(out[b], 0.0f, 1e-6f, "a zero entry count reads no value");
        }
    }

    // ── Scenario M: a zero entry count writes no output slot ──────────────
    // With an INVALID tail (the sentinel the entry passes write) nothing is even
    // a candidate, so the output must be left exactly as it was.
    {
        constexpr uint32_t kCapacity = 1024u;
        constexpr uint32_t kMaxKey = 64u;
        constexpr uint32_t kInvalid = 0xFFFFFu;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        FillCountedTail(ctx, 0u, kInvalid);
        ctx.ClearOut();
        ctx.FillOut(7.0f);
        ctx.SetCount(0u);
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            CloseF(out[b], 7.0f, 1e-6f, "a zero entry count writes no output slot");
        }
    }

    // ── Scenario N: an entry count equal to the capacity is the identity ───
    // The same geometry and buffers as the counted cases above, with every entry
    // real: the result must be the plain full-array reduction, exactly as if the
    // bound did not exist.
    {
        constexpr uint32_t kCapacity = 4096u;
        constexpr uint32_t kMaxKey = 2048u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        for (uint32_t i = 0; i < kCapacity; ++i) {
            ctx.SetEntry(i, i / 2u, i);
            ctx.SetValue(0u, i, 1.0f);
        }
        ctx.SetCount(kCapacity);
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kMaxKey; ++b) {
            CloseF(out[b], 2.0f, 1e-4f, "an entry count equal to the capacity is the identity");
        }
    }

    // ── Scenario O: a smaller count after a larger one leaves no residue ───
    // One instance, one set of buffers: first a full-capacity reduction, then the
    // same buffers with a small entry count.  The earlier call's records and the
    // tail's now-unread values must not reach the output.
    {
        constexpr uint32_t kCapacity = 4096u;
        constexpr uint32_t kMaxKey = 2048u;
        constexpr uint32_t kSmallCount = 300u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        for (uint32_t i = 0; i < kCapacity; ++i) {
            ctx.SetEntry(i, i / 2u, i);
            ctx.SetValue(0u, i, 1.0f);
        }
        SumByKey reducer{rsys->GetDeviceContext()};

        ctx.SetCount(kCapacity);
        RunSumByKeyWith(*rsys, ctx, reducer);
        {
            const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
            for (uint32_t b = 0; b < kMaxKey; ++b) {
                CloseF(out[b], 2.0f, 1e-4f, "full-capacity pass before the counted pass");
            }
        }

        ctx.ClearOut();
        ctx.SetCount(kSmallCount);
        RunSumByKeyWith(*rsys, ctx, reducer);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t b = 0; b < kSmallCount / 2u; ++b) {
            CloseF(out[b], 2.0f, 1e-4f, "counted pass after a larger one keeps the real prefix");
        }
        for (uint32_t b = kSmallCount / 2u; b < kMaxKey; ++b) {
            if (out[b] != 0.0f) {
                std::cerr << "FAIL: a smaller entry count leaves no residue, but key " << b << " is " << out[b]
                          << std::endl;
                g_failures++;
            }
        }
    }

    // ── Scenario P: run-length matrix within one block ─────────────────────
    // Run lengths 1, 2, 3, 7, 8, 9 straddle every merge window of the block's
    // pairwise doubling, and a final run of 226 fills the remainder, so the
    // block both starts and ends inside a run.  Channel 0 counts elements
    // (value 1.0) and channel 1 triples them, so a run that absorbed another
    // run's elements would show up as an over-count on both channels.
    {
        constexpr uint32_t kCapacity = 256u;
        constexpr uint32_t kMaxKey = 8u;
        constexpr uint32_t kChannels = 2u;
        constexpr uint32_t kRuns[] = {1u, 2u, 3u, 7u, 8u, 9u, 226u};
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, kChannels);
        uint32_t pos = 0u;
        for (uint32_t key = 0u; key < 7u; ++key) {
            for (uint32_t t = 0u; t < kRuns[key]; ++t, ++pos) {
                ctx.SetEntry(pos, key, pos);
                ctx.SetValue(0u, pos, 1.0f);
                ctx.SetValue(1u, pos, 3.0f);
            }
        }
        Check(pos == kCapacity, "the run-length matrix fills the block exactly");
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        for (uint32_t key = 0u; key < 7u; ++key) {
            CloseF(out[key], static_cast<float>(kRuns[key]), 1e-4f, "run length within one block (channel 0)");
            CloseF(
                out[kMaxKey + key],
                3.0f * static_cast<float>(kRuns[key]),
                1e-3f,
                "run length within one block (channel 1)"
            );
        }
        Check(out[7] == 0.0f, "an absent key stays untouched in the run-length matrix");
    }

    // ── Scenario Q: a whole-block run and a run crossing a block boundary ───
    // 300 entries of key 0 saturate the first block and continue 44 entries into
    // the second; key 1 fills the rest.  That is every boundary-record shape at
    // once: a run filling a whole block (first and last), a run starting at a
    // block's first element, a run ending at a block's last real element, and a
    // run that crosses the boundary.  The upper level reassembles them.
    {
        constexpr uint32_t kCapacity = 512u;
        constexpr uint32_t kMaxKey = 4u;
        constexpr uint32_t kKey0 = 300u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            ctx.SetEntry(i, (i < kKey0) ? 0u : 1u, i);
            ctx.SetValue(0u, i, 1.0f);
        }
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        CloseF(out[0], static_cast<float>(kKey0), 1e-3f, "a run that crosses a block boundary");
        CloseF(out[1], static_cast<float>(kCapacity - kKey0), 1e-3f, "a run filling the rest of the last block");
        Check(out[2] == 0.0f && out[3] == 0.0f, "absent keys stay untouched around a block boundary");
    }

    // ── Scenario R: a run crossing a block boundary by a single element ─────
    // The first block is one run of 256; the second opens with the single
    // remaining element of that run and then a run of 255.
    {
        constexpr uint32_t kCapacity = 512u;
        constexpr uint32_t kMaxKey = 4u;
        constexpr uint32_t kKey0 = 257u;
        auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, 1u);
        for (uint32_t i = 0u; i < kCapacity; ++i) {
            ctx.SetEntry(i, (i < kKey0) ? 0u : 1u, i);
            ctx.SetValue(0u, i, 1.0f);
        }
        RunSumByKey(*rsys, ctx);
        const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
        CloseF(out[0], static_cast<float>(kKey0), 1e-3f, "a run crossing a block boundary by one element");
        CloseF(out[1], static_cast<float>(kCapacity - kKey0), 1e-3f, "the run after a one-element boundary tail");
    }

    // ── Scenario S: 1, 4, 7 and 8 channels reduce the same input identically ─
    // Three level-0 blocks of even 70-element runs, one integer value per
    // channel, so the expected sum is exact and any channel mix-up is visible.
    {
        constexpr uint32_t kCapacity = 700u;
        constexpr uint32_t kMaxKey = 10u;
        constexpr uint32_t kPerKey = kCapacity / kMaxKey; // 70
        constexpr uint32_t kChannelCounts[] = {1u, 4u, 7u, 8u};
        for (uint32_t n = 0u; n < 4u; ++n) {
            const uint32_t channels = kChannelCounts[n];
            auto ctx = MakeCtx(*rsys, kCapacity, kMaxKey, channels);
            for (uint32_t i = 0u; i < kCapacity; ++i) {
                ctx.SetEntry(i, i / kPerKey, i);
                for (uint32_t c = 0u; c < channels; ++c) {
                    ctx.SetValue(c, i, 1.0f + static_cast<float>(c));
                }
            }
            RunSumByKey(*rsys, ctx);
            const auto *out = reinterpret_cast<const float *>(ctx.out->GetVMAddress());
            for (uint32_t b = 0u; b < kMaxKey; ++b) {
                for (uint32_t c = 0u; c < channels; ++c) {
                    CloseF(
                        out[c * kMaxKey + b],
                        static_cast<float>(kPerKey) * (1.0f + static_cast<float>(c)),
                        1e-3f,
                        "the same input reduces identically at 1, 4, 7 and 8 channels"
                    );
                }
            }
        }
    }

    // ── Scenario T: randomized ascending keys against a host-side sum ───────
    // Four capacities exercise a single level, two levels and three levels.
    // Run lengths are drawn from 1..300, so block boundaries fall inside runs at
    // both small and large offsets, and every key's run is eventually long
    // enough to need more than one merge round.  Values are small integers, so
    // the host expectation is exact.
    {
        constexpr uint32_t kCapacities[] = {200u, 600u, 5000u, 70000u};
        constexpr uint32_t kChannels = 3u;
        constexpr uint32_t kMaxKey = 64u;
        std::mt19937 rng(20260915u); // fixed seed: the case is reproducible
        std::uniform_int_distribution<uint32_t> run_len(1u, 300u);
        std::uniform_int_distribution<uint32_t> value(0u, 7u);
        for (uint32_t n = 0u; n < 4u; ++n) {
            const uint32_t capacity = kCapacities[n];
            auto ctx = MakeCtx(*rsys, capacity, kMaxKey, kChannels);
            uint32_t pos = 0u;
            for (uint32_t key = 0u; key < kMaxKey && pos < capacity; ++key) {
                const uint32_t len = std::min(run_len(rng), capacity - pos);
                for (uint32_t t = 0u; t < len; ++t, ++pos) {
                    ctx.SetEntry(pos, key, pos);
                    for (uint32_t c = 0u; c < kChannels; ++c) {
                        ctx.SetValue(c, pos, static_cast<float>(value(rng)));
                    }
                }
            }
            // The keys are exhausted before the capacity: the rest is a trailing
            // run of the last key carrying zeros.
            for (; pos < capacity; ++pos) {
                ctx.SetEntry(pos, kMaxKey - 1u, pos);
                for (uint32_t c = 0u; c < kChannels; ++c) {
                    ctx.SetValue(c, pos, 0.0f);
                }
            }
            const std::vector<float> expect = HostExpected(ctx, capacity);
            RunSumByKey(*rsys, ctx);
            CheckExpected(ctx, expect, 1e-2f, "randomized ascending keys against a host-side sum");
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
