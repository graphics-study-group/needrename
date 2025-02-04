#ifndef RENDER_PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
#define RENDER_PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>
#include <list>

namespace Engine {
    class HomogeneousMesh;
    class Buffer;
    class AllocatedImage2DTexture;

    /// @brief A dispensable command buffer for one-time command like transfer.
    /// @note Due to possible hazards, a transfer command must be synchronized properly, which heavily disrupts render pipeline.
    /// This can have dire consequences over performance, and should be avoided at all costs.
    class TransferCommandBuffer {
    public:
        void Create(std::shared_ptr <RenderSystem> system, vk::CommandPool command_pool, vk::Queue queue);

        void Begin();

        /// @brief Upload vertex buffer and vertex index buffer to GPU
        /// @param mesh mesh with vertex and index buffer
        void CommitVertexBuffer(const HomogeneousMesh & mesh);

        /// @brief Upload pixel data to a texture on GPU
        /// @param texture texture to be updated
        /// @param data pixel data, whose layout is specified by the texture
        /// @param length length of the pixel data, typically width * height * depth of the pixel (3 bytes for RGB and 4 bytes for RGBA)
        void CommitTextureImage(const AllocatedImage2DTexture & texture, const std::byte * data, size_t length);

        void End();

        void SubmitAndExecute();
    protected:
        RenderSystem * m_system {nullptr};

        vk::Queue m_queue {};
        vk::UniqueCommandBuffer m_handle {};
        vk::UniqueFence m_complete_fence {};

        // Temporary buffers created by transfer commands.
        // Cleared after transfer transactions are finished.
        std::list <Buffer> m_pending_buffers {};
    };
}

#endif // RENDER_PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
