#include "RenderGraph.h"

#include "Render/Memory/MemoryAccessHelper.hpp"
#include "RenderGraphStruct.hpp"

namespace Engine {
    struct RenderGraph::impl {
        std::vector<RenderGraphCompiledPass> passes{};
        RenderGraph2ExtraInfo extra_info{};

        const PipelineRuntimeInfoPerRendering *pripr_ptr{nullptr};

        std::vector<std::tuple<const RenderTargetTexture *, MemoryAccessTypeImageBits, MemoryAccessTypeImageBits>>
            pre_barrier_info{}, post_barrier_info{};

        vk::ImageMemoryBarrier2 GetImageBarrier(
            const RenderTargetTexture &t, MemoryAccessTypeImageBits src, MemoryAccessTypeImageBits dst
        ) {
            return vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eAllCommands,
                GetAccessFlags({src}),
                vk::PipelineStageFlagBits2::eAllCommands,
                GetAccessFlags({dst}),
                GetImageLayout({src}),
                GetImageLayout({dst}),
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                t.GetImage(),
                vk::ImageSubresourceRange{
                    ImageUtils::GetVkAspect(t.GetTextureDescription().format),
                    0,
                    vk::RemainingMipLevels,
                    0,
                    vk::RemainingArrayLayers
                }
            };
        }
    };

    RenderGraph::RenderGraph(std::vector<RenderGraphCompiledPass> &&passes, RenderGraph2ExtraInfo &&extra) noexcept :
        pimpl(std::make_unique<impl>()) {
        pimpl->passes = std::move(passes);
        pimpl->extra_info = std::move(extra);
    }

    RenderGraph::~RenderGraph() noexcept {
        for (auto &[k, v] : pimpl->extra_info.transient_texture_storage) {
            if (auto handle = std::get_if<RRTTHandle>(&v)) {
                handle->Release();
            }
        }
    };

    void RenderGraph::AddExternalInputDependency(RGTextureHandle rt_handle, MemoryAccessTypeImageBits access) {
        auto itr = pimpl->extra_info.first_persistent_texture_access.find(rt_handle);
        if (itr == pimpl->extra_info.first_persistent_texture_access.end()) {
            throw std::invalid_argument("Cannot find render target texture.");
        }

        pimpl->pre_barrier_info.push_back(
            std::make_tuple(
                std::visit(RenderTargetTextureVariantVisitor{}, pimpl->extra_info.texture_mapping[rt_handle]),
                access,
                itr->second
            )
        );
    }

    void RenderGraph::AddExternalOutputDependency(RGTextureHandle rt_handle, MemoryAccessTypeImageBits access) {
        auto itr = pimpl->extra_info.last_persistent_texture_access.find(rt_handle);
        if (itr == pimpl->extra_info.last_persistent_texture_access.end()) {
            throw std::invalid_argument("Cannot find render target texture.");
        }

        pimpl->post_barrier_info.push_back(
            std::make_tuple(
                std::visit(RenderTargetTextureVariantVisitor{}, pimpl->extra_info.texture_mapping[rt_handle]),
                itr->second,
                access
            )
        );
    }

    RenderTargetTexture *RenderGraph::GetInternalTextureResource(RGTextureHandle handle) const noexcept {
        auto itr = pimpl->extra_info.texture_mapping.find(handle);
        if (itr != pimpl->extra_info.texture_mapping.end()) {
            return std::visit(RenderTargetTextureVariantVisitor{}, itr->second);
        }
        return nullptr;
    }

    const PipelineRuntimeInfoPerRendering &RenderGraph::GetCurrentPassRuntimeInfo() const noexcept {
        assert(pimpl->pripr_ptr);
        return *pimpl->pripr_ptr;
    }

    void RenderGraph::Record(uint32_t pass, vk::CommandBuffer cb) const {
        assert(pass < pimpl->passes.size());

        std::vector<vk::ImageMemoryBarrier2> imb{};
        std::vector<vk::MemoryBarrier2> bmb{};
        for (const auto &subpass : pimpl->passes[pass].subpasses) {
            // Construct barriers.
            imb.clear();
            imb.reserve(subpass.image_barriers.size());
            for (const auto &[r, b] : subpass.image_barriers) {
                imb.push_back(b);
                imb.back().image = this->GetInternalTextureResource(r)->GetImage();
            }
            bmb.clear();
            bmb.reserve(subpass.buffer_barriers.size());
            for (const auto &[r, b] : subpass.buffer_barriers) {
                bmb.push_back(vk::MemoryBarrier2{b.srcStageMask, b.srcAccessMask, b.dstStageMask, b.dstAccessMask});
            }

            cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, bmb, {}, imb});

            // Invoke pass function.

            // Skip empty work pass
            if (!subpass.pass_work) {
                continue;
            }
            pimpl->pripr_ptr = &subpass.per_rendering_info;
            std::invoke(subpass.pass_work, cb, *this);
            pimpl->pripr_ptr = nullptr;
        }
    }

    void RenderGraph::RecordPrePass(vk::CommandBuffer cb) {
        std::vector<vk::ImageMemoryBarrier2> barriers{pimpl->pre_barrier_info.size()};
        for (size_t i = 0; i < pimpl->pre_barrier_info.size(); i++) {
            auto [t, a1, a2] = pimpl->pre_barrier_info[i];
            barriers[i] = pimpl->GetImageBarrier(*t, a1, a2);
        }
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, {}, {}, barriers});
        pimpl->pre_barrier_info.clear();
    }

    void RenderGraph::RecordPostPass(vk::CommandBuffer cb) {
        std::vector<vk::ImageMemoryBarrier2> barriers{pimpl->post_barrier_info.size()};
        for (size_t i = 0; i < pimpl->post_barrier_info.size(); i++) {
            auto [t, a1, a2] = pimpl->post_barrier_info[i];
            barriers[i] = pimpl->GetImageBarrier(*t, a1, a2);
        }
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, {}, {}, barriers});
        pimpl->post_barrier_info.clear();
    }

    void RenderGraph::RecordAllPasses(vk::CommandBuffer cb) {
        cb.begin(vk::CommandBufferBeginInfo{});
        RecordPrePass(cb);
        for (size_t i = 0; i < pimpl->passes.size(); i++) {
            this->Record(i, cb);
        }
        RecordPostPass(cb);
        cb.end();
    }

    void RenderGraph::Execute(RenderSystem &system) {
        /**
         * @note Currently all passes are recorded onto a single command buffer
         * and submitted to the main (graphics) queue. The `affinity` and
         * `cross_queue_dep` infrastructure in RenderGraphBuilder already
         * detects cross-queue dependencies and splits passes into merge groups
         * with appropriate signal/wait pipeline stages, but the actual
         * multi-queue submission (separate vkQueueSubmit calls with
         * semaphores for graphics / compute / transfer queues) is not yet
         * implemented.
         *
         * When async compute is enabled, this method should:
         * 1. Group passes by their queue affinity
         * 2. Record each group into its own command buffer
         * 3. Submit each group to the appropriate VkQueue
         * 4. Use timeline semaphores (or binary semaphores) between groups
         *    with cross-queue dependencies, using the signal_stage /
         *    wait_stage already computed in RenderGraphCompiledPass.
         *
         * @see RenderGraphPassAffinity
         * @see RenderGraphBuilder::AnalysisCrossQueueDependency
         */
        auto &fm = system.GetFrameManager();
        auto cb = fm.GetRawMainCommandBuffer();

        RecordAllPasses(cb);
        fm.SubmitMainCommandBuffer();
    }

} // namespace Engine
