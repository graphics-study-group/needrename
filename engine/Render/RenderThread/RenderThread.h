#ifndef RENDER_RENDERTHREAD_RENDERTHREAD_INCLUDED
#define RENDER_RENDERTHREAD_RENDERTHREAD_INCLUDED

#include <memory>

namespace Engine {
    class RenderServiceQueue;
    struct RenderThreadState;

    /**
     * @brief A thread that uniquely owns all rendering resources.
     * 
     * Contains a `std::jthread` that performs all works, a task queue for
     * communication and other infrastructures.
     * 
     * The thread holds the unique ownership of the whole rendering subsystem.
     * Therefore, all manipulations of low-level objects such as images or
     * buffers must be completed via the task queue asynchronously.
     * 
     * Render-related workloads encapsulated in the `RenderGraph2` are performed
     * on the render thread.
     */
    class RenderThread {
        struct impl;
        std::unique_ptr <impl> pimpl;
    
    public:
        /**
         * @brief Create the resources for the thread.
         * 
         * The initialization of the rendering system is also performed in the
         * constructor.
         */
        RenderThread(
            std::weak_ptr<SDLWindow> window
        );

        /**
         * @brief Signal the thread to halt and wait for its termination.
         * 
         * The thread will wait for all workloads to be executed and then
         * terminate. This method will therefore block.
         */
        ~RenderThread() noexcept;

        /**
         * @brief Get the service queue for submitting workloads.
         * 
         * The service queue is @b safe for concurrent accesses.
         */
        const RenderServiceQueue & GetServiceQueue() const noexcept;

        /**
         * @brief Get a const reference to the thread state.
         * 
         * This is used to observe its state only. Use the task queue to modify
         * it.
         */
        const RenderThreadState & GetThreadState() const noexcept;
    };
}

#endif // RENDER_RENDERTHREAD_RENDERTHREAD_INCLUDED
