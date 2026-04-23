#ifndef RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED

#include "IRenderResourceManager.h"

#include <unordered_map>

namespace Engine {
    class RenderSystem;
    class MaterialInstance;
} // namespace Engine

namespace Engine::RenderSystemState {
    /**
     * @brief Manager for MaterialInstance render resources.
     *
     * Purpose and lifecycle:
     * - GUID maps to a MaterialAsset GUID (not directly loaded; derived from asset).
     * - Payload is a fully instantiated MaterialInstance object.
     * - MaterialInstance depends on a MaterialLibrary; this manager tracks the dependency
     *   handle and ensures the library stays alive as long as any instance references it.
     * - Upon destruction (`OnDestroyImpl`), no action needed: MaterialInstance destructor
     *   releases the library dependency via its own ~MaterialInstance().
     *
     * Preparation model (eager):
     * - CreateFromAssetImpl eagerly loads the MaterialAsset and immediately instantiates
     *   the MaterialInstance object and its required MaterialLibrary.
     * - AcquireImpl/AcquireAsyncImpl are no-ops; payload is already fully constructed.
     * - EnsureReadyImpl is a no-op; if handle is valid, instance is ready by definition.
     *
     * GPU state and lazy updates:
     * - The MaterialInstance descriptor set allocation and UBO/texture bindings are NOT
     *   prepared by this manager; instead, they are created lazily the first time
     *   MaterialInstance::UpdateGPUInfo() is called (typically during BindMaterial in rendering).
     * - This defers GPU state setup until actually needed, reducing upfront overhead.
     */
    class MaterialInstanceManager final : public IRenderResourceManager<MaterialInstance> {
    public:
        using IRenderResourceManager<MaterialInstance>::IRenderResourceManager;

        /**
         * @brief Construct manager with owning render system reference.
         */
        MaterialInstanceManager(RenderSystem &system);

        /**
         * @brief Create a MaterialInstance from the given asset GUID.
         *
         * Loads MaterialAsset eagerly via AssetRef::Acquire().
         * Creates/reuses the required MaterialLibrary via CreateOrReuseFromAsset().
         * Calls instance->Instantiate(*asset) to populate instance state from asset.
         * Returns a handle with refcount=1.
         *
         * @param guid GUID of the MaterialAsset to instantiate.
         * @param deallocate_after_frames Frame countdown before deferred destruction.
         * @return Newly allocated MaterialInstanceHandle.
         */
        MaterialInstanceHandle CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames);

        /**
         * @brief Synchronous acquire (no-op).
         *
         * Instance is already fully constructed in CreateFromAssetImpl, so no action needed.
         */
        void AcquireImpl(MaterialInstanceHandle &handle);

        /**
         * @brief Asynchronous acquire (no-op).
         *
         * Instance is already fully constructed in CreateFromAssetImpl, so no action needed.
         */
        void AcquireAsyncImpl(MaterialInstanceHandle &handle);

        /**
         * @brief Release (no-op).
         *
         * Deferred reclamation countdown is managed entirely by base class TickFrame logic.
         */
        void ReleaseImpl(MaterialInstanceHandle &handle);

        /**
         * @brief Check whether MaterialInstance payload exists and is ready.
         *
         * Since MaterialInstance is eagerly constructed, validity of the handle is
         * sufficient to declare readiness.
         *
         * @param handle Target handle.
         * @return True if handle is valid (and thus payload is guaranteed ready).
         */
        bool IsReadyImpl(const MaterialInstanceHandle &handle) const noexcept;

        /**
         * @brief Ensure MaterialInstance is ready (no-op).
         *
         * Instance is already fully constructed in CreateFromAssetImpl.
         * GPU descriptor/binding updates happen lazily during BindMaterial.
         */
        void EnsureReadyImpl(MaterialInstanceHandle &handle);

        /**
         * @brief Cleanup upon final destruction (no-op).
         *
         * MaterialInstance destructor automatically releases the MaterialLibrary
         * dependency handle, so no explicit action needed here.
         */
        void OnDestroyImpl(MaterialInstanceHandle &handle) noexcept;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED
