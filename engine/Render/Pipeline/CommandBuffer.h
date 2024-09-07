#ifndef RENDER_PIPELINE_COMMANDBUFFER_INCLUDED
#define RENDER_PIPELINE_COMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>
#include <list>

namespace Engine
{
    class RenderPass;
    class Material;
    class Synchronization;
    class HomogeneousMesh;
    class Buffer;
    class AllocatedImage2DTexture;

    namespace ConstantData {
        struct PerCameraStruct;
    }

    /// @brief A command buffer used for rendering.
    class RenderCommandBuffer
    {
    public:
        /// @brief Create a command buffer used for rendering.
        /// This function is ideally only called from RenderSystem
        /// @param logical_device 
        /// @param command_pool 
        void CreateCommandBuffer(
            std::shared_ptr<RenderSystem> system, 
            vk::CommandPool command_pool, 
            vk::Queue queue, 
            uint32_t inflight_frame_index
        );

        void Begin();

        void BeginRenderPass(const RenderPass & pass, vk::Extent2D extent, uint32_t framebuffer_id);

        /// @brief Bind a material for rendering, and write per-material descriptors.
        /// @param material 
        /// @param pass_index 
        void BindMaterial(const Material & material, uint32_t pass_index);

        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        /// @brief Write per-mesh descriptors, and send draw call to GPU.
        /// @param mesh 
        void DrawMesh(const HomogeneousMesh & mesh);

        void End();

        void Submit(
            const Synchronization & synch
        );

        void Reset();
    protected:
        uint32_t m_inflight_frame_index {};
        vk::UniqueCommandBuffer m_handle {};
        vk::Queue m_queue {};
        std::weak_ptr <RenderSystem> m_system {};
        std::optional<std::pair <std::reference_wrapper<const Material>, uint32_t>> m_bound_material {};
    };

    /// @brief A dispensable command buffer for one-time command like transfer.
    /// @note Due to possible hazards, a transfer command can only be 
    /// recorded when rendering is completely halted by VkDeviceWaitIdle().
    /// This can have dire consequences over performance, and should be avoided at all costs.
    class OneTimeCommandBuffer {
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
} // namespace Engine


#endif // RENDER_PIPELINE_COMMANDBUFFER_INCLUDED
