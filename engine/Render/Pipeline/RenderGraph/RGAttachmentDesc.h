#ifndef PIPELINE_RENDERGRAPH_RGATTACHMENTDESC
#define PIPELINE_RENDERGRAPH_RGATTACHMENTDESC

#include "Render/AttachmentUtils.h"
#include "Render/Memory/TextureSubresourceView.h"

namespace Engine {
    struct RGAttachmentDesc {
        using LoadOp = AttachmentUtils::LoadOperation;
        using StoreOp = AttachmentUtils::StoreOperation;
        using ClearValue = AttachmentUtils::ClearValue;

        int32_t rt_handle{};
        TextureSubresourceRange range{};

        LoadOp load_op{};
        StoreOp store_op{};
        ClearValue clear_value{};
    };
}

#endif // PIPELINE_RENDERGRAPH_RGATTACHMENTDESC
