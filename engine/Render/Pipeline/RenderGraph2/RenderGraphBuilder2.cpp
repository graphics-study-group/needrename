#include "RenderGraphBuilder2.h"

#include <unordered_set>
#include <SDL3/SDL.h>

#include "Render/Memory/MemoryAccessHelper.hpp"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include "Render/Pipeline/RenderGraph2/RenderGraph2.h"
#include "Render/Pipeline/RenderGraph2/RenderGraphPass.h"
#include "Render/Pipeline/RenderGraph2/RenderGraphStruct.hpp"

namespace {
    constexpr vk::PipelineStageFlagBits2 AffinityToPipelineStage(
        Engine::RenderGraphPassAffinity affinity
    ) {
        switch(affinity) {
            using enum Engine::RenderGraphPassAffinity;
            case Transfer:
                return vk::PipelineStageFlagBits2::eAllTransfer;
            case Graphics:
                return vk::PipelineStageFlagBits2::eAllGraphics;
            case Compute:
                return vk::PipelineStageFlagBits2::eComputeShader;
            default:
                return vk::PipelineStageFlagBits2::eNone;
        }
    }

    struct UsageCache {
        std::unordered_map<
            Engine::RGBufferHandle,
            std::vector<std::pair<uint32_t, Engine::MemoryAccessTypeBuffer>>
        > buffer_usages {};
        std::unordered_map<
            Engine::RGTextureHandle,
            std::vector<std::pair<uint32_t, Engine::MemoryAccessTypeImageBits>>
        > image_usages {};

        template <typename T>
        static bool less_by_pass_index (
            const std::pair<uint32_t, T> & lhs,
            const std::pair<uint32_t, T> & rhs
        ) noexcept {
            return lhs.first < rhs.first;
        }

        void SortByPassIndex () noexcept {
            for (auto & [r, u] : buffer_usages) {
                std::sort(u.begin(), u.end(), less_by_pass_index<Engine::MemoryAccessTypeBuffer>);
            }
            for (auto & [r, u] : image_usages) {
                std::sort(u.begin(), u.end(), less_by_pass_index<Engine::MemoryAccessTypeImageBits>);
            }
        }
    };

    struct DependencyGraph {
        std::vector <std::unordered_set<uint32_t>> adjacent_list_out {};
        std::vector <std::unordered_set<uint32_t>> adjacent_list_in {};

        DependencyGraph(size_t size) noexcept {
            adjacent_list_out.resize(size);
            adjacent_list_in.resize(size);
        }

        void AddEdge(uint32_t from, uint32_t to) noexcept {
            adjacent_list_out[from].insert(to);
            adjacent_list_in[to].insert(from);
        }

        std::vector <uint32_t> TopologicalSort() const {
            std::vector<uint32_t> result;
            std::vector<uint32_t> in_degree; // work on a copy
            std::queue<uint32_t> q;

            in_degree.resize(adjacent_list_in.size());
            std::transform(
                adjacent_list_in.begin(), adjacent_list_in.end(),
                in_degree.begin(),
                [] (const std::unordered_set<uint32_t> & edges) -> uint32_t {
                    return edges.size();
                }
            );

            for (uint32_t i = 0; i < adjacent_list_out.size(); ++i) {
                if (in_degree[i] == 0)  q.push(i);
            }
            while (!q.empty()) {
                uint32_t u = q.front();
                q.pop();
                result.push_back(u);
                for (uint32_t v : adjacent_list_out[u]) {
                    if (--in_degree[v] == 0)    q.push(v);
                }
            }

            // Report cycle found
            if (result.size() != adjacent_list_out.size()) {
                throw std::runtime_error("Dependency graph has a cycle.");
            }
            return result;
        }
    };
}

namespace Engine {
    struct RenderGraphBuilder2::impl {
        std::vector <RenderGraphPass> passes{};

        // Imported and requested resources.
        struct ResourceStorage {
            int32_t resource_counter {0};

            struct TextureCreationInfo {
                std::string name{};
                RenderTargetTexture::RenderTargetTextureDesc t {};
                RenderTargetTexture::SamplerDesc s {};
            };
            std::unordered_map <RGTextureHandle, TextureCreationInfo> texture_creation_info;
            std::unordered_map <RGTextureHandle, RenderTargetTexture *> texture_mapping;
            std::unordered_map <RGBufferHandle, const DeviceBuffer *> buffer_mapping;

