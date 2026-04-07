#include "RendererManager.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Core/flagbits.h"
#include "MainClass.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"

#include <SDL3/SDL.h>
#include <unordered_set>

namespace Engine::RenderSystemState {
    struct RendererManager::impl {
        std::unordered_map<GUID, StaticHomogeneousMesh::StaticHMeshSharedDataBlock> static_mesh_asset_data_cache;

        struct RendererEntry {
            int32_t pending_deallocation_countdown = -1;

            std::unique_ptr<IVertexBasedRenderer> renderer;
            MaterialInstance *material = nullptr;

            uint32_t layer = 0xFFFFFFFF;
            bool cast_shadow = false;
            bool is_eagerly_loaded = false;

            glm::mat4 model_matrix{1.0f};
        };

        uint32_t next_handle = 0;
        std::unordered_map<RendererHandle, RendererEntry> m_data;

        void ScanForDeallocation() {
            for (auto it = static_mesh_asset_data_cache.begin(); it != static_mesh_asset_data_cache.end();) {
                if (it->second.refcnt == 0) {
                    it = static_mesh_asset_data_cache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        RendererList CreateRenderers(
            RenderSystem &system,
            AssetRef &mesh_asset_ref,
            const std::vector<AssetRef> &material_asset_refs,
            uint32_t layer,
            bool cast_shadow,
            bool eagerly_loaded
        ) {
            auto *masset = mesh_asset_ref.as<MeshAsset>();
            assert(masset);

            if (!static_mesh_asset_data_cache.contains(mesh_asset_ref.GetGUID())) {
                auto &entry = static_mesh_asset_data_cache[mesh_asset_ref.GetGUID()];
                entry.submeshes.resize(masset->GetSubmeshCount());
            }

            auto &cache_entry = static_mesh_asset_data_cache[mesh_asset_ref.GetGUID()];
            cache_entry.refcnt += 1;

            RendererList rl{};
            rl.reserve(masset->GetSubmeshCount());

            for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
                auto &d = m_data[next_handle];
                d.pending_deallocation_countdown = -1;
                d.renderer = std::make_unique<StaticHomogeneousMesh>(i, *masset, cache_entry);
                d.material = system.GetMaterialRegistry().GetOrCreateInstance(material_asset_refs[i].GetGUID()).get();
                d.layer = layer;
                d.cast_shadow = cast_shadow;
                d.is_eagerly_loaded = eagerly_loaded;

                if (eagerly_loaded) {
                    d.renderer->Submit(system.GetAllocatorState(), system.GetFrameManager().GetSubmissionHelper());
                }

                rl.push_back(next_handle++);
            }
            rl.shrink_to_fit();
            return rl;
        }
    };

    RendererManager::RendererManager(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }
    RendererManager::~RendererManager() = default;

    RendererList RendererManager::RegisterRenderer(
        AssetRef mesh_asset_ref,
        const std::vector<AssetRef> &material_asset_refs,
        uint32_t layer,
        bool cast_shadow,
        bool eagerly_loaded
    ) {
        return pimpl->CreateRenderers(
            m_system, mesh_asset_ref, material_asset_refs, layer, cast_shadow, eagerly_loaded
        );
    }

    void RendererManager::Unregister(RendererHandle handle) {
        auto it = pimpl->m_data.find(handle);
        if (it == pimpl->m_data.end()) return;
        it->second.pending_deallocation_countdown = FrameManager::FRAMES_IN_FLIGHT;
    }

    void RendererManager::UpdateModelMatrix(RendererHandle handle, const glm::mat4 &matrix) {
        auto it = pimpl->m_data.find(handle);
        if (it == pimpl->m_data.end()) return;
        it->second.model_matrix = matrix;
    }

    void RendererManager::PerformPendingCleanUp() {
        for (auto it = pimpl->m_data.begin(); it != pimpl->m_data.end();) {
            if (it->second.pending_deallocation_countdown < 0) {
                ++it;
                continue;
            }
            it->second.pending_deallocation_countdown -= 1;
            if (it->second.pending_deallocation_countdown == 0) {
                it = pimpl->m_data.erase(it);
            } else {
                ++it;
            }
        }
        pimpl->ScanForDeallocation();
    }

    RendererList RendererManager::FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc) {
        assert(sc == SortingCriterion::None && "Unimplemented");
        std::unordered_set<uint32_t> filtered_renderers{};

        for (const auto &[handle, entry] : pimpl->m_data) {
            if (entry.pending_deallocation_countdown >= 0) continue;

            if ((fc.layer & entry.layer) == 0) continue;
            if (fc.is_shadow_caster != FilterCriteria::BinaryCriterion::DontCare) {
                if (entry.cast_shadow != static_cast<int>(fc.is_shadow_caster)) continue;
            }

            if (!entry.renderer->IsReady()) {
                entry.renderer->Submit(m_system.GetAllocatorState(), m_system.GetFrameManager().GetSubmissionHelper());
            }
            filtered_renderers.insert(handle);
        }

        RendererList ret{};
        ret.reserve(filtered_renderers.size());
        for (auto i : filtered_renderers) ret.push_back(i);
        return ret;
    }

    const IVertexBasedRenderer *RendererManager::GetRendererData(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.renderer.get();
    }

    MaterialInstance *RendererManager::GetMaterialInstance(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.material;
    }

    const glm::mat4 &RendererManager::GetModelMatrix(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.model_matrix;
    }

    vk::PushConstantRange RendererManager::GetPushConstantRange() {
        return vk::PushConstantRange{vk::ShaderStageFlagBits::eAllGraphics, 0, sizeof(RendererDataStruct)};
    };
} // namespace Engine::RenderSystemState
