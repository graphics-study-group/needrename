#ifndef RENDER_DEBUGUTILS_INCLUDED
#define RENDER_DEBUGUTILS_INCLUDED

#ifndef NDEBUG

#define DEBUG_CMD_START_LABEL(cmd, ...) (cmd.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{__VA_ARGS__}))
#define DEBUG_CMD_END_LABEL(cmd) (cmd.endDebugUtilsLabelEXT())
#define DEBUG_SET_NAME(device, type, obj, name) (device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{type, (uint64_t)obj, name}))

#else

#define DEBUG_CMD_START_LABEL(cmd, ...)
#define DEBUG_CMD_END_LABEL(cmd)
#define DEBUG_SET_NAME(device, type, obj, name)

#endif

template <typename T>
concept vulkan_hpp_handle = std::same_as<decltype(T::objectType), const vk::ObjectType> && std::convertible_to<typename T::CType, void *>;

template <vulkan_hpp_handle T>
void DEBUG_SET_NAME_TEMPLATE(vk::Device device, T handle, const std::string & name) {
#ifndef NDEBUG
    device.setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT{
            T::objectType,
            reinterpret_cast<uint64_t> (static_cast <T::CType> (handle)),
            name.c_str()
        }
    );
#endif
}

template <vulkan_hpp_handle T>
void DEBUG_SET_NAME_TEMPLATE(vk::Device device, T handle, const char * name) {
#ifndef NDEBUG
    device.setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT{
            T::objectType,
            reinterpret_cast<uint64_t> (static_cast <T::CType> (handle)),
            name
        }
    );
#endif
}

#endif // RENDER_DEBUGUTILS_INCLUDED
