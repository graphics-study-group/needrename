file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE CORE_HEADERS "./*.h")

if(WIN32)
    set(UUID_LIBRARIES rpcrt4)
elseif(EMSCRIPTEN)
    # wasm has neither libuuid nor a system UUID service: guid.cpp generates
    # its own, so nothing has to be linked.
    set(UUID_LIBRARIES "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    
    set(UUID_LIBRARIES ${UUID_LIBRARIES} ${UUID_LDFLAGS})
    include_directories(${UUID_INCLUDE_DIRS})
endif()

add_library(EngineCore ${SOURCE})
target_compile_definitions(EngineCore PRIVATE CORE_DLL_EXPORTS)

# The data root this build staged for the configuration being compiled.
#
# A developer run resolves the runtime data root through the tree it was built
# from, and the staged tree - not the source tree - is what the build produced:
# on a WebGPU build the built-in shaders exist only as baked WGSL/SPIR-V there,
# so reading the source assets would hand the runtime GLSL it cannot compile.
# `ENGINE_DATA_STAGE_DIR` carries `$<CONFIG>`, which is why this is a compile
# definition rather than a header value.
target_compile_definitions(EngineCore PRIVATE
    "ENGINE_BUILD_STAGE_DATA_ROOT=\"${ENGINE_DATA_STAGE_DIR}\""
)
target_link_libraries(EngineCore
    PUBLIC
    EngineLibHeaderInterface
    AnnoRefl
    EngineDepGlm
    EngineDepSdl
    PRIVATE
    EngineConfigHeader
    ${UUID_LIBRARIES}
)

include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(CORE_HEADERS CORE_REFLECTION_HEADERS)

# Set up include dirs needed for parsing Core headers
set(all_include_dirs)
get_include_directories_for_target(EngineCore all_include_dirs)

add_reflection_parser(
    target_name meta_core
    reflection_search_files "${CORE_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
)
add_dependencies(EngineCore meta_core)
target_link_libraries(EngineCore PRIVATE meta_core)


