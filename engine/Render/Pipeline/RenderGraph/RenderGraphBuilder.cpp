#include "RenderGraphBuilder.h"

#include "UserInterface/GUISystem.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace Engine {
    struct RenderGraphBuilder::impl {
        std::vector<vk::ImageMemoryBarrier2> m_image_barriers{};
        std::vector<vk::BufferMemoryBarrier2> m_buffer_barriers{};
        std::vector<std::function<void(vk::CommandBuffer)>> m_commands{};

        struct TextureAccessMemo {
            using AccessTuple = std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2, vk::ImageLayout>;
            std::unordered_map<const Texture *, AccessTuple> m_memo;

            void RegisterTexture(const Texture *texture, AccessTuple previous_access) {
                if (m_memo.contains(texture)) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER, "Texture %p is already registered.", static_cast<const void *>(texture)
                    );
                }
                m_memo[texture] = previous_access;
            }

            AccessTuple UpdateAccessTuple(const Texture *texture, AccessTuple new_access_tuple) {
                if (!m_memo.contains(texture)) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Texture %p is not registered, defaulting to none.",
                        static_cast<const void *>(texture)
                    );

                    m_memo[texture] = std::make_tuple(
                        vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone, vk::ImageLayout::eUndefined
                    );
                }

                std::swap(m_memo[texture], new_access_tuple);
                return new_access_tuple;
            };
        } m_memo;

        vk::ImageMemoryBarrier2 GetImageBarrier(Texture &texture, AccessHelper::ImageAccessType new_access) noexcept {
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
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth
                                                      | vk::ImageAspectFlagBits::eStencil;
            }

            TextureAccessMemo::AccessTuple dst_tuple{AccessHelper::GetAccessScope(new_access)};

            std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) = dst_tuple;
            std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) =
                m_memo.UpdateAccessTuple(&texture, dst_tuple);

            barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
            return barrier;
        }

        static vk::BufferMemoryBarrier2 GetBufferBarrier(
            Buffer &buffer [[maybe_unused]],
            AccessHelper::BufferAccessType new_access [[maybe_unused]],
            AccessHelper::BufferAccessType prev_access [[maybe_unused]]
        ) {
            assert(!"Unimplemented");
            vk::BufferMemoryBarrier2 barrier{};
            return barrier;
        }
    };

    RenderGraphBuilder::RenderGraphBuilder(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    RenderGraphBuilder::~RenderGraphBuilder() = default;

    void RenderGraphBuilder::RegisterImageAccess(Texture &texture, AccessHelper::ImageAccessType prev_access) {
        pimpl->m_memo.RegisterTexture(&texture, AccessHelper::GetAccessScope(prev_access));
    }

    void RenderGraphBuilder::UseImage(Texture &texture, AccessHelper::ImageAccessType new_access) {
        pimpl->m_image_barriers.push_back(pimpl->GetImageBarrier(texture, new_access));
    }
    void RenderGraphBuilder::UseBuffer(
        Buffer &buffer, AccessHelper::BufferAccessType new_access, AccessHelper::BufferAccessType prev_access
    ) {
        pimpl->m_buffer_barriers.push_back(pimpl->GetBufferBarrier(buffer, new_access, prev_access));
    }
    void RenderGraphBuilder::RecordRasterizerPassWithoutRT(std::function<void(GraphicsCommandBuffer &)> pass) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    bb = std::move(pimpl->m_buffer_barriers),
                                                    ib = std::move(pimpl->m_image_barriers)](vk::CommandBuffer cb) {
            vk::DependencyInfo dep{vk::DependencyFlags{}, {}, bb, ib};
            cb.pipelineBarrier2(dep);
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(pass, std::ref(gcb));
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
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
                                                    name,
                                                    bb = std::move(pimpl->m_buffer_barriers),
                                                    ib = std::move(pimpl->m_image_barriers)](vk::CommandBuffer cb) {
            vk::DependencyInfo dep{vk::DependencyFlags{}, {}, bb, ib};
            cb.pipelineBarrier2(dep);
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            gcb.BeginRendering(color, depth, system->GetSwapchain().GetExtent(), name);
            std::invoke(pass, std::ref(gcb));
            gcb.EndRendering();
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
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
                                                    name,
                                                    bb = std::move(pimpl->m_buffer_barriers),
                                                    ib = std::move(pimpl->m_image_barriers)](vk::CommandBuffer cb) {
            vk::DependencyInfo dep{vk::DependencyFlags{}, {}, bb, ib};
            cb.pipelineBarrier2(dep);
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            gcb.BeginRendering(colors, depth, system->GetSwapchain().GetExtent(), name);
            std::invoke(pass, std::ref(gcb));
            gcb.EndRendering();
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    void RenderGraphBuilder::RecordTransferPass(
        std::function<void(TransferCommandBuffer &)> pass, 
        const std::string & name
    ) {
        std::function<void(vk::CommandBuffer)> f = [pass,
                                                    name,
                                                    bb = std::move(pimpl->m_buffer_barriers),
                                                    ib = std::move(pimpl->m_image_barriers)](vk::CommandBuffer cb) {
            vk::DependencyInfo dep{vk::DependencyFlags{}, {}, bb, ib};
            cb.pipelineBarrier2(dep);
            TransferCommandBuffer tcb{cb};
            tcb.GetCommandBuffer().beginDebugUtilsLabelEXT({(name + " (Transfer)").c_str()});
            std::invoke(pass, std::ref(tcb));
            tcb.GetCommandBuffer().endDebugUtilsLabelEXT();
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    void RenderGraphBuilder::RecordComputePass(
        std::function<void(ComputeCommandBuffer &)> pass, const std::string &name
    ) {
        std::function<void(vk::CommandBuffer)> f = [system = &this->m_system,
                                                    pass,
                                                    name,
                                                    bb = std::move(pimpl->m_buffer_barriers),
                                                    ib = std::move(pimpl->m_image_barriers)](vk::CommandBuffer cb) {
            vk::DependencyInfo dep{vk::DependencyFlags{}, {}, bb, ib};
            cb.pipelineBarrier2(dep);
            ComputeCommandBuffer ccb{cb, system->GetFrameManager().GetFrameInFlight()};
            ccb.GetCommandBuffer().beginDebugUtilsLabelEXT({(name + " (Compute)").c_str()});
            std::invoke(pass, std::ref(ccb));
            ccb.GetCommandBuffer().endDebugUtilsLabelEXT();
        };

        pimpl->m_commands.push_back(f);

        // Get STL containers out of ``valid but unspecified'' states.
        pimpl->m_buffer_barriers.clear();
        pimpl->m_image_barriers.clear();
    }
    void RenderGraphBuilder::RecordSynchronization() {
        std::function<void(vk::CommandBuffer)> f = [bb = std::move(pimpl->m_buffer_barriers),
                                                    ib = std::move(pimpl->m_image_barriers)](vk::CommandBuffer cb) {
            vk::DependencyInfo dep{vk::DependencyFlags{}, {}, bb, ib};
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
        return RenderGraph(m_system, cmd);
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
