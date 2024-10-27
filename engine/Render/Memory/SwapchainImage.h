#ifndef RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED
#define RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED

#include "Render/Memory/RenderImageTexture.h"
#include <vector>

namespace Engine {
    class SwapchainImage : public RenderImageTexture {
        
    public:
        SwapchainImage(std::vector <vk::Image> _images, std::vector <vk::ImageView> _image_views, bool is_depth = false);
    };
}

#endif // RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED
