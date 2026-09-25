#include "Rhi/Pipeline/ComputeStage.h"

#include "Rhi/Device/DebugUtils.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Pipeline/ComputeResourceBinding.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include <bitset>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

namespace Engine::Rhi {

    struct ComputeStage::impl {
        PassInfo m_passInfo{};

        std::vector<std::unique_ptr<ComputeResourceBinding>> allocated_bindings;

        Rhi::SPLayout layout{};

        void CreatePipeline(
            DeviceContext &device_context, const std::vector<uint32_t> &spirv_code, const std::string_view name = ""
        ) {
            const auto &device_interface = device_context.GetDeviceInterface();
            vk::Device device = device_interface.GetDevice();

            // Create descriptor and pipeline layout. The set layout comes from
            // the immutable resource cache, so the layout the pipeline layout is
            // built over and the layout a set is allocated against are the same
            // object.
            layout = Rhi::SPLayout::Reflect(spirv_code, false);
            auto desc_bindings = layout.GenerateLayoutBindings(0, true, false);
            vk::DescriptorSetLayoutCreateInfo dslci{vk::DescriptorSetLayoutCreateFlags{}, desc_bindings};
            m_passInfo.desc_layout = device_context.GetIRCache().GetDescriptorSetLayout(
                dslci, std::format("Descriptor Set Layout - Compute {}", name).c_str()
            );

            std::vector<vk::PushConstantRange> pc_ranges;
            if (layout.push_constant_size > 0) {
                pc_ranges.emplace_back(vk::ShaderStageFlagBits::eCompute, 0, layout.push_constant_size);
            }
            vk::PipelineLayoutCreateInfo plci{vk::PipelineLayoutCreateFlags{}, {m_passInfo.desc_layout}, pc_ranges};
            m_passInfo.pipeline_layout = device.createPipelineLayoutUnique(plci);
            DEBUG_SET_NAME_TEMPLATE(
                device, m_passInfo.pipeline_layout.get(), std::format("Pipeline Layout for Compute {}", name)
            );

            // Create shader module
            vk::ShaderModuleCreateInfo smci{
                vk::ShaderModuleCreateFlags{},
                spirv_code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *>(spirv_code.data())
            };
            m_passInfo.shader = device.createShaderModuleUnique(smci);
            DEBUG_SET_NAME_TEMPLATE(
                device, m_passInfo.shader.get(), std::format("Shader Module for Compute Pipeline {}", name)
            );

            vk::PipelineShaderStageCreateInfo pssci{
                vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eCompute, m_passInfo.shader.get(), "main"
            };
            vk::ComputePipelineCreateInfo cpci{vk::PipelineCreateFlags{}, pssci, m_passInfo.pipeline_layout.get()};
            auto ret = device.createComputePipelineUnique(nullptr, cpci);
            m_passInfo.pipeline = std::move(ret.value);
            DEBUG_SET_NAME_TEMPLATE(device, m_passInfo.pipeline.get(), std::format("Compute Pipeline {}", name));
        }
    };

    ComputeStage::ComputeStage(DeviceContext &device_context) :
        m_device_context(device_context), pimpl(std::make_unique<ComputeStage::impl>()) {
    }

    void ComputeStage::Instantiate(const std::vector<uint32_t> &code, const std::string_view name) {
        pimpl->CreatePipeline(m_device_context, code, name);
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
        return pimpl->m_passInfo.desc_layout;
    }

} // namespace Engine::Rhi
