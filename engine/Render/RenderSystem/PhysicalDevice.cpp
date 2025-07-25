#include "PhysicalDevice.h"

#include <SDL3/SDL.h>
#include <string>
#include <unordered_set>

namespace Engine::RenderSystemState {

    QueueFamilyIndices PhysicalDevice::FillQueueFamilyIndices(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        QueueFamilyIndices q;
        auto queueFamilyProps = device.getQueueFamilyProperties();
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "\tInspecting queue families");
        for (size_t i = 0; i < queueFamilyProps.size(); i++) {
            const auto &prop = queueFamilyProps[i];

            bool isGeneralQueueFamily = (prop.queueFlags & vk::QueueFlagBits::eGraphics)
                                        && (prop.queueFlags & vk::QueueFlagBits::eTransfer)
                                        && (prop.queueFlags & vk::QueueFlagBits::eCompute);
            bool supportPresenting = device.getSurfaceSupportKHR(i, surface);

            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                         std::format("\t\tQueue family {} {}{}{}({} present)",
                                     i,
                                     prop.queueFlags & vk::QueueFlagBits::eGraphics ? "Graphics " : "",
                                     prop.queueFlags & vk::QueueFlagBits::eTransfer ? "Transfer " : "",
                                     prop.queueFlags & vk::QueueFlagBits::eCompute ? "Compute " : "",
                                     supportPresenting ? "Can" : "Cannot")
                             .c_str());

            if (isGeneralQueueFamily) {
                if (!q.graphics.has_value()) q.graphics = i;
            }
            if (supportPresenting) {
                if (!q.present.has_value()) q.present = i;
            }
        }
        return q;
    }

    SwapchainSupport PhysicalDevice::FillSwapchainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        SwapchainSupport support{device.getSurfaceCapabilitiesKHR(surface),
                                 device.getSurfaceFormatsKHR(surface),
                                 device.getSurfacePresentModesKHR(surface)};
        return support;
    }

    PhysicalDevice PhysicalDevice::SelectPhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Selecting physical devices.");
        auto devices = instance.enumeratePhysicalDevices();
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Found %llu Vulkan devices.", devices.size());

        vk::PhysicalDevice selected_device;
        for (const auto &device : devices) {
            if (IsDeviceSuitable(device, surface)) {
                selected_device = device;
                break;
            }
        }

        if (!selected_device) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Cannot select appropiate device.");
        }
        assert(selected_device);
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Device %s selected.", selected_device.getProperties().deviceName.data());

        PhysicalDevice dev;
        dev.m_device = selected_device;
        dev.m_memory_properties = selected_device.getMemoryProperties();
        return dev;
    }

    vk::PhysicalDevice PhysicalDevice::get() const {
        return m_device;
    }

    SwapchainSupport PhysicalDevice::GetSwapchainSupport(vk::SurfaceKHR surface) const {
        return FillSwapchainSupport(this->m_device, surface);
    }

    QueueFamilyIndices PhysicalDevice::GetQueueFamilyIndices(vk::SurfaceKHR surface) const {
        return FillQueueFamilyIndices(this->m_device, surface);
    }

    const vk::PhysicalDeviceMemoryProperties &PhysicalDevice::GetMemoryProperties() const {
        return m_memory_properties;
    }

    bool PhysicalDevice::IsDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        auto props = device.getProperties();
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "\tInspecting %s.", props.deviceName.data());

        /* if (!(props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Not discrete GPU.");
            return false;
        } */

        // Check if all queue families are available
        if (!FillQueueFamilyIndices(device, surface).isComplete()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Cannot find complete queue family.");
            return false;
        }

        // Check if swapchain is supported
        auto support = FillSwapchainSupport(device, surface);
        if (support.formats.empty() || support.modes.empty()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Cannot find suitable swapchain.");
            return false;
        }

        // Check features
        auto device_features = device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
        auto features13 = device_features.get<vk::PhysicalDeviceVulkan13Features>();
        if (!(features13.dynamicRendering && features13.synchronization2)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "This physical device does not support needed Vulkan 1.3 features.");
            return false;
        }

        // Check if all extensions are available
        std::unordered_set<std::string> required_extensions{};
        for (const auto &extension_name : DEVICE_EXTENSION_NAMES) {
            required_extensions.insert(extension_name);
        }

        auto extensions = device.enumerateDeviceExtensionProperties();
        for (const auto &extension : extensions) {
            required_extensions.erase(extension.extensionName);
        }

        if (!required_extensions.empty()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                        "Cannot find all extensions, %llu extensions not found.",
                        required_extensions.size());
            for (const auto &name : required_extensions) {
                SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "\t%s", name.data());
            }
            return false;
        }

        return true;
    }
} // namespace Engine::RenderSystemState
