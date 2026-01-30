#include "RendererManager.h"
#include "Core/flagbits.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include <SDL3/SDL.h>
#include <unordered_set>

namespace Engine::RenderSystemState {
    struct RendererManager::impl {

        enum class StatusBits : uint32_t {
            MemoryStatusMask = 0x0000FFFF,
            RendererTypeMask = 0xFFFF0000,

            // Submitted to GPU and ready for rendering.
            Ready = 0b0000'0000'0000'0001,
            // A submission is pending.
            Pending = 0b0000'0000'0000'0010,
            // Loaded to CPU memory from disk.
            Loaded = 0b0000'0000'0000'0100,
            // Eager submission requested.
            Eager = 0b0000'0000'0000'1000,

            // Slated for removal from the manager.
            Removal = 0b0000'0000'1000'0000,

            // This renderer cast shadows.
            ShadowCaster = 0b0000'0001'0000'0000,
        };
        using StatusFlags = Flags<StatusBits>;

        ////////////////////////////////////////////////////////////////////////

        std::unordered_map <
            std::shared_ptr <RendererComponent>,
            RendererList
        > renderer_components;

        struct StaticPerSubmeshData {
            uint32_t refcnt{0};
            std::vector <std::unique_ptr <DeviceBuffer>> vertex_index_buffer;
        };
        std::unordered_map <GUID, StaticPerSubmeshData, GUIDHash> static_mesh_asset_data_cache;

        ////////////////////////////////////////////////////////////////////////

        // XXX: use atomic for multithreads.
        uint32_t total_renderer_count;

        struct RendererDataBlock {
            MaterialInstance * material;
            RendererComponent * component;
            const MeshAsset::Submesh * submesh;
            std::unique_ptr <IVertexBasedRenderer> renderer;
        };
        std::unordered_map<RendererHandle, RendererDataBlock> m_data;

        RendererList CreateHomogeousMeshFromAsset(
            const std::shared_ptr <RendererComponent> & rc,
            const std::shared_ptr <AssetRef> & asset,
            RenderSystem & s
        ) {
            RendererList rl{};
            auto masset = asset->cas<MeshAsset>();
            rl.reserve(masset->GetSubmeshCount());
            for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
                m_data[total_renderer_count].renderer = 
                    std::make_unique<HomogeneousMesh>(
                        s.GetAllocatorState(),
                        asset,
                        i
                    );
                m_data[total_renderer_count].material = rc->GetMaterial(i).get();
                m_data[total_renderer_count].component = rc.get();
                m_data[total_renderer_count].submesh = &masset->m_submeshes[i];
                rl.push_back(total_renderer_count);

                if (rc->m_is_eagerly_loaded) {
                    m_data[total_renderer_count].renderer->Submit(
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

    void RendererManager::RegisterRendererComponent(std::shared_ptr<RendererComponent> component) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "Registering component 0x%p", static_cast<void *>(component.get()));
        // Legacy mesh component
        if (auto mc = std::dynamic_pointer_cast<MeshComponent>(component)) {
            auto rl = pimpl->CreateHomogeousMeshFromAsset(
                mc, mc->m_mesh_asset, m_system
            );
            pimpl->renderer_components[component] = rl;
        }
        // Unknown component
        else {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unknown renderer component type");
        }
    }
    RendererList RendererManager::GetRendererListsFromComponent(
        const std::shared_ptr<RendererComponent> &component
    ) const noexcept {
        auto itr = pimpl->renderer_components.find(component);
        assert(itr != pimpl->renderer_components.end());
        return itr->second;
    }
    void RendererManager::UnregisterRendererComponent(
        const std::shared_ptr<RendererComponent> &component
    ) {
        if (!pimpl->renderer_components.contains(component))    return;
        for (const auto i : pimpl->renderer_components[component]) {
            pimpl->m_data[i].renderer->Remove();
            pimpl->m_data.erase(i);
        }
        pimpl->renderer_components.erase(component);
    }
    void RendererManager::UpdateRendererStates() {
        for (auto & [k, v] : pimpl->m_data) {
            if (!v.renderer->IsReady()) {
                v.renderer->Submit(m_system.GetFrameManager().GetSubmissionHelper());
            }
        }
    }
    void RendererManager::ClearUnregisteredRendererComponent() {
        assert(!"Unimplemented");
    }
    RendererList RendererManager::FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc) {
        assert(sc == SortingCriterion::None && "Unimplemented");
        std::unordered_set <uint32_t> filtered_renderers{};

        for (const auto & [rc, rl] : pimpl->renderer_components) {

            if ((fc.layer & rc->m_layer) == 0) continue;
            if (fc.is_shadow_caster != FilterCriteria::BinaryCriterion::DontCare) {
                if (rc->m_cast_shadow != static_cast<int>(fc.is_shadow_caster)) continue;
            }

            for (auto i : rl) {
                if (!pimpl->m_data[i].renderer->IsReady()) {
                    pimpl->m_data[i].renderer->Submit(m_system.GetFrameManager().GetSubmissionHelper());
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
