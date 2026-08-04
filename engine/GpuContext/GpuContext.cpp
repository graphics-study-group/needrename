#include "GpuContext.h"
#include "DeviceInterface.h"

namespace Engine {
    struct GpuContext::impl {
        RenderSystemState::DeviceInterface m_device_interface;
        RenderSystemState::AllocatorState m_allocator_state;

        impl(RenderSystemState::DeviceInterface::DeviceConfiguration cfg) : m_device_interface(std::move(cfg)) {
            m_allocator_state.SetDeviceInterface(m_device_interface);
            m_allocator_state.Create();
        }
    };

    GpuContext::GpuContext(RenderSystemState::DeviceInterface::DeviceConfiguration cfg) :
        pimpl(std::make_unique<impl>(std::move(cfg))) {
    }

    GpuContext::~GpuContext() = default;

    vk::Device GpuContext::GetDevice() const {
        return pimpl->m_device_interface.GetDevice();
    }

    const RenderSystemState::DeviceInterface &GpuContext::GetDeviceInterface() const {
        return pimpl->m_device_interface;
    }

    const RenderSystemState::AllocatorState &GpuContext::GetAllocatorState() const {
        return pimpl->m_allocator_state;
    }
} // namespace Engine
