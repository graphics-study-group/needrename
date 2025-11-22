#include "GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/DebugUtils.h"
#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {
    void GlobalConstantDescriptorPool::CreateLayouts(std::shared_ptr<RenderSystem> system) {
        m_per_scene_constant_layout.Create(system->GetDevice());
    }

    void GlobalConstantDescriptorPool::AllocateGlobalSets(
        std::shared_ptr<RenderSystem> system, uint32_t inflight_frame_count
    ) {
        // Allocate buffers
        auto ubo_alignment = system->GetDeviceInterface().QueryLimit(
            RenderSystemState::DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
        );

        m_per_scene_buffers.clear();
        m_per_scene_memories.clear();
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            m_per_scene_buffers.emplace_back(
                Buffer::Create(
                    system->GetAllocatorState(), 
                    Engine::Buffer::BufferType::Uniform, 
                    sizeof(Engine::ConstantData::PerSceneStruct), 
                    std::format("Buffer - scene uniforms {}", i)
                )
            );
            m_per_scene_memories.push_back(m_per_scene_buffers[i].GetVMAddress());
        }

        // Allocate descriptor sets

        m_per_scene_descriptor_sets.clear();
        std::vector layouts(inflight_frame_count, m_per_scene_constant_layout.get());
        vk::DescriptorSetAllocateInfo info{m_handle.get(), layouts, nullptr};
        auto descriptor_sets = system->GetDevice().allocateDescriptorSets(info);
        assert(descriptor_sets.size() == inflight_frame_count);
        m_per_scene_descriptor_sets = {descriptor_sets.begin(), descriptor_sets.end()};
        assert(m_per_scene_descriptor_sets.size() == inflight_frame_count);

        for (size_t i = 0; i < inflight_frame_count; i++) {
            
            DEBUG_SET_NAME_TEMPLATE(
                system->GetDevice(), m_per_scene_descriptor_sets[i], std::format("Desc Set - Scene {}", i)
            );
        }

        // Write descriptor sets
        std::vector<vk::DescriptorBufferInfo> buffers{inflight_frame_count};
        std::vector<vk::WriteDescriptorSet> writes{inflight_frame_count};
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            buffers[i] =
                vk::DescriptorBufferInfo{m_per_scene_buffers[i].GetBuffer(), 0, vk::WholeSize};
        }
        for (uint32_t i = 0; i < inflight_frame_count; i++) {
            writes[i] =
                vk::WriteDescriptorSet{m_per_scene_descriptor_sets[i], 0, 0, vk::DescriptorType::eUniformBuffer, {}};
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &buffers[i];
        }
        system->GetDevice().updateDescriptorSets(writes, {});
    }

    void GlobalConstantDescriptorPool::Create(std::weak_ptr<RenderSystem> system, uint32_t inflight_frame_count) {
        auto p_system = system.lock();
        vk::DescriptorPoolCreateInfo info{vk::DescriptorPoolCreateFlags{}, MAX_SET_SIZE, DESCRIPTOR_POOL_SIZES};
        m_handle = p_system->GetDevice().createDescriptorPoolUnique(info);
        DEBUG_SET_NAME_TEMPLATE(p_system->GetDevice(), m_handle.get(), "Desc Pool - Global");

        CreateLayouts(p_system);
        AllocateGlobalSets(p_system, inflight_frame_count);
    }
    auto GlobalConstantDescriptorPool::GetPerSceneConstantLayout() const -> const
        decltype(m_per_scene_constant_layout) & {
        assert(m_handle);
        return m_per_scene_constant_layout;
    }
    auto GlobalConstantDescriptorPool::GetPerSceneConstantSet(uint32_t inflight) const -> const
        decltype(m_per_scene_descriptor_sets[inflight]) & {
        assert(inflight < m_per_scene_descriptor_sets.size());
        return m_per_scene_descriptor_sets[inflight];
    }
    std::byte *GlobalConstantDescriptorPool::GetPerSceneConstantMemory(uint32_t inflight) const {
        assert(inflight < m_per_scene_descriptor_sets.size());
        return m_per_scene_memories[inflight];
    }
    void GlobalConstantDescriptorPool::FlushPerSceneConstantMemory(uint32_t inflight) const {
        assert(inflight < m_per_scene_descriptor_sets.size());
        m_per_scene_buffers[inflight].Flush();
    }
}; // namespace Engine::RenderSystemState
