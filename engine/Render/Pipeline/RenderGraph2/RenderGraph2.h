#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPH2
#define PIPELINE_RENDERGRAPH2_RENDERGRAPH2

namespace vk {
    struct CommandBuffer;
}

namespace Engine {

    class RenderGraphCompiledPass;
    class RenderGraph2ExtraInfo;

    enum class RGTextureHandle : int32_t;
    enum class RGBufferHandle : int32_t;

    class RenderGraph2 {
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        RenderGraph2(
            std::vector <RenderGraphCompiledPass> && passes,
            RenderGraph2ExtraInfo && extra
        ) noexcept;
        ~RenderGraph2() noexcept;

        /**
         * @brief Add an external input dependency on a texture for this frame.
         * Useful for setting up temporal reused textures.
         * 
         * This method can only be used on imported resources. It may insert
         * a full pipeline barrier to effectuate the desired access, so use this
         * method sparingly.
         */
        void AddExternalInputDependency(
            RGTextureHandle rt_handle,
            MemoryAccessTypeImageBits access
        );

        /**
         * @brief Add an external output dependency on a texture for this frame.
         * Useful for setting up textures read-back by CPU side.
         * 
         * This method can only be used on imported resources. It may insert
         * a full pipeline barrier to effectuate the desired access, so use this
         * method sparingly.
         */
        void AddExternalOutputDependency(
            RGTextureHandle rt_handle,
            MemoryAccessTypeImageBits access
        );

        /**
         * @brief Get a internally managed render target texture.
         * 
         * @return nullptr if handle is not available.
         */
        RenderTargetTexture * GetInternalTextureResource(
            RGTextureHandle handle
        ) const noexcept;

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
        void RecordPrePass (vk::CommandBuffer);

        /**
         * @brief Record synchronization after the render graph.
         * 
         * Such synchronization will only happen if external output dependencies
         * are specified.
         * External output dependencies will be reset.
         */
        void RecordPostPass (vk::CommandBuffer);

        /**
         * @brief Execute the render graph by recording all commands onto the
         * main command buffer and submitting it for execution.
         * 
         * This method disregards task affinities, and enforces serialized
         * execution on GPU.
         */
        void Execute(RenderSystem & system);
    };
}

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPH2
