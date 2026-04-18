#include "RendererManager.h"

#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/Renderer/RuntimeRenderer.h"

#include <SDL3/SDL.h>
#include <memory>
#include <unordered_set>

namespace Engine::RenderSystemState {
    struct RendererManager::impl {
        struct RendererEntry {
            int32_t pending_deallocation_countdown = -1;

            std::unique_ptr<RuntimeRenderer> renderer{};

            glm::mat4 model_matrix{1.0f};
        };

        uint32_t next_handle = 0;
        std::unordered_map<RendererHandle, RendererEntry> m_data;

        RendererHandle CreateRenderer(std::unique_ptr<RuntimeRenderer> renderer) {
            assert(renderer && "Renderer must not be null");
            auto &d = m_data[next_handle];
            d.pending_deallocation_countdown = -1;
            d.renderer = std::move(renderer);

            auto ret = next_handle++;
            return ret;
        }
    };

    RendererManager::RendererManager(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }
    RendererManager::~RendererManager() = default;

    RendererHandle RendererManager::RegisterRenderer(std::unique_ptr<RuntimeRenderer> renderer) {
        return pimpl->CreateRenderer(std::move(renderer));
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
                for (auto handle : it->second.renderer->GetResourceHandles()) {
                    if (handle.IsValid()) {
                        resource_manager.Release(handle);
                    }
                }
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

            if ((fc.layer & entry.renderer->GetLayer()) == 0) continue;
            if (fc.is_shadow_caster != FilterCriteria::BinaryCriterion::DontCare) {
                if (entry.renderer->CastShadow() != static_cast<bool>(fc.is_shadow_caster)) continue;
            }

            if (!entry.renderer->IsResourcesReady(resource_manager)) continue;
            filtered_renderers.insert(handle);
        }

        RendererList ret{};
        ret.reserve(filtered_renderers.size());
        for (auto i : filtered_renderers) ret.push_back(i);
        return ret;
    }

    const RuntimeRenderer *RendererManager::GetRenderer(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.renderer.get();
    }

    RenderResourceHandle RendererManager::GetMaterialResourceHandle(RendererHandle handle) const noexcept {
        auto it = pimpl->m_data.find(handle);
        assert(it != pimpl->m_data.end());
        return it->second.renderer->GetResourceHandle(RuntimeRenderer::MATERIAL_RESOURCE_SLOT);
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
