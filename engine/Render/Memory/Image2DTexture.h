#ifndef RENDER_MEMORY_IMAGE2DTEXTURE_INCLUDED
#define RENDER_MEMORY_IMAGE2DTEXTURE_INCLUDED

#include "Render/Memory/Image2D.h"

namespace Engine {
    class RenderSystem;
    class Buffer;

    class AllocatedImage2DTexture : public AllocatedImage2D {
    protected:
        using AllocatedImage2D::Create;

    public:
        AllocatedImage2DTexture(std::weak_ptr <RenderSystem> system);
        void Create(uint32_t width, uint32_t height, ImageUtils::ImageFormat format, uint32_t mip = 1);
        
        Buffer CreateStagingBuffer() const;
    };
}

#endif // RENDER_MEMORY_IMAGE2DTEXTURE_INCLUDED
