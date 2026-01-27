#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHBUILDER_INCLUDED
#define PIPELINE_RENDERGRAPH_RENDERGRAPHBUILDER_INCLUDED

#include <memory>
#include <functional>

#include "Render/AttachmentUtils.h"
#include "Render/Memory/RenderTargetTexture.h"
#include "Render/Memory/MemoryAccessTypes.h"

namespace Engine {
    class RenderSystem;
    class GUISystem;
    class DeviceBuffer;
    class RenderGraph;
    class GraphicsCommandBuffer;
    class TransferCommandBuffer;
    class ComputeCommandBuffer;

    /**
     * @brief Helper class for building a `RenderGraph`.
     * 
     * Helps to resolve data dependencies between passes and set up barriers.
     */
    class RenderGraphBuilder {
        RenderSystem & m_system;
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:

        struct RGAttachmentDesc {
            using LoadOp = AttachmentUtils::LoadOperation;
            using StoreOp = AttachmentUtils::StoreOperation;
            using ClearValue = AttachmentUtils::ClearValue;

            int32_t rt_handle;
            LoadOp load_op{};
            StoreOp store_op{};
            ClearValue clear_value{};

            // These values control the image view used in the rendering.
            // They are currently unused. All renderings are defaulted
            // to be performed on the full image view.
            uint32_t base_mip {0};
            uint32_t base_array_layer {1};
            uint32_t mip_range {0};
            uint32_t array_layer_range {1};
        };

        RenderGraphBuilder(RenderSystem & system);
        ~RenderGraphBuilder();

        /**
         * @brief Register a new image texture to manage its access by the internal memo system.
         * 
         * The lifetime of this texture is managed by the caller.
         * This method facilitates persistent data usage between frames (for TAA, for example).
         * 
         * @return a handle to the managed resource, used in the render graph internally.
         * External resources will have negative handles.
         */
        int32_t ImportExternalResource (
            const RenderTargetTexture & texture,
            MemoryAccessTypeImageBits prev_access = MemoryAccessTypeImageBits::None
        );

        /**
         * @brief Register a new buffer to manage its access by the internal memo system.
         * 
         * @return a handle to the managed resource, used in the render graph internally.
         * External resources will have negative handles.
         */
        int32_t ImportExternalResource (
            const DeviceBuffer & buffer,
            MemoryAccessTypeBuffer prev_access = {MemoryAccessTypeBufferBits::None}
        );

        /**
         * @brief Request a new render target texture to be created when compiling the
         * render graph.
         * 
         * Such resources will have their lifetime managed automatically by the compiled
         * render graph.
         * 
         * @return a handle to the managed resource, used in the render graph internally.
         * Internal resouces will have positive handles.
         */
        int32_t RequestRenderTargetTexture (
            RenderTargetTexture::RenderTargetTextureDesc texture_description,
            RenderTargetTexture::SamplerDesc sampler_description
        ) noexcept;
        
        /**
         * @brief Mark an image to be used in the following pass.
         */
        void UseImage (
            int32_t texture_handle,
            MemoryAccessTypeImageBits access
        );

        /**
         * @brief Mark a buffer to be used in the following pass.
         */
        void UseBuffer (
            int32_t buffer_handle,
            MemoryAccessTypeBuffer access
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * To use this method, you have to manually set up render targets within the `pass` function.
         * You also cannot access any internal resources managed by the render graph.
         */
        void RecordRasterizerPassWithoutRT (
            std::function<void(GraphicsCommandBuffer &, const RenderGraph &)> pass
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * Depth attachment is defaulted to null.
         * This method automatically begins and terminates rendering.
         * Its extent is set to swapchain extent.
         */
        void RecordRasterizerPass (
            RGAttachmentDesc color,
            std::function<void(GraphicsCommandBuffer &, const RenderGraph &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * This method automatically begins and terminates rendering.
         * Its extent is set to swapchain extent.
         */
        void RecordRasterizerPass (
            RGAttachmentDesc color,
            RGAttachmentDesc depth,
            std::function<void(GraphicsCommandBuffer &, const RenderGraph &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * This method automatically begins and terminates rendering.
         * Its extent is set to swapchain extent.
         */
        void RecordRasterizerPass (
            std::initializer_list <RGAttachmentDesc> colors,
            RGAttachmentDesc depth,
            std::function<void(GraphicsCommandBuffer &, const RenderGraph &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass with transfer commands.
         */
        void RecordTransferPass (
            std::function<void(TransferCommandBuffer &, const RenderGraph &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass with compute shader dispatches.
         */
        void RecordComputePass (
            std::function<void(ComputeCommandBuffer &, const RenderGraph &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Create a RenderGraph and reset internal states to default.
         */
        RenderGraph BuildRenderGraph();

        /**
         * @brief Build a default render graph.
         * 
         * The render graph will have defaulted attachments.
         * Color and depth attachments will always have handle value 0 and 1 respectively.
         */
        RenderGraph BuildDefaultRenderGraph(
            uint32_t width, uint32_t height,
            GUISystem * gui_system = nullptr
        );
    };
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPHBUILDER_INCLUDED
