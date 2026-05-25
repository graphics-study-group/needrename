#include "RenderThread.h"

#include <thread>
#include "RenderServiceQueue.h"
#include "RenderThreadState.h"

#include "Render/RenderThread/Tasks/RenderTaskControl.h"

namespace {
    void render_thread_main_func(
        Engine::RenderThreadState & state,
        Engine::RenderServiceQueue & queue
    ) {
        while(true) {

            switch(state.state) {
            case Engine::RenderThreadState::State::WAITING:
                std::this_thread::yield();
                break;

            case Engine::RenderThreadState::State::HALTED:
                if (state.render_system) {
                    state.render_system->WaitForIdle();
                }
                state.render_system.reset();
                return;
    
            case Engine::RenderThreadState::State::RUNNING: {
                    while(!queue.empty()) {
                        auto f = queue.pop();
                        f->Execute(state);

                        if (state.state != Engine::RenderThreadState::State::RUNNING) {
                            break;
                        }
                    }
                }
            }
        }
        
    }
}

namespace Engine {
    struct RenderThread::impl {
        std::jthread thread{};
        RenderThreadState state{};
        RenderServiceQueue queue{};
    };

    RenderThread::RenderThread(
        std::weak_ptr <SDLWindow> window
    ) : pimpl(std::make_unique<impl>()) {
        pimpl->thread = std::jthread{
            render_thread_main_func,
            std::ref(pimpl->state),
            std::ref(pimpl->queue)
        };

        pimpl->queue.push(
            std::make_unique<RenderTasks::RenderTaskInitialize>(window)
        );
    }

    RenderThread::~RenderThread() noexcept {
        pimpl->queue.push(std::make_unique<RenderTasks::RenderTaskFinish>());
        pimpl->thread.get_stop_source().request_stop();
        pimpl->thread.join();
    };

    const RenderServiceQueue & RenderThread::GetServiceQueue() const noexcept {
        return pimpl->queue;
    }

    const RenderThreadState & RenderThread::GetThreadState() const noexcept {
        return pimpl->state;
    }
}