            /**
             * @brief Materialize render target textures from the
             * `texture_creation_info`.
             */
            std::unordered_map <RGTextureHandle, std::unique_ptr<RenderTargetTexture>>
            MaterializeRenderTargetTextures(RenderSystem & s) const {
                std::unordered_map <RGTextureHandle, std::unique_ptr<RenderTargetTexture>> ret{};
                for (const auto & [k, v] : texture_creation_info) {
                    ret[k] = RenderTargetTexture::CreateUnique(
                        s,
                        v.t,
                        v.s,
                        v.name
                    );
                }
                return ret;
            }
        } rs{};

        /**
         * @brief Discover usages to each resources.
         */
        UsageCache AnalysisUsage() const {
            UsageCache uc{};
            for (size_t i = 0; i < passes.size(); i++) {
                const auto & p = passes[i];
                for (const auto & [r, a] : p.image_access) {
                    uc.image_usages[r].push_back(std::make_pair(i, a));
                }
                for (const auto & [r, a] : p.buffer_access) {
                    uc.buffer_usages[r].push_back(std::make_pair(i, a));
                }
            }
            return uc;
        }

        /**
         * @brief Discover dependencies carried by render graph passes, and
         * build a dependency graph.
         */
        DependencyGraph AnalysisDependency(const UsageCache & usages) const {
            DependencyGraph dg{passes.size()};

            // Discover dependency by texture
            for (const auto & [r, u] : usages.image_usages) {
                for (size_t i = 0; i < u.size(); i++) {
                    for (size_t j = i + 1; j < u.size(); j++) {
                        auto prev{u[i]}, next{u[j]};
                        assert(prev.first < next.first);

                        // Skip read-after-read "hazard".
                        if ((!HasWriteAccess({prev.second})) && (!HasWriteAccess({next.second}))) {
                            continue;
                        }
                        // Transient resource has read before write.
                        auto rid = static_cast<int32_t>(r);
                        if (rid > 0) {
                            if (HasReadAccess({prev.second}) && HasWriteAccess({next.second})) {
                                SDL_LogInfo(
                                    SDL_LOG_CATEGORY_RENDER,
                                    std::format(
                                        "Transient render target {} has read access before write access. "
                                        "Dependency chain is reversed for this access.",
                                        rid
                                    ).c_str()
                                );
                                std::swap(prev, next);
                            }
                        }

                        dg.AddEdge(prev.first, next.first);
                    }
                }
            }

            // Discover dependency by buffer
            for (const auto & [r, u] : usages.buffer_usages) {
                for (size_t i = 0; i < u.size(); i++) {
                    for (size_t j = i + 1; j < u.size(); j++) {
                        auto prev{u[i]}, next{u[j]};
                        assert(prev.first < next.first);

                        // Skip read-after-read "hazard".
                        if ((!HasWriteAccess({prev.second})) && (!HasWriteAccess({next.second}))) {
                            continue;
                        }
                        // Transient resource has read before write.
                        auto rid = static_cast<int32_t>(r);
                        if (rid > 0) {
                            if (HasReadAccess({prev.second}) && HasWriteAccess({next.second})) {
                                SDL_LogWarn(
                                    SDL_LOG_CATEGORY_RENDER,
                                    std::format(
                                        "Transient buffer {} has read access before write access. "
                                        "Dependency chain is reversed for this access.",
                                        rid
                                    ).c_str()
                                );
                                std::swap(prev, next);
                            }
                        }
                        dg.AddEdge(prev.first, next.first);
                    }
                }
            }
            return dg;
        }
    };

    RenderGraphBuilder2::RenderGraphBuilder2(
        RenderSystem &system
    ) : system(system), pimpl(std::make_unique<impl>()) {
        // Append a source pass.
        /* this->AddPass(
            RenderGraphPassBuilder{system}.SetName("Virtual Source").Get()
        ); */
    }
    RenderGraphBuilder2::~RenderGraphBuilder2() = default;

