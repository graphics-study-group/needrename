#ifndef RENDER_RENDERSYSTEM_RENDERERMANAGER
#define RENDER_RENDERSYSTEM_RENDERERMANAGER

#include <memory>
#include <vector>
#include <glm.hpp>
#include <Framework/world/Handle.h>

namespace vk {
    class PushConstantRange;
}

namespace Engine {
    class RenderSystem;
    class RendererComponent;

    namespace RenderSystemState {
        class RendererManager {
            RenderSystem &m_system;
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            /**
             * @brief A small struct holding all renderer data pushed
             * to the GPU.
             */
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

            RendererHandle RegisterRendererComponent(RendererComponent *component);

            /**
             * @brief Unregister a component from the manager.
             * It will no longer be present in the `RendererList` returned by the manager.
             * Its underlying resources are not deallocated until a `UpdateRendererStates()` call,
             * and its auxillary data will remain in the manager until a
             * `ClearUnregisteredRendererComponent()` call.
             */
            void UnregisterRendererComponent(RendererHandle handle);

            /**
             * @brief Update renderer component states.
             * Eagerly loaded renderers which are not currently loaded will be prepared
             * to be submitted to GPU, and unregistered renderers are removed from GPU.
             *
             * Actual uploading is performed by `FrameManager`.
             */
            void UpdateRendererStates();

            /**
             * @brief Clear up unregistered renderers from internal data structure, and
             * cosolidate internal memory to avoid fragmentation.
             */
            void ClearUnregisteredRendererComponent();

            /**
             * @brief Filter and sort renderers based on given criteria.
             * All returned renderers that are not loaded to GPU (i.e. lazily loaded) will
             * be prepared to be submitted.
             *
             * Actual uploading is performed by `FrameManager`.
             */
            RendererList FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc = SortingCriterion::None);

            /**
             * @brief Get the renderer data used for draw calls.
             */
            const RendererComponent *GetRendererData(RendererHandle handle) const noexcept;

            /**
             * @brief Get the push constant range for renderers.
             */
            static vk::PushConstantRange GetPushConstantRange();
        };
    } // namespace RenderSystemState

    using RendererHandle = RenderSystemState::RendererManager::RendererHandle;
    using RendererList = RenderSystemState::RendererManager::RendererList;
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_RENDERERMANAGER
