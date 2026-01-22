#include "RenderGraphBuilder.h"

#include "UserInterface/GUISystem.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphUtils.hpp"
#include "Render/Pipeline/RenderGraph/RenderGraphTask.hpp"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace Engine {
    struct RenderGraphBuilder::impl {
        std::vector<vk::ImageMemoryBarrier2> m_image_barriers{};
        std::vector<vk::BufferMemoryBarrier2> m_buffer_barriers{};
        std::vector<RenderGraphImpl::Task> m_tasks{};

        RenderGraphImpl::TextureAccessMemo m_memo;
    };

    RenderGraphBuilder::RenderGraphBuilder(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    RenderGraphBuilder::~RenderGraphBuilder() = default;

    void RenderGraphBuilder::RegisterImageAccess(Texture &texture, AccessHelper::ImageAccessType prev_access) {
        pimpl->m_memo.RegisterTexture(&texture, AccessHelper::GetAccessScope(prev_access));
    }

    void RenderGraphBuilder::UseImage(Texture &texture, AccessHelper::ImageAccessType new_access) {
        auto new_tuple = AccessHelper::GetAccessScope(new_access);
        pimpl->m_image_barriers.push_back(
            RenderGraphImpl::GetImageBarrier(
                texture,
                pimpl->m_memo.GetAccessTuple(&texture),
                new_tuple
            )
        );
        pimpl->m_memo.UpdateAccessTuple(&texture, new_tuple);
    }
    void RenderGraphBuilder::UseBuffer(
        DeviceBuffer &buffer, AccessHelper::BufferAccessType new_access, AccessHelper::BufferAccessType prev_access
    ) {
        pimpl->m_buffer_barriers.push_back(RenderGraphImpl::GetBufferBarrier(buffer, prev_access, new_access));
    }
    void RenderGraphBuilder::RecordRasterizerPassWithoutRT(std::function<void(GraphicsCommandBuffer &)> pass) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass](vk::CommandBuffer cb) {

            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(pass, std::ref(gcb));
        };
        this->RecordSynchronization();
        pimpl->m_tasks.push_back(
            RenderGraphImpl::Command{
                RenderGraphImpl::Command::Type::Graphics,
                f
            }
        );
    }
    void RenderGraphBuilder::RecordRasterizerPass(
        AttachmentUtils::AttachmentDescription color,
        std::function<void(GraphicsCommandBuffer &)> pass,
        const std::string &name
    ) {
        this->RecordRasterizerPass(
            color,
            AttachmentUtils::AttachmentDescription{
                nullptr, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::DontCare
            },
            pass,
            name
        );
    }
    void RenderGraphBuilder::RecordRasterizerPass(
        AttachmentUtils::AttachmentDescription color,
        AttachmentUtils::AttachmentDescription depth,
        std::function<void(GraphicsCommandBuffer &)> pass,
        const std::string &name
    ) {

        if (!pimpl->m_memo.m_memo.contains(color.texture)
            || std::get<2>(pimpl->m_memo.m_memo[color.texture]) != vk::ImageLayout::eColorAttachmentOptimal) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Color attachment texture %p is not sychronized properly.",
                static_cast<const void *>(color.texture)
            );
        }
        if (depth.texture) {
            if (!pimpl->m_memo.m_memo.contains(depth.texture)
                || std::get<2>(pimpl->m_memo.m_memo[depth.texture])
                       != vk::ImageLayout::eDepthStencilAttachmentOptimal) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Depth attachment texture %p is not sychronized properly.",
                    static_cast<const void *>(depth.texture)
                );
            }
        }

        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    color,
                                                    depth,
                                                    name](vk::CommandBuffer cb) {
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            gcb.BeginRendering(color, depth, system->GetSwapchain().GetExtent(), name);
            std::invoke(pass, std::ref(gcb));
            gcb.EndRendering();
        };

        this->RecordSynchronization();
        pimpl->m_tasks.push_back(
            RenderGraphImpl::Command{
                RenderGraphImpl::Command::Type::Graphics,
                f
            }
        );
    }

    void RenderGraphBuilder::RecordRasterizerPass(
        std::initializer_list<AttachmentUtils::AttachmentDescription> colors,
        AttachmentUtils::AttachmentDescription depth,
        std::function<void(GraphicsCommandBuffer &)> pass,
        const std::string &name
    ) {
        for (const auto & color : colors) {
            if (!pimpl->m_memo.m_memo.contains(color.texture)
                || std::get<2>(pimpl->m_memo.m_memo[color.texture]) != vk::ImageLayout::eColorAttachmentOptimal) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Color attachment texture %p is not sychronized properly.",
                    static_cast<const void *>(color.texture)
                );
            }
        }
        if (depth.texture) {
            if (!pimpl->m_memo.m_memo.contains(depth.texture)
                || std::get<2>(pimpl->m_memo.m_memo[depth.texture])
                       != vk::ImageLayout::eDepthStencilAttachmentOptimal) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Depth attachment texture %p is not sychronized properly.",
                    static_cast<const void *>(depth.texture)
                );
            }
        }

        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    &colors,
                                                    depth,
                                                    name](vk::CommandBuffer cb) {

            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            gcb.BeginRendering(colors, depth, system->GetSwapchain().GetExtent(), name);
            std::invoke(pass, std::ref(gcb));
            gcb.EndRendering();
        };
        this->RecordSynchronization();
        pimpl->m_tasks.push_back(
            RenderGraphImpl::Command{
                RenderGraphImpl::Command::Type::Graphics,
                f
            }
        );
    }
    void RenderGraphBuilder::RecordTransferPass(
        std::function<void(TransferCommandBuffer &)> pass, 
        const std::string & name
    ) {
        std::function<void(vk::CommandBuffer)> f = [pass,
                                                    name](vk::CommandBuffer cb) {
            TransferCommandBuffer tcb{cb};
            tcb.GetCommandBuffer().beginDebugUtilsLabelEXT({(name + " (Transfer)").c_str()});
            std::invoke(pass, std::ref(tcb));
            tcb.GetCommandBuffer().endDebugUtilsLabelEXT();
        };

        this->RecordSynchronization();
        pimpl->m_tasks.push_back(RenderGraphImpl::Command{
            RenderGraphImpl::Command::Type::Transfer,
            f
        });
    }
    void RenderGraphBuilder::RecordComputePass(
        std::function<void(ComputeCommandBuffer &)> pass, const std::string &name
    ) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    name](vk::CommandBuffer cb) {
            ComputeCommandBuffer ccb{cb, system->GetFrameManager().GetFrameInFlight()};
            ccb.GetCommandBuffer().beginDebugUtilsLabelEXT({(name + " (Compute)").c_str()});
            std::invoke(pass, std::ref(ccb));
            ccb.GetCommandBuffer().endDebugUtilsLabelEXT();
        };

        this->RecordSynchronization();
        pimpl->m_tasks.push_back(
            RenderGraphImpl::Command{
                RenderGraphImpl::Command::Type::Compute,
                f
            }
        );
    }
    void RenderGraphBuilder::RecordSynchronization() {
        pimpl->m_tasks.push_back(
            RenderGraphImpl::Synchronization{
                {},
                std::move(pimpl->m_buffer_barriers),
                std::move(pimpl->m_image_barriers)
            }
        );
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    RenderGraph RenderGraphBuilder::BuildRenderGraph() {
        if (!pimpl->m_buffer_barriers.empty() || !pimpl->m_image_barriers.empty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Leftover memory barriers when building render graph.");
            RecordSynchronization();
        }

        std::vector <std::function<void(vk::CommandBuffer)>> compiled;
        RenderGraphImpl::RenderGraphExtraInfo extra{};

        extra.m_initial_image_access = std::move(pimpl->m_memo.m_initial_access);
        extra.m_final_image_access = std::move(pimpl->m_memo.m_memo);
        pimpl->m_memo.m_initial_access.clear();
        pimpl->m_memo.m_memo.clear();

        for (auto & t : pimpl->m_tasks) {
            if (auto p = std::get_if<RenderGraphImpl::Command>(&t)) {
                compiled.push_back(p->func);
            } else if (auto p = std::get_if<RenderGraphImpl::Synchronization>(&t)) {
                // Ideally we can merge barriers here.
                compiled.push_back(p->GetBarrierCommand());
            } else if (auto p = std::get_if<RenderGraphImpl::Present>(&t)) {
                // extra...
            }
        }
        
        pimpl->m_tasks.clear();
        return RenderGraph(m_system, std::move(compiled), std::move(extra));
    }
    RenderGraph RenderGraphBuilder::BuildDefaultRenderGraph(
        RenderTargetTexture &color_attachment, RenderTargetTexture &depth_attachment, GUISystem *gui_system
    ) {
        using IAT = AccessHelper::ImageAccessType;
        this->RegisterImageAccess(color_attachment, IAT::None);
        this->RegisterImageAccess(depth_attachment, IAT::None);
        this->UseImage(color_attachment, IAT::ColorAttachmentWrite);
        this->UseImage(depth_attachment, IAT::DepthAttachmentWrite);
        this->RecordRasterizerPass(
            {&color_attachment, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
            {&depth_attachment,
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            [this, &color_attachment, &depth_attachment](Engine::GraphicsCommandBuffer &gcb) {
                gcb.DrawRenderers("", this->m_system.GetRendererManager().FilterAndSortRenderers({}));
            }
        );

        if (gui_system) {
            this->UseImage(color_attachment, IAT::ColorAttachmentWrite);
            this->RecordRasterizerPassWithoutRT([this, gui_system, &color_attachment](Engine::GraphicsCommandBuffer &gcb) {
                gui_system->DrawGUI(
                    {&color_attachment,
                     nullptr,
                     AttachmentUtils::LoadOperation::Load,
                     AttachmentUtils::StoreOperation::Store},
                    this->m_system.GetSwapchain().GetExtent(),
                    gcb
                );
            });
        }
        return BuildRenderGraph();
    }
} // namespace Engine
