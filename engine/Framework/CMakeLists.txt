file(GLOB_RECURSE SOURCE "./*.cpp")
# MainClass is compiled directly into the Engine target in this phase.
list(FILTER SOURCE EXCLUDE REGEX "MainClass\\.cpp")

add_library(EngineLibFramework
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibFramework 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
    EnginePhysics
)
target_link_libraries(EngineLibFramework
    PUBLIC
    tinyxml2
    tinyobjloader
    stb
    fastgltf::fastgltf
)

# Reflection: per-DLL registration for Framework types.
file(GLOB_RECURSE FRAMEWORK_HEADERS "./*.h")
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(FRAMEWORK_HEADERS FRAMEWORK_REFLECTION_HEADERS)

set(framework_include_dirs)
get_include_directories_for_target(EngineLibHeaderInterface framework_include_dirs)
get_include_directories_for_target(EngineLibExternalDependency framework_include_dirs)

add_reflection_parser(
    target_name meta_framework
    reflection_search_files "${FRAMEWORK_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${framework_include_dirs}"
    parent_projects meta_core_generation meta_rhi_generation meta_asset_core_generation meta_physics_generation meta_render_generation
)
add_dependencies(EngineLibFramework meta_framework)
target_link_libraries(EngineLibFramework PRIVATE meta_framework)
