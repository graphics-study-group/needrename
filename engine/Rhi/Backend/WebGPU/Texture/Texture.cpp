#include "Rhi/Texture/Texture.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/BindingRef.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/Hasher.hpp"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Texture/ImageUtils.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include <SDL3/SDL.h>
#include <unordered_map>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /// @brief The largest value the seam uses to mean "all of them".
        constexpr uint32_t ALL = std::numeric_limits<uint32_t>::max();

        WGPUTextureViewDimension ToWgpuViewDimension(const TextureDesc &desc) {
            if (desc.is_cube_map) {
                return WGPUTextureViewDimension_Cube;
            }
            switch (desc.dimensions) {
            case 1:
                return WGPUTextureViewDimension_1D;
            case 3:
                return WGPUTextureViewDimension_3D;
            default:
                // A layered texture is sampled as an array view, but an attachment has to be a
                // single layer: the validator rejects a layered view used as a render target.
                // The subresource path below picks per view; this is the whole-texture default.
                return desc.array_layers > 1 ? WGPUTextureViewDimension_2DArray
                                            : WGPUTextureViewDimension_2D;
            }
        }
    } // namespace

    /**
     * @brief One image, one default view, and a view per distinct subresource.
     *
     * A texture here owns its `WGPUTexture` and hands out views. The default view is
     * created with the texture so that the common case - bind the whole thing - costs
     * nothing; a narrower range gets its own view, cached by range because a render
     * graph asks for the same ranges every frame.
     *
     * Swizzle and sRGB conversion in `TextureSubresourceRange` are not expressed. The
     * standard interface has no per-view swizzle; sRGB is a property of the texture's
     * format instead. A caller that needs either picks the format at creation.
     */
    struct Texture::impl {
        WGPUTexture texture{nullptr};
        WGPUTextureView default_view{nullptr};
        TextureDesc tdesc{};
        SamplerDesc sdesc{};
        SamplerRef sampler_ref{};
        std::unique_ptr<ImageAllocation> image{};

        struct ViewEntry {
            WGPUTextureView view{nullptr};
            VulkanIdentity id{};
        };
        std::unordered_map<uint64_t, ViewEntry> views{};
        std::string name{};
    };

    Texture::Texture() : pimpl(nullptr) {
    }

    Texture::Texture(DeviceContext &device_context, TextureDesc texture, SamplerDesc sampler, const std::string &name) :
        pimpl(std::make_unique<impl>()) {
        auto &allocator = device_context.GetAllocatorState();

        // Mip levels default to the full chain when the caller leaves the field zero.
        uint32_t mip_levels = texture.mipmap_levels;
        if (mip_levels == 0) {
            uint32_t largest = std::max({texture.width, texture.height, texture.depth});
            mip_levels = 1;
            while (largest > 1) {
                largest >>= 1;
                ++mip_levels;
            }
        }

        pimpl->image = allocator.AllocateImageUnique(
            AllocatorState::ImageAllocationDescription{
                texture.memory_type,
                texture.dimensions == 1 ? ImageType::D1
                                        : (texture.dimensions == 2 ? ImageType::D2 : ImageType::D3),
                Extent3D{texture.width, texture.height, texture.depth},
                texture.format,
                mip_levels,
                texture.array_layers,
                texture.is_cube_map,
                SampleCountBits::C1
            },
            name
        );

        pimpl->texture = ToWgpuTexture(pimpl->image->GetImage());
        pimpl->tdesc = texture;
        pimpl->tdesc.mipmap_levels = mip_levels;
        pimpl->name = name;

        // The default view covers the whole texture: every mip, every layer.
        WGPUTextureViewDescriptor view_desc{};
        view_desc.format = ToWgpuTextureFormat(texture.format);
        view_desc.dimension = ToWgpuViewDimension(texture);
        view_desc.baseMipLevel = 0;
        view_desc.mipLevelCount = mip_levels;
        view_desc.baseArrayLayer = 0;
        view_desc.arrayLayerCount = texture.is_cube_map ? 6 : texture.array_layers;
        view_desc.aspect = WGPUTextureAspect_All;
        pimpl->default_view = wgpuTextureCreateView(pimpl->texture, &view_desc);
        if (pimpl->default_view == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: default texture view creation failed.");
        }

        pimpl->sdesc = sampler;
        pimpl->sampler_ref = device_context.GetIRCache().GetSamplerRef(sampler);
    }

    Texture::Texture(Texture &&o) noexcept : Texture() {
        std::swap(this->pimpl, o.pimpl);
    }

    Texture::~Texture() {
        if (!pimpl) {
            return;
        }
        for (auto &[key, entry] : pimpl->views) {
            static_cast<void>(key);
            if (entry.view != nullptr) {
                wgpuTextureViewRelease(entry.view);
            }
        }
        if (pimpl->default_view != nullptr) {
            wgpuTextureViewRelease(pimpl->default_view);
        }
        // The image allocation releases the texture itself.
    }

    const Texture::TextureDesc &Texture::GetTextureDescription() const noexcept {
        return pimpl->tdesc;
    }

    const Texture::SamplerDesc &Texture::GetSamplerDescription() const noexcept {
        return pimpl->sdesc;
    }

    ImageHandle Texture::GetImage() const noexcept {
        return pimpl->image->GetImage();
    }

    SamplerHandle Texture::GetSampler() const noexcept {
        return pimpl->sampler_ref.handle;
    }

    SamplerRef Texture::GetSamplerRef() const noexcept {
        return pimpl->sampler_ref;
    }

    ImageViewHandle Texture::GetImageView() const {
        return FromWgpuTextureView(pimpl->default_view);
    }

    ImageViewHandle Texture::GetImageView(const TextureSubresourceRange &tsr) const {
        if (tsr.array_layer_base == 0 && tsr.array_layer_size == ALL && tsr.mip_level_base == 0
            && tsr.mip_level_size == ALL) {
            // The common case: the range is the whole texture, so it is the default view.
            return FromWgpuTextureView(pimpl->default_view);
        }

        RenderResourceHasher h;
        h.u32(tsr.array_layer_base);
        h.u32(tsr.array_layer_size);
        h.u32(tsr.mip_level_base);
        h.u32(tsr.mip_level_size);
        const uint64_t key = h.get();

        if (auto itr = pimpl->views.find(key); itr != pimpl->views.end()) {
            return FromWgpuTextureView(itr->second.view);
        }

        WGPUTextureViewDescriptor view_desc{};
        view_desc.format = ToWgpuTextureFormat(pimpl->tdesc.format);
        view_desc.dimension = ToWgpuViewDimension(pimpl->tdesc);
        view_desc.baseMipLevel = tsr.mip_level_base;
        view_desc.mipLevelCount = tsr.mip_level_size == ALL
                                      ? pimpl->tdesc.mipmap_levels - tsr.mip_level_base
                                      : tsr.mip_level_size;
        view_desc.baseArrayLayer = tsr.array_layer_base;
        view_desc.arrayLayerCount = tsr.array_layer_size == ALL
                                        ? pimpl->tdesc.array_layers - tsr.array_layer_base
                                        : tsr.array_layer_size;
        view_desc.aspect = WGPUTextureAspect_All;
        // A one-layer view is a plain 2D view even on a layered texture. The dimension was
        // chosen from the texture, so it has to be corrected to what the range actually
        // covers - which is what an attachment of a single layer needs.
        if (view_desc.arrayLayerCount == 1 && view_desc.dimension == WGPUTextureViewDimension_2DArray) {
            view_desc.dimension = WGPUTextureViewDimension_2D;
        }

        WGPUTextureView view = wgpuTextureCreateView(pimpl->texture, &view_desc);
        if (view == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: subresource view creation failed.");
            return {};
        }
        pimpl->views.emplace(key, impl::ViewEntry{view, MintVulkanIdentity()});
        return FromWgpuTextureView(view);
    }

    ViewRef Texture::GetViewRef(const TextureSubresourceRange &tsr) const {
        RenderResourceHasher h;
        h.u32(tsr.array_layer_base);
        h.u32(tsr.array_layer_size);
        h.u32(tsr.mip_level_base);
        h.u32(tsr.mip_level_size);
        const uint64_t key = h.get();

        // Minting on demand keeps the identity stable for a range: the entry is created
        // the first time the range is asked for and returned unchanged afterwards.
        auto itr = pimpl->views.find(key);
        if (itr == pimpl->views.end()) {
            GetImageView(tsr);
            itr = pimpl->views.find(key);
        }
        if (itr == pimpl->views.end()) {
            return ViewRef{};
        }
        // BindingRef's constructor is private to the RHI wrappers, of which Texture is
        // one; the identity follows the view, not the handle.
        return ViewRef{FromWgpuTextureView(itr->second.view), itr->second.id};
    }

    size_t Texture::CalculateStagingBufferSizeNoMipmap() const noexcept {
        return pimpl->tdesc.height * pimpl->tdesc.width * pimpl->tdesc.depth * pimpl->tdesc.array_layers
               * Rhi::GetPixelSize(pimpl->tdesc.format);
    }

    bool Texture::SupportRandomAccess() const noexcept {
        // Only a storage-texture format can be written from a shader, and the standard
        // interface restricts which ones; the seam's answer is the conservative one.
        return false;
    }

    bool Texture::SupportAtomicOperation() const noexcept {
        // Atomic operations on an image need a format the standard interface does not
        // expose for that purpose.
        return false;
    }
} // namespace Engine::Rhi
