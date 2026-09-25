// Tests for the device-scoped descriptor arena:
//   - the pool layer and on-demand pool growth (task 1.1, 5.3)
//   - the content-keyed cache layer and cross-epoch reuse (task 1.2, 5.1, 5.2)
//   - the epoch guard and the pinning rule for unclaimed sets (task 1.4, 5.4)
//   - pressure-driven eviction under a soft budget (task 1.5, 5.3)
//   - raw sets the caller writes (task 1.6)
//   - the device-idle broadcast (task 1.7, 5.6)
//   - material-path churn (task 5.5)
//
// Every scenario builds its own headless DeviceContext with no render system,
// which is also the "arena available without a render system" scenario, and
// leaves entries resident at teardown.

#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Pipeline/ShaderInterface.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Pipeline/ShaderResourceBinding.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Texture/ImageTexture.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

using namespace Engine;
using namespace Engine::Rhi;

static bool g_pass = true;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                                        \
            g_pass = false;                                                                                            \
        }                                                                                                              \
    } while (0)

namespace {
    /// @brief Handles carry no `operator==` under typesafe conversion.
    template <typename T>
    bool Same(T lhs, T rhs) {
        return static_cast<typename T::CType>(lhs) == static_cast<typename T::CType>(rhs);
    }

    /// @brief One dynamic uniform buffer binding, the shape a compute binding uses.
    const vk::DescriptorSetLayoutBinding DYNAMIC_UBO_BINDING{
        0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll
    };

    vk::DescriptorSetLayoutCreateInfo DynamicUboLayout() {
        return vk::DescriptorSetLayoutCreateInfo{vk::DescriptorSetLayoutCreateFlags{}, {DYNAMIC_UBO_BINDING}};
    }

    std::vector<ResolvedBinding> One(vk::Buffer buffer, size_t offset = 0) {
        return {ResolvedBinding{
            .binding = 0,
            .type = vk::DescriptorType::eUniformBufferDynamic,
            .buffer = buffer,
            .offset = offset,
            .range = 256
        }};
    }

    /// @brief A reflected layout holding one sampled image named "diffuse" in set 2.
    SPLayout ImageLayout() {
        SPLayout layout{};
        auto image = std::make_unique<SPInterfaceOpaqueImage>();
        image->name = "diffuse";
        image->layout_set = 2;
        image->layout_binding = 0;
        image->array_size = 0;
        image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::HasSampler);
        layout.interface_name_mapping["diffuse"] = image.get();
        layout.interfaces.push_back(std::move(image));
        return layout;
    }

    std::shared_ptr<ImageTexture> MakeTexture(DeviceContext &context, const char *name) {
        return std::shared_ptr<ImageTexture>(ImageTexture::CreateUnique(
            context,
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
            Texture::SamplerDesc{},
            name
        ));
    }

    /// @brief A headless device with the arena on it, plus buffers to bind.
    ///
    /// @note Declaration order matters: the allocations are destroyed before
    /// the context that owns their allocator.
    struct Fixture {
        DeviceContext context{DeviceInterface::DeviceConfiguration{
            .window = nullptr,
            .application_name = "Rhi::DescriptorArena Test",
            .application_version = 0,
            .dynamic_dispatcher = nullptr,
        }};
        std::vector<BufferAllocation> buffers{};

        explicit Fixture(size_t buffer_count = 40) {
            buffers.reserve(buffer_count);
            for (size_t i = 0; i < buffer_count; i++) {
                buffers.push_back(
                    context.GetAllocatorState().AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Arena test buffer")
                );
            }
        }

        DescriptorArena &Arena() {
            return context.GetDescriptorArena();
        }
        EpochTracker &Tracker() {
            return context.GetEpochTracker();
        }
        vk::Buffer Buffer(size_t index) {
            return buffers[index].GetBuffer();
        }

        /// @brief Resolve a layout description through this device's arena.
        vk::DescriptorSetLayout Resolve(const vk::DescriptorSetLayoutCreateInfo &layout) {
            return Arena().ResolveLayout(layout);
        }
    };
} // namespace

