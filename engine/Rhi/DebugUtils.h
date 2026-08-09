#ifndef ENGINE_RHI_DEBUGUTILS_INCLUDED
#define ENGINE_RHI_DEBUGUTILS_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    namespace RenderDebugUtils {
        /// @brief Set to true when VK_EXT_debug_utils device extension is available.
        /// Controls whether DEBUG_SET_NAME macros actually call into Vulkan.
        inline bool g_debug_utils_available = false;
    } // namespace RenderDebugUtils
} // namespace Engine::Rhi

#ifndef NDEBUG

#define DEBUG_CMD_START_LABEL(cmd, ...)                                                                                \
    do {                                                                                                               \
        if (::Engine::Rhi::RenderDebugUtils::g_debug_utils_available)                                                  \
            (cmd.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{__VA_ARGS__}));                                        \
    } while (0)
#define DEBUG_CMD_END_LABEL(cmd)                                                                                       \
    do {                                                                                                               \
        if (::Engine::Rhi::RenderDebugUtils::g_debug_utils_available) (cmd.endDebugUtilsLabelEXT());                   \
    } while (0)
#define DEBUG_SET_NAME(device, type, obj, name)                                                                        \
    do {                                                                                                               \
        if (::Engine::Rhi::RenderDebugUtils::g_debug_utils_available)                                                  \
            (device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{type, (uint64_t)obj, name}));           \
    } while (0)
#define DEBUG_SET_NAME_TEMPLATE(device, obj, name) (DEBUG_SET_NAME_TEMPLATE_IMPL(device, obj, name))

#else

#define DEBUG_CMD_START_LABEL(cmd, ...) (void(0))
#define DEBUG_CMD_END_LABEL(cmd) (void(0))
#define DEBUG_SET_NAME(device, type, obj, name) (void(0))
#define DEBUG_SET_NAME_TEMPLATE(device, obj, name) (void(0))

#endif

template <typename T>
concept vulkan_hpp_handle =
    std::same_as<decltype(T::objectType), const vk::ObjectType> && std::convertible_to<typename T::CType, void *>;

template <vulkan_hpp_handle T>
void DEBUG_SET_NAME_TEMPLATE_IMPL(vk::Device device, T handle, const std::string &name) {
#ifndef NDEBUG
    if (name.empty() || !::Engine::Rhi::RenderDebugUtils::g_debug_utils_available) return;
    device.setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT{
            T::objectType, reinterpret_cast<uint64_t>(static_cast<T::CType>(handle)), name.c_str()
        }
    );
#endif
}

template <vulkan_hpp_handle T>
void DEBUG_SET_NAME_TEMPLATE_IMPL(vk::Device device, T handle, const char *name) {
#ifndef NDEBUG
    if (strlen(name) == 0 || !::Engine::Rhi::RenderDebugUtils::g_debug_utils_available) return;
    device.setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT{T::objectType, reinterpret_cast<uint64_t>(static_cast<T::CType>(handle)), name}
    );
#endif
}

#endif // ENGINE_RHI_DEBUGUTILS_INCLUDED
