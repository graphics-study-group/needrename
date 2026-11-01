#include "Rhi/Device/Fence.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>

namespace Engine::Rhi {
    /**
     * @brief A fence on a backend that has none.
     *
     * The standard interface has no host-waitable fence: completion arrives as a
     * callback on the queue. A `Fence` object here therefore holds no device object,
     * and `IsSignaled` answers the question a caller actually asks before submitting -
     * "can I reuse this slot yet?" - with the only answer that is safe when nothing is
     * known to be in flight.
     *
     * That answer is `true`, and it is correct for as long as `Submit` does nothing.
     * When submissions exist, the slot's fence is the queue callback and this becomes a
     * flag the callback sets. Reporting `false` meanwhile would stall a frame loop on a
     * fence that can never signal.
     */
    struct Fence::impl {
        bool signaled{true};
        std::string name{};
    };

    Fence::Fence(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {
    }

    Fence Fence::Create(const DeviceInterface &, bool signaled, std::string_view name) {
        auto state = std::make_unique<impl>();
        state->signaled = signaled;
        state->name = std::string{name};
        return Fence(std::move(state));
    }

    Fence::Fence(Fence &&) noexcept = default;
    Fence &Fence::operator=(Fence &&) noexcept = default;
    Fence::~Fence() = default;

    FenceHandle Fence::GetHandle() const noexcept {
        // No device object to name. The handle is invalid, which is what
        // DeviceContext::WaitForFence reports rather than pretending to wait.
        return FenceHandle{};
    }

    bool Fence::IsSignaled() const {
        return pimpl->signaled;
    }

    void Fence::Reset() {
        // Nothing to reset on the device; the flag is the whole object.
        pimpl->signaled = false;
    }
} // namespace Engine::Rhi
