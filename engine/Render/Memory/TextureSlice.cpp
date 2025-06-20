#include "TextureSlice.h"
#include "Render/Memory/Texture.h"
#include "Render/Memory/SampledTexture.h"
#include "Render/RenderSystem.h"

namespace Engine {
    SlicedTextureView::SlicedTextureView(
        RenderSystem & system,
        const Texture & tex,
        TextureSlice slice
    ) : m_system(system), m_texture(tex), m_slice(slice), m_view(nullptr)
    {
        vk::ImageViewCreateInfo ivci{
            vk::ImageViewCreateFlags{},
            tex.GetImage(),
            Texture::InferImageViewType(tex.GetTextureDescription()),
            ImageUtils::GetVkFormat(tex.GetTextureDescription().format),
            vk::ComponentMapping{},
            vk::ImageSubresourceRange{
                ImageUtils::GetVkAspect(tex.GetTextureDescription().format),
                m_slice.mipmap_base, m_slice.mipmap_size,
                m_slice.array_base, m_slice.array_size
            }
        };
        m_view = m_system.getDevice().createImageViewUnique(ivci);
    }

    const TextureSlice & SlicedTextureView::GetSlice() const noexcept
    {
        return m_slice;
    }

    const Texture &SlicedTextureView::GetTexture() const noexcept
    {
        return m_texture;
    }

    vk::ImageView SlicedTextureView::GetImageView() const noexcept
    {
        return m_view.get();
    }
    SlicedSampledTextureView::SlicedSampledTextureView(
        RenderSystem &system, 
        SampledTexture &texture, 
        TextureSlice slice
    ) : SlicedTextureView(system, texture, slice)
    {
    }
}
