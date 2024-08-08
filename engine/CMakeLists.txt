message("COMPILE engine library")
file(GLOB_RECURSE SRC "./*")
foreach(SRC IN ITEMS ${SRC}) 
    get_filename_component(SRC_PATH "${SRC}" PATH)
    file(RELATIVE_PATH SRC_PATH_REL "${CMAKE_CURRENT_SOURCE_DIR}" "${SRC_PATH}")
    string(REPLACE "/" "\\" GROUP_PATH "${SRC_PATH_REL}")
    source_group("${GROUP_PATH}" FILES "${SRC}")
endforeach()

add_library(engine STATIC ${SRC})
target_include_directories(
    engine
    PUBLIC
    ${ENGINE_DIR}
)
target_link_libraries(engine PUBLIC SDL3::SDL3 Vulkan::Vulkan glm tinyobjloader stb)
