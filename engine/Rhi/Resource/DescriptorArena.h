#ifndef ENGINE_RHI_DESCRIPTORARENA_INCLUDED
#define ENGINE_RHI_DESCRIPTORARENA_INCLUDED

#include "Rhi/rhi_export.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    class DeviceContext;

    /**
     * @brief One resource resolved onto a descriptor-set binding.
     *
     * Produced by whoever knows the shader interface — today
     * `ShaderResourceBinding` maps names onto the reflected layout — and handed
     * to the arena, which turns the entries into descriptor writes when it
     * creates an entry. The arena never resolves names itself: it can only key
     * on content it produced.
     */
    struct RHI_API ResolvedBinding {
        /// @brief Binding number within the descriptor set.
        uint32_t binding{0};
        /// @brief Descriptor type the binding is declared with.
        vk::DescriptorType type{vk::DescriptorType::eUniformBuffer};
        /// @brief Image view, for image bindings.
        vk::ImageView image_view{};
        /// @brief Sampler, for combined-image-sampler bindings.
        vk::Sampler sampler{};
        /// @brief Layout the sampled image is in.
        vk::ImageLayout image_layout{vk::ImageLayout::eReadOnlyOptimal};
        /// @brief Buffer handle, for buffer bindings.
        vk::Buffer buffer{};
        /// @brief Byte offset into the buffer.
        size_t offset{0};
        /// @brief Byte range of the buffer binding (`vk::WholeSize` for all of it).
        size_t range{std::numeric_limits<size_t>::max()};

        /// @note Handles are compared by native value: Vulkan-Hpp's handles have
        /// no `operator==` when typesafe conversion is enabled.
        bool operator==(const ResolvedBinding &other) const noexcept;
    };

    /**
     * @brief A descriptor set together with the data needed to bind it.
     *
     * Returning the handle inside a call result is what makes the arena's
     * caller contract structural: a caller is never handed a set it can store
     * for a later epoch.
     */
    struct RHI_API DescriptorSetBinding {
        vk::DescriptorSet set{};
        /// @brief Dynamic offsets, in the order of the set's dynamic bindings.
        std::vector<uint32_t> dynamic_offsets{};
    };

    /**
     * @brief Device-scoped owner of every descriptor pool and source of every
     * descriptor set the engine acquires for its own bindings.
     *
     * It offers two services over one pool bookkeeping:
     *
     * - the **pool layer** (`AcquireRawSet`) allocates a set for a layout and
     *   hands it to a caller that writes the descriptors itself. Such a set is
     *   never interned and never reclaimed by the arena, and the caller must not
     *   rewrite it while a command buffer that binds it may still be executing;
     *
     * - the **cache layer** (`Acquire`) keeps sets resident, keyed by the
     *   descriptor-set layout, the dynamic-offset flags and set index, and the
     *   resolved binding content, and reuses them across submission epochs. It
     *   builds the descriptor writes itself and never rewrites a resident
     *   entry, because an entry's identity is its content.
     *
     * Reclamation is the cache layer's only release path and has exactly one
     * trigger: the resident count exceeding a soft budget. Among the entries
     * the epoch guard makes **eligible** — claimed by an epoch and recorded at
     * or below the completed prefix read from the retirement facility — the
     * least recently acquired are released. An entry still referenced by an
     * outstanding epoch is never released: the arena exceeds its budget rather
     * than corrupting an in-flight submission.
     *
     * Sets acquired while no epoch is open are **unclaimed** and never
     * evictable, because the command buffer that bound them has not been
     * submitted yet. The first epoch to open afterwards claims them.
     *
     * @note Not thread-safe, like the rest of the device facilities.
     */
    class RHI_API DescriptorArena {
    public:
        /**
         * @brief Default number of resident cache entries tolerated before the
         * arena reclaims.
         *
         * A safety bound rather than a tuning knob: in steady state the binding
         * content of a kernel or a material is stable, so the resident count
         * stays far below it.
         */
        static constexpr size_t DEFAULT_RESIDENT_BUDGET = 4096;

        /**
         * @brief Create the arena for a device context.
         *
         * The context must outlive the arena; the arena reads its device, its
         * immutable resource cache and its epoch tracker.
         */
        explicit DescriptorArena(DeviceContext &device_context);
        ~DescriptorArena();

        DescriptorArena(const DescriptorArena &) = delete;
        DescriptorArena &operator=(const DescriptorArena &) = delete;

        /**
         * @brief Resolve a descriptor-set layout through the immutable resource cache.
         *
         * Equal layout descriptions resolve to one layout object, which is what
         * makes a layout handle usable as a cache key: it is content-addressed
         * and never released, unlike a reflected-layout object's address.
         */
        vk::DescriptorSetLayout ResolveLayout(
            const vk::DescriptorSetLayoutCreateInfo &layout, const char *name = nullptr
        );

        /**
         * @brief Pool layer: allocate a set for a layout, without knowing its contents.
         *
         * The returned set is neither interned nor reclaimed: the arena cannot
         * observe whether the caller still holds the handle, nor the last epoch
         * that bound it. It stays valid until the arena is destroyed, and there
         * is deliberately no release entry point.
         *
         * @param name Debug name applied to the pool and the set.
         */
        vk::DescriptorSet AcquireRawSet(const vk::DescriptorSetLayoutCreateInfo &layout, std::string_view name = {});

        /**
         * @brief Cache layer: return the set for a content, writing it on a miss.
         *
         * A repeated request of the same key returns the same set, including
         * across epoch boundaries, and refreshes the epoch recorded for it. A
         * request whose content differs from a resident entry's mints a distinct
         * entry rather than rewriting the resident one.
         *
         * @param content The resolved bindings the arena writes on a miss.
         */
        vk::DescriptorSet Acquire(
            const vk::DescriptorSetLayoutCreateInfo &layout,
            uint32_t set_id,
            bool enforce_dynamic_uniform,
            bool enforce_dynamic_storage,
            std::span<const ResolvedBinding> content
        );

        /**
         * @brief Record that everything issued so far has completed.
         *
         * Called after a device-wide idle wait, which proves more than any
         * single report. It records the newest watermark as the idle watermark
         * and releases nothing: an idle wait makes entries eligible, but
         * reclamation still requires budget pressure. Recording a watermark
         * rather than a flag is what keeps an entry acquired after the wait
         * ineligible.
         */
        void OnDeviceIdle() noexcept;

        /// @brief Set the soft resident budget.
        void SetResidentBudget(size_t budget) noexcept;
        /// @brief Get the soft resident budget.
        size_t GetResidentBudget() const noexcept;
        /// @brief Get the number of live pools.
        size_t GetLivePoolCount() const noexcept;
        /// @brief Get the number of live sets (resident entries and raw sets).
        size_t GetLiveSetCount() const noexcept;
        /// @brief Get the number of resident cache entries.
        size_t GetResidentEntryCount() const noexcept;
        /// @brief Get the number of resident entries the epoch guard holds back.
        size_t GetGuardedEntryCount() const noexcept;
        /// @brief Get the cumulative number of sets the cache layer has minted.
        size_t GetMintedSetCount() const noexcept;
        /// @brief Get how far the resident count exceeds the budget (0 when within it).
        size_t GetBudgetExcess() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_DESCRIPTORARENA_INCLUDED
