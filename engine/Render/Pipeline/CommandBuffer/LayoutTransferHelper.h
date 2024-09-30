#ifndef PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class LayoutTransferHelper {
    public:

        enum class AttachmentTransferType {
            ColorAttachmentPrepare,
            ColorAttachmentPresent,
            DepthAttachmentPrepare
        };

        enum class TextureTransferType {
            TextureUploadBefore,
            TextureUploadAfter
        };

        static vk::ImageMemoryBarrier2 GetAttachmentBarrier(AttachmentTransferType type, vk::Image image);
        static vk::ImageMemoryBarrier2 GetTextureBarrier(TextureTransferType type, vk::Image image);
    private:
        static std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope1(AttachmentTransferType);
        static std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope2(AttachmentTransferType);
        static std::pair<vk::ImageLayout, vk::ImageLayout> GetLayouts(AttachmentTransferType);
        static vk::ImageAspectFlags GetAspectFlags(AttachmentTransferType);
    };
}

#endif // PIPELINE_COMMANDBUFFER_LAYOUTTRANSFERHELPER_INCLUDED
