#ifndef RENDER_MEMORY_IMAGE2D_INCLUDED
#define RENDER_MEMORY_IMAGE2D_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/ImageUtils.h"
#include "Render/Memory/ImageInterface.h"
#include "Render/Memory/AllocatedMemory.h"

namespace Engine {

    class RenderSystem;

    class AllocatedImage2D : public ImageInterface {
    protected:
        std::weak_ptr <RenderSystem> m_system;

        std::unique_ptr <AllocatedMemory> m_allocated_memory {};
        vk::UniqueImageView m_view {};

        ImageUtils::ImageFormat m_format {};
        vk::Extent2D m_extent{};
        uint32_t m_mip_level {};
    
    public:

        AllocatedImage2D(std::weak_ptr <RenderSystem> system);

        void Create(uint32_t width, uint32_t height, ImageUtils::ImageType type, ImageUtils::ImageFormat format, uint32_t mip = 1);

        vk::Image GetImage() const override;
        vk::ImageView GetImageView() const override;

        vk::Extent2D GetExtent() const;
    };
};

#endif // RENDER_MEMORY_IMAGE2D_INCLUDED
