#include "Rhi/Resource/ImmutableResourceCache.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Device/BindingRef.h"
#include "Rhi/Device/Hasher.hpp"

#include <SDL3/SDL.h>
#include <unordered_map>
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
        sampler_desc.maxAnisotropy = static_cast<uint16_t>(desc.max_anisotropy);

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



