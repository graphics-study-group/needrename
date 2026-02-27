#include "RenderGraphPass.h"

#include "Render/DebugUtils.h"
#include "Render/RenderSystem.h"
#include "Render/Pipeline/RenderGraph2/RenderGraph2.h"

namespace Engine {
    RenderGraphPassBuilder &RenderGraphPassBuilder::SetRasterizerPassFunction(
        std::function<void(GraphicsCommandBuffer &, const RenderGraph2 &)> fn
    ) noexcept {
        auto f = [system = &this->system,
                fn](vk::CommandBuffer cb, const RenderGraph2 & rg) {
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(fn, std::ref(gcb), std::cref(rg));
        };
        pass.pass_function = f;
        pass.actual_type = RenderGraphPassAffinity::Graphics;
        pass.affinity = RenderGraphPassAffinity::Graphics;
        return *this;
    }

    RenderGraphPassBuilder &RenderGraphPassBuilder::SetComputePassFunction(
        std::function<void(ComputeCommandBuffer &, const RenderGraph2 &)> fn
    ) noexcept {
        auto f = [system = &this->system,
                fn](vk::CommandBuffer cb, const RenderGraph2 & rg) {
            ComputeCommandBuffer ccb{cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(fn, std::ref(ccb), std::cref(rg));
        };
        pass.pass_function = f;
        pass.actual_type = RenderGraphPassAffinity::Compute;
        pass.affinity = RenderGraphPassAffinity::Compute;
        return *this;
    }
    RenderGraphPassBuilder &RenderGraphPassBuilder::WrapRenderPass() noexcept {
        assert(pass.actual_type == RenderGraphPassAffinity::Graphics);
        assert(pass.color_attachments.size() > 0 || pass.depth_attachment.rt_handle != 0);

        auto f = [
            // These values are all copied
            ca = pass.color_attachments,
            da = pass.depth_attachment,
            name = pass.name,
            wrapped = pass.pass_function
        ](vk::CommandBuffer cb, const RenderGraph2 & rg) {
            // Construct rendering info

            vk::Rect2D rendering_area{
                {0, 0},
                {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()}
            };
            std::vector <vk::RenderingAttachmentInfo> cai{ca.size()};
            vk::RenderingAttachmentInfo dai{};

            // Fill up color attachment info.
            for (size_t i = 0; i < cai.size(); i++) {
                auto t = rg.GetInternalTextureResource(ca[i].rt_handle);
                cai[i] = vk::RenderingAttachmentInfo{
                    t->GetImageView(ca[i].range),
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ResolveModeFlagBits::eNone,
                    nullptr,
                    vk::ImageLayout::eUndefined,
                    AttachmentUtils::GetVkLoadOp(ca[i].load_op),
                    AttachmentUtils::GetVkStoreOp(ca[i].store_op),
                    AttachmentUtils::GetVkClearValue(ca[i].clear_value)
                };
                rendering_area.extent.width = std::min(
                    t->GetTextureDescription().width,
                    rendering_area.extent.width
                );
                rendering_area.extent.height = std::min(
                    t->GetTextureDescription().height,
                    rendering_area.extent.height
                );
            }

            if (da.rt_handle != 0) {
                auto t = rg.GetInternalTextureResource(da.rt_handle);
                dai = vk::RenderingAttachmentInfo{
                    t->GetImageView(da.range),
                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    vk::ResolveModeFlagBits::eNone,
                    nullptr,
                    vk::ImageLayout::eUndefined,
                    AttachmentUtils::GetVkLoadOp(da.load_op),
                    AttachmentUtils::GetVkStoreOp(da.store_op),
                    AttachmentUtils::GetVkClearValue(da.clear_value)
                };
            } else {
                dai = vk::RenderingAttachmentInfo{
                    nullptr
                };
            }

            cb.beginRendering(vk::RenderingInfo{
                vk::RenderingFlags{},
                rendering_area,
                1, 0,
                cai,
                &dai,
                &dai
            });
            DEBUG_CMD_START_LABEL(cb, std::format("Rasterizer Pass {}", name).c_str());
            wrapped(cb, rg);
            DEBUG_CMD_END_LABEL(cb);
            cb.endRendering();
        };
        pass.pass_function = f;
        return *this;
    }
} // namespace Engine
