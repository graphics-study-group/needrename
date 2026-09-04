#include "Rhi/Texture/Texture.h"

#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/Hasher.hpp"
#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Texture/ImageUtilsFunc.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include <vulkan/vulkan.hpp>

#include <unordered_map>

namespace {
    constexpr vk::ImageType GetImageType(const Engine::Rhi::Texture::TextureDesc &d) {
        return d.dimensions == 1 ? vk::ImageType::e1D : (d.dimensions == 2 ? vk::ImageType::e2D : vk::ImageType::e3D);
    }

    constexpr vk::ImageViewType GetImageViewType(
        const Engine::Rhi::Texture::TextureDesc &d, const Engine::Rhi::TextureSubresourceRange &r
    ) {
        assert(r.array_layer_base < d.array_layers && "Array layer base out of range.");
        assert(
            ((r.array_layer_base + r.array_layer_size <= d.array_layers)
             || (r.array_layer_size == vk::RemainingArrayLayers))
            && "Array layer out of range."
        );
        assert(r.mip_level_base < d.mipmap_levels && "Mipmap level base out of range.");
        assert(
            ((r.mip_level_base + r.mip_level_size <= d.mipmap_levels) || (r.mip_level_size == vk::RemainingMipLevels))
            && "Mipmap level out of range."
        );

        if (d.is_cube_map) {
            assert(d.dimensions == 2 && "Cubemap is not 2D.");
            return vk::ImageViewType::eCube;
        }

        if (d.dimensions == 3) {
            assert(
                (r.array_layer_size == 1 || r.array_layer_size == vk::RemainingArrayLayers)
                && "3D texture array is not supported"
            );
            return vk::ImageViewType::e3D;
        } else {
            if (r.array_layer_size == 1 || (r.array_layer_size == vk::RemainingArrayLayers && d.array_layers == 1)) {
                return (d.dimensions == 1 ? vk::ImageViewType::e1D : vk::ImageViewType::e2D);
            } else {
                return (d.dimensions == 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e2DArray);
            }
        }
    }

    constexpr vk::ComponentSwizzle ToVkComponentSwizzle(
        const Engine::Rhi::TextureSubresourceRange::SwizzleAndSrgb::ColorSwizzle cs
    ) {
        switch (cs) {
            using enum Engine::Rhi::TextureSubresourceRange::SwizzleAndSrgb::ColorSwizzle;
        case Identity:
            return vk::ComponentSwizzle::eIdentity;
        case Zero:
            return vk::ComponentSwizzle::eZero;
        case One:
            return vk::ComponentSwizzle::eOne;
        case Red:
            return vk::ComponentSwizzle::eR;
        case Green:
            return vk::ComponentSwizzle::eG;
        case Blue:
            return vk::ComponentSwizzle::eB;
        case Alpha:
            return vk::ComponentSwizzle::eA;
        }
#if defined(_MSC_VER)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }

    constexpr vk::ComponentMapping ToVkComponentMapping(
        const Engine::Rhi::TextureSubresourceRange::SwizzleAndSrgb &sas
    ) {
        return vk::ComponentMapping{
            ToVkComponentSwizzle(sas.r),
            ToVkComponentSwizzle(sas.g),
            ToVkComponentSwizzle(sas.b),
            ToVkComponentSwizzle(sas.a)
        };
    }

    constexpr vk::Format ConvertSrgbFormat(
        vk::Format original, const Engine::Rhi::TextureSubresourceRange::SwizzleAndSrgb &sc
    ) {
        if (sc.srgb == Engine::Rhi::TextureSubresourceRange::SwizzleAndSrgb::SrgbConversion::ForceSrgb) {
            switch (original) {
                using enum vk::Format;
            case eR8G8B8A8Unorm:
                return eR8G8B8A8Srgb;
            default:
                return original;
            }
        } else if (sc.srgb == Engine::Rhi::TextureSubresourceRange::SwizzleAndSrgb::SrgbConversion::ForceUnorm) {
            switch (original) {
                using enum vk::Format;
            case eR8G8B8A8Srgb:
                return eR8G8B8A8Unorm;
            default:
                return original;
            }
        }

        return original;
    }
} // namespace

namespace Engine::Rhi {

    struct Texture::impl {
        vk::Device device{};

        TextureDesc m_tdesc{};
        SamplerDesc m_sdesc{};
        std::unique_ptr<ImageAllocation> m_image{};

