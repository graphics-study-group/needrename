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

        void CommitVertexBuffer(const HomogeneousMesh & mesh);

        void CommitTextureImage(const AllocatedImage2DTexture & texture, std::byte * data, size_t length);

        void End();

        void SubmitAndExecute();
    protected:
        std::weak_ptr <RenderSystem> m_system {};
        vk::Queue m_queue {};
        vk::UniqueCommandBuffer m_handle {};
        vk::UniqueFence m_complete_fence {};

        // Temporary buffers created by transfer commands.
        // Cleared after transfer transactions are finished.
        std::list <Buffer> m_pending_buffers {};
    };
}

#endif // RENDER_PIPELINE_COMMANDBUFFER_TRANSFERCOMMANDBUFFER_INCLUDED
