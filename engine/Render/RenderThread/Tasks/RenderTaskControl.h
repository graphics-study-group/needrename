#ifndef RENDERTHREAD_TASKS_RENDERTASKCONTROL_INCLUDED
#define RENDERTHREAD_TASKS_RENDERTASKCONTROL_INCLUDED

#include "Render/RenderThread/Tasks/RenderTaskBase.h"
#include "Render/RenderThread/RenderThreadState.h"

namespace Engine::RenderTasks {

    struct RenderTaskControl : public RenderTaskBase {
        typedef void result_type;

        std::promise<result_type> p;
        auto GetFuture() noexcept { return p.get_future(); }
    };

    /**
     * @brief Signal the render thread to create the rendering system.
     */
    struct RenderTaskInitialize : public RenderTaskControl {
        std::weak_ptr <SDLWindow> window;

        RenderTaskInitialize(std::weak_ptr <SDLWindow> window) : window(window) {}

        void Execute(RenderThreadState & rts) override {
            assert(!rts.render_system);

            rts.render_system = std::make_unique<RenderSystem>(this->window);
            rts.render_system->Create();
            rts.state = RenderThreadState::State::RUNNING;

            p.set_value();
        };
    };

    /**
     * @brief Signal the render thread to finish all tasks and end gracefully.
     */
    struct RenderTaskFinish : public RenderTaskControl {
        void Execute(RenderThreadState & rts) override {
            rts.state = RenderThreadState::State::HALTED;

            p.set_value();
        }
    };
}

#endif // RENDERTHREAD_TASKS_RENDERTASKCONTROL_INCLUDED
