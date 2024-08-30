#ifndef RENDER_RENDERSYSTEM_STRUCTS_INCLUDED
#define RENDER_RENDERSYSTEM_STRUCTS_INCLUDED

#include <optional>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::RenderSystemState {
    struct QueueFamilyIndices
    {
        std::optional <uint32_t> graphics {};
        std::optional <uint32_t> present {};

        bool isComplete() {
            if (!graphics.has_value()) return false;
            if (!present.has_value()) return false;
            return true;
        }
    };

    struct SwapchainSupport
    {
        vk::SurfaceCapabilitiesKHR capabilities;
        // std::unordered_set requires default constructor
        std::vector <vk::SurfaceFormatKHR> formats;
        std::vector <vk::PresentModeKHR> modes;
    };
}

#endif // RENDER_RENDERSYSTEM_STRUCTS_INCLUDED
