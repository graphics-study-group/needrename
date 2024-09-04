#ifndef RENDER_PIPELINE_COMMANDBUFFER_INCLUDED
#define RENDER_PIPELINE_COMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>

namespace Engine
{
    class RenderPass;
    class Material;
    class Synchronization;
    class HomogeneousMesh;

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
        void CreateCommandBuffer(std::shared_ptr<RenderSystem> system, vk::CommandPool command_pool, uint32_t inflight_frame_index);

        void Begin();

        void BeginRenderPass(const RenderPass & pass, vk::Extent2D extent, uint32_t framebuffer_id);

        /// @brief Bind a material for rendering, and write per-material descriptors.
        /// @param material 
        /// @param pass_index 
        void BindMaterial(const Material & material, uint32_t pass_index);

        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        void CommitVertexBuffer(const HomogeneousMesh & mesh);

        /// @brief Write per-mesh descriptors, and send draw call to GPU.
        /// @param mesh 
        void DrawMesh(const HomogeneousMesh & mesh);

        void End();

        void SubmitToQueue(
            vk::Queue queue, 
            const Synchronization & synch
        );

        void Reset();
    protected:
        uint32_t m_inflight_frame_index {};
        vk::UniqueCommandBuffer m_handle {};
        std::weak_ptr <RenderSystem> m_system {};
        std::optional<std::pair <std::reference_wrapper<const Material>, uint32_t>> m_bound_material {};
    };
} // namespace Engine


#endif // RENDER_PIPELINE_COMMANDBUFFER_INCLUDED
