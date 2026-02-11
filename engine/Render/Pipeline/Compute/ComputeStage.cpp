#include "ComputeStage.h"

#include "Asset/AssetRef.h"
#include "Asset/Shader/ShaderAsset.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/DebugUtils.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Pipeline/Compute/ComputeResourceBinding.h"
#include <string>
#include <bitset>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL.h>

namespace Engine {

    struct ComputeStage::impl {
        
        static constexpr size_t MAX_COMPUTE_DESCRIPTORS_PER_POOL = 128;
        static constexpr std::array DEFAULT_COMPUTE_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 128}
        };

        PassInfo m_passInfo{};
        // This will create a lot of allocations of descriptor pool.
        // We might need to optimize it a little.
        vk::UniqueDescriptorPool desc_pool {};

        std::vector <std::unique_ptr <ComputeResourceBinding>> allocated_bindings;

        ShdrRfl::SPLayout layout{};

        void CreatePipeline(RenderSystem &system, const std::vector <uint32_t> & spirv_code, const std::string_view name = "") {
            // Create descriptor and pipeline layout
            layout = ShdrRfl::SPLayout::Reflect(spirv_code, false);
            auto desc_bindings = layout.GenerateAllLayoutBindings();
            if (desc_bindings.size() > 1) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Found multiple descriptor sets. Only zeroth set is currently utilized."
                );
            }
            assert(desc_bindings.contains(0) && "Descriptor set zero is empty.");
            vk::DescriptorSetLayoutCreateInfo dslci{
                vk::DescriptorSetLayoutCreateFlags{},
                desc_bindings[0]
            };
            m_passInfo.desc_layout = system.GetDevice().createDescriptorSetLayoutUnique(dslci);

            vk::PipelineLayoutCreateInfo plci{vk::PipelineLayoutCreateFlags{}, {m_passInfo.desc_layout.get()}, {}};
            m_passInfo.pipeline_layout = system.GetDevice().createPipelineLayoutUnique(plci);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                m_passInfo.pipeline_layout.get(),
                std::format("Pipeline Layout for Compute {}", name)
            );

            // Create descriptor pool
            vk::DescriptorPoolCreateInfo dpci {
                vk::DescriptorPoolCreateFlags{},
                MAX_COMPUTE_DESCRIPTORS_PER_POOL,
                DEFAULT_COMPUTE_DESCRIPTOR_POOL_SIZE
            };
            desc_pool = system.GetDevice().createDescriptorPoolUnique(dpci);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                desc_pool.get(),
                std::format("Descriptor Pool for Compute Pipeline {}", name)
            );

            // Create shader module
            vk::ShaderModuleCreateInfo smci{
                vk::ShaderModuleCreateFlags{},
                spirv_code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *>(spirv_code.data())
            };
            m_passInfo.shader = system.GetDevice().createShaderModuleUnique(smci);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                m_passInfo.shader.get(),
                std::format("Shader Module for Compute Pipeline {}", name)
            );

            vk::PipelineShaderStageCreateInfo pssci{
                vk::PipelineShaderStageCreateFlags{},
                vk::ShaderStageFlagBits::eCompute,
                m_passInfo.shader.get(),
                "main"
            };
            vk::ComputePipelineCreateInfo cpci{vk::PipelineCreateFlags{}, pssci, m_passInfo.pipeline_layout.get()};
            auto ret = system.GetDevice().createComputePipelineUnique(nullptr, cpci);
            m_passInfo.pipeline = std::move(ret.value);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                m_passInfo.pipeline.get(),
                std::format("Compute Pipeline {}", name)
            );
        }
    };

    ComputeStage::ComputeStage(RenderSystem &system) : m_system(system), pimpl(std::make_unique<ComputeStage::impl>()) {
    }

    void ComputeStage::Instantiate(const ShaderAsset &asset) {
        assert(asset.shaderType == ShaderAsset::ShaderType::Compute);
        Instantiate(asset.binary, asset.m_name);
    }

    void ComputeStage::Instantiate(const std::vector<uint32_t> &code, const std::string_view name) {
        pimpl->CreatePipeline(m_system, code, name);
    }

    ComputeStage::~ComputeStage() = default;


    ComputeResourceBinding &ComputeStage::AllocateResourceBinding() noexcept {
        pimpl->allocated_bindings.push_back(
            std::make_unique<ComputeResourceBinding>(
                m_system, *this
            )
        );
        return *pimpl->allocated_bindings.back();
    }

    const ShdrRfl::SPLayout &ComputeStage::GetReflectedShaderInfo() const noexcept {
        return pimpl->layout;
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
    vk::DescriptorPool ComputeStage::GetDescriptorPool() const noexcept {
        return pimpl->desc_pool.get();
    }

} // namespace Engine
