#ifndef RENDER_PIPELINE_COMMANDBUFFER_INCLUDED
#define RENDER_PIPELINE_COMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>



namespace Engine
{
    class RenderPass;
    class Pipeline;
    class Synchronization;

    class CommandBuffer
    {
    public:
        /// @brief Create a command buffer. This function is ideally only called from RenderSystem
        /// @param logical_device 
        /// @param command_pool 
        void CreateCommandBuffer(vk::Device logical_device, vk::CommandPool command_pool);

        void BeginRenderPass(const RenderPass & pass, uint32_t frame_index, vk::Extent2D extent);

        void BindPipelineProgram(const Pipeline & pipeline);

        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        void Draw();

        void End();

        void SubmitToQueue(
            vk::Queue queue, 
            const Synchronization & synch,
            uint32_t frame_index
        );

        void Reset();
    protected:
        vk::UniqueCommandBuffer m_handle {};
    };
} // namespace Engine


#endif // RENDER_PIPELINE_COMMANDBUFFER_INCLUDED
