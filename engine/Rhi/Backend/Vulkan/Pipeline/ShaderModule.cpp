#include "Rhi/Pipeline/ShaderModule.h"

#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Device/DeviceInterface.h"

#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct ShaderModule::impl {
        vk::UniqueShaderModule module{};
        vk::Device device{};
    };

    ShaderModule::ShaderModule(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {}

    ShaderModule ShaderModule::Create(
        const DeviceInterface &device_interface, std::span<const std::byte> code, std::string_view name
    ) {
        auto p = std::make_unique<impl>();
        p->device = ToVulkanDevice(device_interface.GetDevice());

        // vk::ShaderModuleCreateInfo wants uint32_t words; the bytes are already
        // SPIR-V, so this is a reinterpretation rather than a conversion.
        const auto *words = reinterpret_cast<const uint32_t *>(code.data());
        const vk::ShaderModuleCreateInfo info{{}, code.size(), words};
        p->module = p->device.createShaderModuleUnique(info);

        if (!name.empty()) {
            DEBUG_SET_NAME_TEMPLATE(p->device, p->module.get(), std::string(name));
        }
        return ShaderModule(std::move(p));
    }

    ShaderModule::~ShaderModule() = default;
    ShaderModule::ShaderModule(ShaderModule &&) noexcept = default;
    ShaderModule &ShaderModule::operator=(ShaderModule &&) noexcept = default;

    ShaderModuleHandle ShaderModule::GetHandle() const noexcept {
        return FromVulkan(pimpl->module.get());
    }
} // namespace Engine::Rhi
