#include "RadixSort.h"

#include <vulkan/vulkan.hpp>

#include "ParallelScan.h"
#include "Physics/PhysicsDispatch.h"
#include "Physics/PhysicsSpirvLoader.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ComputeKernel.h"

#include <cassert>
#include <utility>

namespace {
    /// @brief Number of bits needed to represent @p value; zero for zero.
    uint32_t BitWidth(uint32_t value) noexcept {
        uint32_t bits = 0u;
        while (value != 0u) {
            ++bits;
            value >>= 1u;
        }
        return bits;
    }

    /// @brief `ceil(bit_width(max_key_value) / 8)`, with the zero-pass shortcut.
    ///
    /// A key bound of zero is rejected by `Record`, so this only distinguishes
    /// the "no byte carries information" case: a bound of one means every key the
    /// caller may write is zero (the contract is "every key is below
    /// `2^(8 * num_passes)`"), so the input is already sorted and no pass is
    /// recorded.
    uint32_t NumRadixPasses(uint32_t max_key_value) noexcept {
        if (max_key_value <= 1u) {
            return 0u;
        }
        return (BitWidth(max_key_value) + 7u) / 8u;
    }

    /// @brief Reject a call whose geometry outgrows one of the buffers bound for it.
    void RequireSize(
        const Engine::Rhi::ComputeBuffer &buffer, size_t required_bytes, uint32_t elem_capacity, const char *what
    ) {
        if (buffer.GetSize() >= required_bytes) {
            return;
        }
        throw std::runtime_error(
            "RadixSort::Record: elem_capacity " + std::to_string(elem_capacity) + " needs "
            + std::to_string(required_bytes) + " bytes for " + what + ", which exceeds the buffer bound for this call"
        );
    }
} // namespace

namespace Engine {

    // Push-constant layouts, matching the blocks in
    // engine/Physics/shader/algorithm/ (std430).
    struct RadixHistogramPush {
        uint32_t byte_shift;
        uint32_t num_blocks;
    };
    static_assert(sizeof(RadixHistogramPush) == 8, "RadixHistogramPush must be 8 bytes");

    struct RadixScatterPush {
        uint32_t byte_shift;
        uint32_t num_blocks;
        uint32_t has_payload;
    };
    static_assert(sizeof(RadixScatterPush) == 12, "RadixScatterPush must be 12 bytes");

    // Match radix_copy_back.comp's push block.
    struct RadixCopyBackPush {
        uint32_t elem_count;
        uint32_t has_payload;
    };
    static_assert(sizeof(RadixCopyBackPush) == 8, "RadixCopyBackPush must be 8 bytes");

    struct RadixSort::Impl {
        Rhi::DeviceContext &device_context;
        bool initialized = false;

        Rhi::ComputeKernel *histogram_kernel = nullptr;
        Rhi::ComputeKernel *scatter_kernel = nullptr;
        Rhi::ComputeKernel *copy_back_kernel = nullptr;

        // The prefix-sum step is an implementation detail: the class owns the
        // instance, which owns its own block-sums storage.
        std::unique_ptr<ParallelScan> scan{};

        // Working storage, grown geometrically and reused across calls: the
        // transposed per-block digit histogram, and the ping-pong partner arrays
        // the caller's records are sorted into and out of.  The partner arrays
        // never hold a result a caller depends on — an odd pass count is copied
        // back into the caller's arrays — so sharing them across calls is safe.
        std::unique_ptr<Rhi::ComputeBuffer> histogram{};
        std::unique_ptr<Rhi::ComputeBuffer> keys_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> payload_tmp{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            histogram_kernel =
                &LoadPhysicsKernel(device_context, "algorithm/radix_block_histogram.comp.spv", "RadixBlockHistogram");
            scatter_kernel = &LoadPhysicsKernel(device_context, "algorithm/radix_scatter.comp.spv", "RadixScatter");
            copy_back_kernel =
                &LoadPhysicsKernel(device_context, "algorithm/radix_copy_back.comp.spv", "RadixCopyBack");
            scan = std::make_unique<ParallelScan>(device_context);
        }

        /// @brief Grow the working storage to hold a call of this geometry.
        void EnsureWorkingStorage(size_t key_bytes, size_t histogram_bytes, bool has_payload) {
            const Rhi::AllocatorState &allocator = device_context.GetAllocatorState();
            Rhi::EnsureComputeBuffer(histogram, allocator, histogram_bytes, false, "RadixSort Histogram");
            Rhi::EnsureComputeBuffer(keys_tmp, allocator, key_bytes, false, "RadixSort KeysTemp");
            if (has_payload) {
                Rhi::EnsureComputeBuffer(payload_tmp, allocator, key_bytes, false, "RadixSort PayloadTemp");
            }
        }
    };

