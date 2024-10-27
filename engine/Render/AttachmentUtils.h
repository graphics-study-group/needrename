#ifndef ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
#define ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace AttachmentUtils {
        enum class LoadOperation {
            Load,
            Clear,
            DontCare
        };

        enum class StoreOperation {
            Store,
            DontCare
        };

        struct AttachmentDescription {
            vk::Image image;
            vk::ImageView image_view;
            vk::AttachmentLoadOp load_op;
            vk::AttachmentStoreOp store_op;
        };

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

        constexpr vk::RenderingAttachmentInfo GetVkAttachmentInfo(const AttachmentDescription& desc, vk::ImageLayout layout, vk::ClearValue clear) {
            vk::RenderingAttachmentInfo info {
                desc.image_view,
                layout,
                vk::ResolveModeFlagBits::eNone,
                nullptr,
                vk::ImageLayout::eUndefined,
                desc.load_op,
                desc.store_op,
                clear
            };
            return info;
        }
    }
}

#endif // ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
