# Build-time helper executables. Gated by ENGINE_BUILD_TOOLS.

# shader_baker rewrites the built-in shader asset archives from GLSL to
# embedded-SPIR-V form using the engine's own shader compiler, so the baked
# binaries match what runtime compilation would produce.
add_executable(shader_baker shader_baker/main.cpp)
target_link_libraries(shader_baker PRIVATE Engine)
set_target_properties(shader_baker PROPERTIES FOLDER tools)

# The WebGPU spike harness is a measurement tool of the rhi-webgpu-backend
# change, built only on request.
if (ENGINE_BUILD_WEBGPU_SPIKE)
    add_subdirectory(webgpu_spike)
endif()
