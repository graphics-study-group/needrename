#ifndef RENDER_RENDERSYSTEM_RENDERERMANAGER
#define RENDER_RENDERSYSTEM_RENDERERMANAGER

#include <memory>
#include <vector>

namespace Engine {
    class RenderSystem;

    class MeshComponent;

    class RendererManager {
        RenderSystem & m_system;
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:

        struct FilterCriteria {
            uint32_t layer;
        };

        enum class SortingCriterion {
            None,
            ByPriority,
            ByMaterial,
            ByDistanceToActiveCamera
        };

        using RendererHandle = uint32_t;
        using RendererList = std::vector <RendererHandle>;

        RendererManager(RenderSystem & system);
        ~RendererManager();

        void RegisterRendererComponent(MeshComponent & component);

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
         * Actual uploading is performed by `SubmissionHelper` on `StartFrame()` call.
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
         * Actual uploading is performed by `SubmissionHelper` on `StartFrame()` call.
         */
        RendererList FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc = SortingCriterion::None);
    };
}

#endif // RENDER_RENDERSYSTEM_RENDERERMANAGER
