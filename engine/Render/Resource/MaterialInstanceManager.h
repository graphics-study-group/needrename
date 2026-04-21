#ifndef RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED

#include "IRenderResourceManager.h"

#include <unordered_map>

namespace Engine {
    class RenderSystem;
    class MaterialInstance;
}

namespace Engine::RenderSystemState {
    /**
     * @brief Manager for MaterialInstance render resources.
     *
     * GUID maps to a MaterialAsset; payload is an instantiated
     * MaterialInstance. This provider also tracks and releases
     * MaterialLibrary dependency handles when records are destroyed.
     */
    class MaterialInstanceManager final : public IRenderResourceManager<MaterialInstance> {
    public:
        using IRenderResourceManager<MaterialInstance>::IRenderResourceManager;

        MaterialInstanceManager(RenderSystem &system);

        MaterialInstanceHandle CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames);
        /**
         * @brief Synchronously acquire or create a MaterialInstance record.
         *
         * Ensures MaterialAsset is available and tracks dependent
         * MaterialLibrary handle in provider-owned dependency table.
         */
        void AcquireImpl(MaterialInstanceHandle handle);

        /**
         * @brief Acquire through async-friendly path.
         *
         * Current implementation may fallback to synchronous completion and
         * acquires MaterialLibrary via manager async entry.
         */
        void AcquireAsyncImpl(MaterialInstanceHandle handle);

        /**
         * @brief Resolve payload pointer as MaterialInstance.
         */
        void ReleaseImpl(MaterialInstanceHandle handle);

        /**
         * @brief Check whether MaterialInstance payload exists.
         */
        bool IsReadyImpl(MaterialInstanceHandle handle) const noexcept;

        /**
         * @brief Synchronous readiness barrier for instance object.
         *
         * Material instance descriptor allocation and UBO updates still happen lazily during BindMaterial -> UpdateGPUInfo.
         * Manager readiness here guarantees only that the instance object and its immediate dependencies exist now.
         */
        void EnsureReadyImpl(MaterialInstanceHandle handle);

        /**
         * @brief Release tracked dependency handles for destroyed record.
         */
        void OnDestroyImpl(MaterialInstanceHandle handle) noexcept;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED
