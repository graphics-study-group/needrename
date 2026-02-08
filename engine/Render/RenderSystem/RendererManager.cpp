#include "RendererManager.h"
#include "Core/flagbits.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"
#include "Framework/world/WorldSystem.h"
#include "Framework/world/Handle.h"
#include "Framework/world/Scene.h"
#include "MainClass.h"

#include <SDL3/SDL.h>
#include <unordered_set>

namespace Engine::RenderSystemState {
    struct RendererManager::impl {
        std::unordered_map <
            ComponentHandle,
            RendererList
        > renderer_components;

        std::unordered_map <
            GUID,
            StaticHomogeneousMesh::StaticHMeshSharedDataBlock
        > static_mesh_asset_data_cache;
        
        void ScanForDeallocation() {
            for (auto & [k, v] : static_mesh_asset_data_cache) {
                if (v.refcnt == 0) {
                    static_mesh_asset_data_cache.erase(k);
                }
            }
        }

        ////////////////////////////////////////////////////////////////////////

        // XXX: use atomic for multithreads.
        uint32_t total_renderer_count;

        struct RendererDataBlock {
            // Count down deallocation until all frames-in-flight are rendered.
            int32_t pending_deallocation_countdown;
            MaterialInstance * material;
            RendererComponent * component;
            const MeshAsset::Submesh * submesh;
            std::unique_ptr <IVertexBasedRenderer> renderer;
        };
        std::unordered_map<RendererHandle, RendererDataBlock> m_data;

        RendererList CreateHomogeousMeshFromAsset(
            MeshComponent * rc,
            const std::shared_ptr <AssetRef> & asset,
            RenderSystem & s
        ) {
            RendererList rl{};
            auto masset = asset->cas<MeshAsset>();
            assert(masset);

            rl.reserve(masset->GetSubmeshCount());
            for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
                auto & d = m_data[total_renderer_count];
                d.pending_deallocation_countdown = -1;
                d.renderer = 
                    std::make_unique<HomogeneousMesh>(
                        s.GetAllocatorState(),
                        asset,
                        i
                    );
                d.material = rc->GetMaterial(i).get();
                d.component = rc;
                d.submesh = &masset->m_submeshes[i];
                rl.push_back(total_renderer_count);

                if (rc->m_is_eagerly_loaded) {
                    m_data[total_renderer_count].renderer->Submit(
                        s.GetAllocatorState(),
                        s.GetFrameManager().GetSubmissionHelper()
                    );
                }

                total_renderer_count ++;
            }
            rl.shrink_to_fit();
            return rl;
        }

