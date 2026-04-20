#ifndef RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"

#include <unordered_map>

namespace Engine::RenderSystemState {
    /**
     * @brief Provider for MaterialInstance render resources.
     *
     * GUID maps to a MaterialAsset; payload is an instantiated
     * MaterialInstance. This provider also tracks and releases
     * MaterialLibrary dependency handles when records are destroyed.
     */
    class MaterialInstanceProvider final : public IRenderResourceProvider {
        std::unordered_map<uint32_t, std::vector<RenderResourceHandle>> m_dependencies{};

    public:
        /**
         * @brief Provider dispatch key, typeid(MaterialInstance*).
         */
        std::type_index GetTypeID() const noexcept override;

        /**
         * @brief Synchronously acquire or create a MaterialInstance record.
         *
         * Ensures MaterialAsset is available and tracks dependent
         * MaterialLibrary handle in provider-owned dependency table.
         */
        RenderResourceHandle Acquire(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;

        /**
         * @brief Acquire through async-friendly path.
         *
         * Current implementation may fallback to synchronous completion and
         * acquires MaterialLibrary via manager async entry.
         */
        RenderResourceHandle AcquireAsync(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;

        /**
         * @brief Resolve payload pointer as MaterialInstance.
         */
        void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept override;

        /**
         * @brief Check whether MaterialInstance payload exists.
         */
        bool IsReady(
            RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
        ) const noexcept override;

        /**
         * @brief Synchronous readiness barrier for instance object.
         *
         * Material instance descriptor allocation and UBO updates still happen lazily during BindMaterial -> UpdateGPUInfo.
         * Provider readiness here guarantees only that the instance object and its immediate dependencies exist now.
         */
        void EnsureReady(RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle) override;

        /**
         * @brief Release tracked dependency handles for destroyed record.
         */
        void OnRecordDestroy(RenderResourceManager &manager, RenderResourceHandle handle) noexcept override;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALINSTANCEPROVIDER_INCLUDED
