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
         * 
         * It blocks until the initialization of the render system is completed.
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

        /**
         * @brief Acquires the render thread state and all resources of the
         * thread.
         * 
         * To ensure correct synchronization, the render thread will be
         * suspended before any resources can be acquired by other threads.
         * This function therefore blocks until the render thread has completed
         * its current task, and suspends the thread.
         * Call `Resume()` manually after you have finished manipulating the
         * state.
         * 
         * @warning This is intended as an escape hatch to access the render
         * system. Do not abuse it!
         */
        RenderThreadState & BlockAndAcquireThreadState() noexcept;

        /**
         * @brief Resume the rendering thread.
         * 
         * Has no effect if the thread is already waiting or terminated.
         */
        void Resume() const noexcept;

        /**
         * @brief Suspend the rendering thread.
         * 
         * Has no effect if the thread is already suspended or terminated.
         * 
         * This method will block until the current task submitted to the
         * rendering thread is completed, and the thread is actually sleeping.
         */
        void Suspend() const noexcept;
    };
}

#endif // RENDER_RENDERTHREAD_RENDERTHREAD_INCLUDED
