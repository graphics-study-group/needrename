// Unit tests for the GPU resource retirement facility:
//   - the allocation hand-off on destruction (task 1.1)
//   - the reported-set / completed-prefix knowledge model (task 1.2)
//   - the two parking modes and their release conditions (task 1.3)
//   - epoch termination: report and abandonment (task 1.4)
//   - debug observability and assertions (task 1.5)
//   - device-idle release of parked resources (task 2.3)
//
// The assertion checks run in a child process of this same executable, because
// an assertion failure terminates the process that trips it.

#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Submission/EpochTracker.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#include <cstdlib>
#include <process.h>
#endif

using namespace Engine;
using namespace Engine::Rhi;

static bool g_pass = true;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                                        \
            g_pass = false;                                                                                            \
        }                                                                                                              \
    } while (0)

/// @brief Number of live VMA allocations, used to observe that device memory was
/// actually freed (or deliberately not freed).
static uint32_t LiveAllocationCount(AllocatorState &allocator) {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator.GetAllocator(), &stats);
    return stats.total.statistics.allocationCount;
}

// ── Assertion (death) scenarios, run in a child process ─────────────────────

static int RunDeathScenario(const char *name) {
#if defined(_MSC_VER) && !defined(NDEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    SDL_Init(SDL_INIT_VIDEO);
    DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "EpochTracker death scenario",
        .application_version = 0,
        .dynamic_dispatcher = nullptr,
    };
    DeviceInterface gpu_device{cfg};

    const std::string scenario{name};
    if (scenario == "--death-second-tracker") {
        EpochTracker first{gpu_device.GetDevice()};
        // A device must have exactly one tracker; this must assert.
        EpochTracker second{gpu_device.GetDevice()};
        std::cerr << "UNREACHABLE: a second tracker for the same device was accepted." << std::endl;
        return 0;
    }
    if (scenario == "--death-outstanding-epochs") {
        EpochTracker tracker{gpu_device.GetDevice()};
        // Open epochs and never terminate them: the completed prefix can never
        // advance past them, so the debug bound must fire.
        for (size_t i = 0; i <= EpochTracker::MAX_OUTSTANDING_EPOCHS; ++i) {
            tracker.BeginEpoch();
        }
        std::cerr << "UNREACHABLE: outstanding-epoch bound was not asserted." << std::endl;
        return 0;
    }
    std::cerr << "Unknown death scenario: " << name << std::endl;
    return 2;
}

/// @brief Spawn this executable with the given scenario flag; return its exit code.
static int SpawnSelf(const char *self, const char *flag) {
#if defined(_MSC_VER)
    return static_cast<int>(_spawnl(_P_WAIT, self, self, flag, static_cast<char *>(nullptr)));
#else
    const std::string cmd = std::string{"\""} + self + "\" " + flag;
    return std::system(cmd.c_str());
#endif
}