        RendererList CreateStaticHMesh(
            StaticMeshComponent * rc,
            const std::shared_ptr <AssetRef> & asset,
            RenderSystem & s
        ) {
            auto masset = asset->cas<MeshAsset>();
            assert(masset);

            if (!static_mesh_asset_data_cache.contains(asset->GetGUID())) {
                static_mesh_asset_data_cache[asset->GetGUID()].submeshes.resize(masset->GetSubmeshCount());
            }

            auto & e = static_mesh_asset_data_cache[asset->GetGUID()];
            e.refcnt += 1;

            RendererList rl{};
            rl.reserve(masset->GetSubmeshCount());
            for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
                auto & d = m_data[total_renderer_count];
                d.pending_deallocation_countdown = -1;
                d.renderer = 
                    std::make_unique<StaticHomogeneousMesh>(
                        i,
                        *masset,
                        e
                    );
                d.material = rc->GetMaterial(i).get();
                d.component = rc;
                d.submesh = &masset->m_submeshes[i];
                rl.push_back(total_renderer_count);

                if (rc->m_is_eagerly_loaded) {
                    d.renderer->Submit(
                        s.GetAllocatorState(),
                        s.GetFrameManager().GetSubmissionHelper()
                    );
                }

                total_renderer_count ++;
            }
            rl.shrink_to_fit();
            return rl;
        }
    };

    RendererManager::RendererManager(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }
    RendererManager::~RendererManager() = default;

    void RendererManager::RegisterRendererComponent(const ComponentHandle &comp_handle) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "Registering component %d in scene %d", comp_handle.GetID(), comp_handle.GetSceneID());
        auto component = comp_handle.GetComponent();
        // Legacy mesh component
        if (auto mc = dynamic_cast<MeshComponent*>(component)) {
            auto rl = pimpl->CreateHomogeousMeshFromAsset(
                mc, mc->m_mesh_asset, m_system
            );
            pimpl->renderer_components[comp_handle] = rl;
        } else if (auto smc = dynamic_cast<StaticMeshComponent*>(component)) {
            auto rl = pimpl->CreateStaticHMesh(
                smc, smc->m_mesh_asset, m_system
            );
            pimpl->renderer_components[comp_handle] = rl;
        }
        // Unknown component
        else {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unknown renderer component type");
        }
    }
    RendererList RendererManager::GetRendererListsFromComponent(
        const ComponentHandle &component
    ) const noexcept {
        auto itr = pimpl->renderer_components.find(component);
        assert(itr != pimpl->renderer_components.end());
        return itr->second;
    }
    void RendererManager::UnregisterRendererComponent(
        const ComponentHandle &component
    ) {
        if (!pimpl->renderer_components.contains(component))    return;

        // Mark resources for pending deallocation.
        for (auto i : pimpl->renderer_components[component]) {
            pimpl->m_data[i].pending_deallocation_countdown = RenderSystemState::FrameManager::FRAMES_IN_FLIGHT;
        }

        pimpl->renderer_components.erase(component);
    }

    void RendererManager::PerformPendingCleanUp() {
        for (auto & [k, v] : pimpl->m_data) {
            if (v.pending_deallocation_countdown < 0)    continue;
            v.pending_deallocation_countdown -= 1;
            if (v.pending_deallocation_countdown == 0) pimpl->m_data.erase(k);
        }
        pimpl->ScanForDeallocation();
    }

    RendererList RendererManager::FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc) {
        assert(sc == SortingCriterion::None && "Unimplemented");
        std::unordered_set <uint32_t> filtered_renderers{};
        auto &scene = MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();

        for (const auto & [handle, rl] : pimpl->renderer_components) {
            auto rc = dynamic_cast<RendererComponent*>(scene.GetComponent(handle));

            if ((fc.layer & rc->m_layer) == 0) continue;
            if (fc.is_shadow_caster != FilterCriteria::BinaryCriterion::DontCare) {
                if (rc->m_cast_shadow != static_cast<int>(fc.is_shadow_caster)) continue;
            }

            for (auto i : rl) {
                if (pimpl->m_data[i].pending_deallocation_countdown >= 0)  continue;

                if (!pimpl->m_data[i].renderer->IsReady()) {
                    pimpl->m_data[i].renderer->Submit(
                        m_system.GetAllocatorState(),
                        m_system.GetFrameManager().GetSubmissionHelper()
                    );
                }
                filtered_renderers.insert(i);
            }
        }

        RendererList ret{};
        ret.reserve(filtered_renderers.size());
        for (auto i : filtered_renderers)   ret.push_back(i);

        return ret;
    }

    const IVertexBasedRenderer *RendererManager::GetRendererData(RendererHandle handle) const noexcept {
        assert(pimpl->m_data.contains(handle));
        return pimpl->m_data[handle].renderer.get();
    }
    const RendererComponent *RendererManager::GetRendererComponent(RendererHandle handle) const noexcept {
        assert(pimpl->m_data.contains(handle));
        return pimpl->m_data[handle].component;
    }
    MaterialInstance *RendererManager::GetMaterialInstance(RendererHandle handle) const noexcept {
        assert(pimpl->m_data.contains(handle));
        return pimpl->m_data[handle].material;
    }
    vk::PushConstantRange RendererManager::GetPushConstantRange() {
        return vk::PushConstantRange{
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof(RendererDataStruct)
        };
    }
}; // namespace Engine::RenderSystemState
