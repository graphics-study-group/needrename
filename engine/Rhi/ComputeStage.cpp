#include "Rhi/ComputeStage.h"

#include "Rhi/ComputeResourceBinding.h"
#include "Rhi/DebugUtils.h"
#include "Rhi/DeviceContext.h"
#include "Rhi/DeviceInterface.h"
#include "Rhi/ShaderParameterLayout.h"
#include <bitset>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

namespace Engine::Rhi {

    struct ComputeStage::impl {

        static constexpr size_t MAX_COMPUTE_DESCRIPTORS_PER_POOL = 128;
        static constexpr std::array DEFAULT_COMPUTE_DESCRIPTOR_POOL_SIZE{
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
        vk::UniqueDescriptorPool desc_pool{};

        std::vector<std::unique_ptr<ComputeResourceBinding>> allocated_bindings;

        Rhi::SPLayout layout{};

        void CreatePipeline(
            const DeviceInterface &device_interface,
            const std::vector<uint32_t> &spirv_code,
            const std::string_view name = ""
        ) {
            // Create descriptor and pipeline layout
            layout = Rhi::SPLayout::Reflect(spirv_code, false);
            auto desc_bindings = layout.GenerateLayoutBindings(0, true, false);
            vk::DescriptorSetLayoutCreateInfo dslci{vk::DescriptorSetLayoutCreateFlags{}, desc_bindings};
            m_passInfo.desc_layout = device_interface.GetDevice().createDescriptorSetLayoutUnique(dslci);

            std::vector<vk::PushConstantRange> pc_ranges;
            if (layout.push_constant_size > 0) {
                pc_ranges.emplace_back(vk::ShaderStageFlagBits::eCompute, 0, layout.push_constant_size);
            }
            vk::PipelineLayoutCreateInfo plci{
                vk::PipelineLayoutCreateFlags{}, {m_passInfo.desc_layout.get()}, pc_ranges
            };
            m_passInfo.pipeline_layout = device_interface.GetDevice().createPipelineLayoutUnique(plci);
            DEBUG_SET_NAME_TEMPLATE(
                device_interface.GetDevice(),
                m_passInfo.pipeline_layout.get(),
                std::format("Pipeline Layout for Compute {}", name)
            );

            // Create descriptor pool
            vk::DescriptorPoolCreateInfo dpci{
                vk::DescriptorPoolCreateFlags{}, MAX_COMPUTE_DESCRIPTORS_PER_POOL, DEFAULT_COMPUTE_DESCRIPTOR_POOL_SIZE
            };
            desc_pool = device_interface.GetDevice().createDescriptorPoolUnique(dpci);
            DEBUG_SET_NAME_TEMPLATE(
                device_interface.GetDevice(),
                desc_pool.get(),
                std::format("Descriptor Pool for Compute Pipeline {}", name)
            );

            // Create shader module
            vk::ShaderModuleCreateInfo smci{
                vk::ShaderModuleCreateFlags{},
                spirv_code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *>(spirv_code.data())
            };
            m_passInfo.shader = device_interface.GetDevice().createShaderModuleUnique(smci);
            DEBUG_SET_NAME_TEMPLATE(
                device_interface.GetDevice(),
                m_passInfo.shader.get(),
                std::format("Shader Module for Compute Pipeline {}", name)
            );

            vk::PipelineShaderStageCreateInfo pssci{
                vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eCompute, m_passInfo.shader.get(), "main"
            };
            vk::ComputePipelineCreateInfo cpci{vk::PipelineCreateFlags{}, pssci, m_passInfo.pipeline_layout.get()};
            auto ret = device_interface.GetDevice().createComputePipelineUnique(nullptr, cpci);
            m_passInfo.pipeline = std::move(ret.value);
            DEBUG_SET_NAME_TEMPLATE(
                device_interface.GetDevice(), m_passInfo.pipeline.get(), std::format("Compute Pipeline {}", name)
            );
        }
    };

    ComputeStage::ComputeStage(DeviceContext &device_context) :
        m_device_context(device_context), pimpl(std::make_unique<ComputeStage::impl>()) {
    }

    void ComputeStage::Instantiate(const std::vector<uint32_t> &code, const std::string_view name) {
        pimpl->CreatePipeline(m_device_context.GetDeviceInterface(), code, name);
    }

    ComputeStage::~ComputeStage() = default;

    ComputeResourceBinding &ComputeStage::AllocateResourceBinding(uint32_t slot_count) noexcept {
        pimpl->allocated_bindings.push_back(
            std::make_unique<ComputeResourceBinding>(m_device_context, *this, slot_count)
        );
        return *pimpl->allocated_bindings.back();
    }

    const Rhi::SPLayout &ComputeStage::GetReflectedShaderInfo() const noexcept {
        return pimpl->layout;
    }

    uint32_t ComputeStage::GetPushConstantSize() const noexcept {
        return pimpl->layout.push_constant_size;
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

} // namespace Engine::Rhi
