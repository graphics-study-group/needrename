#include "RadixSort.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Rhi/ComputeBuffer.h>
#include <Rhi/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Rhi/ComputeResourceBinding.h>
#include <Rhi/ComputeStage.h>
#include <Render/RenderSystem.h>

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

    struct alignas(16) RadixSortParamsGpu {
        uint32_t byte_shift;
        uint32_t word_select;
        uint32_t elem_capacity;
        uint32_t _pad;
    };
    static_assert(sizeof(RadixSortParamsGpu) == 16, "RadixSortParamsGpu must be 16 bytes");

    struct RadixSort::Impl {
        RenderSystem &render_system;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        std::unique_ptr<ComputeStage> histogram_stage{};
        std::vector<uint32_t> histogram_spirv{};

        std::unique_ptr<ComputeStage> prefix_sum_stage{};
        std::vector<uint32_t> prefix_sum_spirv{};

        std::unique_ptr<ComputeStage> scatter_stage{};
        std::vector<uint32_t> scatter_spirv{};

        std::unique_ptr<ComputeStage> memset_stage{};
        std::vector<uint32_t> memset_spirv{};

        std::unique_ptr<ComputeBuffer> gpu_const_256{};

        std::vector<std::unique_ptr<ComputeBuffer>> histogram_param_pool{};
        size_t histogram_param_index = 0;

        std::vector<std::unique_ptr<ComputeBuffer>> scatter_param_pool{};
        size_t scatter_param_index = 0;

        ComputeResourceBinding *histogram_binding = nullptr;
        ComputeResourceBinding *prefix_sum_binding = nullptr;
        ComputeResourceBinding *scatter_binding = nullptr;
        ComputeResourceBinding *memset_binding = nullptr;

        explicit Impl(RenderSystem &rs, uint32_t mec) : render_system(rs), max_elem_count(mec) {
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
            histogram_stage = std::make_unique<ComputeStage>(render_system);
            histogram_stage->Instantiate(histogram_spirv, "RadixHistogram");
            histogram_binding = &histogram_stage->AllocateResourceBinding();

            const char *prefix_sum_path = "algorithm/radix_prefix_sum_256.comp.spv";
            prefix_sum_spirv = LoadPhysicsSpirvBytes(prefix_sum_path);
            prefix_sum_stage = std::make_unique<ComputeStage>(render_system);
            prefix_sum_stage->Instantiate(prefix_sum_spirv, "RadixPrefixSum256");
            prefix_sum_binding = &prefix_sum_stage->AllocateResourceBinding();

            const char *scatter_path = "algorithm/radix_scatter.comp.spv";
            scatter_spirv = LoadPhysicsSpirvBytes(scatter_path);
            scatter_stage = std::make_unique<ComputeStage>(render_system);
            scatter_stage->Instantiate(scatter_spirv, "RadixScatter");
            scatter_binding = &scatter_stage->AllocateResourceBinding();

            const char *memset_path = "collision/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<ComputeStage>(render_system);
            memset_stage->Instantiate(memset_spirv, "RadixMemset");
            memset_binding = &memset_stage->AllocateResourceBinding();

            {
                const auto &alloc = render_system.GetAllocatorState();
                gpu_const_256 = ComputeBuffer::CreateUnique(
                    alloc, sizeof(uint32_t), true, false, false, false, "RadixSort Const256"
                );
                auto *addr = reinterpret_cast<uint32_t *>(gpu_const_256->GetVMAddress());
                *addr = RadixSort::kNumBins;
            }
        }

        ComputeBuffer &AcquireHistogramParam(uint32_t byte_shift, uint32_t word_select, uint32_t elem_capacity) {
            if (histogram_param_index >= histogram_param_pool.size()) {
                const auto &alloc = render_system.GetAllocatorState();
                auto buf = ComputeBuffer::CreateUnique(
                    alloc, sizeof(RadixSortParamsGpu), true, false, false, false, "RadixSort HistogramParams"
                );
                histogram_param_pool.push_back(std::move(buf));
            }
            auto &buf = *histogram_param_pool[histogram_param_index++];
            auto *addr = reinterpret_cast<RadixSortParamsGpu *>(buf.GetVMAddress());
            addr->byte_shift = byte_shift;
            addr->word_select = word_select;
            addr->elem_capacity = elem_capacity;
            addr->_pad = 0u;
            return buf;
        }

        ComputeBuffer &AcquireScatterParam(uint32_t byte_shift, uint32_t word_select, uint32_t elem_capacity) {
            if (scatter_param_index >= scatter_param_pool.size()) {
                const auto &alloc = render_system.GetAllocatorState();
                auto buf = ComputeBuffer::CreateUnique(
                    alloc, sizeof(RadixSortParamsGpu), true, false, false, false, "RadixSort ScatterParams"
                );
                scatter_param_pool.push_back(std::move(buf));
            }
            auto &buf = *scatter_param_pool[scatter_param_index++];
            auto *addr = reinterpret_cast<RadixSortParamsGpu *>(buf.GetVMAddress());
            addr->byte_shift = byte_shift;
            addr->word_select = word_select;
            addr->elem_capacity = elem_capacity;
            addr->_pad = 0u;
            return buf;
        }

        void RecordClearPass(CommandBuffer &cb, ComputeBuffer &scratch_buf) {
            auto &srb = memset_binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", scratch_buf);
            srb.BindBuffer("ElemCount", *gpu_const_256);

            cb.BindComputeStage(*memset_stage);
            cb.BindComputeResource(*memset_binding);
            cb.DispatchCompute(4, 1, 1); // 4 WGs for 256 elements
        }

        void RecordHistogramPass(
            CommandBuffer &cb,
            ComputeBuffer &pairs_buf,
            ComputeBuffer &scratch_buf,
            ComputeBuffer &param_buf,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            auto &srb = histogram_binding->GetShaderResourceBinding();
            srb.BindBuffer("PairsIn", pairs_buf);
            srb.BindBuffer("Histogram", scratch_buf);
            srb.BindBuffer("RadixParams", param_buf);
            srb.BindBuffer("PairCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            cb.BindComputeStage(*histogram_stage);
            cb.BindComputeResource(*histogram_binding);
            cb.DispatchCompute(wg, 1, 1);
        }

        void RecordPrefixSumPass(CommandBuffer &cb, ComputeBuffer &scratch_buf) {
            auto &srb = prefix_sum_binding->GetShaderResourceBinding();
            srb.BindBuffer("Histogram", scratch_buf);

            cb.BindComputeStage(*prefix_sum_stage);
            cb.BindComputeResource(*prefix_sum_binding);
            cb.DispatchCompute(1, 1, 1);
        }

        void RecordScatterPass(
            CommandBuffer &cb,
            ComputeBuffer &pairs_in_buf,
            ComputeBuffer &pairs_out_buf,
            ComputeBuffer &scratch_buf,
            ComputeBuffer &param_buf,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            auto &srb = scatter_binding->GetShaderResourceBinding();
            srb.BindBuffer("PairsIn", pairs_in_buf);
            srb.BindBuffer("PairsOut", pairs_out_buf);
            srb.BindBuffer("Histogram", scratch_buf);
            srb.BindBuffer("RadixParams", param_buf);
            srb.BindBuffer("PairCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            cb.BindComputeStage(*scatter_stage);
            cb.BindComputeResource(*scatter_binding);
            cb.DispatchCompute(wg, 1, 1);
        }

        void RecordRadixPass(
            CommandBuffer &cb,
            ComputeBuffer &pairs_in_buf,
            ComputeBuffer &pairs_out_buf,
            ComputeBuffer &scratch_buf,
            ComputeBuffer &pair_count_buf,
            uint32_t byte_shift,
            uint32_t word_select,
            uint32_t elem_capacity
        ) {
            RecordClearPass(cb, scratch_buf);
            cb.GetCommandBuffer().pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            auto &hist_param = AcquireHistogramParam(byte_shift, word_select, elem_capacity);
            RecordHistogramPass(cb, pairs_in_buf, scratch_buf, hist_param, pair_count_buf, elem_capacity);
            cb.GetCommandBuffer().pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            RecordPrefixSumPass(cb, scratch_buf);
            cb.GetCommandBuffer().pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            auto &scat_param = AcquireScatterParam(byte_shift, word_select, elem_capacity);
            RecordScatterPass(cb, pairs_in_buf, pairs_out_buf, scratch_buf, scat_param, pair_count_buf, elem_capacity);
        }
    };

    RadixSort::RadixSort(RenderSystem &render_system, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(render_system, max_elem_count)) {
    }

    RadixSort::~RadixSort() = default;

    bool RadixSort::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t RadixSort::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void RadixSort::Record(
        CommandBuffer &cb,
        ComputeBuffer &pairs_buf_a,
        ComputeBuffer &pairs_buf_b,
        ComputeBuffer &scratch_buf,
        uint32_t elem_capacity,
        ComputeBuffer &pair_count_buf,
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
                cb.GetCommandBuffer().pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});
            }
        }
    }

    void RadixSort::ResetParamPool() {
        m_impl->histogram_param_index = 0;
        m_impl->scatter_param_index = 0;
    }
} // namespace Engine
