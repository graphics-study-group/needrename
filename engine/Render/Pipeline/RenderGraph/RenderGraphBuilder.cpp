#include "RenderGraphBuilder.h"

#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "GUI/GUISystem.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    struct RenderGraphBuilder::impl {
        std::vector <vk::ImageMemoryBarrier2> m_image_barriers {};
        std::vector <vk::BufferMemoryBarrier2> m_buffer_barriers {};
        std::vector <std::function<void(vk::CommandBuffer)>> m_commands {};

        static vk::ImageMemoryBarrier2 GetImageBarrier(
            Texture & texture, 
            AccessHelper::ImageAccessType new_access, 
            AccessHelper::ImageAccessType prev_access
        ) noexcept {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.image = texture.GetImage();
            barrier.subresourceRange = vk::ImageSubresourceRange{
                ImageUtils::GetVkAspect(texture.GetTextureDescription().format),
                0,
                vk::RemainingMipLevels,
                0,
                vk::RemainingArrayLayers
            };

            if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eNone) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
                barrier.subresourceRange.aspectMask =
                    vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
            }

            std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) =
                AccessHelper::GetAccessScope(new_access);
            std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) =
                AccessHelper::GetAccessScope(prev_access);
            barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
            return barrier;
        }

        static vk::BufferMemoryBarrier2 GetBufferBarrier (
            Buffer & buffer [[maybe_unused]],
            AccessHelper::BufferAccessType new_access [[maybe_unused]],
            AccessHelper::BufferAccessType prev_access [[maybe_unused]]
        ) {
            assert(!"Unimplemented");
            vk::BufferMemoryBarrier2 barrier {};
            return barrier;
        }
    };

    RenderGraphBuilder::RenderGraphBuilder(
        RenderSystem & system
    ) : m_system(system), pimpl(std::make_unique<impl>())
    {

    }

    RenderGraphBuilder::~RenderGraphBuilder() = default;

    void RenderGraphBuilder::UseImage(
        Texture &texture, AccessHelper::ImageAccessType new_access, AccessHelper::ImageAccessType prev_access
    ) {
        pimpl->m_image_barriers.push_back(pimpl->GetImageBarrier(texture, new_access, prev_access));
    }
    void RenderGraphBuilder::UseBuffer(
        Buffer &buffer, AccessHelper::BufferAccessType new_access, AccessHelper::BufferAccessType prev_access
    ) {
        pimpl->m_buffer_barriers.push_back(pimpl->GetBufferBarrier(buffer, new_access, prev_access));
    }
    void RenderGraphBuilder::RecordRasterizerPass(std::function<void(GraphicsCommandBuffer &)> pass) {

        std::function <void(vk::CommandBuffer)> f = [
            system = &this->m_system,
            pass,
            bb = std::move(pimpl->m_buffer_barriers), 
            ib = std::move(pimpl->m_image_barriers)
        ] (vk::CommandBuffer cb) {
            vk::DependencyInfo dep{
                vk::DependencyFlags{},
                {},
                bb,
                ib
            };
            cb.pipelineBarrier2(dep);
            GraphicsContext gc = system->GetFrameManager().GetGraphicsContext();
            GraphicsCommandBuffer & gcb = dynamic_cast<GraphicsCommandBuffer &>(
                gc.GetCommandBuffer()
            );
            std::invoke(pass, std::ref(gcb));
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    void RenderGraphBuilder::RecordTransferPass(std::function<void(TransferCommandBuffer &)> pass) {
        assert(!"Unimplmented");
    }
    void RenderGraphBuilder::RecordComputePass(std::function<void(ComputeCommandBuffer &)> pass) {
        std::function <void(vk::CommandBuffer)> f = [
            system = &this->m_system, 
            pass,
            bb = std::move(pimpl->m_buffer_barriers), 
            ib = std::move(pimpl->m_image_barriers)
        ] (vk::CommandBuffer cb) {
            vk::DependencyInfo dep{
                vk::DependencyFlags{},
                {},
                bb,
                ib
            };
            cb.pipelineBarrier2(dep);
            ComputeContext cc = system->GetFrameManager().GetComputeContext();
            ComputeCommandBuffer & ccb = dynamic_cast<ComputeCommandBuffer &>(
                cc.GetCommandBuffer()
            );
            std::invoke(pass, std::ref(ccb));
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    void RenderGraphBuilder::RecordSynchronization() {
        std::function <void(vk::CommandBuffer)> f = [
            bb = std::move(pimpl->m_buffer_barriers), 
            ib = std::move(pimpl->m_image_barriers)
        ] (vk::CommandBuffer cb) {
            vk::DependencyInfo dep{
                vk::DependencyFlags{},
                {},
                bb,
                ib
            };
            cb.pipelineBarrier2(dep);
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    RenderGraph RenderGraphBuilder::BuildRenderGraph() {
        auto cmd = std::move(pimpl->m_commands);
        pimpl->m_commands.clear();
        if (!pimpl->m_buffer_barriers.empty() || !pimpl->m_image_barriers.empty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Leftover memory barriers when building render graph.");
            pimpl->m_buffer_barriers.clear();
            pimpl->m_image_barriers.clear();
        }
        return RenderGraph(cmd);
    }
    RenderGraph RenderGraphBuilder::BuildDefaultRenderGraph(
        Texture &color_attachment, 
        Texture &depth_attachment,
        GUISystem * gui_system
    ) {
        using IAT = AccessHelper::ImageAccessType;

        this->UseImage(color_attachment, IAT::ColorAttachmentWrite, IAT::None);
        this->UseImage(depth_attachment, IAT::DepthAttachmentWrite, IAT::None);
        this->RecordRasterizerPass(
            [this, &color_attachment, &depth_attachment](Engine::GraphicsCommandBuffer & gcb) {
                gcb.BeginRendering(
                    {
                        &color_attachment, 
                        nullptr, 
                        AttachmentUtils::LoadOperation::Clear, 
                        AttachmentUtils::StoreOperation::Store
                    },
                    {
                        &depth_attachment,
                        nullptr,
                        AttachmentUtils::LoadOperation::Clear, 
                        AttachmentUtils::StoreOperation::DontCare
                    },
                    this->m_system.GetSwapchain().GetExtent()
                );
                gcb.DrawRenderers(this->m_system.GetRendererManager().FilterAndSortRenderers({}), 0);
                gcb.EndRendering();
            }
        );

        if (gui_system) {
            this->UseImage(color_attachment, IAT::ColorAttachmentWrite, IAT::ColorAttachmentWrite);
            this->RecordRasterizerPass(
                [this, gui_system, &color_attachment](Engine::GraphicsCommandBuffer & gcb) {
                    gui_system->DrawGUI(
                        {
                            &color_attachment, 
                            nullptr, 
                            AttachmentUtils::LoadOperation::Load, 
                            AttachmentUtils::StoreOperation::Store
                        }, 
                        this->m_system.GetSwapchain().GetExtent(), 
                        gcb
                    );
                }
            );
        }
        return BuildRenderGraph();
    }
} // namespace Engine
