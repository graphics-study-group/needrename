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
    # Runtime GLSL compilation is Vulkan's path. The other backend ships baked SPIR-V, so the
    # compiler and its glslang includer are not built there at all - which is what lets the
    # browser link without glslang. The stub that keeps ShaderCompiler's interface exists is
    # the mirror case and is not built on Vulkan.
    if (ENGINE_RHI_BACKEND_WEBGPU AND _src MATCHES "/Asset/Shader/(ShaderCompiler|ShaderIncluder)\\.cpp$")
        continue()
    endif()
    if (ENGINE_RHI_BACKEND_VULKAN AND _src MATCHES "/Asset/Shader/ShaderCompilerUnavailable\\.cpp$")
        continue()
    endif()
    # The render graph records its ordering as Vulkan transitions. Until the barrier work
    # lands, this backend has no graph builder to compile - the alternative would be 47
    # translations that emit nothing.
    if (ENGINE_RHI_BACKEND_WEBGPU AND _src MATCHES "/Pipeline/RenderGraph/RenderGraphBuilder\\.cpp$")
        continue()
    endif()
    if (ENGINE_RHI_BACKEND_VULKAN AND _src MATCHES "/Backend/WebGPU/Pipeline/RenderGraphBuilderUnavailable\\.cpp$")
        continue()
    endif()
    list(APPEND SOURCE "${_src}")
endforeach()
file(GLOB_RECURSE HEADER_PRIVATE "./*.hpp")

# glslang is only needed where shaders are compiled at run time.
set(GLSLANG_DEP "")
if (ENGINE_RHI_BACKEND_VULKAN)
    set(GLSLANG_DEP glslang)
endif()

add_library(EngineRender ${SOURCE})
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
    ${GLSLANG_DEP}
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


