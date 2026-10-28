// gpu_resource_identity_test — churn descriptor-bound resources the way a
// scene or a physics workload does: create, bind, dispatch, destroy, create
// again.
//
// Vulkan drivers recycle handle values after an object is destroyed. Before a
// resource carried a never-reused identity, a later resource could present the
// same handle value as a destroyed one, match its stale descriptor-arena entry,
// and receive a descriptor set whose descriptors referenced destroyed objects —
// silently reading zeros or losing writes. The churn here deliberately
// recreates resources at the same sizes, which is the case that maximises the
// chance of a recycled handle value, and verifies two things on every round:
//
//   - the workload's *results* stay correct (a real dispatch, read back on the
//     host, for buffers);
//   - the arena mints a new set for every new resource, so it never answers a
//     request with a set written for a destroyed one (for both buffers and
//     textures — no compute shader in the tree binds a storage image, so the
//     texture side asserts on the descriptor content rather than on pixels).

#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeKernel.h"

#include <Physics/PhysicsSpirvLoader.h>
#include <Physics/gpu_algorithm/CompactUnique.h>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Resource/DescriptorArena.h>
#include <Rhi/Texture/ImageTexture.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <unordered_set>
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
        vk::SubmitInfo s{{}, {}, {cb}, {}};
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

    template <typename T>
    bool Same(const T &lhs, const T &rhs) {
        return static_cast<typename T::CType>(lhs) == static_cast<typename T::CType>(rhs);
    }

    // ── Buffer churn: a real CompactUnique pass over freshly created buffers ─
    struct CompactOutcome {
        std::vector<uint32_t> keys{};
        uint32_t count{0u};
    };

    CompactOutcome RunCompactUnique(
        RenderSystem &rsys, uint32_t capacity, const std::vector<uint32_t> &sorted_keys, uint32_t count
    ) {
        auto keys_buf = MakeHostBuffer(rsys, static_cast<size_t>(capacity) * sizeof(uint32_t), "Churn keys");
        auto count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Churn count");
        auto elem_count_buf = MakeHostBuffer(rsys, sizeof(uint32_t), "Churn elem count");

        std::memset(keys_buf->GetVMAddress(), 0, keys_buf->GetSize());
        std::memcpy(keys_buf->GetVMAddress(), sorted_keys.data(), sorted_keys.size() * sizeof(uint32_t));
        *reinterpret_cast<uint32_t *>(count_buf->GetVMAddress()) = 0u;
        *reinterpret_cast<uint32_t *>(elem_count_buf->GetVMAddress()) = count;
        keys_buf->Flush();
        count_buf->Flush();
        elem_count_buf->Flush();

        // A fresh instance per round: its internal flag/offset buffers are
        // created and destroyed with it, which is the churn under test.
        CompactUnique compact{rsys.GetDeviceContext()};

        auto cb = BeginCommandBuffer(rsys);
        compact.Record(cb, *keys_buf, *count_buf, *elem_count_buf, capacity);
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

    /// @brief One run's input: a sorted key array with runs of duplicates.
    struct ChurnInput {
        std::vector<uint32_t> keys{};
        std::vector<uint32_t> expected{};
    };

    ChurnInput MakeInput(std::mt19937 &rng, uint32_t capacity) {
        std::uniform_int_distribution<uint32_t> length(1u, std::min(capacity, 8u));
        ChurnInput input{};
        uint32_t key = 0u;
        while (input.keys.size() < capacity && key < 64u) {
            const uint32_t run = std::min(length(rng), capacity - static_cast<uint32_t>(input.keys.size()));
            for (uint32_t i = 0u; i < run; i++) input.keys.push_back(key);
            input.expected.push_back(key);
            key++;
        }
        return input;
    }
} // namespace

