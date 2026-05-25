#ifndef RENDERTHREAD_TASKS_RENDERTASKBASE_INCLUDED
#define RENDERTHREAD_TASKS_RENDERTASKBASE_INCLUDED

#include <future>

namespace Engine {
    struct RenderThreadState;

    namespace RenderTasks {
        /**
         * @brief ABC for render tasks.
         */
        struct RenderTaskBase {

            virtual ~RenderTaskBase() = default;
            /**
             * @brief Execute the task and modify the renderer state.
             * 
             * This method should also invoke a `std::promise::set_value()`
             * method to notify the caller that an operation is completed.
             */
            virtual void Execute(RenderThreadState & rts) = 0;
        };

        template <class T>
        concept is_render_task = std::is_base_of_v <RenderTaskBase, T> && requires (T t) {
            typename T::result_type;
            { t.GetFuture()  } -> std::same_as<std::future<typename T::result_type>>;
        };
    }
}

#endif // RENDERTHREAD_TASKS_RENDERTASKBASE_INCLUDED
