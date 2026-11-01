#include "Rhi/Resource/ImmutableResourceCache.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Device/BindingRef.h"
#include "Rhi/Device/Hasher.hpp"

#include <SDL3/SDL.h>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        WGPUAddressMode ToWgpuAddressMode(SamplerDesc::AddressMode mode) {
            switch (mode) {
            case SamplerDesc::AddressMode::Repeat:
                return WGPUAddressMode_Repeat;
            case SamplerDesc::AddressMode::MirroredRepeat:
                return WGPUAddressMode_MirrorRepeat;
            case SamplerDesc::AddressMode::ClampToEdge:
            case SamplerDesc::AddressMode::ClampToBorder_TransparentBlack:
            case SamplerDesc::AddressMode::ClampToBorder_OpaqueBlack:
            case SamplerDesc::AddressMode::ClampToBorder_OpaqueWhite:
            default:
                // The standard interface has one clamp mode with no border colour; a
                // border is a property of the texture binding, not of the sampler.
                return WGPUAddressMode_ClampToEdge;
            }
        }

        WGPUFilterMode ToWgpuFilterMode(SamplerDesc::FilterMode mode) {
            return mode == SamplerDesc::FilterMode::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        }

        /// @brief A key that is stable for one description and different for another.
        ///
        /// The seam's SamplerDesc compares field by field, so the hash does too; there
        /// is no Vulkan create info to hash here.
        size_t Hash(const SamplerDesc &desc) noexcept {
            RenderResourceHasher h;
            h.u32(static_cast<uint32_t>(desc.u_address));
            h.u32(static_cast<uint32_t>(desc.v_address));
            h.u32(static_cast<uint32_t>(desc.w_address));
            h.u32(static_cast<uint32_t>(desc.min_filter));
            h.u32(static_cast<uint32_t>(desc.max_filter));
            h.u32(static_cast<uint32_t>(desc.mipmap_filter));
            h.f32(desc.bias_lod);
            h.f32(desc.min_lod);
            h.f32(desc.max_lod);
            h.f32(desc.max_anisotropy);
            h.u32(static_cast<uint32_t>(desc.comparator));
            return h.get();
        }
    } // namespace

    struct ImmutableResourceCache::impl {
        WGPUDevice device{nullptr};
        std::unordered_map<size_t, WGPUSampler> samplers{};
        std::unordered_map<size_t, SamplerRef> sampler_refs{};
        std::unordered_map<size_t, DescriptorSetLayoutRef> layout_refs{};
        std::unordered_map<size_t, WGPUBindGroupLayout> layout_handles{};
        std::unordered_map<size_t, WGPUPipelineLayout> pipeline_layouts{};

        ~impl() {
            for (auto &[key, sampler] : samplers) {
                static_cast<void>(key);
                if (sampler != nullptr) {
                    wgpuSamplerRelease(sampler);
                }
            }
        }
    };

    ImmutableResourceCache::ImmutableResourceCache(DeviceHandle device) : pimpl(std::make_unique<impl>()) {
        pimpl->device = ToWgpuDevice(device);
    }

    ImmutableResourceCache::~ImmutableResourceCache() = default;

    SamplerRef ImmutableResourceCache::GetSamplerRef(const Rhi::SamplerDesc &desc) {
        const size_t key = Hash(desc);
        auto itr = pimpl->sampler_refs.find(key);
        if (itr != pimpl->sampler_refs.end()) {
            return itr->second;
        }

        WGPUSamplerDescriptor sampler_desc{};
        sampler_desc.addressModeU = ToWgpuAddressMode(desc.u_address);
        sampler_desc.addressModeV = ToWgpuAddressMode(desc.v_address);
        sampler_desc.addressModeW = ToWgpuAddressMode(desc.w_address);
        sampler_desc.magFilter = ToWgpuFilterMode(desc.max_filter);
        sampler_desc.minFilter = ToWgpuFilterMode(desc.min_filter);
        sampler_desc.mipmapFilter = desc.mipmap_filter == SamplerDesc::FilterMode::Linear
                                       ? WGPUMipmapFilterMode_Linear
                                       : WGPUMipmapFilterMode_Nearest;
        sampler_desc.lodMinClamp = desc.min_lod;
        sampler_desc.lodMaxClamp = desc.max_lod;
        // The seam disables anisotropic filtering with a value below one; the standard
        // interface rejects zero and wants at least one, so a disabled clamp becomes one.
        sampler_desc.maxAnisotropy =
            desc.max_anisotropy < 1.0f ? 1 : static_cast<uint16_t>(desc.max_anisotropy);

        WGPUSampler sampler = wgpuDeviceCreateSampler(pimpl->device, &sampler_desc);
        if (sampler == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: sampler creation failed.");
        }

        pimpl->samplers.emplace(key, sampler);
        const SamplerRef ref{FromWgpuSampler(sampler), MintVulkanIdentity()};
        pimpl->sampler_refs.emplace(key, ref);
        return ref;
    }

    SamplerHandle ImmutableResourceCache::GetSampler(const Rhi::SamplerDesc &desc) {
        return GetSamplerRef(desc).handle;
    }
} // namespace Engine::Rhi

