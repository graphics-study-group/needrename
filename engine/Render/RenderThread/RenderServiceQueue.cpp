#include "RenderServiceQueue.h"

namespace Engine {
    struct RenderServiceQueue::impl {
        std::deque<std::unique_ptr<RenderTasks::RenderTaskBase>> queue;
        mutable std::mutex mtx;
    };

    RenderServiceQueue::RenderServiceQueue() :
        pimpl(std::make_unique<impl>()) {

        };

    RenderServiceQueue::~RenderServiceQueue() noexcept = default;

    void RenderServiceQueue::push_in_queue(std::unique_ptr<RenderTasks::RenderTaskBase> t) const noexcept {
        std::lock_guard _{pimpl->mtx};
        pimpl->queue.push_back(std::move(t));
    }

    size_t RenderServiceQueue::size() const noexcept {
        return pimpl->queue.size();
    }

    bool RenderServiceQueue::empty() const noexcept {
        return pimpl->queue.empty();
    }

    std::unique_ptr<RenderTasks::RenderTaskBase> RenderServiceQueue::pop() noexcept {
        std::lock_guard _{pimpl->mtx};
        auto fnt = std::move(pimpl->queue.front());
        pimpl->queue.pop_front();
        return fnt;
    }
} // namespace Engine
