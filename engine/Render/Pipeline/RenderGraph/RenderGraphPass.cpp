#include "RenderGraphPass.h"

#include "Render/AttachmentUtils.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/RenderSystem.h"
#if defined(ENGINE_RHI_BACKEND_VULKAN)
#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Backend/Vulkan/VulkanImageUtils.h"
#endif


namespace Engine {
    RenderGraphPassBuilder &RenderGraphPassBuilder::SetPassFunction(
        std::function<void(CommandBuffer &, const RenderGraph &)> fn
    ) noexcept {
        RenderGraphPassAffinity actual_type;
        bool is_graphics = false;

        switch (pass.affinity) {
            using enum RenderGraphPassAffinity;
        case Graphics:
            actual_type = Graphics;
            is_graphics = true;
            break;
        case Compute:
            actual_type = Compute;
            break;
        case Transfer:
            actual_type = Transfer;
            break;
        default:
            actual_type = Graphics;
            is_graphics = true;
            break;
        }

        auto f = [system = &this->system, fn, is_graphics](Rhi::CommandEncoder &encoder, const RenderGraph &rg) {
            CommandBuffer cmdbuf{*system, encoder, system->GetFrameManager().GetFrameInFlight()};
            if (is_graphics) {
                cmdbuf.SetRenderingInfo(rg.GetCurrentPassRuntimeInfo());
            }
            std::invoke(fn, std::ref(cmdbuf), std::cref(rg));
        };
        pass.pass_function = f;
        pass.actual_type = actual_type;
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
                     wrapped = pass.pass_function](Rhi::CommandEncoder &encoder, const RenderGraph &rg) {
#if defined(ENGINE_RHI_BACKEND_VULKAN)
            vk::CommandBuffer cb = Rhi::ToVulkanCommandBuffer(encoder.GetHandle());
#endif

            Rhi::RenderPassDescription description{};
            // A pass covers everything its attachments can cover; the smaller of
            // them wins, which is what the render area has always been.
            description.area = Rhi::Rect2D{
                Rhi::Offset2D{0, 0}, Rhi::Extent2D{std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()}
            };
            description.color_attachments.reserve(ca.size());
            for (const auto &attachment : ca) {
                auto t = rg.GetInternalTextureResource(attachment.rt_handle);
                Rhi::ColorAttachmentDescription color{};
                color.view = t->GetImageView(attachment.range);
                color.load = ToRhiLoadOperation(attachment.load_op);
                color.store = ToRhiStoreOperation(attachment.store_op);
                color.clear = ToRhiClearColor(attachment.clear_value);
                description.color_attachments.push_back(color);

                description.area.extent.width =
                    std::min(t->GetTextureDescription().width, description.area.extent.width);
                description.area.extent.height =
                    std::min(t->GetTextureDescription().height, description.area.extent.height);
            }

            if (static_cast<int32_t>(da.rt_handle) != 0) {
                auto t = rg.GetInternalTextureResource(da.rt_handle);
                description.depth_attachment.view = t->GetImageView(da.range);
                description.depth_attachment.load = ToRhiLoadOperation(da.load_op);
                description.depth_attachment.store = ToRhiStoreOperation(da.store_op);
                description.depth_attachment.clear_depth = ToRhiClearDepth(da.clear_value);
            }

#if defined(ENGINE_RHI_BACKEND_VULKAN)
            DEBUG_CMD_START_LABEL(cb, std::format("{} (Rasterizer)", name).c_str());
#endif
            encoder.BeginRenderPass(description);
            wrapped(encoder, rg);
            encoder.EndRenderPass();
#if defined(ENGINE_RHI_BACKEND_VULKAN)
            DEBUG_CMD_END_LABEL(cb);
#endif
        };
        pass.pass_function = f;
        return *this;
    }
} // namespace Engine





