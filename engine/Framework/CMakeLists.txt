file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineFramework ${SOURCE})
target_compile_definitions(EngineFramework PRIVATE FRAMEWORK_DLL_EXPORTS)
target_link_libraries(EngineFramework
    PUBLIC
    EngineLibHeaderInterface
    AnnoRefl
    EngineDepGlm
    PRIVATE
    EngineRender
    EnginePhysics
    EngineAssetCore
    EngineRhi
    EngineCore
    EngineDepRhiBackend
    EngineDepSdl
    EngineDepJson
    EngineDepImgui
    tinyxml2
    fastgltf
    tinyobjloader
    stb
    ktx
)

# Reflection: per-DLL registration for Framework types.
file(GLOB_RECURSE FRAMEWORK_HEADERS "./*.h")
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(FRAMEWORK_HEADERS FRAMEWORK_REFLECTION_HEADERS)

set(framework_include_dirs)
foreach(dep IN ITEMS
    EngineLibHeaderInterface AnnoRefl EngineDepGlm EngineDepJson EngineDepSdl
    EngineDepRhiBackend EngineDepImgui fastgltf tinyxml2 tinyobjloader stb)
    get_include_directories_for_target(${dep} framework_include_dirs)
endforeach()

add_reflection_parser(
    target_name meta_framework
    reflection_search_files "${FRAMEWORK_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${framework_include_dirs}"
    parent_projects meta_core_generation meta_rhi_generation meta_asset_core_generation meta_physics_generation meta_render_generation
)
add_dependencies(EngineFramework meta_framework)
target_link_libraries(EngineFramework PRIVATE meta_framework)

# Engine modules already share the global output directory. Only SDL3 and
# libktx need copying from their own dependency output locations.
if (NOT EMSCRIPTEN)
    add_custom_command(TARGET EngineFramework POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:EngineFramework>
        COMMENT "Copying SDL3 and libktx to the runtime output directory"
    )
endif()

