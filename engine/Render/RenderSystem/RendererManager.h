#ifndef RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED

#include "Render/Resource/RenderResourceManager.h"

#include <Framework/world/Handle.h>
#include <glm.hpp>
#include <memory>
#include <vector>

namespace vk {
    class PushConstantRange;
}

namespace Engine {
    class RuntimeRenderer;
    class RenderSystem;

    namespace RenderSystemState {
        /**
         * @brief Manages runtime rendering data (meshes, materials) as a stateless data pool.
         *
         * Callers register renderers and receive handles that they own.
         * Callers are responsible for calling Unregister() when done (typically in destructor).
         * Callers must push per-frame data (e.g. model matrix) via UpdateModelMatrix() before drawing.
         *
         * This class stores NO Component references of any kind.
         */
        class RendererManager {
            RenderSystem &m_system;
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            struct RendererDataStruct {
                glm::mat4 model_matrix;
                int32_t camera_index;
            };

            struct FilterCriteria {
                enum class BinaryCriterion : uint8_t {
                    No,
                    Yes,
                    DontCare
                };

                BinaryCriterion is_shadow_caster{BinaryCriterion::DontCare};
                uint32_t layer{0xFFFFFFFF};
            };

            enum class SortingCriterion {
                None,
                ByPriority,
                ByMaterial,
                ByDistanceToActiveCamera
            };

            using RendererHandle = uint32_t;
            using RendererList = std::vector<RendererHandle>;

            RendererManager(RenderSystem &system);
            ~RendererManager();

            /**
             * @brief Register a runtime renderer object and transfer ownership.
             *
             * The caller MUST call Unregister() for returned handle when done
             * (typically in destructor).
             */
            RendererHandle RegisterRenderer(std::unique_ptr<RuntimeRenderer> renderer);

            /**
             * @brief Mark a renderer for deferred deallocation.
             * Caller invokes this when it no longer needs the renderer.
             */
            void Unregister(RendererHandle handle);

            /**
             * @brief Update the model matrix for a renderer.
             * Must be called each frame BEFORE DrawRenderers is invoked.
             */
            void UpdateModelMatrix(RendererHandle handle, const glm::mat4 &matrix);

            /**
             * @brief Clear up unregistered renderers from internal data structure.
             */
            void PerformPendingCleanUp();

            /**
             * @brief Filter and sort all live renderers by given criteria.
             */
            RendererList FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc = SortingCriterion::None);

            /// @brief Get runtime renderer used for draw submission.
            const RuntimeRenderer *GetRenderer(RendererHandle handle) const noexcept;

            /// @brief Get the resource handle of the material payload.
            RenderResourceHandle GetMaterialResourceHandle(RendererHandle handle) const noexcept;

            /**
             * @brief Get the cached model matrix for a renderer.
             */
            const glm::mat4 &GetModelMatrix(RendererHandle handle) const noexcept;

            static vk::PushConstantRange GetPushConstantRange();
        };
    } // namespace RenderSystemState

    using RendererHandle = RenderSystemState::RendererManager::RendererHandle;
    using RendererList = RenderSystemState::RendererManager::RendererList;
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED
