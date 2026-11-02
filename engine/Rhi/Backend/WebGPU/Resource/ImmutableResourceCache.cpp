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

        /**
         * @brief The comparison a sampler performs, or Undefined when it is not a comparator.
         *
         * A shadow sampler is a comparison sampler: the shader asks whether the fragment is
         * nearer than what the map holds and the sampler answers with a filtered 0 or 1. A
         * sampler created without a comparison cannot be bound where the shader declares one,
         * and the device reports it as a validation error rather than falling back quietly.
         */
        WGPUCompareFunction ToWgpuCompare(DSComparator comparator) {
            switch (comparator) {
            case DSComparator::Never:
            case DSComparator::Always:
                // The seam spells "no comparison" as a comparison that always passes; a
                // comparison sampler cannot be bound where the shader declared a plain one.
                return WGPUCompareFunction_Undefined;
            case DSComparator::Less:
                return WGPUCompareFunction_Less;
            case DSComparator::Equal:
                return WGPUCompareFunction_Equal;
            case DSComparator::LEqual:
                return WGPUCompareFunction_LessEqual;
            case DSComparator::Greater:
                return WGPUCompareFunction_Greater;
            case DSComparator::NEqual:
                return WGPUCompareFunction_NotEqual;
            case DSComparator::GEqual:
                return WGPUCompareFunction_GreaterEqual;
            }
            return WGPUCompareFunction_Undefined;
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
        /// @brief Cached layouts for the groups a pipeline layout pads or appends.
        WGPUBindGroupLayout empty_group_layout{nullptr};
        WGPUBindGroupLayout constant_group_layout{nullptr};

        WGPUBindGroupLayout EmptyGroupLayout() {
            if (empty_group_layout != nullptr) {
                return empty_group_layout;
            }
            WGPUBindGroupLayoutDescriptor desc{};
            desc.entryCount = 0;
            desc.entries = nullptr;
            desc.label = WGPUStringView{"Empty Bind Group Layout", 25};
            empty_group_layout = wgpuDeviceCreateBindGroupLayout(device, &desc);
            return empty_group_layout;
        }

        WGPUBindGroupLayout ConstantGroupLayout() {
            if (constant_group_layout != nullptr) {
                return constant_group_layout;
            }
            const WGPUBindGroupLayoutEntry entry = ConstantBlockLayoutEntry();
            WGPUBindGroupLayoutDescriptor desc{};
            desc.entryCount = 1;
            desc.entries = &entry;
            desc.label = WGPUStringView{"Constant Block Layout", 23};
            constant_group_layout = wgpuDeviceCreateBindGroupLayout(device, &desc);
            return constant_group_layout;
        }

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
        // A comparator sampler is what a shadow lookup binds; `Never` is the seam's "not a
        // comparator" and becomes the undefined function, which is a plain filtering sampler.
        sampler_desc.compare = ToWgpuCompare(desc.comparator);

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

        WGPUTextureSampleType ToWgpuSampleType(TextureSampleType type) {
            switch (type) {
            case TextureSampleType::Depth:
                return WGPUTextureSampleType_Depth;
            case TextureSampleType::UnfilterableFloat:
                return WGPUTextureSampleType_UnfilterableFloat;
            case TextureSampleType::Sint:
                return WGPUTextureSampleType_Sint;
            case TextureSampleType::Uint:
                return WGPUTextureSampleType_Uint;
            case TextureSampleType::Float:
            default:
                return WGPUTextureSampleType_Float;
            }
        }

        WGPUTextureViewDimension ToWgpuViewDimension(TextureViewDimension dimension) {
            switch (dimension) {
            case TextureViewDimension::D1:
                return WGPUTextureViewDimension_1D;
            case TextureViewDimension::D2Array:
                return WGPUTextureViewDimension_2DArray;
            case TextureViewDimension::D3:
                return WGPUTextureViewDimension_3D;
            case TextureViewDimension::Cube:
                return WGPUTextureViewDimension_Cube;
            case TextureViewDimension::CubeArray:
                return WGPUTextureViewDimension_CubeArray;
            case TextureViewDimension::D2:
            default:
                return WGPUTextureViewDimension_2D;
            }
        }

        /// @brief Where the sampler of a combined binding goes, honouring an explicit slot.
        uint32_t SamplerSlotOf(const DescriptorBinding &binding) noexcept {
            return binding.sampler_binding == std::numeric_limits<uint32_t>::max()
                       ? SplitSamplerBinding(binding.binding)
                       : binding.sampler_binding;
        }

        size_t HashLayout(const DescriptorSetLayout &layout) noexcept {
            RenderResourceHasher h;
            for (const DescriptorBinding &binding : layout) {
                h.u32(binding.binding);
                h.u32(static_cast<uint32_t>(binding.type));
                // `count` is part of the layout: an array binding declares its length with
                // bindingArraySize, so two layouts differing in it are different layouts.
                h.u32(binding.count);
                // So is how a texture is declared: a depth array and a colour 2D texture are
                // different binding kinds here, not just different resources.
                h.u32(static_cast<uint32_t>(binding.sample_type));
                h.u32(static_cast<uint32_t>(binding.view_dimension));
                h.u32(SamplerSlotOf(binding));
                h.u32(static_cast<uint32_t>(binding.storage_format));
                h.u32(binding.storage_read_only ? 1u : 0u);
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
            // A binding that holds more than one descriptor is an array, and the layout has to
            // say how long it is: an array the shader indexes into is a different declaration
            // from a single binding, not a detail of the set.
            entry.bindingArraySize = binding.count;
            if (binding.type == DescriptorType::CombinedImageSampler) {
                // The standard interface has no combined binding: the shader language declares
                // the texture and its sampler separately, so the layout has two entries. The
                // texture keeps the binding the shader names and the sampler goes to the slot
                // the split rule names - see `Rhi::SplitSamplerBinding`.
                entry.texture.sampleType = ToWgpuSampleType(binding.sample_type);
                entry.texture.viewDimension = ToWgpuViewDimension(binding.view_dimension);
                entries.push_back(entry);

                WGPUBindGroupLayoutEntry sampler_entry{};
                sampler_entry.binding = SamplerSlotOf(binding);
                sampler_entry.visibility = entry.visibility;
                sampler_entry.sampler.type = binding.sample_type == TextureSampleType::Depth
                                                 ? WGPUSamplerBindingType_Comparison
                                                 : WGPUSamplerBindingType_Filtering;
                entries.push_back(sampler_entry);
                continue;
            }
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
                // Unreachable: a combined binding took the two-entry path above. The case is
                // kept so the enumeration stays fully handled, and it states the texture's
                // shape rather than the sampler's.
                entry.texture.sampleType = ToWgpuSampleType(binding.sample_type);
                entry.texture.viewDimension = ToWgpuViewDimension(binding.view_dimension);
                break;
            case DescriptorType::SampledImage:
                entry.texture.sampleType = ToWgpuSampleType(binding.sample_type);
                entry.texture.viewDimension = ToWgpuViewDimension(binding.view_dimension);
                break;
            case DescriptorType::Sampler:
                entry.sampler.type = binding.sample_type == TextureSampleType::Depth
                                         ? WGPUSamplerBindingType_Comparison
                                         : WGPUSamplerBindingType_Filtering;
                break;
            case DescriptorType::StorageImage:
                entry.storageTexture.access = binding.storage_read_only ? WGPUStorageTextureAccess_ReadOnly
                                                                        : WGPUStorageTextureAccess_WriteOnly;
                entry.storageTexture.format = ToWgpuTextureFormat(static_cast<ImageFormat>(binding.storage_format));
                if (entry.storageTexture.format == WGPUTextureFormat_Undefined) {
                    // The shader declared no format the seam knows; the interface requires one, so
                    // the entry names the colour format the engine's own storage images use and
                    // says so rather than letting the device reject an undefined format.
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "WebGPU: storage image binding %u declares a format the seam does not name; "
                        "using RGBA8Unorm.",
                        binding.binding
                    );
                    entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
                }
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                // A written storage texture is not legal in the vertex stage of the standard
                // interface, and a layout entry that claims vertex visibility is rejected.
                entry.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
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
        group_layouts.reserve(description.set_layouts.size() + 1);
        for (const DescriptorSetLayoutHandle &layout : description.set_layouts) {
            group_layouts.push_back(ToWgpuBindGroupLayout(layout));
        }

        if (!description.push_constant_ranges.empty()) {
            // The shader's push-constant block is a uniform buffer in group 3 (see
            // `k_constant_bind_group`), so the pipeline layout has to declare four groups. A
            // description with fewer set layouts is padded with empty groups up to that index:
            // a group the shader does not read still has to exist in the layout it was built
            // against, and an empty layout is what "the caller binds nothing here" means.
            while (group_layouts.size() < k_constant_bind_group) {
                group_layouts.push_back(pimpl->EmptyGroupLayout());
            }
            group_layouts.push_back(pimpl->ConstantGroupLayout());
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

