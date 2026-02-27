#include "RenderGraphBuilder2.h"

#include <unordered_set>
#include <SDL3/SDL.h>

#include "Render/Memory/MemoryAccessHelper.hpp"
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
            int32_t,
            std::vector<std::pair<uint32_t, Engine::MemoryAccessTypeBuffer>>
        > buffer_usages {};
        std::unordered_map<
            int32_t,
            std::vector<std::pair<uint32_t, Engine::MemoryAccessTypeImageBits>>
        > image_usages {};

        void SortByPassIndex () noexcept {
            for (auto & [r, u] : buffer_usages) {
                std::sort(u.begin(), u.end(), [](const auto & lhs, const auto & rhs) {
                    return lhs.first < rhs.first;
                });
            }
            for (auto & [r, u] : image_usages) {
                std::sort(u.begin(), u.end(), [](const auto & lhs, const auto & rhs) {
                    return lhs.first < rhs.first;
                });
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
                adjacent_list_in.begin(), adjacent_list_out.end(),
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
            std::unordered_map <int32_t, TextureCreationInfo> texture_creation_info;
            std::unordered_map <int32_t, RenderTargetTexture *> texture_mapping;
            std::unordered_map <int32_t, const DeviceBuffer *> buffer_mapping;

            /**
             * @brief Materialize render target textures from the
             * `texture_creation_info`.
             */
            std::unordered_map <int32_t, std::unique_ptr<RenderTargetTexture>>
            MaterializeRenderTargetTextures(RenderSystem & s) const {
                std::unordered_map <int32_t, std::unique_ptr<RenderTargetTexture>> ret{};
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
                        if (r > 0) {
                            if (HasReadAccess({prev.second}) && HasWriteAccess({next.second})) {
                                SDL_LogInfo(
                                    SDL_LOG_CATEGORY_RENDER,
                                    std::format(
                                        "Transient render target {} has read access before write access. "
                                        "Dependency chain is reversed for this access.",
                                        r
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
                        if (r > 0) {
                            if (HasReadAccess({prev.second}) && HasWriteAccess({next.second})) {
                                SDL_LogWarn(
                                    SDL_LOG_CATEGORY_RENDER,
                                    std::format(
                                        "Transient buffer {} has read access before write access. "
                                        "Dependency chain is reversed for this access.",
                                        r
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
        this->AddPass(
            RenderGraphPassBuilder{system}.SetName("Virtual Source").Get()
        );
    }
    RenderGraphBuilder2::~RenderGraphBuilder2() = default;

    int32_t RenderGraphBuilder2::ImportExternalResource(
        RenderTargetTexture &texture, MemoryAccessTypeImageBits prev_access
    ) {
        pimpl->rs.resource_counter++;
        auto ret = -pimpl->rs.resource_counter;
        pimpl->rs.texture_mapping[ret] = &texture;
        pimpl->passes.front().image_access[ret] = prev_access;
        return ret;
    }

    int32_t RenderGraphBuilder2::ImportExternalResource(
        const DeviceBuffer &buffer, MemoryAccessTypeBuffer prev_access
    ) {
        pimpl->rs.resource_counter++;
        auto ret = -pimpl->rs.resource_counter;
        pimpl->rs.buffer_mapping[ret] = &buffer;
        pimpl->passes.front().buffer_access[ret] = prev_access;
        return ret;
    }

    int32_t RenderGraphBuilder2::RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc texture_description,
        RenderTargetTexture::SamplerDesc sampler_description,
        std::string_view name
    ) noexcept {
        pimpl->rs.resource_counter++;
        auto ret = pimpl->rs.resource_counter;
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
        for (auto & [r, u] : usage.image_usages) {
            for (auto & ru : u) ru.first = reordered_pass_lut[ru.first];
        }
        for (auto & [r, u] : usage.buffer_usages) {
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
                for (const auto & usage : u) {
                    if (pimpl->passes[pass_order[usage.first]].affinity != last_affinity) {
                        SDL_LogInfo(
                            SDL_LOG_CATEGORY_RENDER,
                            std::format(
                                "Found cross-queue dependency between pass {} and "
                                "{} incurred by resource {}",
                                pass_order[last_affinity_pass],
                                pass_order[usage.first],
                                r
                            ).c_str()
                        );
                        auto new_affinity = pimpl->passes[pass_order[usage.first]].affinity;

                        auto src{AffinityToPipelineStage(last_affinity)}, dst{AffinityToPipelineStage(new_affinity)};
                        cross_queue_dep[usage.first][last_affinity_pass] = std::make_pair(src, dst);
                        
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
            merged_passes.back().push_back(i);
            merged_pass_lut[i] = merged_passes.size() - 1;
            if (affected_passes.contains(i)) {
                merged_passes.push_back({});
            }
        }

        // Rescan merged passes to obtain stages
        std::vector <vk::PipelineStageFlags2> signal_stage{}, wait_stage{};
        signal_stage.resize(merged_passes.size());
        wait_stage.resize(merged_passes.size());
        for (size_t i = 0; i < merged_passes.size(); i++) {
            for (auto src_pass : merged_passes[i]) {
                for (const auto & cqd : cross_queue_dep[src_pass]) {
                    auto dst_pass = cqd.first;

                    signal_stage[src_pass] |= cqd.second.first;
                    wait_stage[dst_pass] |= cqd.second.second;
                }
            }
        }

        // Construct compiled passes
        std::vector <RenderGraphCompiledPass> p{};
        p.resize(merged_passes.size());
        for (size_t i = 0; i < p.size(); i++) {
            p[i].wait_stage = wait_stage[i];
            p[i].signal_stage = signal_stage[i];
            p[i].pass_works = {};
            for (auto subpass : merged_passes[i]) {
                p[i].pass_works.push_back(
                    pimpl->passes[reordered_pass_lut[subpass]].pass_function
                );
            }
        }


        RenderGraph2ExtraInfo e{};
        e.buffer_mapping = std::move(pimpl->rs.buffer_mapping);
        e.texture_mapping = std::move(pimpl->rs.texture_mapping);
        e.transient_texture_storage = std::move(pimpl->rs.MaterializeRenderTargetTextures(system));

        for (const auto & [r, a] : usage.image_usages) {
            if (r > 0)  continue;
            e.first_persistent_texture_access[r] = a.front().second;
            e.last_persistent_texture_access[r] = a.back().second;
        }
        return RenderGraph2(std::move(p), std::move(e));
    }

} // namespace Engine
