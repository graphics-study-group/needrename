#include "Rhi/Resource/DescriptorArena.h"

#include "Rhi/Device/DebugUtils.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/Hasher.hpp"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Submission/EpochTracker.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine::Rhi {
    namespace {
        /// @brief Sets a pool is created to serve by default.
        constexpr uint32_t POOL_SET_CAPACITY = 64;
        /// @brief Upper bound on the descriptors of one type a single pool declares.
        constexpr uint32_t POOL_MAX_DESCRIPTORS_PER_TYPE = 4096;

        /// @brief Descriptors of each type one set of a layout consumes.
        using DescriptorRequirements = std::map<vk::DescriptorType, uint32_t>;

        DescriptorRequirements RequirementsOf(const vk::DescriptorSetLayoutCreateInfo &layout) {
            DescriptorRequirements requirements;
            for (uint32_t i = 0; i < layout.bindingCount; i++) {
                const auto &binding = layout.pBindings[i];
                requirements[binding.descriptorType] += binding.descriptorCount;
            }
            return requirements;
        }

        bool IsImageDescriptor(vk::DescriptorType type) noexcept {
            return type == vk::DescriptorType::eCombinedImageSampler || type == vk::DescriptorType::eStorageImage
                   || type == vk::DescriptorType::eSampledImage || type == vk::DescriptorType::eSampler;
        }

        /// @brief Compare two Vulkan-Hpp handles by native value; the handles
        /// themselves carry no `operator==` under typesafe conversion.
        template <typename T>
        bool SameHandle(T lhs, T rhs) noexcept {
            return static_cast<typename T::CType>(lhs) == static_cast<typename T::CType>(rhs);
        }

        uint64_t HandleValue(vk::DescriptorSetLayout layout) noexcept {
            return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(layout));
        }
    } // namespace

    bool ResolvedBinding::operator==(const ResolvedBinding &other) const noexcept {
        return binding == other.binding && type == other.type && image_layout == other.image_layout
               && offset == other.offset && range == other.range && SameHandle(image_view, other.image_view)
               && SameHandle(sampler, other.sampler) && SameHandle(buffer, other.buffer);
    }

    struct DescriptorArena::impl {
        /// @brief A pool created for one layout, plus the accounting that decides
        /// whether a request can be served without growing.
        struct Pool {
            vk::UniqueDescriptorPool handle{};
            uint32_t set_capacity{0};
            uint32_t sets_used{0};
        };

        /// @brief An unclaimed entry is one acquired while no epoch was open.
        /// A distinct state, never the zero watermark: zero compares as "already
        /// complete" and would make the entry evictable immediately.
        static constexpr EpochWatermark UNCLAIMED = 0;
        static_assert(
            UNCLAIMED == INVALID_EPOCH_WATERMARK,
            "The unclaimed state shares the invalid watermark's value but not its meaning: an entry is unclaimed "
            "because no epoch has opened yet, not because it is complete."
        );

        /// @brief Cache key: layout handle, dynamic-offset flags and set index,
        /// plus a hash of the resolved binding content. The full content is kept
        /// on the entry so equality is exact rather than hash-based.
        struct EntryKey {
            vk::DescriptorSetLayout layout{};
            uint32_t set_id{0};
            uint32_t flags{0};
            size_t content_hash{0};

            bool operator==(const EntryKey &other) const noexcept {
                return set_id == other.set_id && flags == other.flags && content_hash == other.content_hash
                       && SameHandle(layout, other.layout);
            }
        };

        struct EntryKeyHash {
            size_t operator()(const EntryKey &key) const noexcept {
                RenderResourceHasher h;
                h.handle(key.layout);
                h.u32(key.set_id);
                h.u32(key.flags);
                h.u64(key.content_hash);
                return h.get();
            }
        };

        struct Entry {
            vk::DescriptorSet set{};
            /// @brief Pool this set was allocated from; releases return there.
            Pool *pool{nullptr};
            /// @brief The epochs that can reference this set; `UNCLAIMED` while none is known.
            EpochWatermark last_epoch{UNCLAIMED};
            /// @brief Monotonic acquisition counter, the eviction order.
            uint64_t acquisition_order{0};
            /// @brief The content the entry was written with; its identity.
            std::vector<ResolvedBinding> content{};
        };

        DeviceContext *device_context{nullptr};

        std::vector<std::unique_ptr<Pool>> pools{};
        /// @brief The pool currently serving each layout, so growth is per layout.
        std::unordered_map<uint64_t, Pool *> serving_pool{};

        std::unordered_map<EntryKey, Entry, EntryKeyHash> resident{};

        size_t resident_budget{DEFAULT_RESIDENT_BUDGET};
        uint64_t acquisition_clock{0};
        size_t raw_set_count{0};
        size_t minted_set_count{0};
        size_t pool_serial{0};
        /// @brief The largest watermark known to be settled by a device-idle wait.
        EpochWatermark idle_watermark{INVALID_EPOCH_WATERMARK};
        /// @brief The largest watermark the arena has already used for claiming.
        EpochWatermark claimed_through{INVALID_EPOCH_WATERMARK};

        vk::Device Device() const noexcept {
            return device_context->GetDevice();
        }
        ImmutableResourceCache &Cache() const noexcept {
            return device_context->GetIRCache();
        }
        EpochTracker &Tracker() const noexcept {
            return device_context->GetEpochTracker();
        }

        static size_t HashContent(std::span<const ResolvedBinding> content) noexcept {
            RenderResourceHasher h;
            h.u32(static_cast<uint32_t>(content.size()));
            for (const auto &entry : content) {
                h.u32(entry.binding);
                h.e(entry.type);
                h.handle(entry.image_view);
                h.handle(entry.sampler);
                h.e(entry.image_layout);
                h.handle(entry.buffer);
                h.u64(entry.offset);
                h.u64(entry.range);
            }
            return h.get();
        }

        Pool *CreatePool(const DescriptorRequirements &requirements, std::string_view name) {
            auto pool = std::make_unique<Pool>();

            // Size the pool for as many sets as a single set's descriptor count
            // allows, so a layout that binds large arrays is still serviceable.
            uint32_t capacity = POOL_SET_CAPACITY;
            for (const auto &[type, count] : requirements) {
                if (count == 0) continue;
                capacity = std::min(capacity, std::max(1u, POOL_MAX_DESCRIPTORS_PER_TYPE / count));
            }
            pool->set_capacity = capacity;

            std::vector<vk::DescriptorPoolSize> sizes;
            sizes.reserve(requirements.size());
            for (const auto &[type, count] : requirements) {
                if (count == 0) continue;
                sizes.emplace_back(type, count * capacity);
            }

            pool->handle = Device().createDescriptorPoolUnique(
                vk::DescriptorPoolCreateInfo{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, capacity, sizes}
            );
            DEBUG_SET_NAME_TEMPLATE(
                Device(), pool->handle.get(), std::format("Descriptor Pool - {} #{}", name, pool_serial++)
            );

            pools.push_back(std::move(pool));
            return pools.back().get();
        }

        /// @brief Pick the pool that serves a layout, growing the arena when the
        /// current one cannot satisfy the request.
        Pool *SelectPool(
            vk::DescriptorSetLayout layout,
            const DescriptorRequirements &requirements,
            std::string_view name,
            bool force_new
        ) {
            if (!force_new) {
                auto itr = serving_pool.find(HandleValue(layout));
                if (itr != serving_pool.end() && itr->second->sets_used < itr->second->set_capacity) {
                    return itr->second;
                }
            }
            Pool *pool = CreatePool(requirements, name);
            serving_pool[HandleValue(layout)] = pool;
            return pool;
        }

        /// @brief A set together with the pool that served it, which is where a
        /// later release returns it.
        struct Allocation {
            Pool *pool{nullptr};
            vk::DescriptorSet set{};
        };

        Allocation AllocateSet(
            vk::DescriptorSetLayout layout, const DescriptorRequirements &requirements, std::string_view name
        ) {
            auto allocate_from = [this, layout](Pool *pool) {
                auto set =
                    Device().allocateDescriptorSets(vk::DescriptorSetAllocateInfo{pool->handle.get(), {layout}})[0];
                pool->sets_used++;
                return Allocation{pool, set};
            };

            // The pool accounting says the request fits; if the driver disagrees,
            // grow once against a fresh pool before giving up.
            try {
                return allocate_from(SelectPool(layout, requirements, name, false));
            } catch (const vk::OutOfPoolMemoryError &) {
                return allocate_from(SelectPool(layout, requirements, name, true));
            }
        }

        static void NameSet(vk::Device device, vk::DescriptorSet set, const std::string &name) {
            DEBUG_SET_NAME_TEMPLATE(device, set, name);
        }

        /// @brief Claim every unclaimed entry with the first epoch that opened
        /// after the last one the arena observed.
        void SyncEpochs() {
            const EpochWatermark newest = Tracker().GetNewestWatermark();
            if (newest <= claimed_through) return;
            const EpochWatermark claimant = claimed_through + 1;
            for (auto &[key, entry] : resident) {
                if (entry.last_epoch == UNCLAIMED) entry.last_epoch = claimant;
            }
            claimed_through = newest;
        }

        EpochWatermark EffectivePrefix() const noexcept {
            return std::max(Tracker().GetCompletedPrefix(), idle_watermark);
        }

        bool IsEligible(const Entry &entry) const noexcept {
            return entry.last_epoch != UNCLAIMED && entry.last_epoch <= EffectivePrefix();
        }

        /// @brief The epoch a newly acquired or refreshed entry records, or
        /// `UNCLAIMED` when no epoch is open.
        EpochWatermark CurrentEpoch() const noexcept {
            const EpochWatermark newest = Tracker().GetNewestWatermark();
            return newest == INVALID_EPOCH_WATERMARK ? UNCLAIMED : newest;
        }

        /// @brief Release the least recently acquired eligible entries until the
        /// resident count is at `target`, or until none is eligible.
        ///
        /// @param protect An entry never chosen for release: the one the current
        /// call is about to return.
        void EvictDownTo(size_t target, const Entry *protect = nullptr) {
            while (resident.size() > target) {
                auto oldest = resident.end();
                for (auto itr = resident.begin(); itr != resident.end(); ++itr) {
                    if (&itr->second == protect) continue;
                    if (!IsEligible(itr->second)) continue;
                    if (oldest == resident.end() || itr->second.acquisition_order < oldest->second.acquisition_order) {
                        oldest = itr;
                    }
                }
                if (oldest == resident.end()) return; // every entry is still referenced
                FreeEntry(oldest->second);
                resident.erase(oldest);
            }
        }

        void FreeEntry(const Entry &entry) {
            Device().freeDescriptorSets(entry.pool->handle.get(), entry.set);
            assert(entry.pool->sets_used > 0);
            entry.pool->sets_used--;
        }

        void WriteDescriptors(vk::DescriptorSet set, std::span<const ResolvedBinding> content) {
            std::vector<vk::DescriptorImageInfo> image_infos;
            std::vector<vk::DescriptorBufferInfo> buffer_infos;
            std::vector<vk::WriteDescriptorSet> writes;
            image_infos.reserve(content.size());
            buffer_infos.reserve(content.size());
            writes.reserve(content.size());

            for (const auto &binding : content) {
                if (IsImageDescriptor(binding.type)) {
                    image_infos.emplace_back(binding.sampler, binding.image_view, binding.image_layout);
                } else {
                    buffer_infos.emplace_back(binding.buffer, binding.offset, binding.range);
                }
                writes.emplace_back(set, binding.binding, 0u, 1u, binding.type);
            }

            // `updateDescriptorSets` consumes the infos synchronously, so the
            // pointers stay valid for the duration of the call.
            size_t image_index{0}, buffer_index{0};
            for (size_t i = 0; i < writes.size(); i++) {
                if (IsImageDescriptor(content[i].type)) {
                    writes[i].setPImageInfo(&image_infos[image_index++]);
                } else {
                    writes[i].setPBufferInfo(&buffer_infos[buffer_index++]);
                }
            }

            Device().updateDescriptorSets(writes, {});
        }
    };

    DescriptorArena::DescriptorArena(DeviceContext &device_context) : pimpl(std::make_unique<impl>()) {
        pimpl->device_context = &device_context;
    }

    DescriptorArena::~DescriptorArena() = default;

    vk::DescriptorSetLayout DescriptorArena::ResolveLayout(
        const vk::DescriptorSetLayoutCreateInfo &layout, const char *name
    ) {
        return pimpl->Cache().GetDescriptorSetLayout(layout, name);
    }

    vk::DescriptorSet DescriptorArena::AcquireRawSet(
        const vk::DescriptorSetLayoutCreateInfo &layout, std::string_view name
    ) {
        const auto resolved = ResolveLayout(layout);
        const auto requirements = RequirementsOf(layout);
        const std::string debug_name = name.empty() ? std::string{"Arena Raw Set"} : std::string{name};

        auto set = pimpl->AllocateSet(resolved, requirements, debug_name).set;
        pimpl->raw_set_count++;
        impl::NameSet(pimpl->Device(), set, debug_name);
        return set;
    }

    vk::DescriptorSet DescriptorArena::Acquire(
        const vk::DescriptorSetLayoutCreateInfo &layout,
        uint32_t set_id,
        bool enforce_dynamic_uniform,
        bool enforce_dynamic_storage,
        std::span<const ResolvedBinding> content
    ) {
        // Claim anything acquired before an epoch existed, and read the prefix
        // afresh: nothing registers with the tracker, both are demand-driven.
        pimpl->SyncEpochs();

        const auto resolved = ResolveLayout(layout);
        const impl::EntryKey key{
            .layout = resolved,
            .set_id = set_id,
            .flags = (enforce_dynamic_uniform ? 1u : 0u) | (enforce_dynamic_storage ? 2u : 0u),
            .content_hash = impl::HashContent(content),
        };

        auto same_content = [content](const impl::Entry &entry) {
            return entry.content.size() == content.size()
                   && std::equal(entry.content.begin(), entry.content.end(), content.begin());
        };

        auto itr = pimpl->resident.find(key);
        if (itr != pimpl->resident.end() && same_content(itr->second)) {
            // A hit is the normal case: in steady state the arena mints nothing.
            itr->second.last_epoch = pimpl->CurrentEpoch();
            itr->second.acquisition_order = ++pimpl->acquisition_clock;
            pimpl->EvictDownTo(pimpl->resident_budget, &itr->second);
            return itr->second.set;
        }

        // Make room for one more entry before minting it, so the resident count
        // never exceeds the budget while an eligible entry is available.
        pimpl->EvictDownTo(pimpl->resident_budget > 0 ? pimpl->resident_budget - 1 : 0);

        const auto allocation = pimpl->AllocateSet(resolved, RequirementsOf(layout), "Arena Cache Set");

        impl::Entry entry{};
        entry.set = allocation.set;
        entry.pool = allocation.pool;
        entry.last_epoch = pimpl->CurrentEpoch();
        entry.acquisition_order = ++pimpl->acquisition_clock;
        entry.content.assign(content.begin(), content.end());

        pimpl->WriteDescriptors(entry.set, content);
        pimpl->minted_set_count++;

        auto [inserted, ok] = pimpl->resident.emplace(key, std::move(entry));
        assert(ok);
        (void)ok;
        impl::NameSet(pimpl->Device(), inserted->second.set, std::format("Arena Cache Set (set {})", set_id));
        return inserted->second.set;
    }

    void DescriptorArena::OnDeviceIdle() noexcept {
        pimpl->idle_watermark = std::max(pimpl->idle_watermark, pimpl->Tracker().GetNewestWatermark());
    }

    void DescriptorArena::SetResidentBudget(size_t budget) noexcept {
        pimpl->resident_budget = budget;
    }

    size_t DescriptorArena::GetResidentBudget() const noexcept {
        return pimpl->resident_budget;
    }

    size_t DescriptorArena::GetLivePoolCount() const noexcept {
        return pimpl->pools.size();
    }

    size_t DescriptorArena::GetLiveSetCount() const noexcept {
        return pimpl->resident.size() + pimpl->raw_set_count;
    }

    size_t DescriptorArena::GetResidentEntryCount() const noexcept {
        return pimpl->resident.size();
    }

    size_t DescriptorArena::GetGuardedEntryCount() const noexcept {
        size_t count = 0;
        for (const auto &[key, entry] : pimpl->resident) {
            if (!pimpl->IsEligible(entry)) count++;
        }
        return count;
    }

    size_t DescriptorArena::GetMintedSetCount() const noexcept {
        return pimpl->minted_set_count;
    }

    size_t DescriptorArena::GetBudgetExcess() const noexcept {
        return pimpl->resident.size() > pimpl->resident_budget ? pimpl->resident.size() - pimpl->resident_budget : 0;
    }
} // namespace Engine::Rhi
