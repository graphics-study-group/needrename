#ifndef RENDER_ATTACHMENTUTILSFUNC
#define RENDER_ATTACHMENTUTILSFUNC

#include "AttachmentUtils.h"
#include "Render/Memory/RenderTargetTexture.h"
#include "Render/Memory/TextureSlice.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace AttachmentUtils {
        constexpr vk::AttachmentLoadOp GetVkLoadOp(LoadOperation op) {
            switch (op) {
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
            switch (op) {
            case StoreOperation::Store:
                return vk::AttachmentStoreOp::eStore;
            case StoreOperation::DontCare:
                return vk::AttachmentStoreOp::eDontCare;
            }
            return vk::AttachmentStoreOp{};
        }

        constexpr vk::ClearValue GetVkClearValue(ClearValue cv) {
            if (cv.index() == 0) {
                const auto &v = std::get<ColorClearValue>(cv);
                return vk::ClearValue{vk::ClearColorValue{v.r, v.g, v.b, v.a}};
            } else {
                const auto &v = std::get<DepthClearValue>(cv);
                return vk::ClearValue{vk::ClearDepthStencilValue{v.depth, v.stencil}};
            }
        }

        const inline vk::RenderingAttachmentInfo GetVkAttachmentInfo(
            const AttachmentDescription &desc, vk::ImageLayout layout
        ) noexcept {
            assert(desc.texture || desc.texture_view);

            if (desc.texture_view) {
                return vk::RenderingAttachmentInfo{
                    desc.texture_view->GetImageView(),
                    layout,
                    // We are not going to support MSAA...
                    vk::ResolveModeFlagBits::eNone,
                    nullptr,
                    vk::ImageLayout::eUndefined,
                    GetVkLoadOp(desc.load_op),
                    GetVkStoreOp(desc.store_op),
                    GetVkClearValue(desc.clear_value)
                };
            } else {
                return vk::RenderingAttachmentInfo{
                    desc.texture->GetImageView(),
                    layout,
                    // We are not going to support MSAA...
                    vk::ResolveModeFlagBits::eNone,
                    nullptr,
                    vk::ImageLayout::eUndefined,
                    GetVkLoadOp(desc.load_op),
                    GetVkStoreOp(desc.store_op),
                    GetVkClearValue(desc.clear_value)
                };
            }
        }
    } // namespace AttachmentUtils
} // namespace Engine

#endif // RENDER_ATTACHMENTUTILSFUNC
