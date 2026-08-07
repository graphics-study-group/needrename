#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPH2
#define PIPELINE_RENDERGRAPH2_RENDERGRAPH2

#include "Rhi/MemoryAccessTypes.h"

namespace vk {
    struct CommandBuffer;
}

namespace Engine {

    class RenderGraphCompiledPass;
    class RenderGraph2ExtraInfo;
    struct PipelineRuntimeInfoPerRendering;

    enum class RGTextureHandle : int32_t;
    enum class RGBufferHandle : int32_t;

    /**
     * @brief Render Graph 2.
     *
     * Contains a list of passes compiled by the builder.
     * It holds ownerships of transient render target textures, and compiles
     * image barriers accordingly.
     */
    class RenderGraph {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        RenderGraph(std::vector<RenderGraphCompiledPass> &&passes, RenderGraph2ExtraInfo &&extra) noexcept;
        ~RenderGraph() noexcept;

        /**
         * @brief Add an external input dependency on a texture for this frame.
         * Useful for setting up temporal reused textures.
         *
         * This method can only be used on imported resources. It may insert
         * a full pipeline barrier to effectuate the desired access, so use this
         * method sparingly.
         */
        void AddExternalInputDependency(RGTextureHandle rt_handle, Rhi::MemoryAccessTypeImageBits access);

        /**
         * @brief Add an external output dependency on a texture for this frame.
         * Useful for setting up textures read-back by CPU side.
         *
         * This method can only be used on imported resources. It may insert
         * a full pipeline barrier to effectuate the desired access, so use this
         * method sparingly.
         */
        void AddExternalOutputDependency(RGTextureHandle rt_handle, Rhi::MemoryAccessTypeImageBits access);

        /**
         * @brief Get a internally managed render target texture.
         *
         * If the render target texture is managed by a `ResizableRTTManager`,
         * it will be automatically resolved.
         *
         * @return nullptr if handle is not available.
         */
        RenderTargetTexture *GetInternalTextureResource(RGTextureHandle handle) const noexcept;

        /**
         * @brief Request the graphics pipeline runtime information of the
         * current pass or subpass.
         */
        const PipelineRuntimeInfoPerRendering &GetCurrentPassRuntimeInfo() const noexcept;

        /**
         * @brief Record all operations of a given pass onto the specified
         * command buffer.
         */
        void Record(uint32_t pass, vk::CommandBuffer cb) const;

        /**
         * @brief Record synchronization prior to any passes.
         *
         * Such synchronization will only happen if external input dependencies
         * are specified.
         * External input dependencies will be reset.
         */
        void RecordPrePass(vk::CommandBuffer);

        /**
         * @brief Record synchronization after the render graph.
         *
         * Such synchronization will only happen if external output dependencies
         * are specified.
         * External output dependencies will be reset.
         */
        void RecordPostPass(vk::CommandBuffer);

        /**
         * @brief Record all passes onto the same command buffer.
         *
         * This method disregards task affinities, and enforces serialized
         * start of execution on GPU.
         */
        void RecordAllPasses(vk::CommandBuffer);

        /**
         * @brief Get the number of compiled passes in this render graph.
         */
        [[nodiscard]]
        uint32_t GetNumPasses() const noexcept;

        /**
         * @brief Record all passes of this render graph onto the main command
         * buffer of the given render system.
         *
         * Recording only — the command buffer is NOT ended or submitted here.
         * The caller must have started the main command buffer (via
         * `FrameManager::BeginMainCommandBuffer`) and must submit afterwards
         * via `RenderSystem::CompleteFrame` (→ `FrameManager::SubmitFrame`),
         * which ends, submits (main CB + copy CB in one batch) and presents.
         *
         * This method disregards task affinities, and enforces serialized
         * start of execution on GPU.
         *
         * @param system The render system whose current frame-in-flight main
         *               command buffer receives the recorded passes.
         */
        void RecordIntoMainCommandBuffer(RenderSystem &system);
    };
} // namespace Engine

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPH2
