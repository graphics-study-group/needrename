#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPHBUILDER2
#define PIPELINE_RENDERGRAPH2_RENDERGRAPHBUILDER2

#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Memory/RenderTargetTexture.h"

namespace Engine {
    class RenderGraph2;
    class RenderGraphPass;
    class RenderSystem;
    class DeviceBuffer;
    class RRTTHandle;

    class RenderGraphBuilder2 {
        struct impl;
        RenderSystem &system;
        std::unique_ptr<impl> pimpl;

    public:
        RenderGraphBuilder2(RenderSystem &system);
        ~RenderGraphBuilder2();

        /**
         * @brief Register a new image texture to manage its access internally.
         *
         * The lifetime of this texture is managed by the caller.
         * This method facilitates persistent data usage between frames
         * (for TAA, for example).
         *
         * The reference is non const as new views might be generated during
         * rendering.
         *
         * @return a handle to the managed resource, used in the render graph
         * internally. External resources will have negative handles.
         */
        [[nodiscard]]
        RGTextureHandle ImportExternalResource(
            RenderTargetTexture &texture, MemoryAccessTypeImageBits prev_access = MemoryAccessTypeImageBits::None
        );
        [[nodiscard]]
        RGTextureHandle ImportExternalResource(
            RRTTHandle texture, MemoryAccessTypeImageBits prev_access = MemoryAccessTypeImageBits::None
        );

        /**
         * @brief Register a new buffer to manage its access internally.
         *
         * @return a handle to the managed resource, used in the render graph
         * internally. External resources will have negative handles.
         */
        [[nodiscard]]
        RGBufferHandle ImportExternalResource(
            const DeviceBuffer &buffer, MemoryAccessTypeBuffer prev_access = {MemoryAccessTypeBufferBits::None}
        );

        /**
         * @brief Request a new render target texture to be created when
         * compiling the render graph.
         *
         * Such resources will have their lifetime managed automatically by the
         * compiled render graph.
         * Render target allocated by this method is considered transient. They
         * cannot be used across frames. If you wish to use a persistent render
         * texture, import it with `ImportExternalResource` method.
         *
         * @return a handle to the managed resource, used in the render graph
         * internally. Internal resouces will have positive handles.
         */
        [[nodiscard]]
        RGTextureHandle RequestRenderTargetTexture(
            RenderTargetTexture::RenderTargetTextureDesc texture_description,
            RenderTargetTexture::SamplerDesc sampler_description,
            std::string_view name = ""
        ) noexcept;

        /**
         * @brief Request a new render target texture to be created when
         * compiling the render graph.
         *
         * This render target texture will be resizable and managed by the
         * manager attached to the current render system. However, its
         * ownership is mantained by the built render graph, and will be release
         * when destructing.
         *
         * @return a handle to the managed resource, used in the render graph
         * internally. Internal resouces will have positive handles.
         */
        [[nodiscard]]
        RGTextureHandle RequestResizableRenderTargetTexture(
            RenderTargetTexture::RenderTargetTextureDesc texture_description,
            RenderTargetTexture::SamplerDesc sampler_description,
            float scale_x = 1.0f,
            float scale_y = 1.0f,
            std::string_view name = ""
        );

        /**
         * @brief Add a pass to this render graph.
         *
         * Use `RenderGraphPassBuilder` to construct the pass.
         */
        void AddPass(RenderGraphPass &&pass) noexcept;

        /**
         * @brief Construct a render graph according to the passes.
         */
        RenderGraph2 BuildRenderGraph();
    };
} // namespace Engine

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPHBUILDER2
