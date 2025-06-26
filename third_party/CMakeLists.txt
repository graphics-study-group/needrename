add_subdirectory(cereal)
set_target_properties(cereal PROPERTIES FOLDER third_party)
add_subdirectory(glad)
set_target_properties(GLAD PROPERTIES FOLDER third_party)
add_subdirectory(glm)
set_target_properties(glm PROPERTIES FOLDER third_party)
add_subdirectory(json)
set_target_properties(json PROPERTIES FOLDER third_party)
add_subdirectory(SDL3)
add_subdirectory(stb)
set_target_properties(stb PROPERTIES FOLDER third_party)
add_subdirectory(tinyobjloader)
set_target_properties(tinyobjloader PROPERTIES FOLDER third_party)
add_subdirectory(vma)
set_target_properties(vma PROPERTIES FOLDER third_party)

# Add ImGUI files
file(GLOB IMGUI_SOURCE 
    "${THIRD_PARTY_DIR}/imgui/*.cpp" 
    "${THIRD_PARTY_DIR}/imgui/backends/*_vulkan.cpp"
    "${THIRD_PARTY_DIR}/imgui/backends/*_sdl3.cpp"
)
message(DEBUG "Found ImGUI files: ${IMGUI_SOURCE}")
add_library(imgui ${IMGUI_SOURCE})
set_target_properties(imgui PROPERTIES FOLDER third_party)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
target_include_directories(
    imgui 
    SYSTEM PUBLIC 
    "${THIRD_PARTY_DIR}/imgui/"
    "${THIRD_PARTY_DIR}/imgui/backends/"
)

add_library(spirv-reflect spirv-reflect/spirv_reflect.cpp)
set_target_properties(spirv-reflect PROPERTIES FOLDER third_party)
target_link_libraries(spirv-reflect PUBLIC Vulkan::Vulkan)
target_include_directories(
    spirv-reflect
    SYSTEM PUBLIC
    "${THIRD_PARTY_DIR}/spirv-reflect"
)
