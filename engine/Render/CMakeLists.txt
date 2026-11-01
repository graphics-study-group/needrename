# Only the selected backend is compiled. Everything outside Backend/ is shared.
file(GLOB_RECURSE RENDER_ALL_SOURCES "./*.cpp")
set(SOURCE "")
foreach(_src IN LISTS RENDER_ALL_SOURCES)
    if (ENGINE_RHI_BACKEND_VULKAN AND _src MATCHES "/Backend/WebGPU/")
        continue()
    endif()
    if (ENGINE_RHI_BACKEND_WEBGPU AND _src MATCHES "/Backend/Vulkan/")
        continue()
    endif()
    list(APPEND SOURCE "${_src}")
endforeach()
file(GLOB_RECURSE HEADER_PRIVATE "./*.hpp")

add_library(EngineRender SHARED ${SOURCE})
target_compile_definitions(EngineRender PRIVATE RENDER_DLL_EXPORTS)
target_link_libraries(EngineRender
    PUBLIC
    EngineLibHeaderInterface
    AnnoRefl
    EngineDepGlm
    PRIVATE
    EngineAssetCore
    EngineRhi
    EngineCore
    EngineConfigHeader
    EngineDepRhiBackend
    EngineDepSdl
    EngineDepImgui
    glslang
    ktx
    stb
)
target_precompile_headers(EngineRender PRIVATE "FullRenderSystem.h")

# Reflection: per-DLL registration for Render types.
file(GLOB_RECURSE RENDER_HEADERS "./*.h")
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(RENDER_HEADERS RENDER_REFLECTION_HEADERS)

set(render_include_dirs)
foreach(dep IN ITEMS
    EngineLibHeaderInterface AnnoRefl EngineDepGlm EngineDepJson EngineDepSdl
    EngineDepRhiBackend EngineDepImgui ktx stb)
    get_include_directories_for_target(${dep} render_include_dirs)
endforeach()

add_reflection_parser(
    target_name meta_render
    reflection_search_files "${RENDER_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${render_include_dirs}"
    parent_projects meta_core_generation meta_rhi_generation meta_asset_core_generation
)
add_dependencies(EngineRender meta_render)
target_link_libraries(EngineRender PRIVATE meta_render)
