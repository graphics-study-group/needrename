#include "PerSceneConstants.h"

void Engine::ConstantData::PerSceneConstantLayout::Create(vk::Device device) {
    vk::DescriptorSetLayoutCreateInfo info{vk::DescriptorSetLayoutCreateFlags{}, BINDINGS.size(), BINDINGS.data()};

    m_handle = device.createDescriptorSetLayoutUnique(info);
}
