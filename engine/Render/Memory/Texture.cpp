#include "Texture.h"

#include "Render/Hasher.hpp"
#include "Render/ImageUtilsFunc.h"
#include "Render/Memory/AllocatedMemory.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Memory/TextureSubresourceView.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"

#include <vulkan/vulkan.hpp>

namespace {
    constexpr vk::ImageType GetImageType(const Engine::Texture::TextureDesc & d) {
        return d.dimensions == 1 ? vk::ImageType::e1D : 
            (d.dimensions == 2 ? vk::ImageType::e2D : vk::ImageType::e3D);
    }

    constexpr vk::ImageViewType GetImageViewType(
        const Engine::Texture::TextureDesc & d,
        const Engine::TextureSubresourceRange & r
    ) {
        assert(r.array_layer_base < d.array_layers && "Array layer base out of range.");
        assert(
            ((r.array_layer_base + r.array_layer_size <= d.array_layers) 
                || (r.array_layer_size == vk::RemainingArrayLayers)
            ) && "Array layer out of range."
        );
        assert(r.mip_level_base < d.mipmap_levels && "Mipmap level base out of range.");
        assert(
            ((r.mip_level_base + r.mip_level_size <= d.mipmap_levels) 
                || (r.mip_level_size == vk::RemainingMipLevels)
            ) && "Mipmap level out of range."
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
            if (
                r.array_layer_size == 1 ||
                (r.array_layer_size == vk::RemainingArrayLayers && d.array_layers == 1)
            ) {
                return (d.dimensions == 1 ? vk::ImageViewType::e1D : vk::ImageViewType::e2D);
            } else {
                return (d.dimensions == 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e2DArray);
            }
        }
    }
}

namespace Engine {

    struct Texture::impl {
        vk::Device device {};

        TextureDesc m_tdesc {};
        SamplerDesc m_sdesc {};
        std::unique_ptr <ImageAllocation> m_image {};

        struct subresource_hasher {
            size_t operator() (const TextureSubresourceRange & r) const noexcept {
                RenderResourceHasher h;
                h.u32(r.array_layer_base);
                h.u32(r.array_layer_size);
                h.u32(r.mip_level_base);
                h.u32(r.mip_level_size);
                return h.get();
            };
        };

        std::unordered_map <
            TextureSubresourceRange,
            vk::UniqueImageView,
            subresource_hasher> m_views;

        vk::Sampler m_sampler {};
        std::string m_name {};
    };

    Texture::Texture() : pimpl(nullptr) {
    }

    Texture::Texture(RenderSystem &system, TextureDesc texture, SamplerDesc sampler, const std::string &name) :
        pimpl(std::make_unique<impl>()) {

        auto &allocator = system.GetAllocatorState();
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
        pimpl->device = system.GetDevice();
        pimpl->m_image = allocator.AllocateImageUnique(
            texture.memory_type,
            dim,
            vk::Extent3D{width, height, depth},
            ImageUtils::GetVkFormat(texture.format),
            mipLevels,
            arrayLayers,
            texture.is_cube_map,
            vk::SampleCountFlagBits::e1,
            name
        );
        pimpl->m_tdesc = texture;
        pimpl->m_name = name;

        pimpl->m_sampler = system.GetIRCache().GetSampler(sampler);
        pimpl->m_sdesc = sampler;
    }

    Texture::Texture(Texture && o) noexcept : Texture() {
        std::swap(this->pimpl, o.pimpl);
    }

    Texture::~Texture() = default;

    const Texture::TextureDesc &Texture::GetTextureDescription() const noexcept {
        return pimpl->m_tdesc;
    }

    const Texture::SamplerDesc &Texture::GetSamplerDescription() const noexcept {
        return pimpl->m_sdesc;
    }

    vk::Image Engine::Texture::GetImage() const noexcept {
        assert(pimpl->m_image && pimpl->m_image->GetImage());
        return pimpl->m_image->GetImage();
    }

    vk::ImageView Engine::Texture::GetImageView() const {
        return this->GetImageView(TextureSubresourceRange{});
    }

    vk::ImageView Texture::GetImageView(const TextureSubresourceRange &tsv) const {
        auto itr = pimpl->m_views.find(tsv);
        if (itr != pimpl->m_views.end())    return itr->second.get();

        vk::ImageViewCreateInfo ivci{
            vk::ImageViewCreateFlags{},
            pimpl->m_image->GetImage(),
            GetImageViewType(pimpl->m_tdesc, tsv),
            ImageUtils::GetVkFormat(pimpl->m_tdesc.format),
            vk::ComponentMapping{},
            vk::ImageSubresourceRange{
                ImageUtils::GetVkAspect(pimpl->m_tdesc.format),
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

    std::unique_ptr <DeviceBuffer> Engine::Texture::CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const {
        uint64_t buffer_size = pimpl->m_tdesc.height 
            * pimpl->m_tdesc.width * pimpl->m_tdesc.depth * pimpl->m_tdesc.array_layers
            * ImageUtils::GetPixelSize(pimpl->m_tdesc.format);
        assert(buffer_size > 0);

        return DeviceBuffer::CreateUnique(
            allocator,
            {BufferTypeBits::StagingToDevice},
            buffer_size,
            std::format("Buffer - texture ({}) staging", pimpl->m_name)
        );
    }
    bool Texture::SupportRandomAccess() const noexcept {
        return false;
    }
    bool Texture::SupportAtomicOperation() const noexcept {
        return false;
    }
} // namespace Engine
