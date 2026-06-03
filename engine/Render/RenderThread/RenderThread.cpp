#include "RenderThread.h"

#include <thread>
#include "RenderServiceQueue.h"
#include "RenderThreadState.h"

#include "Render/RenderThread/Tasks/RenderTaskControl.h"

namespace {
    void render_thread_main_func(
        const Engine::RenderThreadControlBlock & control,
        Engine::RenderThreadState & state,
        Engine::RenderServiceQueue & queue
    ) {
        while(true) {
            auto cur = control.state.load(std::memory_order::acquire);

            if (cur == Engine::RenderThreadControlBlock::Command::HALT) {
                if (state.render_system) {
                    state.render_system->WaitForIdle();
                }
                state.render_system.reset();
                break;
            } else if (cur == Engine::RenderThreadControlBlock::Command::WAIT) {
                std::unique_lock ul{control.mtx};
                state.suspended.test_and_set(std::memory_order::release);
                state.suspended.notify_all();
                control.cv.wait(ul, [&] {
                    return control.state.load(std::memory_order::acquire) != Engine::RenderThreadControlBlock::Command::WAIT;
                });
                state.suspended.clear(std::memory_order::release);
            } else {
                while(!queue.empty()) {
                    auto f = queue.pop();
                    f->Execute(state);
                    if (control.state.load(std::memory_order::acquire) != Engine::RenderThreadControlBlock::Command::CONTINUE) {
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
        RenderThreadControlBlock control{};
        RenderThreadState state{};
        RenderServiceQueue queue{};
    };

    RenderThread::RenderThread(
        std::weak_ptr <SDLWindow> window
    ) : pimpl(std::make_unique<impl>()) {
        pimpl->thread = std::jthread{
            render_thread_main_func,
            std::cref(pimpl->control),
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
        pimpl->control.state = RenderThreadControlBlock::Command::HALT;
        pimpl->control.cv.notify_all();

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
        if (pimpl->control.state == RenderThreadControlBlock::Command::WAIT) {
            pimpl->control.state = RenderThreadControlBlock::Command::CONTINUE;
            pimpl->control.cv.notify_all();
        }
    }

    void RenderThread::Suspend() const noexcept {
        if (pimpl->control.state == RenderThreadControlBlock::Command::CONTINUE) {
            pimpl->control.state = RenderThreadControlBlock::Command::WAIT;
            pimpl->control.cv.notify_all();
            pimpl->state.suspended.wait(false);
        }
    }
}
