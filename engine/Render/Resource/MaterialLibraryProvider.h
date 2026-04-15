#ifndef RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"

#include <unordered_map>

namespace Engine::RenderSystemState {
    /**
     * @brief Provider for MaterialLibrary render resources.
     *
     * GUID maps to a MaterialLibraryAsset; payload is an instantiated
     * MaterialLibrary object. Internal m_records caches GUID -> record index
     * for fast reuse.
     */
    class MaterialLibraryProvider final : public IRenderResourceProvider {
        std::unordered_map<GUID, uint32_t> m_records{};

    public:
        /**
         * @brief Provider dispatch key, typeid(MaterialLibrary*).
         */
        std::type_index GetTypeID() const noexcept override;

        /**
         * @brief Synchronously acquire or create a MaterialLibrary record.
         *
         * This path guarantees underlying MaterialLibraryAsset availability
         * before instantiating MaterialLibrary.
         */
        RenderResourceHandle Acquire(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;

        /**
         * @brief Acquire through async-friendly path.
         *
         * Current implementation first tries async asset query and may
         * fallback to synchronous completion when required.
         */
        RenderResourceHandle AcquireAsync(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;

        /**
         * @brief Resolve payload pointer as MaterialLibrary.
         */
        void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept override;

        /**
         * @brief Check whether MaterialLibrary payload exists.
         */
        bool IsReady(
            RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
        ) const noexcept override;

        /**
         * @brief Synchronous readiness barrier for library object.
         *
         * Keeps semantics aligned with manager API. Library pipelines may
         * still be lazily created later by MaterialLibrary internals.
         */
        void EnsureReady(RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle) override;

        /**
         * @brief Forget GUID mapping after record destruction.
         */
        void OnRecordDestroy(GUID guid) noexcept override;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
