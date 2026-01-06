add_subdirectory(glm EXCLUDE_FROM_ALL)
set_target_properties(glm PROPERTIES FOLDER third_party)

add_subdirectory(json EXCLUDE_FROM_ALL)
set_target_properties(json PROPERTIES FOLDER third_party)

add_subdirectory(stb EXCLUDE_FROM_ALL)
set_target_properties(stb PROPERTIES FOLDER third_party)

add_subdirectory(tinyobjloader EXCLUDE_FROM_ALL)
set_target_properties(tinyobjloader PROPERTIES FOLDER third_party)

add_subdirectory(vma EXCLUDE_FROM_ALL)
set_target_properties(vma PROPERTIES FOLDER third_party)

# For some reasons the SPIRV-Cross in Vulkan SDK cannot be found by CMake...
set(SPIRV_CROSS_CLI OFF CACHE BOOL "Build the CLI binary. Requires SPIRV_CROSS_STATIC." FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "Enable SPIRV-Cross tests." FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "Enable HLSL target support." FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "Enable MSL target support." FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "Enable C API wrapper support in static library." FORCE)
add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
set_target_properties(spirv-cross-core PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-cpp PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-glsl PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-reflect PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-util PROPERTIES FOLDER third_party)

# Add ImGUI files
file(GLOB IMGUI_SOURCE 
    "${THIRD_PARTY_DIR}/imgui/*.cpp" 
    "${THIRD_PARTY_DIR}/imgui/backends/*_vulkan.cpp"
    "${THIRD_PARTY_DIR}/imgui/backends/*_sdl3.cpp"
)
message(DEBUG "Found ImGUI files: ${IMGUI_SOURCE}")
add_library(imgui OBJECT EXCLUDE_FROM_ALL ${IMGUI_SOURCE})
set_target_properties(imgui PROPERTIES FOLDER third_party)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
target_include_directories(
    imgui 
    SYSTEM PUBLIC 
    "${THIRD_PARTY_DIR}/imgui/"
    "${THIRD_PARTY_DIR}/imgui/backends/"
)

set(ENABLE_OPT OFF CACHE BOOL "Enable optimizations" FORCE)
add_subdirectory(glslang EXCLUDE_FROM_ALL)
