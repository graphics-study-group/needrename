#ifndef RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
#define RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED

#include <atomic>
#include <condition_variable>
#include <memory>

namespace Engine {
    class RenderSystem;
    class RenderGraph2;

    /**
     * @brief Control interface of the render thread.
     */
    struct RenderThreadControlBlock {
        /// State enumeration of the render thread.
        enum class Command {
            /// The thread is waiting for an instruction.
            WAIT,
            /// The thread is currently running.
            CONTINUE,
            /// The thread is halted and will shortly terminate.
            HALT
        };

        std::atomic<Command> state{Command::WAIT};
        mutable std::mutex mtx{};
        mutable std::condition_variable cv{};
    };

    /**
     * @brief State of the rendering thread.
     *
     * Its contents except the state are only modified within the queue via tasks.
     * Therefore it is safe to not synchronize its access.
     */
    struct RenderThreadState {
        /// @brief Render system instance held by the thread.
        std::unique_ptr<RenderSystem> render_system{};
        /// @brief Active render graph for the frame.
        std::unique_ptr<RenderGraph2> active_render_graph{};

        /// @brief State of the render thread.
        enum class State {
            /// @brief Suspended due to command of other threads.
            SUSPENDING,
            /// @brief Running normally.
            RUNNING,
            /// @brief Waiting for GPU work.
            WAITING_FOR_GPU
        };

        std::atomic<State> state{State::SUSPENDING};
        mutable std::mutex state_mtx{};
        mutable std::condition_variable state_cv{};
    };
} // namespace Engine

#endif // RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
