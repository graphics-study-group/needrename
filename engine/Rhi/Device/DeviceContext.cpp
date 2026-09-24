#include "Rhi/Device/DeviceContext.h"

#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Submission/EpochTracker.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    DeviceContext::DeviceContext(DeviceInterface::DeviceConfiguration cfg) :
        m_device_interface(std::make_unique<DeviceInterface>(std::move(cfg))) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device_interface->GetInstance(), ::vkGetInstanceProcAddr);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device_interface->GetDevice());

        m_immutable_resource_cache = std::make_unique<ImmutableResourceCache>(m_device_interface->GetDevice());
        m_allocator_state = std::make_unique<AllocatorState>(*m_device_interface);
        m_epoch_tracker = std::make_unique<EpochTracker>(m_device_interface->GetDevice());
        // Install the tracker as the allocator's retire sink: from here on every
        // buffer allocated through this context is retire-safe by construction.
        m_allocator_state->SetRetireSink(m_epoch_tracker.get());
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

    EpochTracker &DeviceContext::GetEpochTracker() noexcept {
        return *m_epoch_tracker;
    }

    const EpochTracker &DeviceContext::GetEpochTracker() const noexcept {
        return *m_epoch_tracker;
    }

    vk::Device DeviceContext::GetDevice() const noexcept {
        return m_device_interface->GetDevice();
    }
} // namespace Engine::Rhi
