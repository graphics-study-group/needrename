#ifndef PIPELINE_RENDERGRAPH_RGATTACHMENTDESC
#define PIPELINE_RENDERGRAPH_RGATTACHMENTDESC

#include "Render/AttachmentUtils.h"
#include "Render/Memory/TextureSubresourceView.h"

namespace Engine {
    enum class RGTextureHandle : int32_t {};
    enum class RGBufferHandle : int32_t {};

    template <class T>
    struct RGAttachmentDescTemplate {
        using LoadOp = AttachmentUtils::LoadOperation;
        using StoreOp = AttachmentUtils::StoreOperation;
        using ClearValue = AttachmentUtils::ClearValue;

        T rt_handle{};
        TextureSubresourceRange range{};

        LoadOp load_op{};
        StoreOp store_op{};
        ClearValue clear_value{};
    };

    using RGAttachmentDesc = RGAttachmentDescTemplate<int32_t>;
    using RGAttachmentDesc2 = RGAttachmentDescTemplate<RGTextureHandle>;
}

#endif // PIPELINE_RENDERGRAPH_RGATTACHMENTDESC
