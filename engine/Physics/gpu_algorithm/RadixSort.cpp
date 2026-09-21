#include "RadixSort.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeHelpers.h>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Pipeline/ComputeResourceBinding.h>
#include <Rhi/Pipeline/ComputeStage.h>
#include <Rhi/Pipeline/ShaderResourceBinding.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
    std::vector<uint32_t> LoadPhysicsSpirvBytes(const char *relative_path) {
        std::filesystem::path full = std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / relative_path;
        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        }
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u) {
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        }
        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }

    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };

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

    struct RadixSort::Impl {
        Rhi::DeviceContext &device_context;
        bool initialized = false;

        std::unique_ptr<Rhi::ComputeStage> histogram_stage{};
        std::vector<uint32_t> histogram_spirv{};

        std::unique_ptr<Rhi::ComputeStage> scatter_stage{};
        std::vector<uint32_t> scatter_spirv{};

        Rhi::ComputeResourceBinding *histogram_binding = nullptr;
        Rhi::ComputeResourceBinding *scatter_binding = nullptr;

        // The prefix-sum step is an implementation detail: the class owns the
        // instance and rebuilds it when a call's element capacity outgrows it.
        std::unique_ptr<ParallelScan> scan{};
        uint32_t scan_max_elem_count = 0u;

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *histogram_path = "algorithm/radix_block_histogram.comp.spv";
            histogram_spirv = LoadPhysicsSpirvBytes(histogram_path);
            histogram_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            histogram_stage->Instantiate(histogram_spirv, "RadixBlockHistogram");
            histogram_binding = &histogram_stage->AllocateResourceBinding();

            const char *scatter_path = "algorithm/radix_scatter.comp.spv";
            scatter_spirv = LoadPhysicsSpirvBytes(scatter_path);
            scatter_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            scatter_stage->Instantiate(scatter_spirv, "RadixScatter");
            scatter_binding = &scatter_stage->AllocateResourceBinding();
        }

        /// @brief Make sure the internal scan can cover @p elem_count elements.
        void EnsureScan(uint32_t elem_count) {
            if (scan != nullptr && scan_max_elem_count >= elem_count) {
                return;
            }
            scan = std::make_unique<ParallelScan>(device_context, elem_count);
            scan_max_elem_count = elem_count;
        }

        void RecordHistogramPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_in_buf,
            Rhi::ComputeBuffer &scratch_buf,
            Rhi::ComputeBuffer &elem_count_buf,
            size_t histogram_bytes,
            const RadixHistogramPush &params,
            uint32_t num_workgroups
        ) {
            auto &srb = histogram_binding->GetShaderResourceBinding();
            srb.BindBuffer("KeysIn", keys_in_buf);
            srb.BindBuffer("Histogram", scratch_buf, 0u, histogram_bytes);
            srb.BindBuffer("ElemCount", elem_count_buf, 0u, sizeof(uint32_t));

            Rhi::PushConstants(cb, *histogram_stage, params);
            Rhi::BindComputeStage(cb, *histogram_stage);
            Rhi::BindComputeResource(cb, *histogram_stage, *histogram_binding);
            Rhi::DispatchCompute(cb, num_workgroups, 1, 1);
        }

        void RecordScatterPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_in_buf,
            Rhi::ComputeBuffer &keys_out_buf,
            Rhi::ComputeBuffer &payload_in_buf,
            Rhi::ComputeBuffer &payload_out_buf,
            Rhi::ComputeBuffer &scratch_buf,
            Rhi::ComputeBuffer &elem_count_buf,
            size_t histogram_bytes,
            const RadixScatterPush &params,
            uint32_t num_workgroups
        ) {
            auto &srb = scatter_binding->GetShaderResourceBinding();
            srb.BindBuffer("KeysIn", keys_in_buf);
            srb.BindBuffer("KeysOut", keys_out_buf);
            srb.BindBuffer("PayloadIn", payload_in_buf);
            srb.BindBuffer("PayloadOut", payload_out_buf);
            srb.BindBuffer("Histogram", scratch_buf, 0u, histogram_bytes);
            srb.BindBuffer("ElemCount", elem_count_buf, 0u, sizeof(uint32_t));

            Rhi::PushConstants(cb, *scatter_stage, params);
            Rhi::BindComputeStage(cb, *scatter_stage);
            Rhi::BindComputeResource(cb, *scatter_stage, *scatter_binding);
            Rhi::DispatchCompute(cb, num_workgroups, 1, 1);
        }
    };

    RadixSort::RadixSort(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    RadixSort::~RadixSort() = default;

    bool RadixSort::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    size_t RadixSort::GetRequiredScratchBytes(uint32_t max_elem_capacity) noexcept {
        const uint32_t num_blocks = (max_elem_capacity + kBlockSize - 1u) / kBlockSize;
        const uint32_t num_elements = kNumBins * num_blocks;
        return static_cast<size_t>(num_elements) * sizeof(uint32_t)
               + ParallelScan::GetRequiredBlockSumsBytes(num_elements);
    }

    size_t RadixSort::GetRequiredTempBytes(uint32_t max_elem_capacity) noexcept {
        return static_cast<size_t>(max_elem_capacity) * sizeof(uint32_t);
    }

    RadixSortOutput RadixSort::Record(
        vk::CommandBuffer cb,
        const RadixSortBuffers &buffers,
        uint32_t elem_capacity,
        uint32_t max_key_value
    ) {
        // The record is a key array plus an *optional* payload array, so supplying
        // exactly one of the two payload arrays is a caller error, not a mode.
        const bool has_payload = (buffers.payload_a != nullptr) || (buffers.payload_b != nullptr);
        if ((buffers.payload_a != nullptr) != (buffers.payload_b != nullptr)) {
            throw std::invalid_argument(
                "RadixSort::Record: a payload array must be supplied for both ping-pong buffers or for neither"
            );
        }
        if (max_key_value == 0u) {
            throw std::invalid_argument("RadixSort::Record: max_key_value must be > 0");
        }

        // A zero capacity is a no-op that leaves the caller's input in place, and
        // so is a key bound whose domain holds a single value.
        RadixSortOutput result{buffers.keys_a, has_payload ? buffers.payload_a : nullptr};
        if (elem_capacity == 0u) {
            return result;
        }
        const uint32_t num_passes = NumRadixPasses(max_key_value);
        if (num_passes == 0u) {
            return result;
        }

        if (buffers.keys_a == nullptr || buffers.keys_b == nullptr || buffers.scratch == nullptr
            || buffers.count == nullptr) {
            throw std::runtime_error(
                "RadixSort::Record: the key arrays, the scratch buffer and the count buffer are all required"
            );
        }

        // Geometry is per call, so the out-of-range guard is a per-call check
        // against the buffers actually bound for this call.
        const size_t key_bytes = static_cast<size_t>(elem_capacity) * sizeof(uint32_t);
        if (buffers.keys_a->GetSize() < key_bytes || buffers.keys_b->GetSize() < key_bytes) {
            throw std::runtime_error(
                "RadixSort::Record: elem_capacity " + std::to_string(elem_capacity)
                + " needs " + std::to_string(key_bytes)
                + " bytes per key array, which exceeds the buffer bound for this call"
            );
        }
        if (has_payload
            && (buffers.payload_a->GetSize() < key_bytes || buffers.payload_b->GetSize() < key_bytes)) {
            throw std::runtime_error(
                "RadixSort::Record: elem_capacity " + std::to_string(elem_capacity)
                + " needs " + std::to_string(key_bytes)
                + " bytes per payload array, which exceeds the buffer bound for this call"
            );
        }
        if (buffers.count->GetSize() < sizeof(uint32_t)) {
            throw std::runtime_error("RadixSort::Record: the count buffer must hold at least one uint");
        }
        const size_t scratch_required = GetRequiredScratchBytes(elem_capacity);
        if (buffers.scratch->GetSize() < scratch_required) {
            throw std::runtime_error(
                "RadixSort::Record: elem_capacity " + std::to_string(elem_capacity)
                + " needs a scratch buffer of " + std::to_string(scratch_required)
                + " bytes, which exceeds the buffer bound for this call"
            );
        }

        m_impl->EnsureInitialized();

        // One block of 256 elements per workgroup; the histogram covers
        // kNumBins cells per block, and its flat exclusive scan is the whole
        // prefix-sum step.
        const uint32_t num_blocks = (elem_capacity + kBlockSize - 1u) / kBlockSize;
        const uint32_t scan_elem_count = kNumBins * num_blocks;
        const size_t histogram_bytes = static_cast<size_t>(scan_elem_count) * sizeof(uint32_t);
        m_impl->EnsureScan(scan_elem_count);

        // Pass `p` reads the array the previous pass wrote and writes the other
        // one, so the ping-pong parity decides which array holds the result; the
        // loop's final swap makes `keys_in` name it.
        Rhi::ComputeBuffer *keys_in = buffers.keys_a;
        Rhi::ComputeBuffer *keys_out = buffers.keys_b;
        // Without a payload the shader never dereferences these bindings, so the
        // key arrays serve as placeholders.
        Rhi::ComputeBuffer *payload_in = has_payload ? buffers.payload_a : buffers.keys_a;
        Rhi::ComputeBuffer *payload_out = has_payload ? buffers.payload_b : buffers.keys_b;

        for (uint32_t pass = 0u; pass < num_passes; ++pass) {
            if (pass > 0u) {
                cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});
            }

            const uint32_t byte_shift = pass * 8u;

            // 1. Per-block histogram, written transposed and in full: no clear
            //    pass precedes it, and none is needed.
            m_impl->RecordHistogramPass(
                cb,
                *keys_in,
                *buffers.scratch,
                *buffers.count,
                histogram_bytes,
                RadixHistogramPush{byte_shift, num_blocks},
                num_blocks
            );
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // 2. One flat exclusive scan over the transposed histogram.  The
            //    scratch's first `histogram_bytes` are the data; the scan's own
            //    block sums live after them, inside the same buffer.
            assert(m_impl->scan != nullptr);
            m_impl->scan->Record(
                cb, *buffers.scratch, *buffers.scratch, *buffers.scratch, scan_elem_count, histogram_bytes
            );
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // 3. Stable scatter into the other array.
            m_impl->RecordScatterPass(
                cb,
                *keys_in,
                *keys_out,
                *payload_in,
                *payload_out,
                *buffers.scratch,
                *buffers.count,
                histogram_bytes,
                RadixScatterPush{byte_shift, num_blocks, has_payload ? 1u : 0u},
                num_blocks
            );

            std::swap(keys_in, keys_out);
            std::swap(payload_in, payload_out);
        }

        return RadixSortOutput{keys_in, has_payload ? payload_in : nullptr};
    }
} // namespace Engine
