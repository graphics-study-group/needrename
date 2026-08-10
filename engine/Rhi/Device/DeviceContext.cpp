#include "Rhi/Device/DeviceContext.h"

#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Resource/ImmutableResourceCache.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    DeviceContext::DeviceContext(DeviceInterface::DeviceConfiguration cfg) :
        m_device_interface(std::make_unique<DeviceInterface>(std::move(cfg))) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device_interface->GetInstance(), ::vkGetInstanceProcAddr);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device_interface->GetDevice());

        m_immutable_resource_cache = std::make_unique<ImmutableResourceCache>(m_device_interface->GetDevice());
        m_allocator_state = std::make_unique<AllocatorState>(*m_device_interface);
    }

    DeviceContext::~DeviceContext() = default;

    DeviceInterface &DeviceContext::GetDeviceInterface() noexcept {
        return *m_device_interface;
    }

    const DeviceInterface &DeviceContext::GetDeviceInterface() const noexcept {
        return *m_device_interface;
    }

    AllocatorState &DeviceContext::GetAllocatorState() noexcept {
        return *m_allocator_state;
    }

    const AllocatorState &DeviceContext::GetAllocatorState() const noexcept {
        return *m_allocator_state;
    }

    ImmutableResourceCache &DeviceContext::GetIRCache() noexcept {
        return *m_immutable_resource_cache;
    }

    const ImmutableResourceCache &DeviceContext::GetIRCache() const noexcept {
        return *m_immutable_resource_cache;
    }

    vk::Device DeviceContext::GetDevice() const noexcept {
        return m_device_interface->GetDevice();
    }
} // namespace Engine::Rhi
