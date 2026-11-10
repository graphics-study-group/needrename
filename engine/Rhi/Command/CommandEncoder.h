#ifndef ENGINE_RHI_COMMANDENCODER_INCLUDED
#define ENGINE_RHI_COMMANDENCODER_INCLUDED

#include "Rhi/Pipeline/PipelineDescription.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <cstdint>
#include <memory>
#include <span>

namespace Engine::Rhi {
    /**
     * @brief Records commands into one command buffer as a flat stream.
     *
     * The encoder is the seam's only recording surface. It does not expose
     * command-buffer or pass vocabulary: a caller records work and, where it
     * knows data has to be made visible to later work, records a synchronization
     * mark (`Barrier`). A backend that can only synchronize at coarse boundaries
     * carries the ordering in those boundaries; a backend with explicit barriers
     * records one at each mark. Either way the caller states *where* ordering is
     * needed and never *how* it is achieved.
     *
     * The encoder is constructed over a command buffer that some other layer
     * owns and hands out, which is why it holds an opaque handle rather than
     * creating one.
     *
     * @invariant An encoder records into exactly one command buffer for its
     * whole lifetime, and that command buffer is in the recording state.
     */
    class RHI_API CommandEncoder {
    public:
        /**
         * @brief Construct an encoder over an existing command buffer.
         *
         * @param handle The command buffer to record into, in the recording
         * state. The encoder does not take ownership of it.
         */
        explicit CommandEncoder(CommandBufferHandle handle);
        ~CommandEncoder();

        CommandEncoder(const CommandEncoder &) = delete;
        CommandEncoder &operator=(const CommandEncoder &) = delete;
        CommandEncoder(CommandEncoder &&) noexcept;
        CommandEncoder &operator=(CommandEncoder &&) noexcept;

        /**
         * @brief Record a synchronization mark.
         *
         * The mark says "work recorded after this point observes everything
         * recorded before it". It carries no stage or access information,
         * because that is the backend's business: a backend with explicit
         * barriers chooses the widest correct pair, and a backend that orders at
         * coarser boundaries makes the mark a no-op.
         *
         * @note The mark is meaningful only for shader-visible data. Transfers
         * and attachment transitions are the recording layer's own concern.
         */
        void Barrier();

        /**
         * @brief Transition an image from one access to another.
         *
         * A mark states that ordering is needed; this states what the ordering is
         * *for*: work recorded after it reads this image the way `to` describes,
         * and whatever wrote it did so the way `from` describes. A render graph
         * that rewrites a resource's layout between passes records one of these,
         * because a mark alone cannot say which layout to move to.
         *
         * @param barrier The image, the part of it, and the two access descriptions.
         */
        void Barrier(const ImageBarrier &barrier);

        /**
         * @brief Transition several images as one dependency.
         *
         * A render graph transitions everything a pass needs before the pass
         * records any work, and the transitions are one dependency: nothing
         * between them needs the earlier ones to have completed. Recording them
         * as one call says that, and a backend that can only express the batch at
         * a coarser boundary loses nothing by treating it as one.
         *
         * @param barriers The transitions, applied together.
         */
        void Barrier(std::span<const ImageBarrier> barriers);

        /**
         * @brief Copy one box of an image into one box of another.
         *
         * A blit is a transfer: it reads one layout and writes another, and both
         * are stated by the transitions recorded around it. The caller says
         * whether the copy filters when the boxes differ in size, because a
         * backend defaults differently.
         *
         * @param description Source, destination, boxes and layers.
         * @param linear Whether to filter when scaling.
         */
        void BlitImage(const ImageBlitDescription &description, bool linear);

        /**
         * @brief Begin a pass over the given attachments.
         *
         * A pass states its attachments, what to do with their previous contents,
         * and what to leave behind; between `BeginRenderPass` and `EndRenderPass`
         * the encoder only accepts drawing work. Vulkan binds the attachments per
         * pass and WebGPU fixes them for a pass's duration, which is why a pass is
         * described once rather than assembled call by call.
         *
         * @param description The attachments, their operations, and the area.
         */
        void BeginRenderPass(const RenderPassDescription &description);

