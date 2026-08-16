#ifndef ASSET_ASSETRUNTIME_INCLUDED
#define ASSET_ASSETRUNTIME_INCLUDED

#include "Asset/asset_export.h"

namespace Engine {
    class AssetManager;
    class AssetDatabase;

    /**
     * @brief Runtime services the asset core needs, provided by the host application.
     * The host seeds the context once at startup via `SetAssetRuntime`; the pointers
     * remain valid while the host application lives.
     */
    struct AssetRuntimeContext {
        AssetManager *asset_manager{nullptr};
        AssetDatabase *asset_database{nullptr};
    };

    /// @brief Set the asset runtime context. Called by the host application at startup.
    ASSET_CORE_API void SetAssetRuntime(const AssetRuntimeContext &ctx) noexcept;
    /// @brief Get the asset runtime context.
    ASSET_CORE_API const AssetRuntimeContext &GetAssetRuntime() noexcept;
} // namespace Engine

#endif // ASSET_ASSETRUNTIME_INCLUDED
