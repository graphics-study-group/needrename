#ifndef ENGINE_RHI_BACKEND_WEBGPU_WGPUNATIVELOG_INCLUDED
#define ENGINE_RHI_BACKEND_WEBGPU_WGPUNATIVELOG_INCLUDED

#include <SDL3/SDL.h>

#include <string_view>

#if !defined(__EMSCRIPTEN__)
// wgpu-native keeps its extensions in `wgpu.h`, which the browser port does not have. The
// standard header is what the backend codes against; this is the desktop build's one use of
// the native one, and it is guarded rather than assumed.
#include <webgpu/wgpu.h>
#endif

namespace Engine::Rhi {
    /**
     * @brief Route the device's own diagnostics into the engine's log, when asked to.
     *
     * The implementation reports a failed operation through its status and a message, and some
     * of those messages say only "Validation Error". The detail is in the implementation's own
     * log, which is off by default and which the browser has no way to enable - so this is a
     * desktop debugging aid, controlled by `WGPU_LOG_LEVEL` (`error`, `warn`, `info`, `debug`,
     * `trace`).
     *
     * @return Whether a level was applied.
     */
    inline bool ConfigureWgpuLoggingFromEnvironment() {
#if defined(__EMSCRIPTEN__)
        return false;
#else
        const char *level = SDL_getenv("WGPU_LOG_LEVEL");
        if (level == nullptr || level[0] == '\0') {
            return false;
        }
        WGPULogLevel value = WGPULogLevel_Info;
        const std::string_view name{level};
        if (name == "error") {
            value = WGPULogLevel_Error;
        } else if (name == "warn") {
            value = WGPULogLevel_Warn;
        } else if (name == "debug") {
            value = WGPULogLevel_Debug;
        } else if (name == "trace") {
            value = WGPULogLevel_Trace;
        }
        wgpuSetLogCallback(
            [](WGPULogLevel, WGPUStringView message, void *) {
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_RENDER,
                    "wgpu: %.*s",
                    static_cast<int>(message.length),
                    message.data != nullptr ? message.data : ""
                );
            },
            nullptr
        );
        wgpuSetLogLevel(value);
        return true;
#endif
    }
} // namespace Engine::Rhi

#endif // ENGINE_RHI_BACKEND_WEBGPU_WGPUNATIVELOG_INCLUDED
