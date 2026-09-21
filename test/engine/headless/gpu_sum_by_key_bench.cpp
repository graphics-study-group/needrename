// gpu_sum_by_key_bench - isolated timing for SumByKey's block-internal
// reduction.
//
// Why isolated: the reduction runs ~100 times per frame in the XPBD solver, but
// a scene-level wall clock cannot resolve its cost: the previous change's
// verification note measured its traffic saving as real yet unresolvable
// against a ~21 ms step dominated by fixed per-step costs.  This benchmark puts
// nothing but the reduction in the submission: no scene, no collision
// detection, no solver passes.
//
// It is deliberately not a pass/fail test.  It records `iterations` reductions
// back to back (with the caller's outer barrier between them) on the same
// synthetic buffers, submits once, waits, and reports wall clock per case, so
// the same binary can time a baseline shader and a candidate shader.  A light
// sanity value (out[0], which must equal the first run's length) is printed
// beside each row so a measurement of a broken shader is recognisable.
//
// Usage: gpu_sum_by_key_bench [iterations]   (default 100)

#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeHelpers.h"

#include <Physics/gpu_algorithm/SumByKey.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace Engine;
using namespace Engine::Rhi;

namespace {

    constexpr uint32_t kCapacity = 200000u; // default; override with argv[2]
    constexpr uint32_t kChannels = 7u;      // the solver's channel count
    constexpr uint32_t kMixtureSmallRun = 4u;

    // Caller-side barrier: consecutive Record calls touch the same record and
    // output buffers, so they must be ordered exactly as the solver orders its
    // passes.
    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };

    std::unique_ptr<ComputeBuffer> MakeHostBuffer(RenderSystem &rsys, size_t bytes, const char *name) {
        return ComputeBuffer::CreateUnique(rsys.GetAllocatorState(), bytes, true, false, false, false, name);
    }

    struct BenchCtx {
        std::unique_ptr<ComputeBuffer> keys;
        std::unique_ptr<ComputeBuffer> payloads;
        std::unique_ptr<ComputeBuffer> values;
        std::unique_ptr<ComputeBuffer> records;
        std::unique_ptr<ComputeBuffer> out;
        std::unique_ptr<ComputeBuffer> count;
        uint32_t capacity = 0u;
        uint32_t max_key_value = 0u;

        void SetEntry(uint32_t i, uint32_t key, uint32_t payload) const {
            reinterpret_cast<uint32_t *>(keys->GetVMAddress())[i] = key;
            reinterpret_cast<uint32_t *>(payloads->GetVMAddress())[i] = payload;
        }

        void SetValue(uint32_t channel, uint32_t slot, float value) const {
            auto *v = reinterpret_cast<float *>(values->GetVMAddress());
            v[static_cast<size_t>(channel) * capacity + slot] = value;
        }

        void Flush() const {
            keys->Flush();
            payloads->Flush();
            values->Flush();
            records->Flush();
            out->Flush();
            count->Flush();
        }
    };

    BenchCtx MakeCtx(RenderSystem &rsys, uint32_t capacity, uint32_t max_key_value) {
        BenchCtx ctx{};
        ctx.capacity = capacity;
        ctx.max_key_value = max_key_value;
        ctx.keys = MakeHostBuffer(rsys, static_cast<size_t>(capacity) * sizeof(uint32_t), "Bench keys");
        ctx.payloads = MakeHostBuffer(rsys, static_cast<size_t>(capacity) * sizeof(uint32_t), "Bench payloads");
        ctx.values = MakeHostBuffer(rsys, static_cast<size_t>(kChannels) * capacity * sizeof(uint32_t), "Bench values");
        size_t rec_bytes = SumByKey::GetRequiredRecordsBytes(capacity, kChannels);
        if (rec_bytes == 0u) rec_bytes = 1u;
        ctx.records = MakeHostBuffer(rsys, rec_bytes, "Bench records");
        ctx.out = MakeHostBuffer(
            rsys, static_cast<size_t>(kChannels) * max_key_value * sizeof(uint32_t), "Bench out"
        );
        ctx.count = MakeHostBuffer(rsys, sizeof(uint32_t), "Bench count");

        std::memset(ctx.keys->GetVMAddress(), 0, ctx.keys->GetSize());
        std::memset(ctx.payloads->GetVMAddress(), 0, ctx.payloads->GetSize());
        std::memset(ctx.values->GetVMAddress(), 0, ctx.values->GetSize());
        std::memset(ctx.out->GetVMAddress(), 0, ctx.out->GetSize());
        std::memset(ctx.records->GetVMAddress(), 0, ctx.records->GetSize());
        *reinterpret_cast<uint32_t *>(ctx.count->GetVMAddress()) = capacity;
        return ctx;
    }

    enum class Shape : uint32_t { Uniform = 0u, SingleKey, Mixture };

    struct BenchCase {
        const char *name;
        Shape shape;
        uint32_t run_length; // Uniform only
    };

    // The lengths of the key runs, in ascending key order.  Each run becomes one
    // key, so the run count is also the number of output slots the case needs.
    std::vector<uint32_t> BuildRuns(const BenchCase &bench, uint32_t capacity) {
        std::vector<uint32_t> runs;
        if (bench.shape == Shape::SingleKey) {
            runs.push_back(capacity);
            return runs;
        }
        if (bench.shape == Shape::Mixture) {
            // A "ground body"-like distribution: many short runs, one large run,
            // and a final run that absorbs the rest, as proportions of the
            // capacity so the shape survives a smaller capacity.
            const uint32_t small_slots = std::max(kMixtureSmallRun, capacity / 10u);
            for (uint32_t used = 0u; used + kMixtureSmallRun <= small_slots; used += kMixtureSmallRun) {
                runs.push_back(kMixtureSmallRun);
            }
            runs.push_back(std::max(1u, capacity / 3u));
            uint32_t used = 0u;
            for (uint32_t len : runs) used += len;
            runs.push_back(capacity - used);
            return runs;
        }
        uint32_t remaining = capacity;
        while (remaining > 0u) {
            const uint32_t len = std::min(bench.run_length, remaining);
            runs.push_back(len);
            remaining -= len;
        }
        return runs;
    }

    void FillFromRuns(BenchCtx &ctx, const std::vector<uint32_t> &runs) {
        uint32_t pos = 0u;
        for (uint32_t key = 0u; key < runs.size(); ++key) {
            for (uint32_t t = 0u; t < runs[key]; ++t, ++pos) {
                ctx.SetEntry(pos, key, pos);
                for (uint32_t c = 0u; c < kChannels; ++c) {
                    ctx.SetValue(c, pos, 1.0f);
                }
            }
        }
    }

    struct Result {
        double total_ms = 0.0;
        double per_iter_ms = 0.0;
        double out0 = 0.0;
    };

    Result RunCase(
        RenderSystem &rsys,
        SumByKey &reducer,
        BenchCtx &ctx,
        uint32_t iterations
    ) {
        const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
        auto cb = rsys.GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];

        ctx.Flush();
        cb.begin(vk::CommandBufferBeginInfo{});
        for (uint32_t i = 0u; i < iterations; ++i) {
            if (i > 0u) {
                cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});
            }
            reducer.Record(
                cb,
                *ctx.keys,
                *ctx.payloads,
                *ctx.values,
                *ctx.records,
                *ctx.out,
                *ctx.count,
                ctx.capacity,
                kChannels,
                ctx.max_key_value
            );
        }
        cb.end();

        const auto t0 = std::chrono::steady_clock::now();
        queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
        queues.graphicsQueue.waitIdle();
        const auto t1 = std::chrono::steady_clock::now();

        ctx.out->Invalidate();

        Result r{};
        r.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.per_iter_ms = r.total_ms / static_cast<double>(iterations);
        r.out0 = static_cast<double>(reinterpret_cast<const float *>(ctx.out->GetVMAddress())[0]);
        return r;
    }
} // namespace

