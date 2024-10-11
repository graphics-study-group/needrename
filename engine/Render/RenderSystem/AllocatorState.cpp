#include "AllocatorState.h"

#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {
    AllocatorState::~AllocatorState()
    {
        vmaDestroyAllocator(m_allocator);
    }
    void AllocatorState::Create(std::shared_ptr<RenderSystem> system)
    {
        m_system = system;

        VmaAllocatorCreateInfo info;
        info.device = system->getDevice();
        info.physicalDevice = system->GetPhysicalDevice();
        info.instance = system->getInstance();
        info.vulkanApiVersion = vk::ApiVersion13;

        vmaDestroyAllocator(m_allocator);
        vmaCreateAllocator(&info, &m_allocator);
    }
}
