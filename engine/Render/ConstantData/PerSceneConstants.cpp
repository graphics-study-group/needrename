#include "PerSceneConstants.h"

void Engine::ConstantData::PerSceneConstantLayout::Create(vk::Device device) {

    static constexpr std::array<vk::DescriptorSetLayoutBinding, 1> BINDINGS = {
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics}
    };

    vk::DescriptorSetLayoutCreateInfo info{vk::DescriptorSetLayoutCreateFlags{}, BINDINGS.size(), BINDINGS.data()};

    m_handle = device.createDescriptorSetLayoutUnique(info);
}
