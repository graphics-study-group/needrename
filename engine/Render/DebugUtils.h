#ifndef RENDER_DEBUGUTILS_INCLUDED
#define RENDER_DEBUGUTILS_INCLUDED

#ifndef NDEBUG

#define DEBUG_CMD_START_LABEL(cmd, ...) (cmd.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{__VA_ARGS__}, vk::detail::DispatchLoaderDynamic{}))
#define DEBUG_CMD_END_LABEL(cmd) (cmd.endDebugUtilsLabelEXT())
#define DEBUG_SET_NAME(device, type, obj, tag) (device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{type, (uint64_t)obj, tag}))

#else

#define DEBUG_CMD_START_LABEL(cmd, ...)
#define DEBUG_CMD_END_LABEL(cmd)
#define DEBUG_SET_NAME(device, type, obj, tag)

#endif

#endif // RENDER_DEBUGUTILS_INCLUDED
