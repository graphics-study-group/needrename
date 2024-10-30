add_subdirectory(cereal)
add_subdirectory(glad)
add_subdirectory(glm)
add_subdirectory(json)
add_subdirectory(SDL3)
add_subdirectory(stb)
add_subdirectory(tinyobjloader)
add_subdirectory(vma)

# Add ImGUI files
file(GLOB IMGUI_SOURCE 
    "${THIRD_PARTY_DIR}/imgui/*.cpp" 
    "${THIRD_PARTY_DIR}/imgui/backends/*_vulkan.cpp"
    "${THIRD_PARTY_DIR}/imgui/backends/*_sdl3.cpp"
)
message(STATUS "Found ImGUI files: ${IMGUI_SOURCE}")
add_library(imgui ${IMGUI_SOURCE})
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
target_include_directories(
    imgui 
    PUBLIC 
    "${THIRD_PARTY_DIR}/imgui/"
    "${THIRD_PARTY_DIR}/imgui/backends/"
)
