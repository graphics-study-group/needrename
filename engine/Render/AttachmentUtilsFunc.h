#ifndef RENDER_ATTACHMENTUTILSFUNC
#define RENDER_ATTACHMENTUTILSFUNC

#include "AttachmentUtils.h"
#include "Render/Memory/Texture.h"
#include "Render/Memory/TextureSlice.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace AttachmentUtils {
        constexpr vk::AttachmentLoadOp GetVkLoadOp(LoadOperation op) {
            switch(op) {
                case LoadOperation::Load:
                    return vk::AttachmentLoadOp::eLoad;
                case LoadOperation::Clear:
                    return vk::AttachmentLoadOp::eClear;
                case LoadOperation::DontCare:
                    return vk::AttachmentLoadOp::eDontCare;
            }
            return vk::AttachmentLoadOp{};
        }

        constexpr vk::AttachmentStoreOp GetVkStoreOp(StoreOperation op) {
            switch(op) {
                case StoreOperation::Store:
                    return vk::AttachmentStoreOp::eStore;
                case StoreOperation::DontCare:
                    return vk::AttachmentStoreOp::eDontCare;
            }
            return vk::AttachmentStoreOp{};
        }

        constexpr vk::RenderingAttachmentInfo GetVkAttachmentInfo(
            const AttachmentDescription& desc, 
            vk::ImageLayout layout, 
            vk::ClearValue clear
        ) {
            vk::RenderingAttachmentInfo info {
                desc.image_view,
                layout,
                vk::ResolveModeFlagBits::eNone,
                nullptr,
                vk::ImageLayout::eUndefined,
                GetVkLoadOp(desc.load_op),
                GetVkStoreOp(desc.store_op),
                clear
            };
            return info;
        }

        constexpr vk::RenderingAttachmentInfo GetVkAttachmentInfo(
            vk::ImageView view, 
            AttachmentOp op,
            vk::ImageLayout layout
        ) {
            vk::RenderingAttachmentInfo info {
                view,
                layout,
                vk::ResolveModeFlagBits::eNone,
                nullptr,
                vk::ImageLayout::eUndefined,
                GetVkLoadOp(op.load_op),
                GetVkStoreOp(op.store_op),
                op.clear_value
            };
            return info;
        }
    }
}

#endif // RENDER_ATTACHMENTUTILSFUNC
