#ifndef RENDERTHREAD_TASKS_RENDERTASKCONTROL_INCLUDED
#define RENDERTHREAD_TASKS_RENDERTASKCONTROL_INCLUDED

#include "Render/RenderThread/Tasks/RenderTaskBase.h"

namespace Engine::RenderTasks {

    struct RenderTaskControl : public RenderTaskBase {
        typedef void result_type;

        std::promise<result_type> p{};
        auto get_future() noexcept { return p.get_future(); }
    };

    /**
     * @brief Signal the render thread to create the rendering system.
     */
    struct RenderTaskInitialize : public RenderTaskControl {
        std::weak_ptr <SDLWindow> window;

        RenderTaskInitialize(std::weak_ptr <SDLWindow> window) : window(window) {}

        void do_execute(RenderThreadState & rts) override;
    };

    /**
     * @brief Render one frame with the rendering thread, if a render graph is
     * active.
     */
    struct RenderTaskRenderOneFrameWithActiveGraph : RenderTaskControl {
        RGTextureHandle presenting_texture;
        void do_execute(RenderThreadState & rts) override;
    };

    /**
     * @brief Build a render graph and set it as the current active one.
     */
    struct RenderTaskBuildActiveRenderGraph : RenderTaskControl {
        std::unique_ptr <RenderGraphBuilder2> builder_;
        RenderTaskBuildActiveRenderGraph(std::unique_ptr <RenderGraphBuilder2> builder) : builder_(std::move(builder)) {}
        void do_execute(RenderThreadState & rts) override;
    };
}

#endif // RENDERTHREAD_TASKS_RENDERTASKCONTROL_INCLUDED
