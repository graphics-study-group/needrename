#ifndef RENDER_RENDERSYSTEM_SUBMISSIONHELPER_INCLUDED
#define RENDER_RENDERSYSTEM_SUBMISSIONHELPER_INCLUDED

#include <queue>
#include <vector>
#include <functional>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class Buffer;
    class HomogeneousMesh;
    class AllocatedImage2DTexture;

    namespace RenderSystemState {
        /// @brief A helper for submitting data to GPU.
        /// Used in `FrameManager`.
        class SubmissionHelper final {
            using CmdOperation = std::function<void(vk::CommandBuffer)>;
        private:
            RenderSystem & m_system;

            std::queue <CmdOperation> m_pending_operations {};
            std::vector <Buffer> m_pending_dellocations {};

            vk::UniqueCommandBuffer m_one_time_cb {};
            vk::UniqueFence m_completion_fence {};
        public:
            SubmissionHelper (RenderSystem & system);

            void EnqueueVertexBufferSubmission(const HomogeneousMesh & mesh);
            void EnqueueTextureBufferSubmission(const AllocatedImage2DTexture& texture, const std::byte * data, size_t length);

            void StartFrame();
            void CompleteFrame();
        };
    }
}

#endif // RENDER_RENDERSYSTEM_SUBMISSIONHELPER_INCLUDED
