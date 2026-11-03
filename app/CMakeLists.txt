# The editor is a Vulkan application: it includes the backend's headers directly, so a WebGPU
# build cannot compile it. The physics app is backend-neutral, and the WebGPU tree needs it to
# exercise the solver there.
if (ENGINE_RHI_BACKEND_VULKAN)
    add_subdirectory(editor)
endif()
add_subdirectory(physics)