    RGTextureHandle RenderGraphBuilder2::ImportExternalResource(
        RenderTargetTexture &texture, MemoryAccessTypeImageBits prev_access
    ) {
        pimpl->rs.resource_counter++;
        auto ret = static_cast<RGTextureHandle>(-pimpl->rs.resource_counter);
        pimpl->rs.texture_mapping[ret] = &texture;
        pimpl->passes.front().image_access[ret] = prev_access;
        return ret;
    }

    RGBufferHandle RenderGraphBuilder2::ImportExternalResource(
        const DeviceBuffer &buffer, MemoryAccessTypeBuffer prev_access
    ) {
        pimpl->rs.resource_counter++;
        auto ret = static_cast<RGBufferHandle>(-pimpl->rs.resource_counter);
        pimpl->rs.buffer_mapping[ret] = &buffer;
        pimpl->passes.front().buffer_access[ret] = prev_access;
        return ret;
    }

    RGTextureHandle RenderGraphBuilder2::RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc texture_description,
        RenderTargetTexture::SamplerDesc sampler_description,
        std::string_view name
    ) noexcept {
        pimpl->rs.resource_counter++;
        auto ret = static_cast<RGTextureHandle>(pimpl->rs.resource_counter);
        pimpl->rs.texture_creation_info[ret] = {
            .name = std::string{name},
            .t = texture_description,
            .s = sampler_description
        };
        return ret;
    }

    void RenderGraphBuilder2::AddPass(RenderGraphPass &&pass) noexcept {
        pimpl->passes.push_back(std::move(pass));
    }

    RenderGraph2 RenderGraphBuilder2::BuildRenderGraph() {
        auto usage = pimpl->AnalysisUsage();
        auto dg = pimpl->AnalysisDependency(usage);
        // Maps reordered pass indices to original pass indices.
        auto pass_order = dg.TopologicalSort();
        // Remaps original pass indices to reordered pass indices.
        auto reordered_pass_lut = pass_order;
        for (uint32_t i = 0; i < pass_order.size(); i++) {
            reordered_pass_lut[pass_order[i]] = i;
        }
        auto reordered_usage = usage;
        for (auto & [r, u] : reordered_usage.image_usages) {
            for (auto & ru : u) ru.first = reordered_pass_lut[ru.first];
        }
        for (auto & [r, u] : reordered_usage.buffer_usages) {
            for (auto & ru : u) ru.first = reordered_pass_lut[ru.first];
        }
        reordered_usage.SortByPassIndex();

        // Find cross-queue dependencies for textures.
        // These dependencies require semaphores to correctly synchronize.
        std::unordered_map<
            uint32_t,
            std::unordered_map<uint32_t,
                std::pair<vk::PipelineStageFlags2, vk::PipelineStageFlags2>
            >
        > cross_queue_dep;  // < all pass indices are reordered.
        auto AnalysisCrossQueueDependency = [&, this] (const auto & usages) {
            for (const auto & [r, u] : usages) {
                auto last_affinity = pimpl->passes[pass_order[u.front().first]].affinity;
                auto last_affinity_pass = u.front().first;
                auto rid = static_cast<int32_t>(r);
                for (const auto & usage : u) {
                    if (pimpl->passes[pass_order[usage.first]].affinity != last_affinity) {
                        SDL_LogInfo(
                            SDL_LOG_CATEGORY_RENDER,
                            std::format(
                                "Found cross-queue dependency from pass {} to "
                                "{} incurred by resource {}",
                                pimpl->passes[pass_order[last_affinity_pass]].name,
                                pimpl->passes[pass_order[usage.first]].name,
                                rid
                            ).c_str()
                        );
                        auto new_affinity = pimpl->passes[pass_order[usage.first]].affinity;

                        auto src{AffinityToPipelineStage(last_affinity)}, dst{AffinityToPipelineStage(new_affinity)};
                        cross_queue_dep[last_affinity_pass][usage.first] = std::make_pair(src, dst);
                        
                        last_affinity = pimpl->passes[pass_order[usage.first]].affinity;
                        last_affinity_pass = usage.first;
                    }
                }
            }
        };
        AnalysisCrossQueueDependency(reordered_usage.image_usages);
        AnalysisCrossQueueDependency(reordered_usage.buffer_usages);

        // Merge passes that does not have cross queue dependencies.
        std::unordered_set <uint32_t> affected_passes {};
        std::vector <std::vector <uint32_t> > merged_passes {};
        std::unordered_map <uint32_t, uint32_t> merged_pass_lut {};
        
        for (const auto & [p1, v] : cross_queue_dep) {
            affected_passes.insert(p1);
            for (const auto & [p2, _] : v) {
                affected_passes.insert(p2);
            }
        }
        merged_passes.push_back({});
        for (size_t i = 0; i < pass_order.size(); i++) {
            if (affected_passes.contains(i)) {
                merged_passes.push_back({});
            }
            // XXX: merge passes that signal on None and wait on None
            merged_passes.back().push_back(i);
            merged_pass_lut[i] = merged_passes.size() - 1;
        }

        // Rescan merged passes to obtain stages
        std::vector <vk::PipelineStageFlags2> signal_stage{}, wait_stage{};
        signal_stage.resize(merged_passes.size());
        wait_stage.resize(merged_passes.size());
        for (size_t i = 0; i < merged_passes.size(); i++) {
            for (auto src_pass : merged_passes[i]) {
                for (const auto & cqd : cross_queue_dep[src_pass]) {
                    auto dst_pass = cqd.first;

                    signal_stage[merged_pass_lut[src_pass]] |= cqd.second.first;
                    wait_stage[merged_pass_lut[dst_pass]] |= cqd.second.second;
                }
            }
        }

        // Construct compiled passes
        std::vector <RenderGraphCompiledPass> p{};
        p.resize(merged_passes.size());
        for (size_t i = 0; i < p.size(); i++) {
            p[i].wait_stage = wait_stage[i];
            p[i].signal_stage = signal_stage[i];
            p[i].subpasses = {};
#ifndef NDEBUG
            SDL_LogDebug(
                SDL_LOG_CATEGORY_RENDER,
                std::format(
                    "Pass {}: Wait {}, Signal {}",
                    i,
                    vk::to_string(wait_stage[i]),
                    vk::to_string(signal_stage[i])
                ).c_str()
            );
#endif
            for (auto subpass_id : merged_passes[i]) {
                RenderGraphCompiledPass::Subpass subpass;
                const auto & old_p = pimpl->passes[pass_order[subpass_id]];
                subpass.pass_work = old_p.pass_function;
#ifndef NDEBUG
                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    std::format(
                        "Processing subpass \"{}\" (merged into pass {})",
                        old_p.name, i
                    ).c_str()
                );
#endif
                // Build barriers for textures.
                for (auto [r, a] : old_p.image_access) {
                    const auto & u = reordered_usage.image_usages[r];
                    auto itr = std::lower_bound(
                        u.begin(),
                        u.end(),
                        std::make_pair(subpass_id, MemoryAccessTypeImageBits{}),
                        UsageCache::less_by_pass_index<MemoryAccessTypeImageBits>
                    );

                    vk::AccessFlags2 src_access, dst_access;
                    vk::PipelineStageFlags2 src_stage, dst_stage;
                    vk::ImageLayout src_layout, dst_layout;

                    if (itr == u.begin()) {
                        src_access = vk::AccessFlagBits2::eNone;
                        src_stage = vk::PipelineStageFlagBits2::eNone;
                        src_layout = vk::ImageLayout::eUndefined;
                    } else {
                        itr = itr - 1;
                        src_access = GetAccessFlags({itr->second});
                        src_stage = AffinityToPipelineStage(pimpl->passes[pass_order[itr->first]].actual_type);
                        src_layout = GetImageLayout({itr->second});
                    }
                    dst_access = GetAccessFlags({a});
                    dst_stage = AffinityToPipelineStage(old_p.actual_type);
                    dst_layout = GetImageLayout({a});
                    vk::ImageAspectFlags aspect{};
                    if (pimpl->rs.texture_creation_info.contains(r)) {
                        aspect = ImageUtils::GetVkAspect(
                            static_cast<ImageUtils::ImageFormat>(
                                static_cast<std::underlying_type_t<RenderTargetTexture::RTTFormat>>(
                                    pimpl->rs.texture_creation_info[r].t.format
                                )
                            )
                        );
                    } else {
                        assert(pimpl->rs.texture_mapping.contains(r));
                        aspect = ImageUtils::GetVkAspect(
                            pimpl->rs.texture_mapping[r]->GetTextureDescription().format
                        );
                    }
#ifndef NDEBUG
                    SDL_LogDebug(
                        SDL_LOG_CATEGORY_RENDER,
                        std::format(
                            "  Inserting image barrier for resource {}: "
                            "subpass \"{}\" ({}, {}, {}) "
                            "-> subpass \"{}\" ({}, {}, {})",
                            static_cast<int32_t>(r),
                            pimpl->passes[pass_order[itr->first]].name,
                            vk::to_string(src_stage),
                            vk::to_string(src_access),
                            vk::to_string(src_layout),
                            old_p.name,
                            vk::to_string(dst_stage),
                            vk::to_string(dst_access),
                            vk::to_string(dst_layout)
                        ).c_str()
                    );
#endif
                    subpass.image_barriers.push_back(
                        std::make_pair(
                            r,
                            vk::ImageMemoryBarrier2{
                                src_stage, src_access,
                                dst_stage, dst_access,
                                src_layout, dst_layout,
                                // XXX: Need QFOT for exclusive sharing mode resources.
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                nullptr,
                                vk::ImageSubresourceRange{
                                    aspect,
                                    0, vk::RemainingMipLevels,
                                    0, vk::RemainingArrayLayers
                                }
                            }
                        )
                    );
                }

                // Build barrier for buffers
                for (auto [r, a] : old_p.buffer_access) {
                    const auto & u = reordered_usage.buffer_usages[r];
                    auto itr = std::lower_bound(
                        u.begin(),
                        u.end(),
                        std::make_pair(subpass_id, MemoryAccessTypeBuffer{}),
                        UsageCache::less_by_pass_index<MemoryAccessTypeBuffer>
                    );

                    vk::AccessFlags2 src_access, dst_access;
                    vk::PipelineStageFlags2 src_stage, dst_stage;

                    if (itr == u.begin()) {
                        src_access = vk::AccessFlagBits2::eNone;
                        src_stage = vk::PipelineStageFlagBits2::eNone;
                    } else {
                        itr = itr - 1;
                        src_access = GetAccessFlags({itr->second});
                        src_stage = AffinityToPipelineStage(pimpl->passes[pass_order[itr->first]].actual_type);
                    }
                    dst_access = GetAccessFlags({a});
                    dst_stage = AffinityToPipelineStage(old_p.actual_type);

#ifndef NDEBUG
                    SDL_LogDebug(
                        SDL_LOG_CATEGORY_RENDER,
                        std::format(
                            "  Inserting buffer barrier for resource {}: "
                            "subpass \"{}\" ({}, {}) "
                            "-> subpass \"{}\" ({}, {})",
                            static_cast<int32_t>(r),
                            pimpl->passes[pass_order[itr->first]].name,
                            vk::to_string(src_stage),
                            vk::to_string(src_access),
                            old_p.name,
                            vk::to_string(dst_stage),
                            vk::to_string(dst_access)
                        ).c_str()
                    );
#endif

                    subpass.buffer_barriers.push_back(
                        std::make_pair(
                            r,
                            vk::BufferMemoryBarrier2{
                                src_stage, src_access,
                                dst_stage, dst_access,
                                // XXX: Need QFOT for exclusive sharing mode resources.
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                nullptr,
                                0, vk::WholeSize
                            }
                        )
                    );
                }
                p[i].subpasses.push_back(std::move(subpass));
            }
        }

        RenderGraph2ExtraInfo e{};
        e.buffer_mapping = std::move(pimpl->rs.buffer_mapping);
        e.texture_mapping = std::move(pimpl->rs.texture_mapping);
        e.transient_texture_storage = std::move(pimpl->rs.MaterializeRenderTargetTextures(system));
        for (const auto & [k, v] : e.transient_texture_storage) {
            assert(!e.texture_mapping.contains(k));
            e.texture_mapping[k] = v.get();
        }
        for (const auto & [r, a] : usage.image_usages) {
            if (static_cast<int32_t>(r) > 0)  continue;
            e.first_persistent_texture_access[r] = a.front().second;
            e.last_persistent_texture_access[r] = a.back().second;
        }
        return RenderGraph2(std::move(p), std::move(e));
    }

} // namespace Engine
