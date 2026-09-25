// Unit tests for in-place buffer storage replacement (stable-buffer-identity):
//   - the debug name is remembered by the allocation (task 1.1)
//   - the buffer object address survives a reallocation (tasks 1.2, 1.5)
//   - the replaced allocation takes the retirement path (tasks 1.2, 1.5)
//   - a within-capacity grow-only request reallocates nothing (task 1.3)
//   - the debug name is reproduced by the replacement storage (task 1.5)
//   - an allocator with no retirement facility frees the replaced storage
//     immediately (task 1.6)
//
// The setup is standalone: a `DeviceInterface`, an `AllocatorState` and an
// `EpochTracker`, with no `DeviceContext`.

#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Submission/EpochTracker.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

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
/// actually freed (or deliberately retained).
static uint32_t LiveAllocationCount(AllocatorState &allocator) {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator.GetAllocator(), &stats);
    return stats.total.statistics.allocationCount;
}

namespace {
    /**
     * @brief A `DeviceBuffer` that exposes the protected reallocation primitive
     * and the name of the storage it currently owns.
     *
     * Reallocation is `protected` so that only buffer types whose descriptor
     * bindings are re-established on every use expose it; this subclass is the
     * only way a unit test can observe that the primitive reproduces the name.
     */
    class NameProbeBuffer : public DeviceBuffer {
    public:
        NameProbeBuffer(const AllocatorState &allocator, BufferType type, size_t size, const std::string &name) :
            DeviceBuffer(allocator.AllocateBuffer(type, size, name), size) {
        }

        using DeviceBuffer::ReallocateStorage;
    };
} // namespace

int main() {
    // Requires the SDL video subsystem for SDL_Vulkan_LoadLibrary(nullptr).
    SDL_Init(SDL_INIT_VIDEO);

    DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "Rhi::BufferReallocation Test",
        .application_version = 0,
        .dynamic_dispatcher = nullptr,
    };
    DeviceInterface gpu_device{cfg};
    const auto device = gpu_device.GetDevice();
    CHECK(device && "Rhi must create a Vulkan device headlessly.");

    // The tracked allocator (retire sink installed) and a plain one (no
    // retirement facility at all).
    AllocatorState allocator{gpu_device};
    EpochTracker tracker{gpu_device.GetDevice()};
    allocator.SetRetireSink(&tracker);
    AllocatorState plain_allocator{gpu_device};

    // ── Task 1.1: the allocation remembers its debug name ───────────────────

    {
        auto named = plain_allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256, "Named allocation");
        CHECK(named.GetBuffer() != nullptr);

        auto unnamed = plain_allocator.AllocateBuffer({BufferTypeBits::CopyTo}, 256);
    }

    // ── Tasks 1.2, 1.3, 1.5: in-place replacement on a ComputeBuffer ────────

    {
        const EpochWatermark epoch = tracker.BeginEpoch();

        auto buffer = ComputeBuffer::CreateUnique(allocator, 256, true, false, false, false, "Reallocation target");
        CHECK(buffer != nullptr);
        CHECK(buffer->GetSize() == 256);
        CHECK(LiveAllocationCount(allocator) == 1u && "only the buffer's own allocation is live");

        const ComputeBuffer *const address = buffer.get();
        const vk::Buffer handle_before = buffer->GetBuffer();

        // Exact reallocation: the object stays, the storage and handle move.
        buffer->Reallocate(allocator, 1024);
        CHECK(buffer.get() == address && "the buffer object must stay at the same address");
        CHECK(buffer->GetSize() == 1024 && "the buffer reports its new size");
        CHECK(buffer->GetBuffer() != handle_before && "the Vulkan buffer handle must change");
        CHECK(
            tracker.GetParkedResourceCount() == 1u
            && "the replaced allocation must take the retirement path, not be freed"
        );
        CHECK(
            LiveAllocationCount(allocator) == 2u
            && "the replaced allocation is retained (parked) alongside the new storage"
        );

        // A grow-only request at or below the current size is a no-op.
        const size_t size_before = buffer->GetSize();
        const vk::Buffer handle_after = buffer->GetBuffer();
        buffer->EnsureCapacity(allocator, 512);
        CHECK(buffer->GetBuffer() == handle_after && "a within-capacity request must not change the handle");
        CHECK(buffer->GetSize() == size_before && "a within-capacity request must not change the size");
        CHECK(tracker.GetParkedResourceCount() == 1u && "a within-capacity request must not reallocate");
        CHECK(LiveAllocationCount(allocator) == 2u);
        buffer->EnsureCapacity(allocator, size_before);
        CHECK(buffer->GetBuffer() == handle_after && "a request exactly at capacity is a no-op too");

        // A request above capacity grows in place.
        buffer->EnsureCapacity(allocator, 4096);
        CHECK(buffer.get() == address && "growth must keep the object at the same address");
        CHECK(buffer->GetSize() == 4096 && "growth must satisfy the requested size");
        CHECK(buffer->GetBuffer() != handle_after && "growth must replace the handle");
        CHECK(tracker.GetParkedResourceCount() == 2u);
        CHECK(LiveAllocationCount(allocator) == 3u);

        // Releasing the parked allocations returns the allocation count to one.
        tracker.ReleaseAllParked();
        CHECK(tracker.GetParkedResourceCount() == 0u);
        CHECK(LiveAllocationCount(allocator) == 1u && "only the current storage remains after a release");

        tracker.ReportComplete(epoch);
        buffer.reset();
        CHECK(LiveAllocationCount(allocator) == 0u && "a reported epoch lets the buffer free its storage");
    }

    // ── Task 1.5: the replacement storage reproduces the debug name ─────────

    {
        const EpochWatermark epoch = tracker.BeginEpoch();

        NameProbeBuffer buffer{allocator, {BufferTypeBits::CopyTo}, 128, "Name probe"};
        CHECK(buffer.GetSize() == 128);

        buffer.ReallocateStorage(allocator, 512);
        CHECK(buffer.GetSize() == 512);
        CHECK(tracker.GetParkedResourceCount() == 1u && "the replaced storage is parked, not freed");
        CHECK(LiveAllocationCount(allocator) == 2u);

        tracker.ReportComplete(epoch);
        tracker.ReleaseAllParked();
    }
    CHECK(LiveAllocationCount(allocator) == 0u);

    // ── Task 1.6: no retirement facility, the replaced storage is freed ─────

    {
        auto buffer =
            ComputeBuffer::CreateUnique(plain_allocator, 256, true, false, false, false, "No retirement facility");
        CHECK(LiveAllocationCount(plain_allocator) == 1u);

        const ComputeBuffer *const address = buffer.get();
        const vk::Buffer handle_before = buffer->GetBuffer();

        buffer->Reallocate(plain_allocator, 1024);
        CHECK(buffer.get() == address && "the object address survives even without a retirement facility");
        CHECK(buffer->GetSize() == 1024);
        CHECK(buffer->GetBuffer() != handle_before);
        CHECK(
            LiveAllocationCount(plain_allocator) == 1u
            && "without a retirement facility the replaced allocation is destroyed immediately"
        );
        CHECK(tracker.GetParkedResourceCount() == 0u && "nothing may be parked on the tracker");

        buffer.reset();
        CHECK(LiveAllocationCount(plain_allocator) == 0u);
    }

    device.waitIdle();
    tracker.ReleaseAllParked();

    if (!g_pass) {
        std::cerr << "Rhi::BufferReallocation test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Rhi::BufferReallocation test PASSED." << std::endl;
    return 0;
}