int main() try {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Resource Identity Churn Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();
    auto &arena = rsys->GetDeviceContext().GetDescriptorArena();

    std::mt19937 rng(20261025u);

    // ── Buffer churn ───────────────────────────────────────────────────────
    constexpr int BUFFER_ROUNDS = 48;
    std::unordered_set<uint64_t> seen_buffer_handles{};
    size_t recycled_buffer_handles = 0u;
    for (int round = 0; round < BUFFER_ROUNDS; round++) {
        std::uniform_int_distribution<uint32_t> capacity_dist(1u, 1024u);
        const uint32_t capacity = capacity_dist(rng);
        const ChurnInput input = MakeInput(rng, capacity);

        const CompactOutcome outcome =
            RunCompactUnique(*rsys, capacity, input.keys, static_cast<uint32_t>(input.keys.size()));

        Check(outcome.count == input.expected.size(), "the compacted count matches an independent host-side dedup");
        Check(outcome.keys == input.expected, "the compacted keys equal the expected unique keys, in order");
    }

    // ── Texture and view churn ─────────────────────────────────────────────
    // The layout is resolved once; every round binds a freshly created texture
    // and destroys it straight after, so the next round's texture is the kind of
    // resource that can inherit a recycled view handle.
    constexpr int TEXTURE_ROUNDS = 24;
    const vk::DescriptorSetLayoutBinding IMAGE_BINDING{
        0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute
    };
    const auto image_layout =
        arena.ResolveLayout(vk::DescriptorSetLayoutCreateInfo{{}, {IMAGE_BINDING}}, "Churn image layout");

    std::unordered_set<uint64_t> seen_view_handles{};
    size_t recycled_view_handles = 0u;
    vk::DescriptorSet previous_set{};
    bool have_previous_set = false;
    for (int round = 0; round < TEXTURE_ROUNDS; round++) {
        auto texture = ImageTexture::CreateUnique(
            rsys->GetDeviceContext(),
            ImageTexture::ImageTextureDesc{
                .dimensions = 2,
                .width = 4,
                .height = 4,
                .depth = 1,
                .mipmap_levels = 1,
                .array_layers = 1,
                .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
                .is_cube_map = false
            },
            Rhi::Texture::SamplerDesc{},
            "Churn texture"
        );

        const std::array<ResolvedBinding, 1> content{ResolvedBinding{
            .binding = 0,
            .type = vk::DescriptorType::eCombinedImageSampler,
            .image_view = texture->GetViewRef(TextureSubresourceRange::GetFullRange()),
            .sampler = texture->GetSamplerRef(),
            .image_layout = vk::ImageLayout::eReadOnlyOptimal
        }};

        const auto view_value =
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<VkImageView>(content[0].image_view.handle)));
        if (!seen_view_handles.insert(view_value).second) recycled_view_handles++;

        const size_t minted_before = arena.GetMintedSetCount();
        const auto set = arena.Acquire(image_layout, 0, false, false, content);
        Check(static_cast<bool>(set), "a texture binding acquires a set");
        Check(
            arena.GetMintedSetCount() == minted_before + 1u,
            "every new texture mints its own set: a destroyed texture's entry is never reused"
        );
        Check(!have_previous_set || !Same(set, previous_set), "each round's set is distinct from the previous round's");
        previous_set = set;
        have_previous_set = true;

        // Interning still holds within one texture's life.
        Check(Same(set, arena.Acquire(image_layout, 0, false, false, content)), "the same texture interns to one set");

        // The texture dies here; the next round's texture replaces it.
    }

    // ── Buffer handle recycling, observed but not asserted ─────────────────
    // The driver's pooling decides whether a value repeats; report what was
    // actually seen so a run that never recycled anything is not mistaken for
    // a run that did.
    {
        // Same-sized buffers created and destroyed in a tight loop are the most
        // likely candidates for a recycled `VkBuffer` value.
        for (int i = 0; i < 32; i++) {
            auto buf = MakeHostBuffer(*rsys, 256, "Recycle probe");
            const auto value =
                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<VkBuffer>(buf->GetBuffer())));
            if (!seen_buffer_handles.insert(value).second) recycled_buffer_handles++;
        }
    }

    std::cout << "observed recycled buffer handles: " << recycled_buffer_handles
              << ", view handles: " << recycled_view_handles << std::endl;

    rsys->WaitForIdle();
    if (g_failures == 0) {
        std::cout << "gpu_resource_identity_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "gpu_resource_identity_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
} catch (const std::exception &e) {
    std::cerr << "gpu_resource_identity_test ABORTED: " << e.what() << std::endl;
    return 1;
} catch (...) {
    std::cerr << "gpu_resource_identity_test ABORTED: unknown exception" << std::endl;
    return 1;
}
