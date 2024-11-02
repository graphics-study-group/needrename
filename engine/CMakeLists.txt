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

# Get all the include directories from the engine and its dependencies
function(get_include_directories_for_target target include_dirs)
    get_target_property(dirs ${target} INCLUDE_DIRECTORIES)
    get_target_property(interface_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if (dirs)
        foreach(dir ${dirs})
            if (EXISTS ${dir})
                list(APPEND ${include_dirs} ${dir})
            endif()
        endforeach()
    endif()
    if (interface_dirs)
        foreach(dir ${interface_dirs})
            if (EXISTS ${dir})
                list(APPEND ${include_dirs} ${dir})
            endif()
        endforeach()
    endif()
    get_target_property(deps ${target} INTERFACE_LINK_LIBRARIES)
    if (deps)
        foreach(dep ${deps})
            if (TARGET ${dep})
                get_include_directories_for_target(${dep} ${include_dirs})
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES ${include_dirs})
    set(${include_dirs} ${${include_dirs}} PARENT_SCOPE)
endfunction()

# set reflection search files and include directories
set(all_include_dirs)
get_include_directories_for_target(engine all_include_dirs)
set(REFLECTION_SEARCH_FILES "${REFLECTION_SEARCH_FILES};${HEADER}" CACHE STRING "" FORCE)
set(REFLECTION_SEARCH_INCLUDE_DIRS "${REFLECTION_SEARCH_INCLUDE_DIRS};${all_include_dirs}" CACHE STRING "" FORCE)
