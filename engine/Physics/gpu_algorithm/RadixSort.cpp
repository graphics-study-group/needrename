#include "RadixSort.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Rhi/ComputeHelpers.h>
#include <Rhi/DeviceContext.h>

#include <Rhi/ComputeBuffer.h>
#include <Rhi/ComputeResourceBinding.h>
#include <Rhi/ComputeStage.h>
#include <Rhi/ShaderResourceBinding.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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
} // namespace

namespace Engine {

    // Push-constant layout, matching the RadixParamsPush block in
    // engine/Physics/shader/algorithm/ (std430).
    struct RadixParamsPush {
        uint32_t byte_shift;  // 0, 8, 16, or 24
        uint32_t word_select; // 0 = pair.y (secondary), 1 = pair.x (primary)
    };
    static_assert(sizeof(RadixParamsPush) == 8, "RadixParamsPush must be 8 bytes");

    struct RadixSort::Impl {
        Rhi::DeviceContext &device_context;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        std::unique_ptr<Rhi::ComputeStage> histogram_stage{};
        std::vector<uint32_t> histogram_spirv{};

        std::unique_ptr<Rhi::ComputeStage> prefix_sum_stage{};
        std::vector<uint32_t> prefix_sum_spirv{};

        std::unique_ptr<Rhi::ComputeStage> scatter_stage{};
        std::vector<uint32_t> scatter_spirv{};

        std::unique_ptr<Rhi::ComputeStage> memset_stage{};
        std::vector<uint32_t> memset_spirv{};

        Rhi::ComputeResourceBinding *histogram_binding = nullptr;
        Rhi::ComputeResourceBinding *prefix_sum_binding = nullptr;
        Rhi::ComputeResourceBinding *scatter_binding = nullptr;
        Rhi::ComputeResourceBinding *memset_binding = nullptr;

        explicit Impl(Rhi::DeviceContext &ctx, uint32_t mec) : device_context(ctx), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("RadixSort: max_elem_count must be > 0");
            }
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *histogram_path = "algorithm/radix_histogram.comp.spv";
            histogram_spirv = LoadPhysicsSpirvBytes(histogram_path);
            histogram_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            histogram_stage->Instantiate(histogram_spirv, "RadixHistogram");
            histogram_binding = &histogram_stage->AllocateResourceBinding();

            const char *prefix_sum_path = "algorithm/radix_prefix_sum_256.comp.spv";
            prefix_sum_spirv = LoadPhysicsSpirvBytes(prefix_sum_path);
            prefix_sum_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            prefix_sum_stage->Instantiate(prefix_sum_spirv, "RadixPrefixSum256");
            prefix_sum_binding = &prefix_sum_stage->AllocateResourceBinding();

            const char *scatter_path = "algorithm/radix_scatter.comp.spv";
            scatter_spirv = LoadPhysicsSpirvBytes(scatter_path);
            scatter_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            scatter_stage->Instantiate(scatter_spirv, "RadixScatter");
            scatter_binding = &scatter_stage->AllocateResourceBinding();

