#include "RendererManager.h"
#include "Core/flagbits.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include <SDL3/SDL.h>

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

        struct RendererControlBlock {
            StatusFlags status{};
            uint32_t layer{};
        };

        struct RendererDataBlock {
            /// We will try to move data such as materials and buffers from `RendererComponent`
            /// into this manager to form a ECS architecture.
            /// For now we just save its pointer.
            std::shared_ptr<RendererComponent> m_component;

            // uint32_t renderer_idx;
            // uint32_t material_idx;
        };

        std::vector<RendererControlBlock> m_status;
        std::vector<RendererDataBlock> m_data;

        // std::vector <HomogeneousMesh *> m_meshes;
        // std::vector <MaterialInstance *> m_materials;
    };

    RendererManager::RendererManager(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }
    RendererManager::~RendererManager() = default;

    RendererHandle RendererManager::RegisterRendererComponent(std::shared_ptr<RendererComponent> component) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "Registering component 0x%p", static_cast<void *>(component.get()));

        impl::StatusFlags sflag{};
        if (component->m_cast_shadow) sflag.Set(impl::StatusBits::ShadowCaster);
        if (component->m_is_eagerly_loaded) sflag.Set(impl::StatusBits::Eager);
        // sflag.Set(impl::StatusBits::Loaded);

        pimpl->m_status.push_back(impl::RendererControlBlock{.status = sflag, .layer = component->m_layer});
        pimpl->m_data.push_back(impl::RendererDataBlock{component});
        assert(pimpl->m_status.size() == pimpl->m_data.size());

        return pimpl->m_data.size() - 1;
    }
    void RendererManager::UnregisterRendererComponent(RendererHandle handle) {
        assert(handle < pimpl->m_status.size());
        pimpl->m_status[handle].status.Set(impl::StatusBits::Removal);
    }
    void RendererManager::UpdateRendererStates() {
        assert(pimpl->m_status.size() == pimpl->m_data.size());

        for (size_t i = 0; i < pimpl->m_status.size(); i++) {
            auto &s = pimpl->m_status[i].status;

            if (s.Test(impl::StatusBits::Removal)) {
                pimpl->m_data[i].m_component.reset();
                break;
            }

            if (s.Test(impl::StatusBits::Eager) && !s.Test(impl::StatusBits::Loaded)) {
                s.Set(impl::StatusBits::Loaded);
                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    "Eagerly loading component 0x%p",
                    static_cast<void *>(pimpl->m_data[i].m_component.get())
                );
                if (auto mesh_ptr = std::dynamic_pointer_cast<MeshComponent>(pimpl->m_data[i].m_component)) {
                    for (auto mesh : mesh_ptr->GetSubmeshes()) {
                        m_system.GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(*mesh);
                    }
                }
            }
        }
    }
    void RendererManager::ClearUnregisteredRendererComponent() {
        assert(!"Unimplemented");
    }
    RendererList RendererManager::FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc) {
        assert(sc == SortingCriterion::None && "Unimplemented");
        std::vector<uint32_t> ret{};

        for (uint32_t i = 0; i < pimpl->m_status.size(); i++) {

            if ((fc.layer & pimpl->m_status[i].layer) == 0) continue;
            if (fc.is_shadow_caster != FilterCriteria::BinaryCriterion::DontCare) {
                auto shadow_caster = pimpl->m_status[i].status.Test(impl::StatusBits::ShadowCaster);
                if (shadow_caster != static_cast<int>(fc.is_shadow_caster)) continue;
            }

            if (pimpl->m_status[i].status.Test(impl::StatusBits::Removal)) continue;

            ret.push_back(i);
            if (!pimpl->m_status[i].status.Test(impl::StatusBits::Loaded)) {
                pimpl->m_status[i].status.Set(impl::StatusBits::Loaded);
                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    "Lazily loading component 0x%p",
                    static_cast<void *>(pimpl->m_data[i].m_component.get())
                );

                if (auto mesh_ptr = std::dynamic_pointer_cast<MeshComponent>(pimpl->m_data[i].m_component)) {
                    for (auto mesh : mesh_ptr->GetSubmeshes()) {
                        m_system.GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(*mesh);
                    }
                }
            }
        }

        // TODO: Cache the result for better performace.
        return ret;
    }

    const RendererComponent *RendererManager::GetRendererData(RendererHandle handle) const noexcept {
        assert(handle < pimpl->m_data.size());
        assert(pimpl->m_data[handle].m_component);
        return pimpl->m_data[handle].m_component.get();
    }
    vk::PushConstantRange RendererManager::GetPushConstantRange() {
        return vk::PushConstantRange{
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof(RendererDataStruct)
        };
    }
}; // namespace Engine::RenderSystemState
