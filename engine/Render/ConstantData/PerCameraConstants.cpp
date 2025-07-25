#include "PerCameraConstants.h"

namespace Engine::ConstantData {
    void PerCameraConstantLayout::Create(vk::Device device) {
        vk::DescriptorSetLayoutCreateInfo info{vk::DescriptorSetLayoutCreateFlags{}, BINDINGS.size(), BINDINGS.data()};

        m_handle = device.createDescriptorSetLayoutUnique(info);
    }
} // namespace Engine::ConstantData
