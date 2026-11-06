# Optional measurement harnesses. The shader_baker executable and its
# EngineShaderTooling library live in tools/shader_baker and are configured
# before the engine (see the top-level CMakeLists.txt).

if (ENGINE_BUILD_WEBGPU_SPIKE)
    add_subdirectory(webgpu_spike)
endif()