int main(int argc, char **argv) {
    if (argc > 1 && std::string{argv[1]}.rfind("--death-", 0) == 0) {
        return RunDeathScenario(argv[1]);
    }

    // Requires the SDL video subsystem for SDL_Vulkan_LoadLibrary(nullptr).
    SDL_Init(SDL_INIT_VIDEO);

    DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "Rhi::EpochTracker Test",
        .application_version = 0,
        .dynamic_dispatcher = nullptr,
    };
    DeviceInterface gpu_device{cfg};
    const auto device = gpu_device.GetDevice();
    CHECK(device && "Rhi must create a Vulkan device headlessly.");

    // The tracked allocator: its retire sink is the tracker, so every buffer
    // allocated through it becomes retire-safe without opt-in.
    AllocatorState allocator{gpu_device};
    EpochTracker tracker{gpu_device.GetDevice()};
    allocator.SetRetireSink(&tracker);
    // An allocator with no tracker: allocations destroy immediately.
    AllocatorState plain_allocator{gpu_device};

    // ── Task 1.1: the hand-off on destruction ───────────────────────────────

    {
        // A fresh tracker: nothing issued, nothing completed, nothing parked.
        CHECK(tracker.GetNewestWatermark() == INVALID_EPOCH_WATERMARK);
        CHECK(tracker.GetCompletedPrefix() == INVALID_EPOCH_WATERMARK);
        CHECK(tracker.GetParkedResourceCount() == 0u);

        // With the tracker installed, destruction hands the allocation over
        // instead of freeing it. The hand-off is only observable while the
        // allocation could still be referenced, so this block opens the first
        // epoch: that epoch is what the upper-bound release condition waits for.
        const EpochWatermark hand_off_epoch = tracker.BeginEpoch();
        CHECK(hand_off_epoch == 1u && "the first watermark issued by a fresh tracker must be 1");

        plain_allocator.SetRetireSink(&tracker);
        const uint32_t before = LiveAllocationCount(plain_allocator);
        {
            auto allocation = plain_allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Hand-off test buffer");
            CHECK(allocation.GetBuffer() != nullptr);
        }
        CHECK(tracker.GetParkedResourceCount() == 1u && "destruction must hand off exactly once");
        CHECK(
            LiveAllocationCount(plain_allocator) == before + 1u
            && "the hand-off must not free the memory; vmaDestroyBuffer must not have been called"
        );

        // The tracker owns the allocation until the epoch it is parked under is
        // reported; the memory goes then, not before.
        tracker.ReportComplete(hand_off_epoch);
        CHECK(tracker.GetParkedResourceCount() == 0u);
        CHECK(LiveAllocationCount(plain_allocator) == before && "the tracker frees the memory on release");
        plain_allocator.SetRetireSink(nullptr);
    }

    {
        const uint32_t before = LiveAllocationCount(plain_allocator);
        {
            auto allocation = plain_allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "No tracker buffer");
            CHECK(allocation.GetBuffer() != nullptr);
        }
        CHECK(
            LiveAllocationCount(plain_allocator) == before
            && "without a tracker installed the allocation must destroy immediately"
        );
    }

    // ── Task 1.2: a report proves one epoch; the prefix is derived ──────────

    {
        // Watermark numbering continues from the hand-off block above, so these
        // assertions are relative. The "first watermark is 1" claim belongs to
        // that block, where the tracker is genuinely fresh.
        const EpochWatermark e1 = tracker.BeginEpoch();
        const EpochWatermark e2 = tracker.BeginEpoch();
        const EpochWatermark e3 = tracker.BeginEpoch();
        CHECK(e2 == e1 + 1 && e3 == e2 + 1 && "watermarks must be strictly monotonic");
        CHECK(tracker.GetOutstandingEpochCount() == 3u);

        // A report for a later epoch must not advance the prefix.
        const EpochWatermark prefix_before = tracker.GetCompletedPrefix();
        tracker.ReportComplete(e3);
        CHECK(tracker.GetCompletedPrefix() == prefix_before && "a later report must not advance the prefix");
        CHECK(tracker.IsEpochReported(e3) && "the reported epoch itself is proven");
        CHECK(tracker.GetReportedEpochCount() == 1u);
        CHECK(tracker.GetOutstandingEpochCount() == 2u);

        // Filling the gap advances the prefix through the whole contiguous run
        // in one step.
        tracker.ReportComplete(e1);
        CHECK(tracker.GetCompletedPrefix() == e1);
        tracker.ReportComplete(e2);
        CHECK(tracker.GetCompletedPrefix() == e3 && "the prefix must advance in one step over the contiguous run");
        CHECK(tracker.GetReportedEpochCount() == 0u && "reports folded into the prefix are no longer pending");
        CHECK(tracker.GetOutstandingEpochCount() == 0u);
    }

    // ── Task 1.3: upper-bound mode ──────────────────────────────────────────

    {
        // A resource released while several earlier epochs are outstanding parks
        // under the newest watermark issued at that moment.
        const EpochWatermark e1 = tracker.BeginEpoch();
        const EpochWatermark e2 = tracker.BeginEpoch();
        const EpochWatermark e3 = tracker.BeginEpoch();
        const uint32_t before = LiveAllocationCount(allocator);
        {
            auto allocation = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Upper-bound buffer");
            // Destroyed here: parks under e3.
        }
        CHECK(tracker.GetParkedResourceCount() == 1u);
        CHECK(
            LiveAllocationCount(allocator) == before + 1u
            && "an upper-bound parked allocation must not be freed before the prefix arrives"
        );

        // A report that does not fill the gap releases nothing.
        tracker.ReportComplete(e3);
        CHECK(tracker.GetParkedResourceCount() == 1u && "a later report must not release an upper-bound resource");
        tracker.ReportComplete(e2);
        CHECK(tracker.GetParkedResourceCount() == 1u && "the prefix still has not reached the bound");
        tracker.ReportComplete(e1);
        CHECK(tracker.GetParkedResourceCount() == 0u && "the prefix reached the bound");
        CHECK(LiveAllocationCount(allocator) == before && "the parked allocation is freed when the prefix arrives");
    }

    {
        // "Recorded but not yet submitted" is not "not yet referenced": an
        // allocation bound into a command buffer that is still being recorded
        // stays parked until its epoch is reported, even though no submission
        // has been issued.
        const EpochWatermark e = tracker.BeginEpoch();
        {
            auto allocation = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Recorded-not-submitted");
        }
        CHECK(tracker.GetParkedResourceCount() == 1u);
        CHECK(tracker.IsEpochOutstanding(e) && "no submission was issued, but the epoch is still open");
        CHECK(
            tracker.GetParkedResourceCount() == 1u
            && "the allocation must not be released merely because no submission has been issued yet"
        );
        tracker.ReportComplete(e);
        CHECK(tracker.GetParkedResourceCount() == 0u && "reported -> released");
    }

    // ── Task 1.3: exact mode ────────────────────────────────────────────────

    {
        const EpochWatermark e1 = tracker.BeginEpoch();
        const EpochWatermark e2 = tracker.BeginEpoch();
        const uint32_t before = LiveAllocationCount(allocator);
        {
            auto allocation = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Exact-mode staging");
            tracker.RetireExact(std::move(allocation), e2);
        }
        CHECK(tracker.GetParkedResourceCount() == 1u && "an exact claim naming an unreported epoch is retained");
        CHECK(LiveAllocationCount(allocator) == before + 1u && "an unreported exact claim must not be trusted");

        // Reporting the *other* epoch does not release it, even though the
        // prefix has not moved at all either.
        tracker.ReportComplete(e1);
        CHECK(tracker.GetParkedResourceCount() == 1u && "only the named epoch's own report releases it");
        CHECK(tracker.GetCompletedPrefix() == e1);

        tracker.ReportComplete(e2);
        CHECK(tracker.GetParkedResourceCount() == 0u && "the named epoch's report releases it");
        CHECK(LiveAllocationCount(allocator) == before);
    }

    {
        // A condition that already holds when the resource is parked releases
        // immediately, with no separate immediate-release rule.
        const EpochWatermark e = tracker.BeginEpoch();
        tracker.ReportComplete(e);
        const uint32_t before = LiveAllocationCount(allocator);

        {
            auto exact = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Already-complete exact");
            tracker.RetireExact(std::move(exact), e);
        }
        CHECK(tracker.GetParkedResourceCount() == 0u && "an already-reported exact epoch releases immediately");
        CHECK(LiveAllocationCount(allocator) == before);

        {
            // No epoch is open and the newest watermark is already reported, so
            // the upper-bound condition also holds at park time.
            auto upper = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Already-complete upper bound");
        }
        CHECK(tracker.GetParkedResourceCount() == 0u && "an already-satisfied upper bound releases immediately");
        CHECK(LiveAllocationCount(allocator) == before);
    }

    // ── Task 1.4: abandonment ───────────────────────────────────────────────

    {
        const EpochWatermark prefix_before = tracker.GetCompletedPrefix();
        const EpochWatermark e1 = tracker.BeginEpoch();
        const EpochWatermark e2 = tracker.BeginEpoch();
        const uint32_t before = LiveAllocationCount(allocator);
        {
            auto allocation = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Abandoned epoch buffer");
        }
        CHECK(tracker.GetParkedResourceCount() == 1u);

        // Abandoning the epoch the resource is parked under must not release it:
        // an earlier epoch is still unreported and may reference it.
        tracker.AbandonEpoch(e2);
        CHECK(tracker.IsEpochReported(e2) && "abandonment marks the epoch reported");
        CHECK(tracker.GetCompletedPrefix() == prefix_before && "the prefix cannot jump the unreported earlier epoch");
        CHECK(tracker.GetParkedResourceCount() == 1u && "abandonment must not release anything directly");
        CHECK(LiveAllocationCount(allocator) == before + 1u);

        tracker.ReportComplete(e1);
        CHECK(tracker.GetParkedResourceCount() == 0u && "the resource is released once the prefix reaches it");
        CHECK(LiveAllocationCount(allocator) == before);
    }

    // ── Task 1.5: observability ─────────────────────────────────────────────

    {
        const EpochWatermark e = tracker.BeginEpoch();
        CHECK(tracker.GetNewestWatermark() == e);
        CHECK(tracker.GetCompletedPrefix() == e - 1);
        CHECK(tracker.GetOutstandingEpochCount() == 1u);
        CHECK(tracker.IsEpochOutstanding(e));
        CHECK(!tracker.IsEpochReported(e));

        {
            auto allocation = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Observability buffer");
        }
        CHECK(tracker.GetParkedResourceCount() == 1u);
        tracker.ReportComplete(e);
        CHECK(tracker.GetParkedResourceCount() == 0u);
        CHECK(tracker.GetOutstandingEpochCount() == 0u);
        CHECK(tracker.GetCompletedPrefix() == e);
    }

    // ── Task 2.3: device-idle release ───────────────────────────────────────

    {
        const EpochWatermark e = tracker.BeginEpoch();
        {
            auto allocation = allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Parked at idle");
        }
        CHECK(tracker.GetParkedResourceCount() == 1u);

        auto live = DeviceBuffer::CreateUnique(plain_allocator, {BufferTypeBits::CopyTo}, 256, "Still-live buffer");
        CHECK(live != nullptr && live->GetBuffer() != nullptr);

        device.waitIdle();
        tracker.ReleaseAllParked();
        CHECK(tracker.GetParkedResourceCount() == 0u && "an idle wait must release every parked resource");
        CHECK(live->GetBuffer() != nullptr && "live resources are untouched by an idle release");

        // The epoch is still open and still delays the prefix: an idle release
        // is not a substitute for reporting.
        CHECK(tracker.IsEpochOutstanding(e));
        tracker.ReportComplete(e);
    }

    // ── Task 1.6: DeviceContext owns the tracker and wires the sink ─────────

    {
        // A DeviceContext owns the tracker, installs it as the allocator's sink
        // and destroys it before the allocator, so a buffer allocated through it
        // is retire-safe with no opt-in. The block's teardown exercises the
        // destroy order.
        DeviceContext context{cfg};
        CHECK(context.GetAllocatorState().GetRetireSink() == &context.GetEpochTracker());

        const uint32_t before = LiveAllocationCount(context.GetAllocatorState());
        {
            auto allocation = DeviceBuffer::CreateUnique(
                context.GetAllocatorState(), {BufferTypeBits::CopyTo}, 256, "DeviceContext buffer"
            );
            CHECK(allocation != nullptr && allocation->GetBuffer() != nullptr);
        }
        // Destroyed with no epoch open: the upper-bound condition holds already,
        // so it is freed immediately rather than parked.
        CHECK(context.GetEpochTracker().GetParkedResourceCount() == 0u);
        CHECK(LiveAllocationCount(context.GetAllocatorState()) == before);

        // With an epoch open, the same destruction parks instead, and the
        // tracker's teardown releases it.
        const EpochWatermark e = context.GetEpochTracker().BeginEpoch();
        {
            auto allocation = DeviceBuffer::CreateUnique(
                context.GetAllocatorState(), {BufferTypeBits::CopyTo}, 256, "DeviceContext parked buffer"
            );
        }
        CHECK(context.GetEpochTracker().GetParkedResourceCount() == 1u);
        CHECK(LiveAllocationCount(context.GetAllocatorState()) == before + 1u);
        context.GetEpochTracker().ReportComplete(e);
        CHECK(context.GetEpochTracker().GetParkedResourceCount() == 0u);
        CHECK(LiveAllocationCount(context.GetAllocatorState()) == before);
    }

    // ── Task 1.5: assertions (child processes) ──────────────────────────────

#if !defined(NDEBUG)
    {
        const int rc = SpawnSelf(argv[0], "--death-outstanding-epochs");
        CHECK(rc != 0 && "opening epochs without terminating them must trip the outstanding-epoch assertion");
    }
    {
        const int rc = SpawnSelf(argv[0], "--death-second-tracker");
        CHECK(rc != 0 && "a second tracker for one device must trip the singleton assertion");
    }
#else
    std::cout << "Release build: assertion scenarios skipped." << std::endl;
#endif

    device.waitIdle();
    tracker.ReleaseAllParked();

    if (!g_pass) {
        std::cerr << "Rhi::EpochTracker test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Rhi::EpochTracker test PASSED." << std::endl;
    return 0;
}
