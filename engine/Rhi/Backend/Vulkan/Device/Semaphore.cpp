#include "Rhi/Device/Semaphore.h"

#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Device/DeviceInterface.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct Semaphore::impl {
        vk::UniqueSemaphore semaphore{};
        vk::Device device{};
        SemaphoreType type{SemaphoreType::Binary};
    };

    Semaphore::Semaphore(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {}

    Semaphore Semaphore::Create(
        const DeviceInterface &device_interface, SemaphoreType type, uint64_t initial_value, std::string_view name
    ) {
        auto p = std::make_unique<impl>();
        p->device = ToVulkanDevice(device_interface.GetDevice());
        p->type = type;

        vk::SemaphoreTypeCreateInfo type_info{};
        type_info.semaphoreType =
            type == SemaphoreType::Timeline ? vk::SemaphoreType::eTimeline : vk::SemaphoreType::eBinary;
        type_info.initialValue = initial_value;
        const vk::SemaphoreCreateInfo info{{}, &type_info};

        p->semaphore = p->device.createSemaphoreUnique(info);
        if (!name.empty()) {
            DEBUG_SET_NAME_TEMPLATE(p->device, p->semaphore.get(), std::string(name));
        }
        return Semaphore(std::move(p));
    }

    Semaphore::~Semaphore() = default;
    Semaphore::Semaphore(Semaphore &&) noexcept = default;
    Semaphore &Semaphore::operator=(Semaphore &&) noexcept = default;

    SemaphoreHandle Semaphore::GetHandle() const noexcept {
        return FromVulkan(pimpl->semaphore.get());
    }

    SemaphoreType Semaphore::GetType() const noexcept {
        return pimpl->type;
    }

    uint64_t Semaphore::GetValue() const {
        return pimpl->device.getSemaphoreCounterValue(pimpl->semaphore.get());
    }
} // namespace Engine::Rhi
