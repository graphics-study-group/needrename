#ifndef RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
#define RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED

#include <memory>
#include <atomic>
#include <condition_variable>

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

        std::atomic <Command> state{Command::WAIT};
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
        std::unique_ptr <RenderSystem> render_system{};
        /// @brief Active render graph for the frame.
        std::unique_ptr <RenderGraph2> active_render_graph{};

        /// @brief Indicates whether the current thread is suspended due to command.
        std::atomic_flag suspending = ATOMIC_FLAG_INIT;
        /// @brief Indicates whether the current thread is waiting for GPU work,
        std::atomic_flag waiting = ATOMIC_FLAG_INIT;
    };
}

#endif // RENDER_RENDERTHREAD_RENDERTHREADSTATE_INCLUDED
