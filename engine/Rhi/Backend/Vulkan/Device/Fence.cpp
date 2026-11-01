#include "Rhi/Device/Fence.h"

#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Device/DeviceInterface.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct Fence::impl {
        vk::UniqueFence fence{};
        vk::Device device{};
    };

    Fence::Fence(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {}

    Fence Fence::Create(const DeviceInterface &device_interface, bool signaled, std::string_view name) {
        auto p = std::make_unique<impl>();
        p->device = ToVulkanDevice(device_interface.GetDevice());
        const vk::FenceCreateInfo info{signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags{}};
        p->fence = p->device.createFenceUnique(info);
        if (!name.empty()) {
            DEBUG_SET_NAME_TEMPLATE(p->device, p->fence.get(), std::string(name));
        }
        return Fence(std::move(p));
    }

    Fence::~Fence() = default;
    Fence::Fence(Fence &&) noexcept = default;
    Fence &Fence::operator=(Fence &&) noexcept = default;

    FenceHandle Fence::GetHandle() const noexcept {
        return FromVulkan(pimpl->fence.get());
    }

    bool Fence::IsSignaled() const {
        return pimpl->device.getFenceStatus(pimpl->fence.get()) == vk::Result::eSuccess;
    }

    void Fence::Reset() {
        pimpl->device.resetFences(pimpl->fence.get());
    }
} // namespace Engine::Rhi

