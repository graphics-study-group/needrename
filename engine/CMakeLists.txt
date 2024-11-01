message("COMPILE engine library")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

add_library(engine STATIC ${SOURCE})
target_include_directories(
    engine
    PUBLIC
    ${ENGINE_SOURCE_DIR}
    ${REFLECTION_PARSER_DIR}
)

target_link_libraries(engine PUBLIC SDL3::SDL3 Vulkan::Vulkan glm tinyobjloader stb json cereal vma)
add_dependencies(engine reflection_parser)
target_precompile_headers(engine PRIVATE <vulkan/vulkan.hpp>)
target_precompile_headers(engine PRIVATE "Render/VkWrapper.tcc")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

set(REFLECTION_SEARCH_FILES "${REFLECTION_SEARCH_FILES};${HEADER}" CACHE STRING "" FORCE)
set(REFLECTION_SEARCH_INCLUDE_DIRS "${REFLECTION_SEARCH_INCLUDE_DIRS};${ENGINE_SOURCE_DIR}" CACHE STRING "" FORCE)
