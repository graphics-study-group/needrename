#ifndef RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED

#include <vector>
#include <glm.hpp>
#include <Framework/world/Handle.h>

namespace vk {
    class PushConstantRange;
}

namespace Engine {
    class RenderSystem;
    class RendererComponent;
    class MaterialInstance;
    class IVertexBasedRenderer;
    class ComponentHandle;

    namespace RenderSystemState {
        /**
         * @brief This class manages run-time data and their lifetime of renderers (i.e. meshes).
         * 
         * Two sets of interfaces are provided in this class. 
         * The first high-level one faces `RendererComponent`s and their derivatives, controls lifetime of the
         * actual renderers, and how draws are filtered.
         * The second low-level one faces `RendererHandle`s, and is used to sort draws, and obtain draw calls and their
         * information.
         * 
         * `RendererHandle`s are provided on a submesh (i.e. `HomogeneousMesh`) granularity, and one `RendererComponent`
         * can therefore have multipled `RendererHandle`s.
         * They directly interfaces with low level Vulkan functionalities.
         * 
         * @note This class does not handles Asset lifetime. It simply assumes that all used assets are available.
         */
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
            
            /**
             * @brief Register a renderer component to the renderer manager.
             * 
             * This manager will hereafter hold a ComponentHandle to the
             * component until it being unregistered.
             * 
             * Underlying resource of this renderer component will be allocated
             * if necessary, and in the process related assets may be accessed.
             * But its data will not be submitted to GPU until it is used unless
             * it is explicitly specified to be eagerly loaded.
             */
            void RegisterRendererComponent(const ComponentHandle & component);

            /**
             * @brief Fetch the underlying renderers of this renderer.
             */
            RendererList GetRendererListsFromComponent(
                const ComponentHandle & component
            ) const noexcept;

            /**
             * @brief Unregister a component from the manager.
             * It will no longer be present in the `RendererList` returned by the manager.
             * Actual clean up of left-over GPU resources are not performed until
             * `PerformPendingCleanUp()` call, to avoid destroying
             * resources that is still used by GPU.
             */
            void UnregisterRendererComponent(const ComponentHandle & component);

            /**
             * @brief Clear up unregistered renderers from internal data structure, and
             * cosolidate internal memory to avoid fragmentation.
             */
            void PerformPendingCleanUp();

            /**
             * @brief Filter and sort renderers based on given criteria.
             * All returned renderers that are not loaded to GPU (i.e. lazily loaded) will
             * be prepared to be submitted.
             *
             * Actual uploading is performed by `FrameManager`.
             * 
             * @note You should not cache the result of this method across frames,
             * as renderers could be deallocated.
             */
            RendererList FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc = SortingCriterion::None);

            /**
             * @brief Get the renderer data used for draw calls.
             */
            const IVertexBasedRenderer *GetRendererData(RendererHandle handle) const noexcept;

            /**
             * @brief Get the component that the renderer is attached to.
             */
            const RendererComponent *GetRendererComponent(RendererHandle handle) const noexcept;

            /**
             * @brief Get the material that the renderer uses.
             */
            MaterialInstance *GetMaterialInstance(RendererHandle handle) const noexcept;

            /**
             * @brief Get the push constant range for renderers.
             */
            static vk::PushConstantRange GetPushConstantRange();
        };
    } // namespace RenderSystemState

    using RendererHandle = RenderSystemState::RendererManager::RendererHandle;
    using RendererList = RenderSystemState::RendererManager::RendererList;
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED
