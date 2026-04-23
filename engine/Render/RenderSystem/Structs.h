#ifndef RENDER_RENDERSYSTEM_STRUCTS_INCLUDED
#define RENDER_RENDERSYSTEM_STRUCTS_INCLUDED

#include <optional>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::RenderSystemState {

    /// @brief Support of a device to swapchains.
    struct SwapchainSupport {
        /// Surface capabilities supported by the device.
        vk::SurfaceCapabilitiesKHR capabilities;
        /// Surface formats supported by the device.
        /// can't use std::unordered_set as it requires default constructor
        std::vector<vk::SurfaceFormatKHR> formats;
        /// Presenting mode supported by the device.
        std::vector<vk::PresentModeKHR> modes;
    };

    /// @brief Information for Vulkan queues on a device.
    struct QueueInfo {
        /// Queue for graphics operation
        vk::Queue graphicsQueue;
        /// Command pool for graphics operation
        vk::UniqueCommandPool graphicsPool;
        /// Command pool for disposable command buffer for graphics operation
        vk::UniqueCommandPool graphicsOneTimePool;
        /// Queue for presenting. It can be assumed that it is the same as graphicsQueue.
        vk::Queue presentQueue;
        /// Command pool for presenting.
        vk::UniqueCommandPool presentPool;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RENDERSYSTEM_STRUCTS_INCLUDED
