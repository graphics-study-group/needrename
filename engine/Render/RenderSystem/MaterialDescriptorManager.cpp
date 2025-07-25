#include "MaterialDescriptorManager.h"

#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {
    void MaterialDescriptorManager::Create(std::shared_ptr<RenderSystem> system) {
        m_system = system;
        m_logical_device = system->getDevice();

        vk::DescriptorPoolCreateInfo info{
            vk::DescriptorPoolCreateFlags{}, MAX_SET_SIZE, DESCRIPTOR_POOL_SIZES.size(), DESCRIPTOR_POOL_SIZES.data()
        };
        m_descriptor_pool = m_logical_device.createDescriptorPoolUnique(info);
    }

    vk::DescriptorSetLayout MaterialDescriptorManager::NewDescriptorSetLayout(
        std::string name, const std::vector<vk::DescriptorSetLayoutBinding> &bindings
    ) {
        auto found = m_layouts.find(name);
        if (found != m_layouts.end()) {
            return found->second.get();
        }

        vk::DescriptorSetLayoutCreateInfo info{vk::DescriptorSetLayoutCreateFlags{}, bindings};
        auto layout = m_logical_device.createDescriptorSetLayoutUnique(info);
        auto ret = layout.get();
        m_layouts.insert(std::make_pair(name, std::move(layout)));
        return ret;
    }

    vk::DescriptorSet MaterialDescriptorManager::AllocateDescriptorSet(std::string name) {
        auto found = m_layouts.find(name);
        assert(found != m_layouts.end());
        return this->AllocateDescriptorSet(found->second.get());
    }

    vk::DescriptorSet MaterialDescriptorManager::AllocateDescriptorSet(vk::DescriptorSetLayout layout) {
        std::vector<vk::DescriptorSetLayout> layouts{layout};
        vk::DescriptorSetAllocateInfo info{m_descriptor_pool.get(), layouts};
        return m_logical_device.allocateDescriptorSets(info)[0];
    }
} // namespace Engine::RenderSystemState
