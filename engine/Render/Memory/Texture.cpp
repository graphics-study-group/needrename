#include "Texture.h"

#include "Render/ImageUtilsFunc.h"
#include "Render/Memory/AllocatedMemory.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"

#include <vulkan/vulkan.hpp>

namespace Engine {

    struct Texture::impl {
        TextureDesc m_tdesc {};
        SamplerDesc m_sdesc {};
        std::unique_ptr <ImageAllocation> m_image {};
        std::unique_ptr <SlicedTextureView> m_full_view {};
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
        pimpl->m_image = allocator.AllocateImageUnique(
            texture.type,
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

        pimpl->m_sampler = system.GetSamplerManager().GetSampler(sampler);
        pimpl->m_sdesc = sampler;

        pimpl->m_full_view = std::make_unique<SlicedTextureView>(
            system, *this, TextureSlice{0, texture.mipmap_levels, 0, texture.array_layers}
        );
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

    const SlicedTextureView &Texture::GetFullSlice() const noexcept {
        assert(pimpl->m_full_view);
        return *(pimpl->m_full_view);
    }

    vk::ImageView Engine::Texture::GetImageView() const noexcept {
        return this->GetFullSlice().GetImageView();
    }

    vk::Sampler Texture::GetSampler() const noexcept {
        return pimpl->m_sampler;
    }

    std::unique_ptr <DeviceBuffer> Engine::Texture::CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const {
        assert(
            ((std::get<0>(ImageUtils::GetImageFlags(this->GetTextureDescription().type))
              & vk::ImageUsageFlagBits::eTransferDst)
             || (std::get<0>(ImageUtils::GetImageFlags(this->GetTextureDescription().type))
                 & vk::ImageUsageFlagBits::eTransferSrc))
            && "A staging buffer is created, but the image does not support tranfer usage."
        );
        uint64_t buffer_size = pimpl->m_tdesc.height 
            * pimpl->m_tdesc.width * pimpl->m_tdesc.depth * pimpl->m_tdesc.array_layers
            * ImageUtils::GetPixelSize(pimpl->m_tdesc.format);
        assert(buffer_size > 0);

        return DeviceBuffer::CreateUnique(
            allocator,
            DeviceBuffer::BufferType::Staging,
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
