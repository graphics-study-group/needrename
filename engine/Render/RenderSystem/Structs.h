#ifndef RENDER_RENDERSYSTEM_STRUCTS_INCLUDED
#define RENDER_RENDERSYSTEM_STRUCTS_INCLUDED

#include <optional>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::RenderSystemState {
    struct SwapchainSupport {
        vk::SurfaceCapabilitiesKHR capabilities;
        // std::unordered_set requires default constructor
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> modes;
    };

    struct QueueInfo {
        vk::Queue graphicsQueue;
        vk::UniqueCommandPool graphicsPool;
        vk::UniqueCommandPool graphicsOneTimePool;
        vk::Queue presentQueue;
        vk::UniqueCommandPool presentPool;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RENDERSYSTEM_STRUCTS_INCLUDED
