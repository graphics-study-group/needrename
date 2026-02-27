#include "RenderGraph2.h"

#include "RenderGraphStruct.hpp"
#include "Render/Memory/MemoryAccessHelper.hpp"

namespace Engine {
    struct RenderGraph2::impl {
        std::vector <RenderGraphCompiledPass> passes{};
        RenderGraph2ExtraInfo extra_info{};

        std::vector <
            std::tuple<
                const RenderTargetTexture *,
                MemoryAccessTypeImageBits,
                MemoryAccessTypeImageBits
            >
        > pre_barrier_info{}, post_barrier_info{};

        vk::ImageMemoryBarrier2 GetImageBarrier (
            const RenderTargetTexture & t,
            MemoryAccessTypeImageBits src,
            MemoryAccessTypeImageBits dst
        ) {
            return vk::ImageMemoryBarrier2 {
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
                    0, vk::RemainingMipLevels,
                    0, vk::RemainingArrayLayers
                }
            };
        }
    };

    RenderGraph2::RenderGraph2(
        std::vector<RenderGraphCompiledPass> &&passes,
        RenderGraph2ExtraInfo &&extra
    ) noexcept : pimpl(std::make_unique<impl>()) {
        pimpl->passes = std::move(passes);
        pimpl->extra_info = std::move(extra);
    }

    RenderGraph2::~RenderGraph2() noexcept = default;

    void RenderGraph2::AddExternalInputDependency(
        int32_t rt_handle,
        MemoryAccessTypeImageBits access
    ) {
        auto itr = pimpl->extra_info.first_persistent_texture_access.find(rt_handle);
        if (itr == pimpl->extra_info.first_persistent_texture_access.end()) {
            throw std::invalid_argument("Cannot find render target texture.");
        }

        pimpl->pre_barrier_info.push_back(
            std::make_tuple(
                pimpl->extra_info.texture_mapping[rt_handle],
                access,
                itr->second
            )
        );
    }

    void RenderGraph2::AddExternalOutputDependency(
        int32_t rt_handle,
        MemoryAccessTypeImageBits access
    ) {
        auto itr = pimpl->extra_info.last_persistent_texture_access.find(rt_handle);
        if (itr == pimpl->extra_info.last_persistent_texture_access.end()) {
            throw std::invalid_argument("Cannot find render target texture.");
        }

        pimpl->post_barrier_info.push_back(
            std::make_tuple(
                pimpl->extra_info.texture_mapping[rt_handle],
                itr->second,
                access
            )
        );
    }

    RenderTargetTexture *RenderGraph2::GetInternalTextureResource(
        int32_t handle
    ) const noexcept {
        auto itr = pimpl->extra_info.texture_mapping.find(handle);
        if (itr != pimpl->extra_info.texture_mapping.end()) {
            return itr->second;
        }
        return nullptr;
    }

    void RenderGraph2::Record(
        uint32_t pass,
        vk::CommandBuffer cb
    ) const {
        assert(pass < pimpl->passes.size());

        for (const auto & f : pimpl->passes[pass].pass_works) {
            std::invoke(f, cb, *this);
        }
    }

    void RenderGraph2::RecordPrePass(vk::CommandBuffer cb) {
        std::vector <vk::ImageMemoryBarrier2> barriers{pimpl->pre_barrier_info.size()};
        for (size_t i = 0; i < pimpl->pre_barrier_info.size(); i++) {
            auto [t, a1, a2] = pimpl->pre_barrier_info[i];
            barriers[i] = pimpl->GetImageBarrier(*t, a1, a2);
        }
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, {}, {}, barriers});
        pimpl->pre_barrier_info.clear();
    }

    void RenderGraph2::RecordPostPass(vk::CommandBuffer cb) {
        std::vector <vk::ImageMemoryBarrier2> barriers{pimpl->post_barrier_info.size()};
        for (size_t i = 0; i < pimpl->post_barrier_info.size(); i++) {
            auto [t, a1, a2] = pimpl->post_barrier_info[i];
            barriers[i] = pimpl->GetImageBarrier(*t, a1, a2);
        }
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, {}, {}, barriers});
        pimpl->post_barrier_info.clear();
    }

    void RenderGraph2::Execute(RenderSystem &system) {
        auto & fm = system.GetFrameManager();
        auto cb = fm.GetRawMainCommandBuffer();
        
        RecordPrePass(cb);
        for (size_t i = 0; i < pimpl->passes.size(); i++) {
            this->Record(i, cb);
        }
        RecordPostPass(cb);
        fm.SubmitMainCommandBuffer();
    }

} // namespace Engine
