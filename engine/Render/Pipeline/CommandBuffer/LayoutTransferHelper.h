#ifndef PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class LayoutTransferHelper {
    public:
        enum class TextureTransferType {
            TextureUploadBefore,
            TextureUploadAfter,
            TextureClearBefore,
            TextureClearAfter
        };
        static vk::ImageMemoryBarrier2 GetTextureBarrier(TextureTransferType type, vk::Image image);
    };
}

#endif // PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED
