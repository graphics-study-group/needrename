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

        struct AttachmentOp {
            vk::AttachmentLoadOp load_op {};
            vk::AttachmentStoreOp store_op {};
            vk::ClearValue clear_value {};
        };

        struct AttachmentDescription {
            vk::Image image {};
            vk::ImageView image_view {};
            vk::AttachmentLoadOp load_op {};
            vk::AttachmentStoreOp store_op {};
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

        constexpr vk::RenderingAttachmentInfo GetVkAttachmentInfo(vk::ImageView view, AttachmentOp op, vk::ImageLayout layout) {
            vk::RenderingAttachmentInfo info {
                view,
                layout,
                vk::ResolveModeFlagBits::eNone,
                nullptr,
                vk::ImageLayout::eUndefined,
                op.load_op,
                op.store_op,
                op.clear_value
            };
            return info;
        }
    }
}

#endif // ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
