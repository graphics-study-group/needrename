#ifndef RENDER_RENDERRUNTIME_INCLUDED
#define RENDER_RENDERRUNTIME_INCLUDED

#include "Render/render_export.h"

namespace Engine {
    class RenderSystem;
    class ShaderCompiler;

    /**
     * @brief Runtime services the render module needs, provided by the host application.
     * The host seeds the context once at startup via `SetRenderRuntime`; the pointers
     * remain valid while the host application lives.
     */
    struct RENDER_API RenderRuntimeContext {
        RenderSystem *render_system{nullptr};
        /// @brief Editor-only shader compiler; null in shipped games.
        ShaderCompiler *shader_compiler{nullptr};
    };

    /// @brief Set the render runtime context. Called by the host application at startup.
    RENDER_API void SetRenderRuntime(const RenderRuntimeContext &ctx) noexcept;
    /// @brief Get the render runtime context.
    RENDER_API const RenderRuntimeContext &GetRenderRuntime() noexcept;
} // namespace Engine

#endif // RENDER_RENDERRUNTIME_INCLUDED
