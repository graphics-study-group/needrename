#include "ComputeStage.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/ShaderAsset.h"
#include "Render/Memory/Buffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/Memory/ShaderParameters/ShaderParameter.h"
#include <string>
#include <bitset>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL.h>

namespace Engine {

    struct ComputeStage::impl {
        struct InstancedPassInfo {
            static constexpr uint32_t BACK_BUFFERS = 3;

            std::unordered_map <std::string, std::unique_ptr<IndexedBuffer>> ubos{};

            std::bitset<8> _is_ubo_dirty{};
            std::bitset<8> _is_descriptor_dirty{};

            vk::DescriptorSet desc_set{};
            ShdrRfl::ShaderParameters parameters{};
        } m_ipi;
        
        std::vector <std::byte> m_ubo_staging_buffer{};

        PassInfo m_passInfo{};

        void CreatePipeline(RenderSystem &system, const ShaderAsset &asset) {
            assert(asset.shaderType == ShaderAsset::ShaderType::Compute);
            auto code = asset.binary;
        }
    };

    ComputeStage::ComputeStage(RenderSystem &system) : m_system(system), pimpl(std::make_unique<ComputeStage::impl>()) {
    }

    void ComputeStage::Instantiate(const ShaderAsset &asset) {
        
    }

    ComputeStage::~ComputeStage() = default;

    void ComputeStage::SetInBlockVariable(uint32_t index, std::any var) {
    }

    void ComputeStage::SetDescVariable(uint32_t index, std::any var) {
    }

    void ComputeStage::WriteDescriptorSet() {
    }

    void ComputeStage::WriteUBO() {
    }

    vk::Pipeline ComputeStage::GetPipeline() const noexcept {
        return pimpl->m_passInfo.pipeline.get();
    }
    vk::PipelineLayout ComputeStage::GetPipelineLayout() const noexcept {
        return pimpl->m_passInfo.pipeline_layout.get();
    }
    vk::DescriptorSetLayout ComputeStage::GetDescriptorSetLayout() const noexcept {
        return pimpl->m_passInfo.desc_layout.get();
    }
    vk::DescriptorSet ComputeStage::GetDescriptorSet() const noexcept {
        return pimpl->m_ipi.desc_set;
    }
} // namespace Engine