        struct subresource_hasher {
            size_t operator()(const TextureSubresourceRange &r) const noexcept {
                RenderResourceHasher h;
                h.u32(r.array_layer_base);
                h.u32(r.array_layer_size);
                h.u32(r.mip_level_base);
                h.u32(r.mip_level_size);
                h.any(r.swizzle_and_srgb);
                return h.get();
            };
        };

        std::unordered_map<TextureSubresourceRange, vk::UniqueImageView, subresource_hasher> m_views{};

        vk::Sampler m_sampler{};
        std::string m_name{};
    };

    Texture::Texture() : pimpl(nullptr) {
    }

    Texture::Texture(DeviceContext &device_context, TextureDesc texture, SamplerDesc sampler, const std::string &name) :
        pimpl(std::make_unique<impl>()) {

        auto &allocator = device_context.GetAllocatorState();
        auto dimension = texture.dimensions;
        auto [width, height, depth] = std::tie(texture.width, texture.height, texture.depth);
        auto mipLevels = texture.mipmap_levels;
        auto arrayLayers = texture.array_layers;

        // Some prelimary checks
        assert(1 <= dimension && dimension <= 3);
        assert(width >= 1 && height >= 1 && depth >= 1);
        assert(dimension != 1 || (height == 1 && depth == 1));
        assert(dimension != 2 || (depth == 1));
        assert(mipLevels >= 1);
        assert(arrayLayers >= 1);
        assert(!texture.is_cube_map || arrayLayers == 6);

        auto dim = dimension == 1 ? vk::ImageType::e1D : (dimension == 2 ? vk::ImageType::e2D : vk::ImageType::e3D);
        pimpl->device = device_context.GetDevice();
        pimpl->m_image = allocator.AllocateImageUnique(
            Rhi::AllocatorState::ImageAllocationDescription{
                texture.memory_type,
                dim,
                vk::Extent3D{width, height, depth},
                Rhi::GetVkFormat(texture.format),
                mipLevels,
                arrayLayers,
                texture.is_cube_map,
                vk::SampleCountFlagBits::e1
            },
            name
        );
        pimpl->m_tdesc = texture;
        pimpl->m_name = name;

        pimpl->m_sampler = device_context.GetIRCache().GetSampler(sampler);
        pimpl->m_sdesc = sampler;
    }

    Texture::Texture(Texture &&o) noexcept : Texture() {
        std::swap(this->pimpl, o.pimpl);
    }

    Texture::~Texture() = default;

    const Texture::TextureDesc &Texture::GetTextureDescription() const noexcept {
        return pimpl->m_tdesc;
    }

    const Texture::SamplerDesc &Texture::GetSamplerDescription() const noexcept {
        return pimpl->m_sdesc;
    }

    vk::Image Engine::Rhi::Texture::GetImage() const noexcept {
        assert(pimpl->m_image && pimpl->m_image->GetImage());
        return pimpl->m_image->GetImage();
    }

    vk::ImageView Engine::Rhi::Texture::GetImageView() const {
        return this->GetImageView(TextureSubresourceRange{});
    }

    vk::ImageView Texture::GetImageView(const TextureSubresourceRange &tsv) const {
        auto itr = pimpl->m_views.find(tsv);
        if (itr != pimpl->m_views.end()) return itr->second.get();

        vk::ImageViewCreateInfo ivci{
            vk::ImageViewCreateFlags{},
            pimpl->m_image->GetImage(),
            GetImageViewType(pimpl->m_tdesc, tsv),
            ConvertSrgbFormat(Rhi::GetVkFormat(pimpl->m_tdesc.format), tsv.swizzle_and_srgb),
            ToVkComponentMapping(tsv.swizzle_and_srgb),
            vk::ImageSubresourceRange{
                Rhi::GetVkAspect(pimpl->m_tdesc.format),
                tsv.mip_level_base,
                tsv.mip_level_size,
                tsv.array_layer_base,
                tsv.array_layer_size
            }
        };
        pimpl->m_views[tsv] = pimpl->device.createImageViewUnique(ivci);
        return pimpl->m_views[tsv].get();
    }

    vk::Sampler Texture::GetSampler() const noexcept {
        return pimpl->m_sampler;
    }

    size_t Texture::CalculateStagingBufferSizeNoMipmap() const noexcept {
        return pimpl->m_tdesc.height * pimpl->m_tdesc.width * pimpl->m_tdesc.depth * pimpl->m_tdesc.array_layers
               * Rhi::GetPixelSize(pimpl->m_tdesc.format);
    }

    bool Texture::SupportRandomAccess() const noexcept {
        return false;
    }
    bool Texture::SupportAtomicOperation() const noexcept {
        return false;
    }
} // namespace Engine::Rhi
