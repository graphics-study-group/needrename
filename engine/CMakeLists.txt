message("COMPILE engine library")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

list(FILTER SOURCE EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADER EXCLUDE REGEX "__generated__/.*")

add_library(engine STATIC ${SOURCE})
target_compile_definitions(engine PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_include_directories(
    engine
    PUBLIC
    ${ENGINE_SOURCE_DIR}
)

target_link_libraries(engine 
    PUBLIC
    SDL3::SDL3
    Vulkan::Vulkan
    glm
    tinyobjloader
    stb
    json
    cereal
    vma
    imgui
    spirv-reflect
)
target_precompile_headers(engine PRIVATE <vulkan/vulkan.hpp>)
target_precompile_headers(engine PRIVATE "Render/VkWrapper.tcc")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

include(${REFLECTION_PARSER_DIR}/parser.cmake)
set(all_include_dirs)
get_include_directories_for_target(engine all_include_dirs)
filter_files_with_reflection_macros(HEADER REFLECTION_SEARCH_HEADERS)

add_reflection_parser(meta_engine "${REFLECTION_SEARCH_HEADERS}" "${ENGINE_SOURCE_DIR}/__generated__" "${all_include_dirs}")
add_dependencies(engine meta_engine)
target_link_libraries(engine PRIVATE meta_engine)
