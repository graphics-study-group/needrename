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

        ////////////////////////////////////////////////////////////////////////

        // XXX: use atomic for multithreads.
        uint32_t total_renderer_count;
        struct RendererControlBlock {
            StatusFlags status{};
            // XXX: use atomic for multithreads.
            uint32_t refcnt{0};
        };

        struct RendererDataBlock {
            std::unique_ptr <HomogeneousMesh> renderer;
            MaterialInstance * material;
            RendererComponent * component;
        };

        std::unordered_map<RendererHandle, RendererControlBlock> m_status;
        std::unordered_map<RendererHandle, RendererDataBlock> m_data;

        void IncreaseRefCount(const RendererList & l) noexcept {
            for (auto i : l) {
                auto itr = m_status.find(i);
                if (itr != m_status.end()) {
                    itr->second.refcnt++;
                }
            }
        }

        void DecreaseRefCountAndRemove(const RendererList & l) noexcept {
            for (auto i : l) {
                auto itr = m_status.find(i);
                if (itr != m_status.end()) {
                    itr->second.refcnt--;
                    if (itr->second.refcnt == 0) {
                        itr->second.status.Set(impl::StatusBits::Removal);
                    }
                }
            }
        }

        RendererList CreateHomogeousMeshFromAsset(
            const std::shared_ptr <RendererComponent> & rc,
            const std::shared_ptr <AssetRef> & asset,
            StatusFlags sflag,
            const RenderSystemState::AllocatorState & allocator
        ) {
            RendererList rl{};
            auto masset = asset->cas<MeshAsset>();
            rl.reserve(masset->GetSubmeshCount());
            for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
                m_data[total_renderer_count].renderer = 
                    std::make_unique<HomogeneousMesh>(
                        allocator,
                        asset,
                        i
                    );
                m_data[total_renderer_count].material = rc->GetMaterial(i).get();
                m_data[total_renderer_count].component = rc.get();
                m_status[total_renderer_count].status = sflag;
                m_status[total_renderer_count].refcnt = 1;
                rl.push_back(total_renderer_count);
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

        impl::StatusFlags sflag{};
        if (component->m_cast_shadow) sflag.Set(impl::StatusBits::ShadowCaster);
        if (component->m_is_eagerly_loaded) sflag.Set(impl::StatusBits::Eager);

        // Legacy mesh component
        if (auto mc = std::dynamic_pointer_cast<MeshComponent>(component)) {
            auto rl = pimpl->CreateHomogeousMeshFromAsset(
                mc, mc->m_mesh_asset, sflag, m_system.GetAllocatorState()
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
        return RendererList();
    }
    void RendererManager::UnregisterRendererComponent(
        const std::shared_ptr<RendererComponent> &component
    ) {
        if (!pimpl->renderer_components.contains(component))    return;

        pimpl->DecreaseRefCountAndRemove(pimpl->renderer_components[component]);
        pimpl->renderer_components.erase(component);
    }
    void RendererManager::UpdateRendererStates() {
        assert(pimpl->m_status.size() == pimpl->m_data.size());

        for (auto & [k, v] : pimpl->m_status) {

            if (v.status.Test(impl::StatusBits::Removal)) {
                break;
            }

            if (v.status.Test(impl::StatusBits::Eager) && !v.status.Test(impl::StatusBits::Loaded)) {
                v.status.Set(impl::StatusBits::Loaded);
                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    "Eagerly loading renderer %u",
                    k
                );
                m_system.GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(
                    *pimpl->m_data[k].renderer.get()
                );
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
                if (pimpl->m_status[i].status.Test(impl::StatusBits::Removal)) continue;
                filtered_renderers.insert(i);
    
                if (!pimpl->m_status[i].status.Test(impl::StatusBits::Loaded)) {
                    pimpl->m_status[i].status.Set(impl::StatusBits::Loaded);
                    SDL_LogDebug(
                        SDL_LOG_CATEGORY_RENDER,
                        "Lazily loading component %u",
                        i
                    );

                    m_system.GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(
                        *pimpl->m_data[i].renderer.get()
                    );
                }
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
