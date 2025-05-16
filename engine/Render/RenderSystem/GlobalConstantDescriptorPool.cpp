#include "GlobalConstantDescriptorPool.h"

#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState{
    void GlobalConstantDescriptorPool::CreateLayouts(std::shared_ptr <RenderSystem> system) {
        m_per_camera_constant_layout.Create(system->getDevice());
        m_per_scene_constant_layout.Create(system->getDevice());
    }

    void GlobalConstantDescriptorPool::AllocateGlobalSets(std::shared_ptr <RenderSystem> system, uint32_t inflight_frame_count) {
        // Allocate buffers
        m_per_camera_buffers.clear();
        m_per_camera_memories.clear();
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            m_per_camera_buffers.emplace_back(system);
            m_per_camera_buffers[i].Create(
                Engine::Buffer::BufferType::Uniform, 
                sizeof(Engine::ConstantData::PerCameraStruct),
                std::format("Buffer - camera uniforms {}", i)
            );
            m_per_camera_memories.push_back(m_per_camera_buffers[i].Map());
        }

        m_per_scene_buffers.clear();
        m_per_scene_memories.clear();
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            m_per_scene_buffers.emplace_back(system);
            m_per_scene_buffers[i].Create(
                Engine::Buffer::BufferType::Uniform, 
                sizeof(Engine::ConstantData::PerSceneStruct),
                std::format("Buffer - scene uniforms {}", i)
            );
            m_per_scene_memories.push_back(m_per_scene_buffers[i].Map());
        }

        // Allocate descriptor sets
        m_per_camera_descriptor_sets.clear();
        m_per_scene_descriptor_sets.clear();
        std::vector <vk::DescriptorSetLayout> layouts {inflight_frame_count, m_per_camera_constant_layout.get()};
        layouts.insert(layouts.cend(), inflight_frame_count, m_per_scene_constant_layout.get());
        vk::DescriptorSetAllocateInfo info {
            m_handle.get(),
            layouts,
            nullptr
        };
        auto descriptor_sets = system->getDevice().allocateDescriptorSets(info);
        assert(descriptor_sets.size() == inflight_frame_count * 2);
        m_per_camera_descriptor_sets = {descriptor_sets.begin(), descriptor_sets.begin() + inflight_frame_count};
        m_per_scene_descriptor_sets = {descriptor_sets.begin() + inflight_frame_count, descriptor_sets.end()};
        assert(
            m_per_camera_descriptor_sets.size() == inflight_frame_count &&
            m_per_scene_descriptor_sets.size() == inflight_frame_count
        );

        // Write descriptor sets
        std::vector <vk::DescriptorBufferInfo> buffers {inflight_frame_count * 2};
        std::vector <vk::WriteDescriptorSet> writes {inflight_frame_count * 2};
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            buffers[i] = vk::DescriptorBufferInfo{
                m_per_camera_buffers[i].GetBuffer(), 0, sizeof(Engine::ConstantData::PerCameraStruct) 
            };
            buffers[i + inflight_frame_count] = vk::DescriptorBufferInfo{
                m_per_scene_buffers[i].GetBuffer(), 0, sizeof(Engine::ConstantData::PerSceneStruct) 
            };
        }
        for (uint32_t i = 0; i < inflight_frame_count; i++){
            writes[i] = vk::WriteDescriptorSet{
                m_per_camera_descriptor_sets[i],
                0,
                0,
                vk::DescriptorType::eUniformBuffer,
                {}
            };
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &buffers[i];

            writes[i + inflight_frame_count] = vk::WriteDescriptorSet{
                m_per_scene_descriptor_sets[i],
                0,
                0,
                vk::DescriptorType::eUniformBuffer,
                {}
            };
            writes[i + inflight_frame_count].descriptorCount = 1;
            writes[i + inflight_frame_count].pBufferInfo = &buffers[i + inflight_frame_count];
        }
        system->getDevice().updateDescriptorSets(writes, {});
    }

    void GlobalConstantDescriptorPool::Create(std::weak_ptr<RenderSystem> system, uint32_t inflight_frame_count)
    {
        auto p_system = system.lock();
        vk::DescriptorPoolCreateInfo info {
            vk::DescriptorPoolCreateFlags{},
            MAX_SET_SIZE,
            DESCRIPTOR_POOL_SIZES
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
    void GlobalConstantDescriptorPool::FlushPerCameraConstantMemory(uint32_t inflight) const {
        assert(inflight < m_per_camera_memories.size());
        m_per_camera_buffers[inflight].Flush();
    }
    auto GlobalConstantDescriptorPool::GetPerSceneConstantLayout() const -> const decltype(m_per_scene_constant_layout) &
    {
        assert(m_handle);
        return m_per_scene_constant_layout;
    }
    auto GlobalConstantDescriptorPool::GetPerSceneConstantSet(uint32_t inflight) const -> const decltype(m_per_scene_descriptor_sets[inflight]) &
    {
        assert(inflight < m_per_scene_descriptor_sets.size());
        return m_per_scene_descriptor_sets[inflight];
    }
    std::byte *GlobalConstantDescriptorPool::GetPerSceneConstantMemory(uint32_t inflight) const
    {
        assert(inflight < m_per_scene_descriptor_sets.size());
        return m_per_scene_memories[inflight];
    }
    void GlobalConstantDescriptorPool::FlushPerSceneConstantMemory(uint32_t inflight) const
    {
        assert(inflight < m_per_scene_descriptor_sets.size());
        m_per_scene_buffers[inflight].Flush();
    }
};
