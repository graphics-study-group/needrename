#include "Rhi/Device/DeviceContext.h"

#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Resource/DescriptorArena.h"
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
        // The arena reads the tracker, so the tracker must exist first.
        m_descriptor_arena = std::make_unique<DescriptorArena>(*this);
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

    DescriptorArena &DeviceContext::GetDescriptorArena() noexcept {
        return *m_descriptor_arena;
    }

    const DescriptorArena &DeviceContext::GetDescriptorArena() const noexcept {
        return *m_descriptor_arena;
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

    void DeviceContext::WaitForIdle() {
        m_device_interface->GetDevice().waitIdle();
        // What was already parked can go: a device-idle wait proves that every
        // submission finished, not that a live resource has no owner.
        m_epoch_tracker->ReleaseAllParked();
        // What the arena adds: entries become eligible, but nothing is released.
        m_descriptor_arena->OnDeviceIdle();
    }
} // namespace Engine::Rhi
