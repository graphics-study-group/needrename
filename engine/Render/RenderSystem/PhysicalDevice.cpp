#include "PhysicalDevice.h"

#include <string>
#include <unordered_set>
#include <SDL3/SDL.h>

namespace Engine::RenderSystemState {

    PhysicalDevice::QueueFamilyIndices PhysicalDevice::FillQueueFamilyIndices(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        QueueFamilyIndices q;
        auto queueFamilyProps = device.getQueueFamilyProperties();
        for (size_t i = 0; i < queueFamilyProps.size(); i++) {
            const auto & prop = queueFamilyProps[i];
            if (prop.queueFlags & vk::QueueFlagBits::eGraphics) {
                if (!q.graphics.has_value()) q.graphics = i;
            }
            if (device.getSurfaceSupportKHR(i, surface)) {
                if (!q.present.has_value()) q.present = i;
            }
        }
        return q;
    }

    PhysicalDevice::SwapchainSupport PhysicalDevice::FillSwapchainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
        SwapchainSupport support{
            device.getSurfaceCapabilitiesKHR(surface), 
            device.getSurfaceFormatsKHR(surface), 
            device.getSurfacePresentModesKHR(surface)
        };
        return support;
    }

    PhysicalDevice PhysicalDevice::SelectPhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Selecting physical devices.");
        auto devices = instance.enumeratePhysicalDevices();
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Found %llu Vulkan devices.", devices.size());

        vk::PhysicalDevice selected_device;
        for (const auto & device : devices) {
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
        dev.m_indices = FillQueueFamilyIndices(selected_device, surface);
        dev.m_support = FillSwapchainSupport(selected_device, surface);
        dev.m_memory_properties = selected_device.getMemoryProperties();
        return dev;
    }

    vk::PhysicalDevice PhysicalDevice::get() const {
        return m_device;
    }

const PhysicalDevice::SwapchainSupport& PhysicalDevice::GetSwapchainSupport() const {
        return m_support;
    }

    const PhysicalDevice::QueueFamilyIndices& PhysicalDevice::GetQueueFamilyIndices() const {
        return m_indices;
    }

    const vk::PhysicalDeviceMemoryProperties& PhysicalDevice::GetMemoryProperties() const {
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

        // Check if all extensions are available
        std::unordered_set <std::string> required_extensions{};
        for (const auto & extension_name : DEVICE_EXTENSION_NAMES) {
            required_extensions.insert(extension_name);
        }

        auto extensions = device.enumerateDeviceExtensionProperties();
        for (const auto & extension : extensions) {
            required_extensions.erase(extension.extensionName);
        }

        if (!required_extensions.empty()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, 
                "Cannot find all extensions, %llu extensions not found.",
                required_extensions.size()
                );
            for (const auto & name : required_extensions) {
                SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "\t%s", name.data());
            }
        }
        return true;
    }
}
