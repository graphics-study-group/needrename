file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE CORE_HEADERS "./*.h")

if(WIN32)
    set(UUID_LIBRARIES rpcrt4)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    
    set(UUID_LIBRARIES ${UUID_LIBRARIES} ${UUID_LDFLAGS})
    include_directories(${UUID_INCLUDE_DIRS})
endif()

add_library(Core SHARED ${SOURCE})
target_compile_definitions(Core PRIVATE CORE_DLL_EXPORTS)
target_link_libraries(Core
    PUBLIC
    EngineLibHeaderInterface
    Reflection
    EngineDepGlm
    EngineDepSdl
    PRIVATE
    ${UUID_LIBRARIES}
)

include(${REFLECTION_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(CORE_HEADERS CORE_REFLECTION_HEADERS)

# Set up include dirs needed for parsing Core headers
set(all_include_dirs)
get_include_directories_for_target(Core all_include_dirs)

add_reflection_parser(
    target_name meta_core
    reflection_search_files "${CORE_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
)
add_dependencies(Core meta_core)
target_link_libraries(Core PRIVATE meta_core)
