#include "Rhi/Device/Semaphore.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>

namespace Engine::Rhi {
    /**
     * @brief A semaphore on a backend that has none.
     *
     * The standard interface has no timeline semaphore and no way to submit work that
     * waits on one: a queue submission is ordered by the queue itself, and the only
     * ordering primitive is a callback. A `Semaphore` object here therefore carries the
     * value a caller asked for and reports it back, and ordering beyond that is the
     * queue's own.
     *
     * This is enough for the seam's frame manager to construct, and not enough to
     * overlap two submissions. That is the honest state, and the message a caller sees
     * when it tries is on `DeviceContext::Submit` and `SignalSemaphore`.
     */
    struct Semaphore::impl {
        SemaphoreType type{SemaphoreType::Binary};
        uint64_t value{0};
        std::string name{};
    };

    Semaphore::Semaphore(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {
    }

    Semaphore Semaphore::Create(
        const DeviceInterface &, SemaphoreType type, uint64_t initial_value, std::string_view name
    ) {
        auto state = std::make_unique<impl>();
        state->type = type;
        state->value = initial_value;
        state->name = std::string{name};
        SDL_LogWarn(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: created a %s semaphore, which only records its value on this backend.",
            type == SemaphoreType::Timeline ? "timeline" : "binary"
        );
        return Semaphore(std::move(state));
    }

    Semaphore::Semaphore(Semaphore &&) noexcept = default;
    Semaphore &Semaphore::operator=(Semaphore &&) noexcept = default;
    Semaphore::~Semaphore() = default;

    SemaphoreHandle Semaphore::GetHandle() const noexcept {
        return SemaphoreHandle{};
    }

    SemaphoreType Semaphore::GetType() const noexcept {
        return pimpl->type;
    }

    uint64_t Semaphore::GetValue() const {
        return pimpl->value;
    }
} // namespace Engine::Rhi