namespace Engine::Rhi {
    namespace {
        WGPUShaderStage ToWgpuVisibility() {
            // The seam's layout says what a binding accepts, not which stage reads it.
            return WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        }

        size_t HashLayout(const DescriptorSetLayout &layout) noexcept {
            RenderResourceHasher h;
            for (const DescriptorBinding &binding : layout) {
                h.u32(binding.binding);
                h.u32(static_cast<uint32_t>(binding.type));
                // `count` is deliberately absent: WGPUBindGroupLayoutEntry has no
                // array-length field, so a binding is one entry whatever the seam says.
                h.u32(static_cast<uint32_t>(binding.immutable_sampler.GetValue()));
            }
            return h.get();
        }
    } // namespace

    DescriptorSetLayoutRef ImmutableResourceCache::GetDescriptorSetLayoutRef(
        const DescriptorSetLayout &layout, const char *name
    ) {
        const size_t key = HashLayout(layout);
        if (auto itr = pimpl->layout_refs.find(key); itr != pimpl->layout_refs.end()) {
            return itr->second;
        }

        std::vector<WGPUBindGroupLayoutEntry> entries;
        entries.reserve(layout.size());
        for (const DescriptorBinding &binding : layout) {
            WGPUBindGroupLayoutEntry entry{};
            entry.binding = binding.binding;
            entry.visibility = ToWgpuVisibility();
            switch (binding.type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::UniformBufferDynamic:
                entry.buffer.type = WGPUBufferBindingType_Uniform;
                break;
            case DescriptorType::StorageBuffer:
            case DescriptorType::StorageBufferDynamic:
                entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                break;
            case DescriptorType::CombinedImageSampler:
                entry.texture.sampleType = WGPUTextureSampleType_Float;
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
            case DescriptorType::SampledImage:
                entry.texture.sampleType = WGPUTextureSampleType_Float;
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case DescriptorType::Sampler:
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
            case DescriptorType::StorageImage:
                entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            }
            entries.push_back(entry);
        }

        WGPUBindGroupLayoutDescriptor desc{};
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        if (name != nullptr) {
            desc.label = WGPUStringView{name, std::strlen(name)};
        }

        WGPUBindGroupLayout handle = wgpuDeviceCreateBindGroupLayout(pimpl->device, &desc);
        const DescriptorSetLayoutRef ref{FromWgpuBindGroupLayout(handle), MintVulkanIdentity()};
        pimpl->layout_handles.emplace(key, handle);
        pimpl->layout_refs.emplace(key, ref);
        return ref;
    }

    DescriptorSetLayoutHandle ImmutableResourceCache::GetDescriptorSetLayout(
        const DescriptorSetLayout &layout, const char *name
    ) {
        return GetDescriptorSetLayoutRef(layout, name).handle;
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    PipelineLayoutHandle ImmutableResourceCache::GetPipelineLayout(
        const PipelineLayoutDescription &description, const char *name
    ) {
        RenderResourceHasher h;
        for (const DescriptorSetLayoutHandle &layout : description.set_layouts) {
            h.u32(static_cast<uint32_t>(layout.GetValue()));
        }
        for (const PushConstantRange &range : description.push_constant_ranges) {
            h.u32(range.offset);
            h.u32(range.size);
            h.u32(range.compute ? 1u : 0u);
        }
        const size_t key = h.get();
        if (auto itr = pimpl->pipeline_layouts.find(key); itr != pimpl->pipeline_layouts.end()) {
            return FromWgpuPipelineLayout(itr->second);
        }

        std::vector<WGPUBindGroupLayout> group_layouts;
        group_layouts.reserve(description.set_layouts.size());
        for (const DescriptorSetLayoutHandle &layout : description.set_layouts) {
            group_layouts.push_back(ToWgpuBindGroupLayout(layout));
        }

        WGPUPipelineLayoutDescriptor desc{};
        desc.bindGroupLayoutCount = group_layouts.size();
        desc.bindGroupLayouts = group_layouts.data();
        if (name != nullptr) {
            desc.label = WGPUStringView{name, std::strlen(name)};
        }

        // Push-constant ranges are absent on purpose: there is no push-constant block
        // here. The seam keeps them in the description because a backend that has them
        // needs them, and a parameter block becomes a uniform buffer instead.
        WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(pimpl->device, &desc);
        pimpl->pipeline_layouts.emplace(key, pipeline_layout);
        return FromWgpuPipelineLayout(pipeline_layout);
    }
} // namespace Engine::Rhi


