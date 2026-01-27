#include "RenderGraphBuilder.h"

#include "UserInterface/GUISystem.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphUtils.hpp"
#include "Render/Pipeline/RenderGraph/RenderGraphTask.hpp"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace {
}

namespace Engine {
    struct RenderGraphBuilder::impl {

        struct Pass {
            RenderGraphImpl::PassType type;
            std::function <void(vk::CommandBuffer)> operation;
        };

        std::vector <Pass> m_tasks{};

        RenderGraphImpl::PassType GetPassType(int32_t pass_index) {
            assert(pass_index < 0 || pass_index < m_tasks.size());
            return pass_index < 0 ? RenderGraphImpl::PassType::None : m_tasks[pass_index].type;
        }

        RenderGraphImpl::TextureAccessMemo m_texture_memo;
        RenderGraphImpl::BufferAccessMemo m_buffer_memo;

        std::function <void(vk::CommandBuffer)> GetPrePassSynchronizationFunc(int32_t pass_index) {
            std::vector<vk::ImageMemoryBarrier2> image_barriers{};
            std::vector<vk::BufferMemoryBarrier2> buffer_barriers{};
            // Build image barriers
            for (const auto & [img, acc] : m_texture_memo.accesses) {
                if (acc.size() < 2) continue;
                
                auto itr = std::find_if(
                    acc.begin(),
                    acc.end(),
                    [pass_index](const RenderGraphImpl::TextureAccessMemo::Access & access) {
                        return access.pass_index == pass_index;
                    }
                );
                if (itr == acc.begin() || itr == acc.end()) continue;
                const auto pitr = (itr - 1);

                image_barriers.push_back(
                    m_texture_memo.GenerateBarrier(
                        img->GetImage(),
                        pitr->access,
                        GetPassType(pitr->pass_index),
                        itr->access,
                        GetPassType(itr->pass_index),
                        ImageUtils::GetVkAspect(img->GetTextureDescription().format)
                    )
                );
            }
            // Build buffer barriers
            for (const auto & [buf, acc] : m_buffer_memo.accesses) {
                if (acc.size() < 2) continue;

                auto itr = std::find_if(
                    acc.begin(),
                    acc.end(),
                    [pass_index](const RenderGraphImpl::BufferAccessMemo::Access & access) {
                        return access.pass_index == pass_index;
                    }
                );
                if (itr == acc.begin() || itr == acc.end()) continue;
                const auto pitr = (itr - 1);

                buffer_barriers.push_back(
                    m_buffer_memo.GenerateBarrier(
                        buf,
                        pitr->access,
                        GetPassType(pitr->pass_index),
                        itr->access,
                        GetPassType(itr->pass_index)
                    )
                );
            }

            return [
                ib = std::move(image_barriers),
                bb = std::move(buffer_barriers)
            ] (vk::CommandBuffer cb) -> void {
                vk::DependencyInfo dep{
                    vk::DependencyFlags{},
                    {},
                    bb,
                    ib
                };
                cb.pipelineBarrier2(dep);
            };
        }
    };

    RenderGraphBuilder::RenderGraphBuilder(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    RenderGraphBuilder::~RenderGraphBuilder() = default;

    void RenderGraphBuilder::ImportExternalResource(const Texture &texture, MemoryAccessTypeImageBits access) {
        if (pimpl->m_texture_memo.accesses.contains(&texture)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Importing an external texture resource %p after it has already been used by the render graph."
                "Previous dependencies will be cleared.",
                &texture
            );
        }
        pimpl->m_texture_memo.accesses[&texture] ={ {-1, {access}} };
    }

    void RenderGraphBuilder::UseImage(const Texture &texture, MemoryAccessTypeImageBits access) {
        pimpl->m_texture_memo.UpdateLastAccess(&texture, pimpl->m_tasks.size(), {access});
    }
    void RenderGraphBuilder::UseBuffer(const DeviceBuffer &buffer, MemoryAccessTypeBuffer access) {
        pimpl->m_buffer_memo.UpdateLastAccess(buffer.GetBuffer(), pimpl->m_tasks.size(), access);
    }

    void RenderGraphBuilder::RecordRasterizerPassWithoutRT(std::function<void(GraphicsCommandBuffer &)> pass) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass](vk::CommandBuffer cb) {

            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(pass, std::ref(gcb));
        };
        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Graphics,
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

        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Graphics,
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
        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Graphics,
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

        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Transfer,
                f
            }
        );
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

        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Compute,
                f
            }
        );
    }
    
    RenderGraph RenderGraphBuilder::BuildRenderGraph() {
        std::vector <std::function<void(vk::CommandBuffer)>> compiled;
        RenderGraphImpl::RenderGraphExtraInfo extra{};

        for (const auto & [img, acc] : pimpl->m_texture_memo.accesses) {
            extra.m_initial_image_access[img] = std::make_pair(pimpl->GetPassType(acc.front().pass_index), acc.front().access);
            extra.m_final_image_access[img] = std::make_pair(pimpl->GetPassType(acc.back().pass_index), acc.back().access);
        }

        for (size_t pass = 0; pass < pimpl->m_tasks.size(); pass++) {
            compiled.push_back(pimpl->GetPrePassSynchronizationFunc(pass));
            compiled.push_back(pimpl->m_tasks[pass].operation);
        }
        
        pimpl->m_texture_memo.accesses.clear();
        pimpl->m_buffer_memo.accesses.clear();
        pimpl->m_tasks.clear();
        return RenderGraph(m_system, std::move(compiled), std::move(extra));
    }
    RenderGraph RenderGraphBuilder::BuildDefaultRenderGraph(
        RenderTargetTexture &color_attachment, RenderTargetTexture &depth_attachment, GUISystem *gui_system
    ) {
        this->ImportExternalResource(color_attachment, MemoryAccessTypeImageBits::None);
        this->ImportExternalResource(depth_attachment, MemoryAccessTypeImageBits::None);
        this->UseImage(color_attachment, MemoryAccessTypeImageBits::ColorAttachmentWrite);
        this->UseImage(depth_attachment, MemoryAccessTypeImageBits::DepthStencilAttachmentWrite);
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
            this->UseImage(color_attachment, MemoryAccessTypeImageBits::ColorAttachmentWrite);
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