        /// @brief End the pass opened by `BeginRenderPass`.
        void EndRenderPass();

        /**
         * @brief Close a compute pass opened implicitly by a bind or a dispatch.
         *
         * A backend whose command buffer has no pass concept - Vulkan's - has nothing to
         * do here; one whose pass is a separate object must close it before the recording
         * is finished. The seam has no `BeginComputePass` because a caller does not say
         * when compute starts: the first bind or dispatch does.
         *
         * Called by whoever finishes the recording. Calling it twice is not an error.
         */
        void EndComputePass();

        /**
         * @brief Bind a compute pipeline and the layout its constants are read from.
         *
         * @param pipeline The compute pipeline to record with.
         * @param layout The pipeline layout, which is what a backend needs to
         * place the constant block the pipeline reads.
         */
        void BindComputePipeline(PipelineHandle pipeline, PipelineLayoutHandle layout);

        /**
         * @brief Bind a graphics pipeline for the pass already opened.
         *
         * @param pipeline The graphics pipeline to record with.
         * @param layout The pipeline layout its bindings and constants belong to.
         */
        void BindGraphicsPipeline(PipelineHandle pipeline, PipelineLayoutHandle layout);

        /**
         * @brief Record the constant block of the currently bound graphics pipeline.
         *
         * The same idea as `SetKernelParams` one stage over: the values apply to
         * the draws that follow and have no identity of their own. Vulkan reads
         * them from push constants; a backend without them writes the block into
         * a window it binds.
         *
         * @param params The bytes to record, at most the layout's block size.
         */
        void PushConstants(std::span<const std::byte> params);

        /**
         * @brief Bind one descriptor set for graphics work.
         *
         * @param set_index Index of the set in the pipeline layout.
         * @param set The descriptor set to bind.
         * @param layout The layout the set is bound against. A material, a camera
         * and a scene each own their layout, so it is stated rather than taken
         * from the last pipeline bound.
         */
        void BindGraphicsDescriptorSet(
            uint32_t set_index,
            DescriptorSetHandle set,
            PipelineLayoutHandle layout,
            std::span<const uint32_t> dynamic_offsets = {}
        );

        /**
         * @brief Bind the vertex buffers a draw reads.
         *
         * @param buffers The buffers, in binding order, starting at binding zero.
         * @param offsets Byte offset into each buffer.
         */
        void BindVertexBuffers(std::span<const BufferHandle> buffers, std::span<const uint64_t> offsets);

        /**
         * @brief Bind the index buffer a draw reads.
         *
         * @param buffer The index buffer.
         * @param offset Byte offset to the first index.
         * @param wide Whether the indices are 32-bit rather than 16-bit.
         */
        void BindIndexBuffer(BufferHandle buffer, uint64_t offset, bool wide);

        /**
         * @brief Set the viewport and the scissor rectangle a pass draws through.
         *
         * A pass covers one area; the viewport and the scissor say the same thing
         * to two different parts of the pipeline, so they are set together.
         *
         * @param width Viewport width in pixels.
         * @param height Viewport height in pixels.
         * @param scissor The area fragments are kept within.
         */
        void SetViewport(float width, float height, const Rect2D &scissor);

        /**
         * @brief Record an indexed draw.
         *
         * @param index_count Number of indices to read.
         * @param instance_count Number of instances.
         * @param first_index Index to start at.
         * @param vertex_offset Value added to every index before lookup.
         * @param first_instance Instance to start at.
         */
        void DrawIndexed(
            uint32_t index_count,
            uint32_t instance_count,
            uint32_t first_index,
            int32_t vertex_offset,
            uint32_t first_instance
        );

