#include "RenderRuntime.h"

namespace Engine {
    namespace {
        RenderRuntimeContext g_runtime{};
    }

    void SetRenderRuntime(const RenderRuntimeContext &ctx) noexcept {
        g_runtime = ctx;
    }

    const RenderRuntimeContext &GetRenderRuntime() noexcept {
        return g_runtime;
    }
} // namespace Engine
