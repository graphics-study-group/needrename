#ifndef ENGINE_RHI_EPOCHTRACKER_INCLUDED
#define ENGINE_RHI_EPOCHTRACKER_INCLUDED

#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/rhi_export.h"

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    /**
     * @brief Monotonically increasing identifier of a submission epoch.
     *
     * A watermark is issued by the tracker and never handed to Vulkan. The two
     * id spaces (watermarks and timeline semaphore values) are linked only by a
     * submitter reporting "watermark w is complete" after it observed the
     * completion signal of a submission it issued itself.
     */
    using EpochWatermark = uint64_t;

    /// @brief Watermark value that no epoch ever carries.
    inline constexpr EpochWatermark INVALID_EPOCH_WATERMARK = 0;

    /**
     * @brief Device-scoped ledger of submission epochs and the GPU resources retired under them.
     *
     * The tracker answers one question: *may this retired buffer allocation be
     * freed yet?* Both the render side and the physics side can drive it. It is the
     * single recipient of retired buffer allocations.
     * There is deliberately no interface between the two — the
     * tracker is the only thing that ever receives a retired allocation.
     *
     * Two kinds of knowledge are kept apart, and the distinction is load-bearing:
     *
     * - the **set of individually reported epochs** — a report is proof of
     *   exactly the epoch it names, because a report is the only fact a
     *   submitter can hand over (the tracker cannot observe a queue submission);
     *
     * - the **completed prefix** — the largest watermark such that every
     *   watermark at or below it has been reported. It is *derived* from the
     *   reports and advances only through a contiguous run of them.
     *
     * CPU-side reports are routinely out of order (a frame is reported only
     * when its in-flight slot is reused, three frames later, while a blocking
     * submission reports much sooner), so the prefix lags by about one in-flight
     * depth. That lag is a correctness requirement, not an inefficiency: a
     * report for a later epoch proves nothing about an earlier one.
     *
     * A retired allocation is parked under one of two release conditions:
     *
     * - **upper bound** (the default for `~BufferAllocation`): the referencing
     *   epochs are a subset of those at or below the newest watermark issued at
     *   the moment of release; released once the completed prefix reaches it;
     *
     * - **exact**: the caller names the single epoch whose command buffers
     *   reference the resource; released as soon as that epoch is reported.
     *
     * Both conditions are evaluated when a resource is parked and again after
     * every report, so a condition that already holds releases immediately.
     * An incorrect exact claim costs retention, never safety.
     *
     * @note Not thread-safe. The engine drives allocation, submission and
     * retirement from a single thread, and the protocol introduces no
     * synchronization primitives.
     *
     * @invariant A device has exactly one tracker. Two trackers on one device
     * would park resources under watermarks that never complete — a leak, never
     * a dangling reference — so a debug assertion catches it.
     */
    class RHI_API EpochTracker final {
    public:
        /**
         * @brief Debug bound on the number of epochs open at once.
         *
         * Every epoch must eventually be reported or abandoned, otherwise the
         * completed prefix can never advance past it and everything parked above
         * it is stranded. The bound turns that class of bug into a loud failure.
         */
        static constexpr size_t MAX_OUTSTANDING_EPOCHS = 64;

    private:
        struct ParkedResource {
            BufferAllocation allocation;
            EpochWatermark watermark;
            /// @brief True when `watermark` is the single referencing epoch.
            bool exact;
        };

        vk::Device m_device{nullptr};

        EpochWatermark m_newest_watermark{INVALID_EPOCH_WATERMARK};
        EpochWatermark m_completed_prefix{INVALID_EPOCH_WATERMARK};

        /// @brief Epochs individually reported but not yet folded into the prefix.
        std::unordered_set<EpochWatermark> m_reported{};
        /// @brief Epochs issued but not yet reported or abandoned.
        std::unordered_set<EpochWatermark> m_outstanding{};

        std::vector<ParkedResource> m_parked{};

        /// @brief Query whether an epoch has been individually reported.
        bool IsReported(EpochWatermark epoch) const noexcept;
        /// @brief Query whether a parked resource's release condition holds.
        bool IsReleaseConditionMet(const ParkedResource &resource) const noexcept;
        /// @brief Release every parked resource whose condition now holds.
        void ReleaseReady() noexcept;
        /// @brief Move an epoch to its terminal reported state.
        void MarkTerminal(EpochWatermark epoch);

    public:
        /**
         * @brief Construct the tracker for a device.
         *
         * @param device The device the tracker is scoped to. Only used to assert
         * that a device has a single live tracker.
         */
        explicit EpochTracker(vk::Device device);
        ~EpochTracker();

        EpochTracker(const EpochTracker &) = delete;
        EpochTracker &operator=(const EpochTracker &) = delete;

        /**
         * @brief Open a new epoch and issue its watermark.
         *
         * @return The new epoch's watermark, strictly greater than every
         * previously issued one.
         */
        EpochWatermark BeginEpoch();

        /**
         * @brief Report an epoch complete after observing its completion signal.
         *
         * @warning A submitter may report only an epoch it opened itself, and
         * only after it observed the completion signal of a submission it issued
         * for that epoch. Reporting on another submitter's behalf asserts a
         * completion that was never observed.
         *
         * @param epoch The watermark returned by `BeginEpoch`.
         */
        void ReportComplete(EpochWatermark epoch);

        /**
         * @brief Abandon an epoch that produced no GPU work.
         *
         * Marks the epoch as reported without a completion signal, so the
         * completed prefix can advance past it. It does **not** release anything
         * directly: a resource parked under the abandoned epoch may still be
         * referenced by an earlier, unreported epoch, so it is released only
         * once the prefix reaches its bound.
         *
         * @warning Valid only for an epoch for which no submission was issued.
         * If a submission was issued and its completion cannot be observed, that
         * failure must be surfaced rather than masked by abandonment.
         *
         * @param epoch The watermark returned by `BeginEpoch`.
         */
        void AbandonEpoch(EpochWatermark epoch);

        /**
         * @brief Release every parked resource, unconditionally.
         *
         * Call this at every point where the caller has established that all GPU
         * work has finished (a device-wide idle wait), and at teardown.
         *
         * @warning This releases retired allocations only. It is not authority to
         * discard resources that are still live: a device-idle wait proves that
         * submitted work has finished, not that a live resource has no owner.
         */
        void ReleaseAllParked() noexcept;

        /**
         * @brief Retire an allocation under the exact mode.
         *
         * The caller claims that `epoch` is the only epoch whose command buffers
         * reference the allocation — for upload staging, the submission the
         * caller has just waited on. The allocation is released as soon as that
         * epoch is reported; the prefix is not consulted.
         *
         * @param allocation The released allocation, moved in.
         * @param epoch The single epoch that references the allocation.
         */
        void RetireExact(BufferAllocation &&allocation, EpochWatermark epoch) noexcept;

        /**
         * @brief Retire an allocation under the upper-bound mode.
         *
         * This is what `~BufferAllocation` invokes when an allocator has this
         * tracker installed. The bound is the newest watermark issued at this
         * moment.
         *
         * @param allocation The released allocation, moved in.
         */
        void Retire(BufferAllocation &&allocation) noexcept;

        /// @brief Get the newest watermark issued so far.
        EpochWatermark GetNewestWatermark() const noexcept;
        /// @brief Get the largest watermark whose predecessors have all been reported.
        EpochWatermark GetCompletedPrefix() const noexcept;
        /// @brief Get the number of epochs issued but not yet reported or abandoned.
        size_t GetOutstandingEpochCount() const noexcept;
        /// @brief Get the number of reported epochs not yet folded into the prefix.
        size_t GetReportedEpochCount() const noexcept;
        /// @brief Get the number of retired resources still waiting for release.
        size_t GetParkedResourceCount() const noexcept;
        /// @brief Query whether an epoch has been reported or abandoned.
        bool IsEpochReported(EpochWatermark epoch) const noexcept;
        /// @brief Query whether an epoch is issued but not yet terminated.
        bool IsEpochOutstanding(EpochWatermark epoch) const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_EPOCHTRACKER_INCLUDED
