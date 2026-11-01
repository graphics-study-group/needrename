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
         * @brief Bind a compute pipeline and the layout its constants are read from.
         *
         * @param pipeline The compute pipeline to record with.
         * @param layout The pipeline layout, which is what a backend needs to
         * place the constant block the pipeline reads.
         */
        void BindComputePipeline(PipelineHandle pipeline, PipelineLayoutHandle layout);

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

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMMANDENCODER_INCLUDED




