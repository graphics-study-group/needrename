#include "RendererManager.h"

#include "Asset/Mesh/MeshAsset.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"

#include <SDL3/SDL.h>
#include <unordered_set>

namespace Engine::RenderSystemState {
    struct RendererManager::impl {
        struct RendererEntry {
            int32_t pending_deallocation_countdown = -1;

            RenderResourceHandle renderer_resource{};
            RenderResourceHandle material_resource{};

            uint32_t layer = 0xFFFFFFFF;
            bool cast_shadow = false;
            bool is_eagerly_loaded = false;

            glm::mat4 model_matrix{1.0f};
        };

        uint32_t next_handle = 0;
        std::unordered_map<RendererHandle, RendererEntry> m_data;

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

            RendererList rl{};
            rl.reserve(masset->GetSubmeshCount());

            auto &resource_manager = system.GetRenderResourceManager();
            for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
                auto &d = m_data[next_handle];
                d.pending_deallocation_countdown = -1;
                d.renderer_resource = resource_manager.AcquireStaticMeshRenderer(
                    mesh_asset_ref.GetGUID(), static_cast<uint32_t>(i), eagerly_loaded
                );
                d.material_resource = resource_manager.AcquireMaterialInstance(material_asset_refs[i].GetGUID());
                d.layer = layer;
                d.cast_shadow = cast_shadow;
                d.is_eagerly_loaded = eagerly_loaded;

                if (eagerly_loaded) {
                    resource_manager.EnsureRendererReady(d.renderer_resource);
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
        auto &resource_manager = m_system.GetRenderResourceManager();
        for (auto it = pimpl->m_data.begin(); it != pimpl->m_data.end();) {
            if (it->second.pending_deallocation_countdown < 0) {
                ++it;
                continue;
            }
            it->second.pending_deallocation_countdown -= 1;
            if (it->second.pending_deallocation_countdown == 0) {
                resource_manager.Release(it->second.renderer_resource);
                resource_manager.Release(it->second.material_resource);
                it = pimpl->m_data.erase(it);
            } else {
                ++it;
            }
        }
    }

    RendererList RendererManager::FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc) {
        assert(sc == SortingCriterion::None && "Unimplemented");
        std::unordered_set<uint32_t> filtered_renderers{};

        auto &resource_manager = m_system.GetRenderResourceManager();
        for (const auto &[handle, entry] : pimpl->m_data) {
            if (entry.pending_deallocation_countdown >= 0) continue;

            if ((fc.layer & entry.layer) == 0) continue;
            if (fc.is_shadow_caster != FilterCriteria::BinaryCriterion::DontCare) {
                if (entry.cast_shadow != static_cast<int>(fc.is_shadow_caster)) continue;
            }

            if (!resource_manager.EnsureRendererReady(entry.renderer_resource)) continue;
            filtered_renderers.insert(handle);
        }

        RendererList ret{};
        ret.reserve(filtered_renderers.size());
        for (auto i : filtered_renderers) ret.push_back(i);
        return ret;
    }

    RenderResourceHandle RendererManager::GetRendererResourceHandle(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.renderer_resource;
    }

    RenderResourceHandle RendererManager::GetMaterialResourceHandle(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.material_resource;
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
