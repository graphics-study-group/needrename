#ifndef RENDER_RENDERTHREAD_RENDERSERVICEQUEUE_INCLUDED
#define RENDER_RENDERTHREAD_RENDERSERVICEQUEUE_INCLUDED

#include <memory>

#include "Render/RenderThread/Tasks/RenderTaskBase.h"

namespace Engine {
    /**
     * @brief A Multi-Producer Single-Consumer message queue for render thread.
     * 
     * Currently we use a mutex locked deque for simplicity. Maybe we should
     * replace it to Vyukov Queue or other lockfree impl. if necessary.
     */
    class RenderServiceQueue {
        struct impl;
        std::unique_ptr <impl> pimpl;

        void push_in_queue(std::unique_ptr <RenderTasks::RenderTaskBase>) const noexcept;

    public:
        RenderServiceQueue();
        ~RenderServiceQueue() noexcept;

        /**
         * @brief Push a task into the MPSC queue.
         * 
         * This action is @b safe for concurrent access.
         */
        template <class T> requires RenderTasks::is_render_task<T>
        auto push(
            std::unique_ptr <T> task
        ) const noexcept -> decltype(task->GetFuture()) {
            auto f = task->GetFuture();
            this->push_in_queue(std::move(task));
            return f;
        }

        /**
         * @brief Query the number of currently pushed tasks.
         */
        size_t size() const noexcept;

        /**
         * @brief Query whether the queue is empty.
         */
        bool empty() const noexcept;

        /**
         * @brief Get the foremost task from the MPSC queue and pop it.
         */
        std::unique_ptr<RenderTasks::RenderTaskBase>
        pop() noexcept;
    };
}

#endif // RENDER_RENDERTHREAD_RENDERSERVICEQUEUE_INCLUDED
