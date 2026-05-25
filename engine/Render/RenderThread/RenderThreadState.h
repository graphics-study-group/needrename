#ifndef RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
#define RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED

#include <memory>
#include <atomic>
#include <condition_variable>

namespace Engine {
    class RenderSystem;
    class RenderGraph2;

    /**
     * @brief State of the rendering thread.
     * 
     * Its contents except the state are only modified within the queue via tasks.
     * Therefore it is safe to not synchronize its access.
     */
    struct RenderThreadState {
        std::unique_ptr <RenderSystem> render_system{};
        std::unique_ptr <RenderGraph2> active_render_graph{};

        /// State enumeration of the render thread.
        enum class State {
            /// The thread is waiting for an instruction.
            WAITING,
            /// The thread is currently running.
            RUNNING,
            /// The thread is halted and will shortly terminate.
            HALTED
        };

        struct {
            std::atomic <State> state{State::WAITING};
            std::mutex mtx{};
            std::condition_variable cv{};

            std::atomic_flag suspended = ATOMIC_FLAG_INIT;
        } control;
        
    };
}

#endif // RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
