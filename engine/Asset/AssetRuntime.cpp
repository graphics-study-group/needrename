#include "AssetRuntime.h"

namespace Engine {
    namespace {
        AssetRuntimeContext g_runtime{};
    }

    void SetAssetRuntime(const AssetRuntimeContext &ctx) noexcept {
        g_runtime = ctx;
    }

    const AssetRuntimeContext &GetAssetRuntime() noexcept {
        return g_runtime;
    }
} // namespace Engine
