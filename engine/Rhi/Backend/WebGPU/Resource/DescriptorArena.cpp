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
        /// Bind groups by (layout identity, set id, content hash).
        std::unordered_map<size_t, WGPUBindGroup> bind_groups{};
        std::unordered_map<size_t, uint64_t> bind_group_ids{};
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
        DescriptorSetLayoutRef layout,
        uint32_t set_id,
        bool,
        bool,
        std::span<const ResolvedBinding> content
    ) {
        // The shape a caller passes is the one it would bind with, so the key is the
        // layout identity, the set index and the content - and identical requests
        // return one bind group, which is what a per-frame rewrite relies on.
        const size_t key = static_cast<size_t>(layout.id.GetValue()) * 1315423911u
                           ^ (static_cast<size_t>(set_id) << 8) ^ HashContent(content);
        if (auto itr = pimpl->bind_groups.find(key); itr != pimpl->bind_groups.end()) {
            return FromWgpuBindGroup(itr->second);
        }

        std::vector<WGPUBindGroupEntry> entries;
        entries.reserve(content.size());
        for (const ResolvedBinding &binding : content) {
            WGPUBindGroupEntry entry{};
            entry.binding = binding.binding;
            switch (binding.type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::UniformBufferDynamic:
            case DescriptorType::StorageBuffer:
            case DescriptorType::StorageBufferDynamic:
                entry.buffer = ToWgpuBuffer(binding.buffer.handle);
                entry.offset = binding.offset;
                // The seam's "maximum size means all of it" is the standard interface's
                // WGPU_WHOLE_SIZE.
                entry.size = binding.range == std::numeric_limits<size_t>::max() ? WGPU_WHOLE_SIZE
                                                                                : binding.range;
                break;
            case DescriptorType::CombinedImageSampler:
                entry.textureView = ToWgpuTextureView(binding.image_view.handle);
                entry.sampler = ToWgpuSampler(binding.sampler.handle);
                break;
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

        WGPUBindGroup group = wgpuDeviceCreateBindGroup(pimpl->device, &desc);
        if (group == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: bind group creation failed.");
            return {};
        }
        pimpl->bind_groups.emplace(key, group);
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
} // namespace Engine::Rhi
