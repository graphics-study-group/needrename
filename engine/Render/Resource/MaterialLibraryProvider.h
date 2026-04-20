#ifndef RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"

namespace Engine {
    class MaterialLibrary;
}

namespace Engine::RenderSystemState {
    /**
     * @brief Provider for MaterialLibrary render resources.
     *
     * GUID maps to a MaterialLibraryAsset; payload is an instantiated
     * MaterialLibrary object. Pipelines inside the library are still
     * created lazily by MaterialLibrary::FindMaterialTemplate.
     */
    class MaterialLibraryProvider final : public IRenderResourceProvider<MaterialLibrary> {
    public:
        using IRenderResourceProvider<MaterialLibrary>::IRenderResourceProvider;

        /**
         * @brief Create a MaterialLibrary from the given asset GUID and register it.
         *
         * Loads the asset synchronously, instantiates the library, and stores the
         * resulting handle in the GUID cache for future reuse.
         */
        MaterialLibraryHandle CreateFromAssetImpl(GUID guid);

        /// @brief No-op: library is fully instantiated inside CreateFromAssetImpl.
        void AcquireImpl(MaterialLibraryHandle handle);

        /// @brief No-op: async path falls back to synchronous creation for now.
        void AcquireAsyncImpl(MaterialLibraryHandle handle);

        /// @brief No-op: deferred reclamation is driven by TickFrame countdown.
        void ReleaseImpl(MaterialLibraryHandle handle);

        /**
         * @brief Returns true whenever the handle resolves to a live library object.
         *
         * Pipeline creation inside MaterialLibrary is still lazy; provider
         * readiness only guarantees the library object itself exists.
         */
        bool IsReadyImpl(MaterialLibraryHandle handle) const noexcept;

        /// @brief No additional action required beyond object existence.
        void EnsureReadyImpl(MaterialLibraryHandle handle);

        /// @brief No provider-owned dependencies to release for MaterialLibrary.
        void OnDestroyImpl(MaterialLibraryHandle handle) noexcept;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