int main(int argc, char **argv) {
    const uint32_t iterations = (argc > 1) ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 100u;
    const uint32_t capacity = (argc > 2) ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)) : kCapacity;

    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "SumByKey Bench"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    const BenchCase cases[] = {
        {"all distinct keys", Shape::Uniform, 1u},
        {"runs of 2", Shape::Uniform, 2u},
        {"runs of 8", Shape::Uniform, 8u},
        {"runs of 40", Shape::Uniform, 40u},
        {"one key spans all", Shape::SingleKey, 0u},
        {"mixture (short+one huge)", Shape::Mixture, 0u},
    };

    SumByKey reducer{rsys->GetDeviceContext()};

    std::cout << "SumByKey isolated reduction benchmark" << std::endl;
    std::cout << "  capacity " << capacity << ", channels " << kChannels << ", iterations " << iterations << std::endl;
    std::cout << std::left << std::setw(26) << "case" << std::right << std::setw(8) << "keys" << std::setw(9)
              << "longest" << std::setw(11) << "total ms" << std::setw(12) << "ms/iter" << std::setw(12) << "ns/elem"
              << std::setw(10) << "out[0]" << std::endl;

    for (const BenchCase &bench : cases) {
        const std::vector<uint32_t> runs = BuildRuns(bench, capacity);
        // One spare output slot so the bound never depends on the last key's use.
        BenchCtx ctx = MakeCtx(*rsys, capacity, static_cast<uint32_t>(runs.size()) + 1u);
        FillFromRuns(ctx, runs);

        // Warm-up: loads the shader and builds the pipeline, untimed.
        RunCase(*rsys, reducer, ctx, 1u);
        const Result r = RunCase(*rsys, reducer, ctx, iterations);

        uint32_t longest = 0u;
        for (uint32_t len : runs) longest = std::max(longest, len);
        const double ns_per_elem = r.per_iter_ms * 1e6 / static_cast<double>(capacity);

        std::cout << std::left << std::setw(26) << bench.name << std::right << std::setw(8) << runs.size()
                  << std::setw(9) << longest << std::setw(11) << std::fixed << std::setprecision(3) << r.total_ms
                  << std::setw(12) << std::setprecision(4) << r.per_iter_ms << std::setw(12) << std::setprecision(3)
                  << ns_per_elem << std::setw(10) << std::setprecision(1) << r.out0 << std::endl;
    }

    rsys->WaitForIdle();
    return 0;
}
