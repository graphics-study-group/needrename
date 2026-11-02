add_subdirectory(core)
add_subdirectory(shader)
# The Rhi unit tests build a Vulkan device of their own and inspect Vulkan objects, so
# they are the Vulkan backend's tests. `rhi_public_headers_test` lives with them and is
# the one that has to run on every backend; it is registered from here regardless.
if (ENGINE_RHI_BACKEND_VULKAN)
    set(RHI_TEST_PUBLIC_HEADERS_ONLY OFF)
else()
    set(RHI_TEST_PUBLIC_HEADERS_ONLY ON)
endif()
add_subdirectory(rhi)
add_subdirectory(physics)
add_subdirectory(render)
