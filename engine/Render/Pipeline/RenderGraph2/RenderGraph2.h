#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPH2
#define PIPELINE_RENDERGRAPH2_RENDERGRAPH2

namespace vk {
    struct CommandBuffer;
}

namespace Engine {
    struct RenderGraph2Context;
    class RenderGraphCompiledPass;
    class RenderGraph2ExtraInfo;
    class PipelineRuntimeInfoPerRendering;

    enum class RGTextureHandle : int32_t;
    enum class RGBufferHandle : int32_t;

    /**
     * @brief Render Graph 2.
     *
     * Contains a list of passes compiled by the builder.
     * It holds ownerships of transient render target textures, and compiles
     * image barriers accordingly.
     */
    class RenderGraph2 {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        RenderGraph2(std::vector<RenderGraphCompiledPass> &&passes, RenderGraph2ExtraInfo &&extra) noexcept;
        ~RenderGraph2() noexcept;

        /**
         * @brief Add an external input dependency on a texture for this frame.
         * Useful for setting up temporal reused textures.
         *
         * This method can only be used on imported resources. It may insert
         * a full pipeline barrier to effectuate the desired access, so use this
         * method sparingly.
         */
        void AddExternalInputDependency(RGTextureHandle rt_handle, MemoryAccessTypeImageBits access);

        /**
         * @brief Add an external output dependency on a texture for this frame.
         * Useful for setting up textures read-back by CPU side.
         *
         * This method can only be used on imported resources. It may insert
         * a full pipeline barrier to effectuate the desired access, so use this
         * method sparingly.
         */
        void AddExternalOutputDependency(RGTextureHandle rt_handle, MemoryAccessTypeImageBits access);

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
        void Record(uint32_t pass, vk::CommandBuffer cb, const RenderGraph2Context &) const;

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
        void RecordAllPasses(vk::CommandBuffer, RenderSystem &);

        /**
         * @brief Execute the render graph by recording all commands onto the
         * main command buffer and submitting it for execution.
         *
         * This method disregards task affinities, and enforces serialized
         * start of execution on GPU.
         */
        void Execute(RenderSystem &system);
    };
} // namespace Engine

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPH2
