#ifndef ENGINE_RHI_BACKEND_WEBGPU_WGPUIMPLLOG_INCLUDED
#define ENGINE_RHI_BACKEND_WEBGPU_WGPUIMPLLOG_INCLUDED

namespace Engine::Rhi {
    /**
     * @brief Route the device's own diagnostics into the engine's log, when asked to.
     *
     * The implementation reports a failed operation through its status and a message, and some
     * of those messages say only "Validation Error". The detail is in the implementation's own
     * log, which is off by default and which the browser has no way to enable - so this is a
     * desktop debugging aid.
     *
     * The desktop validation build links Dawn, whose C API exposes no per-instance log
     * callback (unlike the old wgpu-native's `wgpuSetLogCallback`), so there is nothing to
     * configure here; the function remains the single place a future implementation hook
     * belongs. The browser has no such channel at all.
     *
     * @return Whether a level was applied.
     */
    inline bool ConfigureWgpuLoggingFromEnvironment() {
        return false;
    }
} // namespace Engine::Rhi

#endif // ENGINE_RHI_BACKEND_WEBGPU_WGPUIMPLLOG_INCLUDED
