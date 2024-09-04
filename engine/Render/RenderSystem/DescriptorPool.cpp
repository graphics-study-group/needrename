#include "DescriptorPool.h"

#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState{
    void GlobalConstantDescriptorPool::CreateLayouts(std::shared_ptr <RenderSystem> system) {
        m_per_camera_constant_layout.Create(system->getDevice());
    }

    void GlobalConstantDescriptorPool::AllocateGlobalSets(std::shared_ptr <RenderSystem> system, uint32_t inflight_frame_count) {
        // Allocate buffers
        m_per_camera_buffers.clear();
        m_per_camera_memories.clear();
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            m_per_camera_buffers.emplace_back(system);
            m_per_camera_buffers[i].Create(
                Engine::Buffer::BufferType::Uniform, 
                sizeof(Engine::ConstantData::PerCameraStruct)
            );
            m_per_camera_memories.push_back(m_per_camera_buffers[i].Map());
        }

        // Allocate descriptor sets
        m_per_camera_descriptor_sets.clear();
        std::vector <vk::DescriptorSetLayout> layouts {inflight_frame_count, m_per_camera_constant_layout.get()};
        vk::DescriptorSetAllocateInfo info {
            m_handle.get(),
            layouts,
            nullptr
        };
        m_per_camera_descriptor_sets = system->getDevice().allocateDescriptorSets(info);

        // Write descriptor sets
        std::vector <vk::DescriptorBufferInfo> buffers {inflight_frame_count};
        std::vector <vk::WriteDescriptorSet> writes {inflight_frame_count};
        for (uint32_t i = 0; i < inflight_frame_count; i++){
            buffers[i] = vk::DescriptorBufferInfo{
                m_per_camera_buffers[i].GetBuffer(), 0, sizeof(Engine::ConstantData::PerCameraStruct) 
            };
            writes[i] = vk::WriteDescriptorSet{
                m_per_camera_descriptor_sets[i],
                0,
                0,
                vk::DescriptorType::eUniformBuffer,
                {}
            };
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &buffers[i];
        }
        system->getDevice().updateDescriptorSets(writes, {});
    }

    void GlobalConstantDescriptorPool::Create(std::weak_ptr <RenderSystem> system, uint32_t inflight_frame_count) {
        auto p_system = system.lock();
        vk::DescriptorPoolCreateInfo info {
            vk::DescriptorPoolCreateFlags{},
            MAX_SET_SIZE,
            DESCRIPTOR_POOL_SIZES.size(),
            DESCRIPTOR_POOL_SIZES.data()
        };
        m_handle = p_system->getDevice().createDescriptorPoolUnique(info);

        CreateLayouts(p_system);
        AllocateGlobalSets(p_system, inflight_frame_count);
    }
    auto GlobalConstantDescriptorPool::GetPerCameraConstantLayout() const -> const decltype(m_per_camera_constant_layout)&
    {
        assert(m_handle);
        return m_per_camera_constant_layout;
    }
    auto GlobalConstantDescriptorPool::GetPerCameraConstantSet(uint32_t inflight) const -> const decltype(m_per_camera_descriptor_sets[inflight])&
    {
        assert(inflight < m_per_camera_descriptor_sets.size());
        return m_per_camera_descriptor_sets[inflight];
    }
    std::byte* GlobalConstantDescriptorPool::GetPerCameraConstantMemory(uint32_t inflight) const {
        assert(inflight < m_per_camera_memories.size());
        return m_per_camera_memories[inflight];
    }
};
