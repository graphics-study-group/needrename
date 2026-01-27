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

            std::vector <RGAttachmentDesc> color_attachments;
            std::optional <RGAttachmentDesc> depth_attachments;
        };
        std::vector <Pass> m_tasks{};

        // Synchronization helpers
        RenderGraphImpl::TextureAccessMemo m_texture_memo;
        RenderGraphImpl::BufferAccessMemo m_buffer_memo;

        // Resource managers
        struct TextureCreationInfo {
            RenderTargetTexture::RenderTargetTextureDesc t;
            RenderTargetTexture::SamplerDesc s;
        };
        int32_t resource_counter = 0;
        std::unordered_map <int32_t, TextureCreationInfo> texture_creation_info;
        std::unordered_map <int32_t, std::unique_ptr <RenderTargetTexture>> internal_texture_cache;
        std::unordered_map <int32_t, const RenderTargetTexture *> texture_mapping;
        std::unordered_map <int32_t, const DeviceBuffer *> buffer_mapping;

        RenderGraphImpl::PassType GetPassType(int32_t pass_index) {
            assert(pass_index < 0 || pass_index < m_tasks.size());
            return pass_index < 0 ? RenderGraphImpl::PassType::None : m_tasks[pass_index].type;
        }

        void CreateInternalResources (RenderSystem & system) {
            for (const auto & [idx, tci] : texture_creation_info) {
                auto ptr = RenderTargetTexture::CreateUnique(system, tci.t, tci.s, std::format("Render Graph Resource {}", idx));
                texture_mapping[idx] = ptr.get();
                internal_texture_cache[idx] = std::move(ptr);
            }
        }

        std::function <void(vk::CommandBuffer)> GetPrePassSynchronizationFunc(int32_t pass_index) {
            std::vector<vk::ImageMemoryBarrier2> image_barriers{};
            std::vector<vk::BufferMemoryBarrier2> buffer_barriers{};
            // Build image barriers
            for (const auto & [img_idx, acc] : m_texture_memo.accesses) {
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

                const auto img = texture_mapping[img_idx];
                assert(img);

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
            for (const auto & [buf_idx, acc] : m_buffer_memo.accesses) {
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

                const auto buf = buffer_mapping[buf_idx];
                assert(buf);

                buffer_barriers.push_back(
                    m_buffer_memo.GenerateBarrier(
                        buf->GetBuffer(),
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

    int32_t RenderGraphBuilder::ImportExternalResource(
        const RenderTargetTexture &texture, MemoryAccessTypeImageBits prev_access
    ) {
        auto curr_id = pimpl->resource_counter++;
        curr_id = -curr_id;
        pimpl->m_texture_memo.accesses[curr_id] ={ {-1, {prev_access}} };
        pimpl->texture_mapping[curr_id] = &texture;
        return curr_id;
    }

    int32_t RenderGraphBuilder::ImportExternalResource(const DeviceBuffer &buffer, MemoryAccessTypeBuffer prev_access) {
        auto curr_id = pimpl->resource_counter++;
        curr_id = -curr_id;
        pimpl->m_buffer_memo.accesses[curr_id] = { {-1, prev_access} };
        pimpl->buffer_mapping[curr_id] = &buffer;
        return curr_id;
    }

    int32_t RenderGraphBuilder::RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc texture_description,
        RenderTargetTexture::SamplerDesc sampler_description
    ) noexcept {
        auto curr_id = pimpl->resource_counter++;
        pimpl->m_texture_memo.accesses[curr_id] = { {-1, {MemoryAccessTypeImageBits::None}} };
        pimpl->texture_creation_info[curr_id] = {
            texture_description,
            sampler_description
        };
        return curr_id;
    }

    void RenderGraphBuilder::UseImage(int32_t texture, MemoryAccessTypeImageBits access) {
        pimpl->m_texture_memo.UpdateLastAccess(texture, pimpl->m_tasks.size(), {access});
    }
    void RenderGraphBuilder::UseBuffer(int32_t buffer, MemoryAccessTypeBuffer access) {
        pimpl->m_buffer_memo.UpdateLastAccess(buffer, pimpl->m_tasks.size(), access);
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
        RGAttachmentDesc color,
        std::function<void(GraphicsCommandBuffer &)> pass,
        const std::string &name
    ) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    name](vk::CommandBuffer cb) {
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(pass, std::ref(gcb));
        };

        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Graphics,
                f,
                {color},
                std::nullopt
            }
        );
    }
    void RenderGraphBuilder::RecordRasterizerPass(
        RGAttachmentDesc color,
        RGAttachmentDesc depth,
        std::function<void(GraphicsCommandBuffer &)> pass,
        const std::string &name
    ) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    name](vk::CommandBuffer cb) {
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(pass, std::ref(gcb));
        };

        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Graphics,
                f,
                {color},
                depth
            }
        );
    }

    void RenderGraphBuilder::RecordRasterizerPass(
        std::initializer_list<RGAttachmentDesc> colors,
        RGAttachmentDesc depth,
        std::function<void(GraphicsCommandBuffer &)> pass,
        const std::string &name
    ) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    name](vk::CommandBuffer cb) {

            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(pass, std::ref(gcb));
        };
        pimpl->m_tasks.push_back(
            impl::Pass{
                RenderGraphImpl::PassType::Graphics,
                f,
                colors,
                depth
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

        pimpl->CreateInternalResources(m_system);

        for (const auto & [img_idx, acc] : pimpl->m_texture_memo.accesses) {
            const auto img = pimpl->texture_mapping[img_idx];
            assert(img);
            extra.m_initial_image_access[img] = std::make_pair(pimpl->GetPassType(acc.front().pass_index), acc.front().access);
            extra.m_final_image_access[img] = std::make_pair(pimpl->GetPassType(acc.back().pass_index), acc.back().access);
        }
        extra.internal_texture_cache = std::move(pimpl->internal_texture_cache);

        for (size_t pass = 0; pass < pimpl->m_tasks.size(); pass++) {
            compiled.push_back(pimpl->GetPrePassSynchronizationFunc(pass));
            const auto & pass_info = pimpl->m_tasks[pass];
            bool has_render_pass = pass_info.color_attachments.size() || pass_info.depth_attachments.has_value();
            if (has_render_pass) {

                std::vector <vk::RenderingAttachmentInfo> color_attachments;
                vk::RenderingAttachmentInfo depth_attachment;

                for (const auto & ca : pass_info.color_attachments) {
                    auto t = pimpl->texture_mapping[ca.rt_handle];
                    color_attachments.push_back(
                        vk::RenderingAttachmentInfo{
                            t->GetImageView(),
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::ResolveModeFlagBits::eNone,
                            nullptr,
                            vk::ImageLayout::eUndefined,
                            AttachmentUtils::GetVkLoadOp(ca.load_op),
                            AttachmentUtils::GetVkStoreOp(ca.store_op),
                            AttachmentUtils::GetVkClearValue(ca.clear_value)
                        }
                    );
                }

                if (pass_info.depth_attachments.has_value()) {
                    const auto & da = pass_info.depth_attachments.value();
                    auto t = pimpl->texture_mapping[da.rt_handle];
                    depth_attachment = vk::RenderingAttachmentInfo{
                        t->GetImageView(),
                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        vk::ResolveModeFlagBits::eNone,
                        nullptr,
                        vk::ImageLayout::eUndefined,
                        AttachmentUtils::GetVkLoadOp(da.load_op),
                        AttachmentUtils::GetVkStoreOp(da.store_op),
                        AttachmentUtils::GetVkClearValue(da.clear_value)
                    };
                }

                auto first_available_attachment = pass_info.color_attachments.empty() ?
                    pimpl->texture_mapping[pass_info.depth_attachments.value().rt_handle] :
                    pimpl->texture_mapping[pass_info.color_attachments.front().rt_handle];
                vk::Extent2D render_extent = {
                    first_available_attachment->GetTextureDescription().width,
                    first_available_attachment->GetTextureDescription().height
                };

                compiled.push_back(
                    [render_extent, color_attachments, depth_attachment, op = pass_info.operation](vk::CommandBuffer cb) -> void {
                        auto ri = vk::RenderingInfo{
                            vk::RenderingFlags{},
                            vk::Rect2D{{0, 0}, render_extent},
                            1, 0,
                            color_attachments,
                            &depth_attachment,
                            nullptr
                        };
                        cb.beginRendering(ri);
                        op(cb);
                        cb.endRendering();
                    }
                );
            } else {
                compiled.push_back(pimpl->m_tasks[pass].operation);
            }
        }
        // Reset everything
        pimpl = std::make_unique<impl>();
        return RenderGraph(m_system, std::move(compiled), std::move(extra));
    }
    RenderGraph RenderGraphBuilder::BuildDefaultRenderGraph(
        RenderTargetTexture &color_attachment, RenderTargetTexture &depth_attachment, GUISystem *gui_system
    ) {
        auto ca = this->ImportExternalResource(color_attachment, MemoryAccessTypeImageBits::None);
        auto da = this->ImportExternalResource(depth_attachment, MemoryAccessTypeImageBits::None);
        this->UseImage(ca, MemoryAccessTypeImageBits::ColorAttachmentWrite);
        this->UseImage(da, MemoryAccessTypeImageBits::DepthStencilAttachmentWrite);
        this->RecordRasterizerPass(
            {ca, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
            {da,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            [this, &color_attachment, &depth_attachment](Engine::GraphicsCommandBuffer &gcb) {
                gcb.DrawRenderers("", this->m_system.GetRendererManager().FilterAndSortRenderers({}));
            }
        );

        if (gui_system) {
            this->UseImage(ca, MemoryAccessTypeImageBits::ColorAttachmentWrite);
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
