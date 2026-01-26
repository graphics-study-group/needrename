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
        std::vector<RenderGraphImpl::Task> m_tasks{};
        std::vector <RenderGraphImpl::PassType> pass_types {};

        RenderGraphImpl::PassType GetPassType(int32_t pass_index) {
            return pass_index < 0 ? RenderGraphImpl::PassType::None : pass_types[pass_index];
        }

        RenderGraphImpl::TextureAccessMemo m_texture_memo;
        RenderGraphImpl::BufferAccessMemo m_buffer_memo;
    };

    RenderGraphBuilder::RenderGraphBuilder(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    RenderGraphBuilder::~RenderGraphBuilder() = default;

    void RenderGraphBuilder::ImportExternalResource(const Texture &texture, MemoryAccessTypeImageBits access) {
        pimpl->m_texture_memo.UpdateLastAccess(&texture, -1, {access});
    }

    void RenderGraphBuilder::UseImage(const Texture &texture, MemoryAccessTypeImageBits access) {
        pimpl->m_texture_memo.UpdateLastAccess(&texture, pimpl->pass_types.size(), {access});
    }
    void RenderGraphBuilder::UseBuffer(const DeviceBuffer &buffer, MemoryAccessTypeBuffer access) {
        pimpl->m_buffer_memo.UpdateLastAccess(buffer.GetBuffer(), pimpl->pass_types.size(), access);
    }

    void RenderGraphBuilder::RecordRasterizerPassWithoutRT(std::function<void(GraphicsCommandBuffer &)> pass) {
        pimpl->pass_types.push_back(RenderGraphImpl::PassType::Graphics);
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
        pimpl->pass_types.push_back(RenderGraphImpl::PassType::Graphics);
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
        pimpl->pass_types.push_back(RenderGraphImpl::PassType::Graphics);
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
        pimpl->pass_types.push_back(RenderGraphImpl::PassType::Transfer);
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
        pimpl->pass_types.push_back(RenderGraphImpl::PassType::Compute);
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
        const auto current_pass_id = pimpl->pass_types.size() - 1;
        std::vector<vk::ImageMemoryBarrier2> image_barriers{};
        std::vector<vk::BufferMemoryBarrier2> buffer_barriers{};
        // Build image barriers
        for (const auto & [img, acc] : pimpl->m_texture_memo.accesses) {
            if (acc.size() < 2) continue;
            if (acc.back().pass_index != current_pass_id)   continue;
            const auto & prev_acc = *(acc.rbegin() - 1);
            image_barriers.push_back(
                pimpl->m_texture_memo.GenerateBarrier(
                    img->GetImage(),
                    prev_acc.access,
                    pimpl->GetPassType(prev_acc.pass_index),
                    acc.back().access,
                    pimpl->GetPassType(acc.back().pass_index),
                    ImageUtils::GetVkAspect(img->GetTextureDescription().format)
                )
            );
        }
        // Build buffer barriers
        for (const auto & [buf, acc] : pimpl->m_buffer_memo.accesses) {
            if (acc.size() < 2) continue;
            if (acc.back().pass_index != current_pass_id)   continue;

            const auto & prev_acc = *(acc.rbegin() - 1);
            buffer_barriers.push_back(
                pimpl->m_buffer_memo.GenerateBarrier(
                    buf,
                    prev_acc.access,
                    pimpl->GetPassType(prev_acc.pass_index),
                    acc.back().access,
                    pimpl->GetPassType(acc.back().pass_index)
                )
            );
        }

        pimpl->m_tasks.push_back(
            RenderGraphImpl::Synchronization{
                {},
                std::move(buffer_barriers),
                std::move(image_barriers)
            }
        );
    }
    RenderGraph RenderGraphBuilder::BuildRenderGraph() {
        std::vector <std::function<void(vk::CommandBuffer)>> compiled;
        RenderGraphImpl::RenderGraphExtraInfo extra{};

        for (const auto & [img, acc] : pimpl->m_texture_memo.accesses) {
            const auto first_access = acc.front();
            const auto last_access = acc.back();
            extra.m_initial_image_access[img] = std::make_tuple(
                pimpl->m_texture_memo.GetPipelineStage(pimpl->GetPassType(first_access.pass_index), first_access.access),
                pimpl->m_texture_memo.GetAccessFlags(first_access.access),
                pimpl->m_texture_memo.GetImageLayout(first_access.access)
            );
            extra.m_final_image_access[img] = std::make_tuple(
                pimpl->m_texture_memo.GetPipelineStage(pimpl->GetPassType(last_access.pass_index), last_access.access),
                pimpl->m_texture_memo.GetAccessFlags(last_access.access),
                pimpl->m_texture_memo.GetImageLayout(last_access.access)
            );
        }

        pimpl->m_texture_memo.accesses.clear();
        pimpl->m_buffer_memo.accesses.clear();

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
