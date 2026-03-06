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
            gcb.SetRenderingInfo(rg.GetCurrentPassRuntimeInfo());
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
        auto f = [name = pass.name, system = &this->system,
                fn](vk::CommandBuffer cb, const RenderGraph2 & rg) {

            ComputeCommandBuffer ccb{cb, system->GetFrameManager().GetFrameInFlight()};
            DEBUG_CMD_START_LABEL(cb, std::format("{} (Compute)", name).c_str());
            std::invoke(fn, std::ref(ccb), std::cref(rg));
            DEBUG_CMD_END_LABEL(cb);
        };
        pass.pass_function = f;
        pass.actual_type = RenderGraphPassAffinity::Compute;
        pass.affinity = RenderGraphPassAffinity::Graphics;
        return *this;
    }
    RenderGraphPassBuilder &RenderGraphPassBuilder::WrapRenderPass() noexcept {
        assert(pass.actual_type == RenderGraphPassAffinity::Graphics);
        assert(pass.color_attachments.size() > 0 || static_cast<int32_t>(pass.depth_attachment.rt_handle) != 0);

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
            vk::RenderingAttachmentInfo dai{}, sai{};

            // Fill up color attachment info.
            for (size_t i = 0; i < ca.size(); i++) {
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

            if (static_cast<int32_t>(da.rt_handle) != 0) {
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

                if (ImageUtils::GetVkAspect(t->GetTextureDescription().format) & vk::ImageAspectFlagBits::eStencil) {
                    sai = dai;
                } else {
                    sai = {nullptr};
                }
            } else {
                dai = {nullptr};
                sai = {nullptr};
            }

            DEBUG_CMD_START_LABEL(cb, std::format("{} (Rasterizer)", name).c_str());
            cb.beginRendering(vk::RenderingInfo{
                vk::RenderingFlags{},
                rendering_area,
                1, 0,
                cai,
                &dai,
                &sai
            });
            wrapped(cb, rg);
            cb.endRendering();
            DEBUG_CMD_END_LABEL(cb);
        };
        pass.pass_function = f;
        return *this;
    }
} // namespace Engine
