#include "Rhi/Submission/EpochTracker.h"

#include <cassert>
#include <utility>

namespace Engine::Rhi {
#if !defined(NDEBUG)
    namespace {
        /**
         * @brief Devices that currently have a live tracker (debug builds only).
         *
         * Watermarks are self-issued, so a duplicated tracker is a programming
         * error rather than a safety hole: it would park resources under
         * watermarks that never complete. Registering makes that loud instead of
         * turning the freedom to construct a tracker outside `DeviceContext`
         * into a silent trap.
         */
        std::unordered_set<VkDevice> g_live_tracker_devices{};
    } // namespace
#endif

    EpochTracker::EpochTracker(vk::Device device) : m_device(device) {
#if !defined(NDEBUG)
        const VkDevice handle = static_cast<VkDevice>(device);
        const auto [it, inserted] = g_live_tracker_devices.insert(handle);
        (void)it;
        assert(inserted && "A device already has a live EpochTracker. A device must have exactly one.");
#endif
    }

    EpochTracker::~EpochTracker() {
        // Whatever is still parked is released here: the device is being torn
        // down, so no submission can still be reading it.
        m_parked.clear();
        m_parked.shrink_to_fit();

#if !defined(NDEBUG)
        g_live_tracker_devices.erase(static_cast<VkDevice>(m_device));
#endif
    }

    EpochWatermark EpochTracker::BeginEpoch() {
        ++m_newest_watermark;
        m_outstanding.insert(m_newest_watermark);
        assert(
            m_outstanding.size() <= MAX_OUTSTANDING_EPOCHS
            && "Too many outstanding epochs: an epoch is never reaching its terminal state, so the completed prefix "
               "cannot advance past it and every resource parked above it is stranded."
        );
        return m_newest_watermark;
    }

    void EpochTracker::MarkTerminal(EpochWatermark epoch) {
        assert(epoch != INVALID_EPOCH_WATERMARK && "Epoch watermark is invalid.");
        assert(epoch <= m_newest_watermark && "Epoch was never issued by this tracker.");

        const size_t erased = m_outstanding.erase(epoch);
        if (erased == 0) {
            assert(false && "Epoch is not outstanding: it was already terminated, or was never issued.");
            return;
        }

        m_reported.insert(epoch);

        // Advance only through the contiguous run of reports. A report for a
        // later epoch must not move the prefix past an earlier, unreported one.
        while (m_reported.erase(m_completed_prefix + 1) > 0) {
            ++m_completed_prefix;
        }

        ReleaseReady();
    }

    void EpochTracker::ReportComplete(EpochWatermark epoch) {
        MarkTerminal(epoch);
    }

    void EpochTracker::AbandonEpoch(EpochWatermark epoch) {
        MarkTerminal(epoch);
    }

    bool EpochTracker::IsReported(EpochWatermark epoch) const noexcept {
        return epoch <= m_completed_prefix || m_reported.contains(epoch);
    }

    bool EpochTracker::IsReleaseConditionMet(const ParkedResource &resource) const noexcept {
        if (resource.exact) {
            return IsReported(resource.watermark);
        }
        return resource.watermark <= m_completed_prefix;
    }

    void EpochTracker::ReleaseReady() noexcept {
        if (m_parked.empty()) return;

        size_t keep = 0;
        for (size_t read = 0; read < m_parked.size(); ++read) {
            if (IsReleaseConditionMet(m_parked[read])) {
                // Dropped: the erase below (or the move assignment that
                // overwrites it) destroys the allocation and frees its memory.
                continue;
            }
            if (keep != read) {
                m_parked[keep] = std::move(m_parked[read]);
            }
            ++keep;
        }
        // `erase` rather than `resize`: ParkedResource holds a
        // BufferAllocation, which is not default-constructible.
        m_parked.erase(m_parked.begin() + static_cast<ptrdiff_t>(keep), m_parked.end());
    }

    void EpochTracker::ReleaseAllParked() noexcept {
        m_parked.clear();
    }

    void EpochTracker::Retire(BufferAllocation &&allocation) noexcept {
        // Take ownership: clear the allocator's hand-off target first, so that
        // destroying the parked allocation frees device memory instead of
        // handing it straight back here.
        allocation.ClearRetireSink();
        // Upper bound: an allocation released now cannot be bound by an epoch
        // whose recording begins after now, so the referencing epochs are a
        // subset of those at or below the newest watermark issued here.
        m_parked.push_back(ParkedResource{std::move(allocation), m_newest_watermark, false});
        ReleaseReady();
    }

    void EpochTracker::RetireExact(BufferAllocation &&allocation, EpochWatermark epoch) noexcept {
        assert(
            epoch != INVALID_EPOCH_WATERMARK && "Exact retirement requires the epoch of the referencing submission."
        );
        assert(epoch <= m_newest_watermark && "Exact retirement named an epoch that was never issued.");
        // Take ownership: clear the allocator's hand-off target first, so that
        // destroying the parked allocation frees device memory instead of
        // handing it straight back here.
        allocation.ClearRetireSink();
        m_parked.push_back(ParkedResource{std::move(allocation), epoch, true});
        ReleaseReady();
    }

    EpochWatermark EpochTracker::GetNewestWatermark() const noexcept {
        return m_newest_watermark;
    }

    EpochWatermark EpochTracker::GetCompletedPrefix() const noexcept {
        return m_completed_prefix;
    }

    size_t EpochTracker::GetOutstandingEpochCount() const noexcept {
        return m_outstanding.size();
    }

    size_t EpochTracker::GetReportedEpochCount() const noexcept {
        return m_reported.size();
    }

    size_t EpochTracker::GetParkedResourceCount() const noexcept {
        return m_parked.size();
    }

    bool EpochTracker::IsEpochReported(EpochWatermark epoch) const noexcept {
        return IsReported(epoch);
    }

    bool EpochTracker::IsEpochOutstanding(EpochWatermark epoch) const noexcept {
        return m_outstanding.contains(epoch);
    }
} // namespace Engine::Rhi
