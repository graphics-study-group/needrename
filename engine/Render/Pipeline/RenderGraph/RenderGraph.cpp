#include "RenderGraph.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include "Rhi/Backend/Vulkan/MemoryAccessHelper.hpp"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "RenderGraphStruct.hpp"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"

namespace Engine {
    struct RenderGraph::impl {
        std::vector<RenderGraphCompiledPass> passes{};
        RenderGraph2ExtraInfo extra_info{};

        const PipelineRuntimeInfoPerRendering *pripr_ptr{nullptr};

        std::vector<
            std::tuple<const RenderTargetTexture *, Rhi::MemoryAccessTypeImageBits, Rhi::MemoryAccessTypeImageBits>>
            pre_barrier_info{}, post_barrier_info{};

        Rhi::ImageBarrier GetImageBarrier(
            const RenderTargetTexture &t, Rhi::MemoryAccessTypeImageBits src, Rhi::MemoryAccessTypeImageBits dst
        ) {
            Rhi::ImageBarrier barrier{};
            barrier.image = t.GetImage();
            // A depth or stencil attachment transitions through its own aspect; the
            // colour aspect leaves it in a layout the pass cannot read.
            const Rhi::ImageFormat format = t.GetTextureDescription().format;
            barrier.aspect = Rhi::HasDepthAspect(format) ? Rhi::ImageAspect::Depth : Rhi::ImageAspect::Color;
            barrier.base_mip = 0;
            // Zero in the description means "every level", so the barrier names the
            // remaining range rather than a count of zero.
            barrier.mip_count = std::numeric_limits<uint32_t>::max();
            barrier.base_layer = 0;
            barrier.layer_count = std::numeric_limits<uint32_t>::max();
            barrier.from = {src};
            barrier.to = {dst};
            return barrier;
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

    void RenderGraph::AddExternalInputDependency(RGTextureHandle rt_handle, Rhi::MemoryAccessTypeImageBits access) {
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

    void RenderGraph::AddExternalOutputDependency(RGTextureHandle rt_handle, Rhi::MemoryAccessTypeImageBits access) {
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

    void RenderGraph::Record(uint32_t pass, Rhi::CommandEncoder &encoder) const {
        const vk::CommandBuffer cb = Rhi::ToVulkanCommandBuffer(encoder.GetHandle());
        assert(pass < pimpl->passes.size());

        std::vector<vk::ImageMemoryBarrier2> imb{};
        std::vector<vk::MemoryBarrier2> bmb{};
        for (const auto &subpass : pimpl->passes[pass].subpasses) {
            // Construct barriers.
            imb.clear();
            imb.reserve(subpass.image_barriers.size());
            for (const auto &[r, b] : subpass.image_barriers) {
                imb.push_back(b);
                imb.back().image = Engine::Rhi::ToVulkanImage(this->GetInternalTextureResource(r)->GetImage());
            }
            bmb.clear();
            bmb.reserve(subpass.buffer_barriers.size());
            for (const auto &[r, b] : subpass.buffer_barriers) {
                bmb.push_back(vk::MemoryBarrier2{b.srcStageMask, b.srcAccessMask, b.dstStageMask, b.dstAccessMask});
                // TODO: Use BufferMemoryBarrier2.
                // for now, we only need one MemoryBarrier2
                break;
            }

            cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, bmb, {}, imb});

            // Invoke pass function.

            // Skip empty work pass
            if (!subpass.pass_work) {
                continue;
            }
            pimpl->pripr_ptr = &subpass.per_rendering_info;
            std::invoke(subpass.pass_work, encoder, *this);
            pimpl->pripr_ptr = nullptr;
        }
    }

    void RenderGraph::RecordPrePass(Rhi::CommandEncoder &encoder) {
        for (const auto &[texture, from, to] : pimpl->pre_barrier_info) {
            encoder.Barrier(pimpl->GetImageBarrier(*texture, from, to));
        }
        pimpl->pre_barrier_info.clear();
    }

    void RenderGraph::RecordPostPass(Rhi::CommandEncoder &encoder) {
        for (const auto &[texture, from, to] : pimpl->post_barrier_info) {
            encoder.Barrier(pimpl->GetImageBarrier(*texture, from, to));
        }
        pimpl->post_barrier_info.clear();
    }

    void RenderGraph::RecordAllPasses(Rhi::CommandEncoder &encoder) {
        const vk::CommandBuffer cb = Rhi::ToVulkanCommandBuffer(encoder.GetHandle());
        RecordPrePass(encoder);
        for (size_t i = 0; i < pimpl->passes.size(); i++) {
            this->Record(static_cast<uint32_t>(i), encoder);
        }
        RecordPostPass(encoder);
    }

    void RenderGraph::RecordIntoMainCommandBuffer(RenderSystem &system) {
        /**
         * @note Currently all passes are recorded onto a single command buffer
         * and submitted to the main (graphics) queue by FrameManager's
         * frame-completion batch (see `FrameManager::SubmitFrame`). The
         * `affinity` and `cross_queue_dep` infrastructure in
         * RenderGraphBuilder already detects cross-queue dependencies and
         * splits passes into merge groups with appropriate signal/wait
         * pipeline stages, but the actual multi-queue submission (separate
         * vkQueueSubmit calls with semaphores for graphics / compute /
         * transfer queues) is not yet implemented.
         *
         * When async compute is enabled, this method should:
         * 1. Group passes by their queue affinity
         * 2. Record each group into its own command buffer
         * 3. Submit each group to the appropriate VkQueue
         * 4. Use timeline semaphores (or binary semaphores) between groups
         *    with cross-queue dependencies, using the signal_stage /
         *    wait_stage already computed in RenderGraphCompiledPass.
         * 5. Signal a "render complete" signal (supplied by FrameManager)
         *    after the last group; `SubmitFrame` then waits on it instead of
         *    relying on in-batch ordering.
         *
         * @see RenderGraphPassAffinity
         * @see RenderGraphBuilder::AnalysisCrossQueueDependency
         */
        auto &fm = system.GetFrameManager();
        // The frame already owns an encoder over this command buffer; borrowing
        // it keeps one encoder per recording instead of one per wrapper.
        auto &encoder = fm.GetMainEncoder();
        RecordAllPasses(encoder);
    }

    uint32_t RenderGraph::GetNumPasses() const noexcept {
        return static_cast<uint32_t>(pimpl->passes.size());
    }

} // namespace Engine





