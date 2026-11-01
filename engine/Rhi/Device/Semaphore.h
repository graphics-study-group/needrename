#ifndef ENGINE_RHI_SEMAPHORE_INCLUDED
#define ENGINE_RHI_SEMAPHORE_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>
#include <string_view>

namespace Engine::Rhi {

    class DeviceInterface;

    /**
     * @brief What a semaphore can be waited on and signalled by.
     *
     * The two kinds differ in what "signalled" means. A binary semaphore is a
     * flag: it is signalled once and consumed by the wait. A timeline semaphore
     * carries a value that only moves forward, so several submissions can wait
     * for different points of the same one without consuming it.
     */
    enum class SemaphoreType : uint8_t {
        /// @brief Signalled or not; consumed by a wait.
        Binary,
        /// @brief Carries a value that callers wait for up to.
        Timeline
    };

    /**
     * @brief An ordering primitive between submissions.
     *
     * A semaphore says "this work may start once that work reaches here". It owns
     * itself, so a submission that outlives the call which created it does not
     * need anyone to track create/destroy pairs.
     *
     * @note Movable but non-copyable.
     */
    class RHI_API Semaphore {
    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

        Semaphore(std::unique_ptr<impl> p) noexcept;

    public:
        /**
         * @brief Create a semaphore.
         *
         * @param device_interface The device that owns it.
         * @param type Binary or timeline.
         * @param initial_value The value a timeline semaphore starts at; binary
         * semaphores ignore it.
         * @param name A debug name, shown by validation and capture tools.
         */
        static Semaphore Create(
            const DeviceInterface &device_interface,
            SemaphoreType type,
            uint64_t initial_value = 0,
            std::string_view name = {}
        );

        ~Semaphore();

        Semaphore(const Semaphore &) = delete;
        Semaphore &operator=(const Semaphore &) = delete;
        Semaphore(Semaphore &&) noexcept;
        Semaphore &operator=(Semaphore &&) noexcept;

        /// @brief The portable handle, for `Submission` and for a present call.
        SemaphoreHandle GetHandle() const noexcept;

        /// @brief What kind it is.
        SemaphoreType GetType() const noexcept;

        /// @brief The value a timeline semaphore has reached.
        uint64_t GetValue() const;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SEMAPHORE_INCLUDED
