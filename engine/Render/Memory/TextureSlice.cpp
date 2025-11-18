#include "TextureSlice.h"

#include "Render/ImageUtilsFunc.h"
#include "Render/Memory/Texture.h"
#include "Render/RenderSystem.h"

#include <vulkan/vulkan.hpp>

namespace Engine {

    struct SlicedTextureView::impl {
        const Texture &m_texture;
        TextureSlice m_slice;
        vk::UniqueImageView m_view;
    };

    SlicedTextureView::SlicedTextureView(RenderSystem &system, const Texture &tex, TextureSlice slice) :
        pimpl(std::make_unique<impl>(tex, slice, vk::UniqueImageView{nullptr})) {
        vk::ImageViewCreateInfo ivci{
            vk::ImageViewCreateFlags{},
            tex.GetImage(),
            ImageUtils::InferImageViewType(tex.GetTextureDescription()),
            ImageUtils::GetVkFormat(tex.GetTextureDescription().format),
            vk::ComponentMapping{},
            vk::ImageSubresourceRange{
                ImageUtils::GetVkAspect(tex.GetTextureDescription().format),
                pimpl->m_slice.mipmap_base,
                pimpl->m_slice.mipmap_size,
                pimpl->m_slice.array_base,
                pimpl->m_slice.array_size
            }
        };
        pimpl->m_view = system.getDevice().createImageViewUnique(ivci);
    }

    SlicedTextureView::~SlicedTextureView() = default;

    const TextureSlice &SlicedTextureView::GetSlice() const noexcept {
        return pimpl->m_slice;
    }

    const Texture &SlicedTextureView::GetTexture() const noexcept {
        return pimpl->m_texture;
    }

    vk::ImageView SlicedTextureView::GetImageView() const noexcept {
        return pimpl->m_view.get();
    }
} // namespace Engine