    RadixSort::RadixSort(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    RadixSort::~RadixSort() = default;

    bool RadixSort::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void RadixSort::Record(
        vk::CommandBuffer cb, const RadixSortBuffers &buffers, uint32_t elem_capacity, uint32_t max_key_value
    ) {
        if (max_key_value == 0u) {
            throw std::invalid_argument("RadixSort::Record: max_key_value must be > 0");
        }
        if (buffers.keys == nullptr || buffers.count == nullptr) {
            throw std::runtime_error("RadixSort::Record: the key array and the count buffer are both required");
        }

        // A zero capacity is a no-op, and so is a key bound whose domain holds a
        // single value: the caller's key array already holds the result.
        if (elem_capacity == 0u) {
            return;
        }
        const uint32_t num_passes = NumRadixPasses(max_key_value);
        if (num_passes == 0u) {
            return;
        }

        // Geometry is per call, so the out-of-range guard is a per-call check
        // against the buffers actually bound for this call.
        const bool has_payload = buffers.payload != nullptr;
        const size_t key_bytes = static_cast<size_t>(elem_capacity) * sizeof(uint32_t);
        RequireSize(*buffers.keys, key_bytes, elem_capacity, "the key array");
        if (has_payload) {
            RequireSize(*buffers.payload, key_bytes, elem_capacity, "the payload array");
        }
        RequireSize(*buffers.count, sizeof(uint32_t), elem_capacity, "the count buffer");

        m_impl->EnsureInitialized();

        // One block of 256 elements per workgroup; the histogram covers
        // kNumBins cells per block, and its flat exclusive scan is the whole
        // prefix-sum step.
        const uint32_t num_blocks = (elem_capacity + kBlockSize - 1u) / kBlockSize;
        const uint32_t scan_elem_count = kNumBins * num_blocks;
        m_impl->EnsureWorkingStorage(key_bytes, static_cast<size_t>(scan_elem_count) * sizeof(uint32_t), has_payload);

        // Pass `p` reads the array the previous pass wrote and writes the other
        // one, so the ping-pong parity decides which array holds the result.
        Rhi::ComputeBuffer *keys_in = buffers.keys;
        Rhi::ComputeBuffer *keys_out = m_impl->keys_tmp.get();
        // Without a payload the shader never dereferences these bindings, so the
        // key arrays serve as placeholders.
        Rhi::ComputeBuffer *payload_in = has_payload ? buffers.payload : buffers.keys;
        Rhi::ComputeBuffer *payload_out = has_payload ? m_impl->payload_tmp.get() : m_impl->keys_tmp.get();

        for (uint32_t pass = 0u; pass < num_passes; ++pass) {
            if (pass > 0u) {
                DispatchBarrier(cb);
            }

            const uint32_t byte_shift = pass * 8u;

            // 1. Per-block histogram, written transposed and in full: no clear
            //    pass precedes it, and none is needed.
            m_impl->histogram_kernel->Dispatch(
                cb,
                {{"KeysIn", *keys_in}, {"Histogram", *m_impl->histogram}, {"ElemCount", *buffers.count}},
                num_blocks,
                1,
                1,
                RadixHistogramPush{byte_shift, num_blocks}
            );
            DispatchBarrier(cb);

            // 2. One flat exclusive scan over the transposed histogram.
            m_impl->scan->Record(cb, *m_impl->histogram, *m_impl->histogram, scan_elem_count);
            DispatchBarrier(cb);

            // 3. Stable scatter into the other array.
            m_impl->scatter_kernel->Dispatch(
                cb,
                {{"KeysIn", *keys_in},
                 {"KeysOut", *keys_out},
                 {"PayloadIn", *payload_in},
                 {"PayloadOut", *payload_out},
                 {"Histogram", *m_impl->histogram},
                 {"ElemCount", *buffers.count}},
                num_blocks,
                1,
                1,
                RadixScatterPush{byte_shift, num_blocks, has_payload ? 1u : 0u}
            );

            std::swap(keys_in, keys_out);
            std::swap(payload_in, payload_out);
        }

        // Each pass writes into the array it did not read, so an even pass count
        // leaves the result in the caller's array and an odd one leaves it in the
        // instance's.  It is copied back because the instance's arrays are shared
        // by every call: a later call would otherwise overwrite the result this
        // one produced, and the caller would have to know which array to read.
        if (num_passes % 2u == 1u) {
            DispatchBarrier(cb);
            m_impl->copy_back_kernel->Dispatch(
                cb,
                {{"SrcKeys", *keys_in},
                 {"DstKeys", *buffers.keys},
                 {"SrcPayload", *payload_in},
                 {"DstPayload", has_payload ? *buffers.payload : *buffers.keys}},
                (elem_capacity + 63u) / 64u,
                1,
                1,
                RadixCopyBackPush{elem_capacity, has_payload ? 1u : 0u}
            );
        }
    }
} // namespace Engine