        /**
         * @brief Record a non-indexed draw.
         *
         * A mesh whose vertices are generated in the shader has no index buffer
         * to bind; the count is the geometry.
         *
         * @param vertex_count Number of vertices to read.
         * @param instance_count Number of instances.
         * @param first_vertex Vertex to start at.
         * @param first_instance Instance to start at.
         */
        void Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

        /**
         * @brief Copy one buffer into another.
         *
         * @param source The buffer being read.
         * @param destination The buffer being written.
         * @param size Bytes to copy from the start of each.
         */
        void CopyBuffer(BufferHandle source, BufferHandle destination, uint64_t size);

        /**
         * @brief Fill a byte range of a buffer with a repeating 32-bit value.
         *
         * A fill is a transfer write, which is why the seam states its ordering
         * rather than leaving it to the caller: a shader that read the range
         * earlier in the recording is ordered before the fill, and the fill is
         * ordered before whatever reads the range afterwards. That is the shape a
         * per-step buffer needs - a compute pass consumes the old contents, the
         * fill clears them, the next step's compute pass reads the cleared range -
         * and a caller records it as one call with no barrier to pair with it.
         *
         * `offset` and `size` must be multiples of four, and the range must lie
         * inside the buffer.
         *
         * @note Not every backend has a patterned fill: one that only has a clear
         * rejects a non-zero `value` rather than writing zero in its place, so a
         * caller that needs a non-zero pattern must write it another way.
         *
         * @param destination The buffer being written.
         * @param offset Byte offset of the range.
         * @param size Byte length of the range.
         * @param value The 32-bit pattern written across the range.
         */
        void FillBuffer(BufferHandle destination, uint64_t offset, uint64_t size, uint32_t value);

        /**
         * @brief Copy a whole image into a buffer.
         *
         * The image is copied as one layer, tightly packed, from its first mip.
         * This is the read-back shape: a target of known extent and format lands
         * in a buffer the host can map.
         *
         * @param image The image being read.
         * @param destination The buffer being written.
         * @param extent The region to copy, from the image origin.
         * @param aspect Which aspect of the image to read.
         */
        void CopyImageToBuffer(
            ImageHandle image, BufferHandle destination, const Extent3D &extent, ImageAspect aspect
        );

        /**
         * @brief Bind one descriptor set for compute work.
         *
         * @param set_index Index of the set in the pipeline layout.
         * @param set The descriptor set to bind.
         */
        void BindDescriptorSet(uint32_t set_index, DescriptorSetHandle set);

        /**
         * @brief Record the constant block of the currently bound compute pipeline.
         *
         * The semantics are kernel parameters — the values one dispatch of this
         * kernel runs with — not a resource: they are consumed by the next
         * dispatch and have no identity a later dispatch could refer to.
         *
         * Vulkan records them as push constants while the block fits the device
         * limit. A backend without push constants writes them into a per-dispatch
         * window and binds that window, which is why the size contract comes from
         * the shader's reflection rather than from the caller.
         *
         * @param params The bytes to record, at most the reflected block size.
         */
        void SetKernelParams(std::span<const std::byte> params);

        /**
         * @brief Record one compute dispatch of the currently bound pipeline.
         *
         * @param group_count_x Workgroups in x.
         * @param group_count_y Workgroups in y.
         * @param group_count_z Workgroups in z.
         */
        void DispatchCompute(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

        /// @brief Get the command buffer this encoder records into.
        CommandBufferHandle GetHandle() const noexcept;

        /**
         * @brief The backend's open render pass object, for a library that records into it itself.
         *
         * Dear ImGui's backends record draw commands into a native object rather than through a
         * seam, and on the WebGPU backend that object is a render pass encoder. A caller that has
         * opened a pass through this encoder gets it here.
         *
         * @return A `WGPURenderPassEncoder` on the WebGPU backend, or null when no render pass is
         * open or the backend has no such object (Vulkan records into the command buffer, which
         * `GetHandle` already names).
         */
        [[nodiscard]] void *GetNativeRenderPass() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMMANDENCODER_INCLUDED










