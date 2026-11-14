#include "Rhi/Resource/DescriptorArena.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/BindingRef.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/Hasher.hpp"
#include "Rhi/Resource/ImmutableResourceCache.h"

#include <SDL3/SDL.h>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /// @brief What one set of a layout consumes, per descriptor type.
        ///
        /// The Vulkan arena sizes a pool from this. There is no pool here, so the
        /// ledger exists only because the seam's `ResolveLayout` documents that it
        /// remembers it - and a future pool would read it.
        using DescriptorRequirements = std::unordered_map<DescriptorType, uint32_t>;

        DescriptorRequirements RequirementsOf(const DescriptorSetLayout &layout) {
            DescriptorRequirements requirements;
            for (const DescriptorBinding &binding : layout) {
                requirements[binding.type] += binding.count;
            }
            return requirements;
        }

        size_t HashContent(std::span<const ResolvedBinding> content) noexcept {
            RenderResourceHasher h;
            for (const ResolvedBinding &binding : content) {
                h.u32(binding.binding);
                h.u32(static_cast<uint32_t>(binding.type));
                h.u32(static_cast<uint32_t>(binding.image_view.id.GetValue()));
                h.u32(static_cast<uint32_t>(binding.sampler.id.GetValue()));
                h.u32(static_cast<uint32_t>(binding.buffer.id.GetValue()));
                h.u32(static_cast<uint32_t>(binding.offset));
                h.u32(static_cast<uint32_t>(binding.range));
            }
            return h.get();
        }
    } // namespace

    /**
     * @brief The bind-group half of this backend's resource cache.
     *
     * Two of the three layers the Vulkan arena has do not exist here. There is no pool
     * to sub-allocate from, and a set cannot be allocated empty and written later: a
     * `WGPUBindGroup` is created with its contents. So `AcquireRawSet` reports that it
     * has nothing to give, and `Acquire` - the content-keyed layer - is the one that
     * works.
     *
     * `ResolveLayout` delegates to the resource cache rather than creating a layout
     * itself, and that is forced rather than chosen: `BindingRef`'s constructor is
     * private to the RHI wrappers, of which the cache is one and this arena is not.
     */
    struct DescriptorArena::impl {
        WGPUDevice device{nullptr};
        ImmutableResourceCache *cache{nullptr};
        std::unordered_map<uint64_t, DescriptorRequirements> requirements_by_layout{};
        /// The layout description each resolved layout was built from, so a bind group can be
        /// assembled from content with the same shape the layout declares.
        std::unordered_map<uint64_t, DescriptorSetLayout> layout_by_id{};
        /// Bind groups by (layout identity, set id, content hash).
        std::unordered_map<size_t, WGPUBindGroup> bind_groups{};
        std::unordered_map<size_t, uint64_t> bind_group_ids{};
        /// How many bind groups this arena has created. A repeated content mints nothing, which
        /// is what makes reuse observable rather than assumed.
        size_t minted{0};
        size_t resident_budget{0};
    };

    DescriptorArena::DescriptorArena(DeviceContext &device_context) : pimpl(std::make_unique<impl>()) {
        pimpl->device = ToWgpuDevice(device_context.GetDevice());
        pimpl->cache = &device_context.GetIRCache();
    }

    DescriptorArena::~DescriptorArena() {
        for (auto &[key, group] : pimpl->bind_groups) {
            static_cast<void>(key);
            if (group != nullptr) {
                wgpuBindGroupRelease(group);
            }
        }
    }

    DescriptorSetLayoutRef DescriptorArena::ResolveLayout(const DescriptorSetLayout &layout, const char *name) {
        const auto resolved = pimpl->cache->GetDescriptorSetLayoutRef(layout, name);
        pimpl->requirements_by_layout[resolved.id.GetValue()] = RequirementsOf(layout);
        pimpl->layout_by_id[resolved.id.GetValue()] = layout;
        return resolved;
    }

    DescriptorSetHandle DescriptorArena::AcquireRawSet(DescriptorSetLayoutRef, std::string_view) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: AcquireRawSet has no meaning on this backend. A bind group is created "
            "with its contents rather than allocated empty and written later, so use Acquire."
        );
        return {};
    }

    DescriptorSetHandle DescriptorArena::Acquire(
        DescriptorSetLayoutRef layout, uint32_t set_id, bool, bool, std::span<const ResolvedBinding> content
    ) {
        // The shape a caller passes is the one it would bind with, so the key is the
        // layout identity, the set index and the content - and identical requests
        // return one bind group, which is what a per-frame rewrite relies on.
        const size_t key = static_cast<size_t>(layout.id.GetValue()) * 1315423911u ^ (static_cast<size_t>(set_id) << 8)
                           ^ HashContent(content);
        if (auto itr = pimpl->bind_groups.find(key); itr != pimpl->bind_groups.end()) {
            return FromWgpuBindGroup(itr->second);
        }

        std::vector<WGPUBindGroupEntry> entries;
        entries.reserve(content.size());
        const DescriptorSetLayout *description = nullptr;
        if (auto itr = pimpl->layout_by_id.find(layout.id.GetValue()); itr != pimpl->layout_by_id.end()) {
            description = &itr->second;
        }
        for (const ResolvedBinding &binding : content) {
            WGPUBindGroupEntry entry{};
            entry.binding = binding.binding;
            switch (binding.type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::UniformBufferDynamic:
            case DescriptorType::StorageBuffer:
            case DescriptorType::StorageBufferDynamic:
                if (binding.buffer.handle.IsValid() == false) {
                    SDL_LogError(
                        SDL_LOG_CATEGORY_RENDER, "WebGPU: binding %u of set %u has no buffer.", binding.binding, set_id
                    );
                    return {};
                }
                entry.buffer = ToWgpuBuffer(binding.buffer.handle);
                entry.offset = binding.offset;
                // The seam's "maximum size means all of it" is the standard interface's
                // WGPU_WHOLE_SIZE.
                entry.size = binding.range == std::numeric_limits<size_t>::max() ? WGPU_WHOLE_SIZE : binding.range;
                break;
            case DescriptorType::CombinedImageSampler: {
                // One seam binding is two entries here: the texture at the binding the shader
                // names, and its sampler at the split slot the layout reserved. The sampler
                // comes from the bound texture; a layout with an immutable sampler uses that
                // instead, which is the closest thing this interface has to one.
                uint32_t sampler_slot = SplitSamplerBinding(binding.binding);
                SamplerHandle sampler = binding.sampler.handle;
                if (description != nullptr) {
                    for (const DescriptorBinding &declared : *description) {
                        if (declared.binding != binding.binding) continue;
                        if (declared.sampler_binding != std::numeric_limits<uint32_t>::max()) {
                            sampler_slot = declared.sampler_binding;
                        }
                        if (!sampler.IsValid()) sampler = declared.immutable_sampler;
                        break;
                    }
                }
                entry.textureView = ToWgpuTextureView(binding.image_view.handle);
                entries.push_back(entry);

                WGPUBindGroupEntry sampler_entry{};
                sampler_entry.binding = sampler_slot;
                sampler_entry.sampler = ToWgpuSampler(sampler);
                entries.push_back(sampler_entry);
                continue;
            }
            case DescriptorType::SampledImage:
                entry.textureView = ToWgpuTextureView(binding.image_view.handle);
                break;
            case DescriptorType::Sampler:
                entry.sampler = ToWgpuSampler(binding.sampler.handle);
                break;
            case DescriptorType::StorageImage:
                entry.textureView = ToWgpuTextureView(binding.image_view.handle);
                break;
            }
            entries.push_back(entry);
        }

        WGPUBindGroupDescriptor desc{};
        desc.layout = ToWgpuBindGroupLayout(layout.handle);
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        for (const WGPUBindGroupEntry &entry : entries) {
        }

        WGPUBindGroup group = wgpuDeviceCreateBindGroup(pimpl->device, &desc);
        if (group == nullptr) {
            return {};
        }
        pimpl->bind_groups.emplace(key, group);
        pimpl->minted++;
        return FromWgpuBindGroup(group);
    }

    void DescriptorArena::OnDeviceIdle() noexcept {
        // No pool and nothing parked: a bind group is one device object, and it stays
        // because the cache layer is content-keyed rather than frame-keyed.
    }

    void DescriptorArena::SetResidentBudget(size_t budget) noexcept {
        pimpl->resident_budget = budget;
    }

    size_t DescriptorArena::GetResidentBudget() const noexcept {
        return pimpl->resident_budget;
    }

    size_t DescriptorArena::GetResidentEntryCount() const noexcept {
        // One resident entry per distinct content: this backend has no pool layer and no epoch
        // guard, so the cache layer is the whole of what it keeps.
        return pimpl->bind_groups.size();
    }

    size_t DescriptorArena::GetMintedSetCount() const noexcept {
        return pimpl->minted;
    }
} // namespace Engine::Rhi
