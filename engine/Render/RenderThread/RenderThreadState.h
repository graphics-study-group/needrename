#ifndef RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
#define RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED

#include <memory>

namespace Engine {
    class RenderSystem;
    class RenderGraph2;

    /**
     * @brief State of the rendering thread.
     * 
     * Its contents are only modified within the queue via tasks. Therefore it
     * is safe to not synchronize its access.
     */
    struct RenderThreadState {
        std::unique_ptr <RenderSystem> render_system;
        std::unique_ptr <RenderGraph2> active_render_graph;

        /// State enumeration of the render thread.
        enum class State {
            /// The render thread is waiting for an instruction.
            WAITING,
            /// The render thread is currently running.
            RUNNING,
            /// The render thread is halted and will shortly terminate.
            HALTED
        };

        // Prepared for quitting.
        State state{State::WAITING};
    };
}

#endif // RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
