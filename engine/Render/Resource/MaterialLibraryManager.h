#ifndef RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED

#include "IRenderResourceManager.h"

namespace Engine {
    class MaterialLibrary;
}

namespace Engine::RenderSystemState {
    /**
     * @brief Manager for MaterialLibrary render resources.
     *
     * Purpose and lifecycle:
     * - GUID maps to a MaterialLibraryAsset.
     * - Payload is a fully instantiated MaterialLibrary object that holds the library's
     *   configuration (shader references, pipeline templates, etc.).
     * - MaterialLibrary is refcounted by instances that depend on it; no explicit
     *   provider-side tracking needed (instances hold handles).
     *
     * Preparation model (eager object, lazy pipelines):
     * - CreateFromAssetImpl eagerly loads the MaterialLibraryAsset and immediately
     *   instantiates the MaterialLibrary object via library->Instantiate(*asset).
     * - AcquireImpl/AcquireAsyncImpl are no-ops; payload object is already constructed.
     * - EnsureReadyImpl is a no-op; object existence is the readiness criterion.
     * - Individual graphics pipelines (VkPipeline) are created lazily by
     *   MaterialLibrary::FindMaterialTemplate() on first use, reducing startup overhead.
     *
     * Comparison to MaterialInstance:
     * - MaterialLibrary is a shared resource dependency; many MaterialInstances may
     *   reference the same MaterialLibrary (deduplication via CreateOrReuseFromAsset).
     * - MaterialInstance holds a handle to MaterialLibrary to ensure it stays alive.
     */
    class MaterialLibraryManager final : public IRenderResourceManager<MaterialLibrary> {
    public:
        using IRenderResourceManager<MaterialLibrary>::IRenderResourceManager;

        /**
         * @brief Create a MaterialLibrary from the given asset GUID.
         *
         * Loads MaterialLibraryAsset eagerly via AssetRef::Acquire().
         * Instantiates MaterialLibrary object and populates its state via Instantiate(*asset).
         * Returns a handle with refcount=1.
         * The asset reference is released after instantiation; asset memory is independent
         * of library resource lifetime.
         *
         * @param guid GUID of the MaterialLibraryAsset to instantiate.
         * @param deallocate_after_frames Frame countdown before deferred destruction.
         * @return Newly allocated MaterialLibraryHandle.
         */
        MaterialLibraryHandle CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames);

        /**
         * @brief Synchronous acquire (no-op).
         *
         * Library object is already fully constructed in CreateFromAssetImpl.
         */
        void AcquireImpl(MaterialLibraryHandle &handle);

        /**
         * @brief Asynchronous acquire (no-op).
         *
         * Library object is already fully constructed in CreateFromAssetImpl.
         */
        void AcquireAsyncImpl(MaterialLibraryHandle &handle);

        /**
         * @brief Release (no-op).
         *
         * Deferred reclamation countdown is managed entirely by base class TickFrame logic.
         */
        void ReleaseImpl(MaterialLibraryHandle &handle);

        /**
         * @brief Check whether MaterialLibrary payload exists and is ready.
         *
         * Validity of the handle is sufficient; all pipelines inside the library
         * are created lazily on demand (FindMaterialTemplate).
         *
         * @param handle Target handle.
         * @return True if handle is valid (and thus payload is guaranteed ready).
         */
        bool IsReadyImpl(const MaterialLibraryHandle &handle) const noexcept;

        /**
         * @brief Ensure MaterialLibrary is ready (no-op).
         *
         * Library object is already fully constructed in CreateFromAssetImpl.
         * Individual pipelines are created lazily.
         */
        void EnsureReadyImpl(MaterialLibraryHandle &handle);

        /**
         * @brief Cleanup upon final destruction (no-op).
         *
         * MaterialLibrary destructor handles cleanup of internal resources
         * (e.g., VkPipelineLayout, descriptor set layouts).
         * No provider-side dependencies to release.
         */
        void OnDestroyImpl(MaterialLibraryHandle &handle) noexcept;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
