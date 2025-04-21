#ifndef PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class LayoutTransferHelper {
    public:
        enum class AttachmentBarrierType {
            // Barrier to transfer color attachment for read-only sampling. Read-after-Write hazard.
            ColorAttachmentRAW,
            // Barrier to trasnfer undefined texture to color attachment. Write-after-Read hazard.
            ColorAttachmentWAR,
            // Barrier to transfer depth attachment for read-only sampling. Read-after-Write hazard.
            DepthAttachmentRAW,
            // Barrier to transfer undefined texture to depth attachment. Write-after-Read hazard.
            DepthAttachmentWAR,
        };

        enum class AttachmentTransferType {
            ColorAttachmentPrepare,
            ColorAttachmentPresent,
            DepthAttachmentPrepare
        };

        enum class TextureTransferType {
            TextureUploadBefore,
            TextureUploadAfter,
            TextureClearBefore,
            TextureClearAfter
        };

        static vk::ImageMemoryBarrier2 GetAttachmentBarrier(AttachmentTransferType type, vk::Image image);
        static vk::ImageMemoryBarrier2 GetAttachmentBarrier(AttachmentBarrierType type, vk::Image image);
        static vk::ImageMemoryBarrier2 GetTextureBarrier(TextureTransferType type, vk::Image image);
    };
}

#endif // PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED
