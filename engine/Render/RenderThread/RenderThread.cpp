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
            auto cur = state.control.state.load(std::memory_order::acquire);

            if (cur == Engine::RenderThreadState::State::HALTED) {
                if (state.render_system) {
                    state.render_system->WaitForIdle();
                }
                state.render_system.reset();
                break;
            } else if (cur == Engine::RenderThreadState::State::WAITING) {
                std::unique_lock ul{state.control.mtx};
                state.control.suspended.test_and_set(std::memory_order::release);
                state.control.suspended.notify_all();
                state.control.cv.wait(ul, [&] {
                    return state.control.state.load(std::memory_order::acquire) != Engine::RenderThreadState::State::WAITING;
                });
                state.control.suspended.clear(std::memory_order::release);
            } else {
                while(!queue.empty()) {
                    auto f = queue.pop();
                    f->Execute(state);
                    if (state.control.state.load(std::memory_order::acquire) != Engine::RenderThreadState::State::RUNNING) {
                        break;
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

        auto initialization_future = pimpl->queue.push(
            std::make_unique<RenderTasks::RenderTaskInitialize>(window)
        );
        this->Resume();
        initialization_future.wait();
    }

    RenderThread::~RenderThread() noexcept {
        pimpl->state.control.state = RenderThreadState::State::HALTED;
        pimpl->state.control.cv.notify_all();

        pimpl->thread.get_stop_source().request_stop();
        pimpl->thread.join();
    };

    const RenderServiceQueue & RenderThread::GetServiceQueue() const noexcept {
        return pimpl->queue;
    }

    const RenderThreadState & RenderThread::GetThreadState() const noexcept {
        return pimpl->state;
    }

    RenderThreadState & RenderThread::BlockAndAcquireThreadState() noexcept {
        this->Suspend();
        return pimpl->state;
    }

    void RenderThread::Resume() const noexcept {
        if (pimpl->state.control.state == RenderThreadState::State::WAITING) {
            pimpl->state.control.state = RenderThreadState::State::RUNNING;
            pimpl->state.control.cv.notify_all();
        }
    }

    void RenderThread::Suspend() const noexcept {
        if (pimpl->state.control.state == RenderThreadState::State::RUNNING) {
            pimpl->state.control.state = RenderThreadState::State::WAITING;
            pimpl->state.control.cv.notify_all();
            pimpl->state.control.suspended.wait(false);
        }
    }
}