int main() {
    // Requires the SDL video subsystem for SDL_Vulkan_LoadLibrary(nullptr).
    SDL_Init(SDL_INIT_VIDEO);

    const auto layout_ci = DynamicUboLayout();

    // ── Task 1.1 and 1.6: the pool layer, and growth on demand ──────────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);

        const auto first = arena.AcquireRawSet(layout, "Raw A");
        const auto second = arena.AcquireRawSet(layout, "Raw B");
        CHECK(first && second && "the pool layer must hand out sets");
        CHECK(!Same(first, second) && "raw sets are never interned: the same layout yields distinct sets");

        const size_t pools_before = arena.GetLivePoolCount();
        size_t raw_count = 2;
        for (size_t i = 0; i < 100; i++) {
            CHECK(arena.AcquireRawSet(layout, "Raw growth") && "a request past one pool's capacity must succeed");
            raw_count++;
        }
        CHECK(
            arena.GetLivePoolCount() > pools_before
            && "the arena must create an additional pool rather than fail the request"
        );

        // Cache entries are released from the pool that served them, under
        // pressure; the raw sets allocated from the same pools are not.
        arena.SetResidentBudget(1);
        for (size_t i = 0; i < 8; i++) {
            const auto e = f.Tracker().BeginEpoch();
            arena.Acquire(layout, 0, true, false, One(f.Buffer(i)));
            f.Tracker().ReportComplete(e);
            arena.AcquireRawSet(layout, "Raw after release");
            raw_count++;
        }
        CHECK(
            arena.GetLiveSetCount() - arena.GetResidentEntryCount() == raw_count
            && "raw sets are never reclaimed, however much pressure is applied"
        );
        CHECK(arena.GetResidentEntryCount() <= 1u && "cache entries are reclaimed down to the budget");
        CHECK(first && second && "the first raw sets remain valid until the arena is destroyed");
    }

    // ── Task 1.2 and 5.1: interning, distinct content, cross-epoch reuse ────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        const auto content_a = One(f.Buffer(0));
        const auto content_b = One(f.Buffer(1));

        const auto e1 = f.Tracker().BeginEpoch();
        const auto first = arena.Acquire(layout, 0, true, false, content_a);
        const auto again = arena.Acquire(layout, 0, true, false, content_a);
        CHECK(Same(first, again) && "two requests in the same epoch with equal content must intern");
        CHECK(arena.GetMintedSetCount() == 1u && "interning must not mint a second set");

        const auto other = arena.Acquire(layout, 0, true, false, content_b);
        CHECK(!Same(first, other) && "different content must coexist as a distinct entry");
        CHECK(arena.GetMintedSetCount() == 2u && "a distinct content mints exactly one more set");
        CHECK(arena.GetResidentEntryCount() == 2u);

        // A resident entry is never rewritten: content that differs from it mints
        // a third entry rather than rewriting the resident one.
        const auto shifted = arena.Acquire(layout, 0, true, false, One(f.Buffer(1), 16));
        CHECK(!Same(shifted, first) && !Same(shifted, other) && "a differing content mints a distinct set");
        CHECK(arena.GetMintedSetCount() == 3u);
        CHECK(!Same(shifted, arena.Acquire(layout, 0, true, false, content_b)) && "the resident entry is untouched");
        f.Tracker().ReportComplete(e1);

        // 5.1 steady state: the same content in a later epoch reuses its entry.
        const auto e2 = f.Tracker().BeginEpoch();
        CHECK(Same(first, arena.Acquire(layout, 0, true, false, content_a)));
        CHECK(Same(other, arena.Acquire(layout, 0, true, false, content_b)));
        CHECK(arena.GetMintedSetCount() == 3u && "reuse across epochs must mint nothing");
        f.Tracker().ReportComplete(e2);
    }

    // ── Task 5.2: a fully serialised submitter still reuses its sets ────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        const auto content = One(f.Buffer(0));

        const auto e1 = f.Tracker().BeginEpoch();
        const auto set = arena.Acquire(layout, 0, true, false, content);
        f.Tracker().ReportComplete(e1);

        // Every later epoch completes before the next one opens, so no set is
        // ever re-acquired before its epoch completes.
        for (int i = 0; i < 8; i++) {
            const auto e = f.Tracker().BeginEpoch();
            const auto reused = arena.Acquire(layout, 0, true, false, content);
            f.Tracker().ReportComplete(e);
            CHECK(Same(set, reused) && "a fully serialised submitter must reuse its sets across epochs");
        }
        CHECK(arena.GetMintedSetCount() == 1u && "nothing is re-minted");
        CHECK(arena.GetResidentEntryCount() == 1u && "nothing is released merely because its epoch completed");
    }

    // ── Task 1.4 and 5.4: pinning an entry acquired outside an epoch ────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        const auto pinned_content = One(f.Buffer(0));

        // Recording with no epoch open: the command buffer binding this set has
        // not been submitted yet, so the entry must never be evictable.
        const auto pinned = arena.Acquire(layout, 0, true, false, pinned_content);
        arena.SetResidentBudget(1);
        for (size_t i = 1; i < 12; i++) {
            arena.Acquire(layout, 0, true, false, One(f.Buffer(i)));
        }
        CHECK(arena.GetBudgetExcess() > 0u && "the budget is a soft cap: the arena exceeds rather than violates it");
        CHECK(arena.GetGuardedEntryCount() == 12u && "every unclaimed entry is held back by the guard");
        CHECK(Same(pinned, arena.Acquire(layout, 0, true, false, pinned_content)) && "the pinned entry survived");

        // An epoch opening now claims every unclaimed entry. Only the prefix
        // passing that epoch makes them eligible: a report for a later epoch
        // proves nothing about it.
        const auto e1 = f.Tracker().BeginEpoch();
        const auto e2 = f.Tracker().BeginEpoch();
        f.Tracker().ReportComplete(e2);
        CHECK(f.Tracker().GetCompletedPrefix() < e1 && "the prefix cannot jump an unreported earlier epoch");
        arena.Acquire(layout, 0, true, false, One(f.Buffer(0)));
        CHECK(arena.GetResidentEntryCount() > 1u && "a claim alone does not make an entry eligible");
        CHECK(arena.GetBudgetExcess() > 0u);

        f.Tracker().ReportComplete(e1);
        arena.Acquire(layout, 0, true, false, One(f.Buffer(1)));
        CHECK(arena.GetBudgetExcess() == 0u && "the prefix passed the recorded epoch, so entries are reclaimable");
        CHECK(arena.GetResidentEntryCount() <= 1u);
    }

    // ── Task 5.4: consecutive record-then-submit rounds ─────────────────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        arena.SetResidentBudget(4);

        auto round = [&](size_t buffer) {
            const auto set = arena.Acquire(layout, 0, true, false, One(f.Buffer(buffer)));
            const auto e = f.Tracker().BeginEpoch();
            f.Tracker().ReportComplete(e);
            return set;
        };
        const auto first = round(0);
        const auto second = round(1);

        CHECK(first && second && "each recorded round obtains its set");
        CHECK(Same(first, round(0)) && "a repeated round reuses its set");
        CHECK(!Same(first, second) && "the rounds' sets stay distinct");
        CHECK(arena.GetBudgetExcess() == 0u && "nothing is released while the arena is within budget");
    }

    // ── Task 1.5 and 5.3: growth past the old budget, and eviction ──────────

    {
        Fixture f(300);
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        arena.SetResidentBudget(4);

        // 5.3 growth: far past the previous per-consumer budget of 128 sets, each
        // with distinct content, all under completed epochs.
        const size_t pools_before = arena.GetLivePoolCount();
        for (size_t i = 0; i < 260; i++) {
            const auto e = f.Tracker().BeginEpoch();
            CHECK(
                arena.Acquire(layout, 0, true, false, One(f.Buffer(i)))
                && "every acquisition past the old 128-set budget must succeed"
            );
            f.Tracker().ReportComplete(e);
        }
        CHECK(arena.GetMintedSetCount() == 260u && "each distinct content minted its own set");
        CHECK(arena.GetLivePoolCount() > pools_before && "the growth must have created additional pools");
        CHECK(arena.GetResidentEntryCount() <= 4u && "eligible entries are evicted down to the budget");
        CHECK(arena.GetBudgetExcess() == 0u);

        // 5.3 eviction with entries still outstanding.
        const auto e_open = f.Tracker().BeginEpoch();
        for (size_t i = 0; i < 10; i++) {
            arena.Acquire(layout, 0, true, false, One(f.Buffer(i)));
        }
        const size_t resident_over = arena.GetResidentEntryCount();
        CHECK(resident_over > 4u && "ineligible entries are not released");
        CHECK(arena.GetBudgetExcess() == resident_over - 4u && "the budget is exceeded, never violated");

        f.Tracker().ReportComplete(e_open);
        arena.Acquire(layout, 0, true, false, One(f.Buffer(0)));
        CHECK(
            arena.GetResidentEntryCount() <= 4u && "the resident count returns to the budget once the prefix advances"
        );
        CHECK(arena.GetBudgetExcess() == 0u);
    }

    // ── Task 5.4: a batch recorded but never submitted ──────────────────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        arena.SetResidentBudget(1);

        for (size_t i = 0; i < 10; i++) {
            arena.Acquire(layout, 0, true, false, One(f.Buffer(i)));
        }
        const size_t resident = arena.GetResidentEntryCount();
        CHECK(resident == 10u && "a never-submitted batch is pinned, not reclaimed");
        CHECK(arena.GetBudgetExcess() == 9u && "the excess is reported");
        // No epoch is ever opened, so nothing further claims them: they are
        // released only when the arena is destroyed.
        arena.Acquire(layout, 0, true, false, One(f.Buffer(0)));
        CHECK(arena.GetResidentEntryCount() == resident && "pressure alone never releases a pinned entry");
    }

    // ── Task 1.7 and 5.6: the device-idle broadcast ─────────────────────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        const auto content = One(f.Buffer(0));

        const auto e_old = f.Tracker().BeginEpoch();
        const auto before_idle = arena.Acquire(layout, 0, true, false, content);
        const size_t resident_before = arena.GetResidentEntryCount();

        f.context.WaitForIdle();
        CHECK(arena.GetResidentEntryCount() == resident_before && "an idle wait alone releases nothing");
        CHECK(Same(before_idle, arena.Acquire(layout, 0, true, false, content)) && "the entry survives and is reused");

        // The idle wait made everything issued so far eligible, so an over-budget
        // acquisition can now reclaim it.
        arena.SetResidentBudget(0);
        for (size_t i = 1; i < 4; i++) {
            arena.Acquire(layout, 0, true, false, One(f.Buffer(i)));
        }
        CHECK(arena.GetResidentEntryCount() <= 1u && "the idle wait made entries reclaimable by a later acquisition");
        f.Tracker().ReportComplete(e_old);

        // An entry acquired after the idle wait stays ineligible: the arena
        // recorded a watermark, not a flag.
        const auto e_new = f.Tracker().BeginEpoch();
        CHECK(arena.Acquire(layout, 0, true, false, One(f.Buffer(9))) && "the acquisition succeeds");
        CHECK(arena.GetGuardedEntryCount() >= 1u && "sets acquired after the idle wait stay ineligible");
        CHECK(arena.GetBudgetExcess() >= 1u && "the new entry is retained rather than released");
        f.Tracker().ReportComplete(e_new);
    }

    // ── Task 5.5: material-path churn, reclaimed lazily ─────────────────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        auto image_layout = ImageLayout();
        // The material-shaped set uses set 2, and its layout is resolved through
        // the same arena, exactly as MaterialLibrary does for a real material.
        const auto image_bindings = image_layout.GenerateLayoutBindings(2, true, false);
        const auto image_set_layout = arena.ResolveLayout(vk::DescriptorSetLayoutCreateInfo{{}, image_bindings});

        const size_t baseline = arena.GetResidentEntryCount();
        const size_t minted_before = arena.GetMintedSetCount();

        // Six "material instances", each binding a different texture and then
        // destroyed. The arena is never told: an entry stops being refreshed
        // when nobody requests its content any more.
        for (int i = 0; i < 6; i++) {
            const auto texture = MakeTexture(f.context, "Churn texture");
            const auto e = f.Tracker().BeginEpoch();
            {
                ShaderResourceBinding srb{arena};
                srb.BindTexture("diffuse", *texture);
                CHECK(
                    srb.GetDescriptorSet(2, image_set_layout, image_layout, true, false)
                    && "a material-shaped acquisition must succeed"
                );
            }
            f.Tracker().ReportComplete(e);
            // The instance and its texture die here; its entry stays resident.
        }
        CHECK(arena.GetMintedSetCount() == minted_before + 6u && "six distinct contents mint six sets");
        CHECK(
            arena.GetResidentEntryCount() == baseline + 6u
            && "reclamation is lazy by design: destroying the instances alone changes nothing"
        );

        // Only pressure, after the prefix has passed the entries, reclaims them.
        arena.SetResidentBudget(baseline);
        const auto e = f.Tracker().BeginEpoch();
        f.Tracker().ReportComplete(e);
        arena.Acquire(layout, 0, true, false, One(f.Buffer(0)));
        CHECK(
            arena.GetResidentEntryCount() <= baseline + 1u
            && "the live set count returns to its baseline once pressure is applied"
        );
    }

    // ── Task 1.7: teardown with entries still resident ──────────────────────

    {
        Fixture f;
        DescriptorArena &arena = f.Arena();
        const auto layout = f.Resolve(layout_ci);
        // No epoch is ever reported here, so these entries are still resident
        // when the arena is destroyed after the tracker.
        for (size_t i = 0; i < 8; i++) {
            const auto e = f.Tracker().BeginEpoch();
            arena.Acquire(layout, 0, true, false, One(f.Buffer(i)));
            (void)e;
        }
        f.context.WaitForIdle();
        CHECK(arena.GetResidentEntryCount() == 8u && "an idle wait does not discard the live cache");
    }

    if (!g_pass) {
        std::cerr << "Rhi::DescriptorArena test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Rhi::DescriptorArena test PASSED." << std::endl;
    return 0;
}
