#ifndef PIPELINE_RENDERGRAPH_RGATTACHMENTDESC
#define PIPELINE_RENDERGRAPH_RGATTACHMENTDESC

#include "Render/AttachmentUtils.h"
#include "Render/Memory/TextureSubresourceView.h"

namespace Engine {
    /// @brief Handle for textures in render graph.
    enum class RGTextureHandle : int32_t {
    };

    /// @brief Handle for buffers in render graph.
    enum class RGBufferHandle : int32_t {
    };

    /**
     * @brief Description of an attachment during rendering for render graphs.
     * @see `Engine::AttachmentUtils::AttachmentDescription`
     */
    template <class T>
    struct RGAttachmentDescTemplate {
        /// Load operation of the attachment.
        using LoadOp = AttachmentUtils::LoadOperation;
        /// Store operation of the attachment.
        using StoreOp = AttachmentUtils::StoreOperation;
        /// Clear value of the attachment.
        using ClearValue = AttachmentUtils::ClearValue;

        /// @brief Which RTT to be written to.
        T rt_handle{};
        /// @brief What subresources are used by the rendering pass.
        TextureSubresourceRange range{};
        /// @brief Load operation of the attachment.
        LoadOp load_op{};
        /// @brief Store operation of the attachment.
        StoreOp store_op{};
        /// @brief Clear value of the attachment.
        /// Only used if load operation is clear.
        ClearValue clear_value{};
    };

    using RGAttachmentDesc = RGAttachmentDescTemplate<int32_t>;
    using RGAttachmentDesc2 = RGAttachmentDescTemplate<RGTextureHandle>;
} // namespace Engine

#endif // PIPELINE_RENDERGRAPH_RGATTACHMENTDESC