            const char *memset_path = "collision/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            memset_stage->Instantiate(memset_spirv, "RadixMemset");
            memset_binding = &memset_stage->AllocateResourceBinding();
        }

        void RecordClearPass(vk::CommandBuffer cb, Rhi::ComputeBuffer &scratch_buf) {
            auto &srb = memset_binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", scratch_buf);

            Rhi::PushConstants(cb, *memset_stage, RadixSort::kNumBins);
            Rhi::BindComputeStage(cb, *memset_stage);
            Rhi::BindComputeResource(cb, *memset_stage, *memset_binding);
            Rhi::DispatchCompute(cb, 4, 1, 1); // 4 WGs for 256 elements
        }

        void RecordHistogramPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_buf,
            Rhi::ComputeBuffer &scratch_buf,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t byte_shift,
            uint32_t word_select,
            uint32_t elem_capacity
        ) {
            auto &srb = histogram_binding->GetShaderResourceBinding();
            srb.BindBuffer("PairsIn", pairs_buf);
            srb.BindBuffer("Histogram", scratch_buf);
            srb.BindBuffer("PairCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            const RadixParamsPush params{byte_shift, word_select};
            Rhi::PushConstants(cb, *histogram_stage, params);
            Rhi::BindComputeStage(cb, *histogram_stage);
            Rhi::BindComputeResource(cb, *histogram_stage, *histogram_binding);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void RecordPrefixSumPass(vk::CommandBuffer cb, Rhi::ComputeBuffer &scratch_buf) {
            auto &srb = prefix_sum_binding->GetShaderResourceBinding();
            srb.BindBuffer("Histogram", scratch_buf);

            Rhi::BindComputeStage(cb, *prefix_sum_stage);
            Rhi::BindComputeResource(cb, *prefix_sum_stage, *prefix_sum_binding);
            Rhi::DispatchCompute(cb, 1, 1, 1);
        }

        void RecordScatterPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_in_buf,
            Rhi::ComputeBuffer &pairs_out_buf,
            Rhi::ComputeBuffer &scratch_buf,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t byte_shift,
            uint32_t word_select,
            uint32_t elem_capacity
        ) {
            auto &srb = scatter_binding->GetShaderResourceBinding();
            srb.BindBuffer("PairsIn", pairs_in_buf);
            srb.BindBuffer("PairsOut", pairs_out_buf);
            srb.BindBuffer("Histogram", scratch_buf);
            srb.BindBuffer("PairCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            const RadixParamsPush params{byte_shift, word_select};
            Rhi::PushConstants(cb, *scatter_stage, params);
            Rhi::BindComputeStage(cb, *scatter_stage);
            Rhi::BindComputeResource(cb, *scatter_stage, *scatter_binding);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void RecordRadixPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_in_buf,
            Rhi::ComputeBuffer &pairs_out_buf,
            Rhi::ComputeBuffer &scratch_buf,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t byte_shift,
            uint32_t word_select,
            uint32_t elem_capacity
        ) {
            RecordClearPass(cb, scratch_buf);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            RecordHistogramPass(cb, pairs_in_buf, scratch_buf, pair_count_buf, byte_shift, word_select, elem_capacity);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            RecordPrefixSumPass(cb, scratch_buf);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            RecordScatterPass(
                cb, pairs_in_buf, pairs_out_buf, scratch_buf, pair_count_buf, byte_shift, word_select, elem_capacity
            );
        }
    };

    RadixSort::RadixSort(Rhi::DeviceContext &device_context, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(device_context, max_elem_count)) {
    }

    RadixSort::~RadixSort() = default;

    bool RadixSort::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t RadixSort::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void RadixSort::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &pairs_buf_a,
        Rhi::ComputeBuffer &pairs_buf_b,
        Rhi::ComputeBuffer &scratch_buf,
        uint32_t elem_capacity,
        Rhi::ComputeBuffer &pair_count_buf,
        uint32_t max_shape_count
    ) {
        if (elem_capacity == 0u) {
            return;
        }
        if (elem_capacity > m_impl->max_elem_count) {
            throw std::runtime_error(
                "RadixSort::Record: elem_capacity " + std::to_string(elem_capacity) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }
        if (max_shape_count > kMaxShapeCount) {
            throw std::runtime_error(
                "RadixSort::Record: max_shape_count " + std::to_string(max_shape_count) + " exceeds kMaxShapeCount "
                + std::to_string(kMaxShapeCount)
            );
        }

        m_impl->EnsureInitialized();

        for (uint32_t pass = 0; pass < kNumPasses; ++pass) {
            uint32_t byte_shift = (pass % 4u) * 8u;
            uint32_t word_select = pass / 4u;
            bool to_b = (pass % 2u) == 0u;

            if (to_b) {
                m_impl->RecordRadixPass(
                    cb, pairs_buf_a, pairs_buf_b, scratch_buf, pair_count_buf, byte_shift, word_select, elem_capacity
                );
            } else {
                m_impl->RecordRadixPass(
                    cb, pairs_buf_b, pairs_buf_a, scratch_buf, pair_count_buf, byte_shift, word_select, elem_capacity
                );
            }

            if (pass + 1 < kNumPasses) {
                cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});
            }
        }
    }
} // namespace Engine
