#include "CompactUnique.h"
#include "ParallelScan.h"

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

    struct CompactUnique::Impl {
        Rhi::DeviceContext &device_context;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        std::unique_ptr<Rhi::ComputeStage> flag_stage{};
        std::vector<uint32_t> flag_spirv{};

        std::unique_ptr<Rhi::ComputeStage> scatter_stage{};
        std::vector<uint32_t> scatter_spirv{};

        std::unique_ptr<Rhi::ComputeStage> copy_stage{};
        std::vector<uint32_t> copy_spirv{};

        std::unique_ptr<Rhi::ComputeStage> memset_stage{};
        std::vector<uint32_t> memset_spirv{};

        Rhi::ComputeResourceBinding *flag_binding = nullptr;
        Rhi::ComputeResourceBinding *scatter_binding = nullptr;
        Rhi::ComputeResourceBinding *copy_binding = nullptr;
        Rhi::ComputeResourceBinding *memset_binding = nullptr;

        explicit Impl(Rhi::DeviceContext &ctx, uint32_t mec) : device_context(ctx), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("CompactUnique: max_elem_count must be > 0");
            }
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *flag_path = "algorithm/flag_unique.comp.spv";
            flag_spirv = LoadPhysicsSpirvBytes(flag_path);
            flag_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            flag_stage->Instantiate(flag_spirv, "FlagUnique");
            flag_binding = &flag_stage->AllocateResourceBinding();

            const char *scatter_path = "algorithm/compact_scatter.comp.spv";
            scatter_spirv = LoadPhysicsSpirvBytes(scatter_path);
            scatter_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            scatter_stage->Instantiate(scatter_spirv, "CompactScatter");
            scatter_binding = &scatter_stage->AllocateResourceBinding();

            const char *copy_path = "collision/SpatialHashBroadDetector/copy_uint.comp.spv";
            copy_spirv = LoadPhysicsSpirvBytes(copy_path);
            copy_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            copy_stage->Instantiate(copy_spirv, "CompactUnique Copy");
            copy_binding = &copy_stage->AllocateResourceBinding();

            const char *memset_path = "collision/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            memset_stage->Instantiate(memset_spirv, "CompactUnique Memset");
            memset_binding = &memset_stage->AllocateResourceBinding();
        }

        void RecordFlagPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_buf,
            Rhi::ComputeBuffer &flags_buf,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            auto &srb = flag_binding->GetShaderResourceBinding();
            srb.BindBuffer("SortedPairs", pairs_buf);
            srb.BindBuffer("UniqueFlags", flags_buf);
            srb.BindBuffer("ElemCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            Rhi::BindComputeStage(cb, *flag_stage);
            Rhi::BindComputeResource(cb, *flag_stage, *flag_binding);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void RecordCopyPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &src_buf,
            Rhi::ComputeBuffer &dst_buf,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            auto &srb = copy_binding->GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", src_buf);
            srb.BindBuffer("DstBuffer", dst_buf);
            srb.BindBuffer("ElemCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            Rhi::BindComputeStage(cb, *copy_stage);
            Rhi::BindComputeResource(cb, *copy_stage, *copy_binding);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void RecordClearCountPass(vk::CommandBuffer cb, Rhi::ComputeBuffer &count_buf) {
            auto &srb = memset_binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", count_buf);

            Rhi::PushConstants(cb, *memset_stage, 1u);
            Rhi::BindComputeStage(cb, *memset_stage);
            Rhi::BindComputeResource(cb, *memset_stage, *memset_binding);
            Rhi::DispatchCompute(cb, 1, 1, 1);
        }

        void RecordScatterPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_buf,
            Rhi::ComputeBuffer &flags_buf,
            Rhi::ComputeBuffer &offsets_buf,
            Rhi::ComputeBuffer &count_buf,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            auto &srb = scatter_binding->GetShaderResourceBinding();
            srb.BindBuffer("SortedPairs", pairs_buf);
            srb.BindBuffer("CompactPairs", pairs_buf);
            srb.BindBuffer("OriginalFlags", flags_buf);
            srb.BindBuffer("FlagOffsets", offsets_buf);
            srb.BindBuffer("UniqueCount", count_buf);
            srb.BindBuffer("ElemCount", pair_count_buf);

            uint32_t wg = (elem_capacity + 63u) / 64u;

            Rhi::BindComputeStage(cb, *scatter_stage);
            Rhi::BindComputeResource(cb, *scatter_stage, *scatter_binding);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }
    };

    CompactUnique::CompactUnique(Rhi::DeviceContext &device_context, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(device_context, max_elem_count)) {
    }

    CompactUnique::~CompactUnique() = default;

    bool CompactUnique::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t CompactUnique::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void CompactUnique::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &pairs_buf,
        Rhi::ComputeBuffer &flags_buf,
        Rhi::ComputeBuffer &offsets_buf,
        Rhi::ComputeBuffer &count_buf,
        Rhi::ComputeBuffer &scan_scratch_buf,
        ParallelScan &scan,
        Rhi::ComputeBuffer &pair_count_buf,
        uint32_t elem_capacity
    ) {
        if (elem_capacity == 0u) {
            return;
        }
        if (elem_capacity > m_impl->max_elem_count) {
            throw std::runtime_error(
                "CompactUnique::Record: elem_capacity " + std::to_string(elem_capacity) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }

        m_impl->EnsureInitialized();

        m_impl->RecordFlagPass(cb, pairs_buf, flags_buf, pair_count_buf, elem_capacity);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->RecordCopyPass(cb, flags_buf, offsets_buf, pair_count_buf, elem_capacity);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        scan.Record(cb, offsets_buf, offsets_buf, scan_scratch_buf, elem_capacity);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->RecordClearCountPass(cb, count_buf);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->RecordScatterPass(cb, pairs_buf, flags_buf, offsets_buf, count_buf, pair_count_buf, elem_capacity);
    }
} // namespace Engine
