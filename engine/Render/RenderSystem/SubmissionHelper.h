#ifndef RENDER_RENDERSYSTEM_SUBMISSIONHELPER_INCLUDED
#define RENDER_RENDERSYSTEM_SUBMISSIONHELPER_INCLUDED

#include <functional>
#include <queue>
#include <vector>

#include "Render/RenderSystem/FrameManagerComponent.h"

namespace Engine {
    class Texture;
    class HomogeneousMesh;

    namespace RenderSystemState {
        /// @brief A helper for submitting data to GPU.
        /// Used in `FrameManager`.
        class SubmissionHelper final : public IFrameManagerComponent {
            using CmdOperation = std::function<void(vk::CommandBuffer)>;

        private:
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            SubmissionHelper(RenderSystem &system);
            virtual ~SubmissionHelper();

            /***
             * @brief Enqueue a vertex buffer uploading.
             * Record corresponding memory
             * barriers and buffer writes to a disposable command buffer at the beginning of a frame.
             * A
             * staging buffer is created, and will be de-allocated at the end of the frame.
             * 
             * @param
             * mesh A homogeneous mesh whose vertex buffer is to be updated.
             */
            void EnqueueVertexBufferSubmission(const HomogeneousMesh &mesh);

            /***
             * @brief Enqueue a texture buffer submission. Record corresponding image
             * barriers and buffer writes to a disposable command buffer.
             * 
             * A staging buffer is created, and will be de-allocated at the end of the frame.
             * The layout of the image will be transferred to optimal for shader read after submission.
             *
             * Only color aspect and the very first level of mipmap is considered for submission, 
             * and no blitting or mipmap generation is recorded, which means that the data must
             * cover the whole image size and all array layers.
             * 
             * @param texture
             * @param data
             * Linearized buffer data. Refer to
             * https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#copies-buffers-images-addressing
             * for how to organize the data.
             * @param length
             */
            void EnqueueTextureBufferSubmission(const Texture &texture, const std::byte *data, size_t length);

            /**
             * @brief Enqueue a texture clear operation.
             * Record corresponding image barriers to a disposable command buffer, and issue a clear 
             * operation. The layout of the image will be transferred to optimal for shader read 
             * after clear operation.
             *
             * Useful for creating a blank default texture.
             * Only color aspect is cleared. All mipmap levels and arrays are cleared.
             * 
             * @param texture
             * @param color
             */
            void EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color_rgba);
            void EnqueueTextureClear(const Texture &texture, float depth);
            // void EnqueueTextureClear(const Texture &texture, std::tuple<float, uint8_t> depth_stencil);

            /***
             * @brief Execute staged submissions. 
             * Allocated a new command buffer if
             * needed, record all pending operations, and submit the
             * buffer to the graphics queue
             * allocated by the render system.
             */
            void ExecuteSubmission();

            void OnPreMainCbSubmission() override;

            /***
             * @brief Complete the frame. Wait for execution of the disposable command buffer,
             * de-allocate staging buffers,
             * reset the fence, and remove the command buffer.
             */
            void OnFrameComplete() override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_SUBMISSIONHELPER_INCLUDED
