#ifndef PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED

#include "Render/Pipeline/CommandBuffer/AccessHelperTypes.h"
#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"

namespace Engine {
    class Texture;

    /**
     * @brief A command buffer used for transfer operations.
     * 
     * Here the transfer operations are
     * defined by access flags and pipeline stages
     * associated with each command, rather than queue family
     * capabilities. For example,
     * Blitting is associated with `VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT` stage, and
     * is
     * therefore included here even if it can only be submitted to a graphic queue.
     * 
     * For data
     * transfer between GPU and host memory, refer to `SubmissionHelper`.
     */
    class TransferCommandBuffer : public ICommandBuffer {
    public:

        /**
         * @brief An area of a texture, used for operations such as blitting
         */
        struct TextureArea {
            uint32_t mip_level;
            uint32_t array_layer_base;
            uint32_t array_layer_count;
            int32_t x0, y0, z0, x1, y1, z1;
        };

        TransferCommandBuffer(RenderSystem &system, vk::CommandBuffer cb);
        virtual ~TransferCommandBuffer() = default;

        TransferCommandBuffer(const TransferCommandBuffer &) = delete;
        TransferCommandBuffer(TransferCommandBuffer &&) = default;
        TransferCommandBuffer &operator=(const TransferCommandBuffer &) = delete;
        TransferCommandBuffer &operator=(TransferCommandBuffer &&) = default;

        /**
         * @brief Blit a color texture to another color texture.
         * Whole texture of the zeroth mip level is blitted.
         */
        void BlitColorImage(const Texture & src, const Texture & dst);
        /**
         * @brief Blit a color texture to another color texture.
         */
        void BlitColorImage(const Texture & src, const Texture & dst, TextureArea src_area, TextureArea dst_area);

        // void ClearColorImage(const Texture & img, std::tuple<float, float, float, float> rgba_value);

        // void ClearDepthStencilImage(const Texture & img, std::tuple<float, uint8_t> depth_stencil_value);

        /**
         * @brief Record a series of commands to generate the mipmap chain.
         * It is synchronized
         * internally, so you don't need to insert barriers through the context
         * before executing this
         * command.
         * 
         * In our synchronization system, this command is considered as a transfer read
         * operation,
         * and should be synchronized as such.
         */
        void GenerateMipmaps(const Texture &img, AccessHelper::ImageAccessType previousAccess);

    protected:
        RenderSystem &m_system;
    };
}; // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
