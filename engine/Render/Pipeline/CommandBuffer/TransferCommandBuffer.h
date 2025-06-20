#ifndef PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"

namespace Engine {
    class Texture;

    /**
     * @brief A command buffer used for transfer operations.
     * 
     * Here the transfer operations are defined by access flags and pipeline stages
     * associated with each command, rather than queue family capabilities. For example,
     * Blitting is associated with `VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT` stage, and is
     * therefore included here even if it can only be submitted to a graphic queue.
     * 
     * For data transfer between GPU and host memory, refer to `SubmissionHelper`.
     */
    class TransferCommandBuffer : public ICommandBuffer {
    public:
        TransferCommandBuffer(RenderSystem & system, vk::CommandBuffer cb);
        virtual ~TransferCommandBuffer() = default;

        TransferCommandBuffer (const TransferCommandBuffer &) = delete;
        TransferCommandBuffer (TransferCommandBuffer &&) = default;
        TransferCommandBuffer & operator = (const TransferCommandBuffer &) = delete;
        TransferCommandBuffer & operator = (TransferCommandBuffer &&) = default;

        // void BlitImage(const Texture & src, const Texture & dst);

        // void ClearColorImage(const Texture & img, std::tuple<float, float, float, float> rgba_value);

        // void ClearDepthStencilImage(const Texture & img, std::tuple<float, uint8_t> depth_stencil_value);

        void GenerateMipmaps(const Texture & img);

    protected:
        RenderSystem & m_system;
    };
};

#endif // PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
