#ifndef ENGINE_RHI_FENCE_INCLUDED
#define ENGINE_RHI_FENCE_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>
#include <string_view>

namespace Engine::Rhi {

    class DeviceInterface;

    /**
     * @brief A device-side flag the host can wait on.
     *
     * A fence is signalled when everything submitted with it has completed, which
     * is the only thing a frame loop needs from one: reset it before submitting,
     * hand it to `Submit`, then wait for it. Ownership is the object's, so a
     * submitted fence outlives the call that created it without anyone tracking
     * pairs of create/destroy.
     *
     * @note Movable but non-copyable.
     */
    class RHI_API Fence {
    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

        Fence(std::unique_ptr<impl> p) noexcept;

    public:
        /**
         * @brief Create a fence.
         *
         * @param device_interface The device that owns it.
         * @param signaled Whether it starts signalled. A fence handed to a first
         * `Submit` before anything has been waited on has to start signalled, or
         * the first wait blocks forever.
         * @param name A debug name, shown by validation and capture tools.
         */
        static Fence Create(const DeviceInterface &device_interface, bool signaled, std::string_view name = {});

        ~Fence();

        Fence(const Fence &) = delete;
        Fence &operator=(const Fence &) = delete;
        Fence(Fence &&) noexcept;
        Fence &operator=(Fence &&) noexcept;

        /// @brief The portable handle, for `Submit` and `WaitForFence`.
        FenceHandle GetHandle() const noexcept;

        /// @brief Whether everything submitted with it has completed.
        bool IsSignaled() const;

        /// @brief Return it to the unsignalled state, ready for the next submission.
        void Reset();
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_FENCE_INCLUDED
