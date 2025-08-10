#include "PerCameraConstants.h"

#include <vulkan/vulkan.hpp>

namespace Engine::ConstantData {
    void PerCameraConstantLayout::Create(vk::Device device) {

        static constexpr std::array<vk::DescriptorSetLayoutBinding, 1> BINDINGS = {vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAllGraphics
        }};

        vk::DescriptorSetLayoutCreateInfo info{vk::DescriptorSetLayoutCreateFlags{}, BINDINGS.size(), BINDINGS.data()};

        m_handle = device.createDescriptorSetLayoutUnique(info);
    }
} // namespace Engine::ConstantData
