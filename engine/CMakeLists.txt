###########################################
# Thirdparty and common interface setting #
###########################################

# Header files and some deps are shared across these modules, so we use an interface to manage them
add_library(EngineLibHeaderInterface INTERFACE)
target_include_directories(EngineLibHeaderInterface INTERFACE ${ENGINE_SOURCE_DIR})

# Granular external dependency targets - each DLL links only what it needs
add_library(EngineDepGlm INTERFACE)
target_link_libraries(EngineDepGlm INTERFACE glm)
target_compile_definitions(EngineDepGlm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_library(EngineDepJson INTERFACE)
target_link_libraries(EngineDepJson INTERFACE json)

add_library(EngineDepSdl INTERFACE)
target_link_libraries(EngineDepSdl INTERFACE SDL3::SDL3)

# Consumers of the Rhi DLL import the dynamic dispatch loader from it.
add_library(EngineDepVulkan INTERFACE)
target_link_libraries(EngineDepVulkan INTERFACE Vulkan::Vulkan vma glslang)
target_compile_definitions(EngineDepVulkan INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
if (WIN32 AND (NOT MSVC))
    target_compile_definitions(EngineDepVulkan INTERFACE "VULKAN_HPP_STORAGE_API=__declspec(dllimport)")
else()
    target_compile_definitions(EngineDepVulkan INTERFACE VULKAN_HPP_STORAGE_SHARED=1)
endif()

add_library(EngineDepImgui INTERFACE)
target_link_libraries(EngineDepImgui INTERFACE imgui)

# Backward-compatible aggregate for remaining OBJECT libraries still in engine.dll
add_library(EngineLibExternalDependency INTERFACE)
target_link_libraries(EngineLibExternalDependency
    INTERFACE
    EngineDepGlm
    EngineDepJson
    EngineDepSdl
    EngineDepVulkan
    EngineDepImgui
    AnnoRefl
)

###########################################
# The parts that are already separated    #
###########################################

add_subdirectory(Core)
add_subdirectory(Rhi)

#################################################
# The parts that are still mixed in engine      #
# (needs to be refactored in the future)        #
#################################################

# Root directory for physics shader SPIR-V artefacts (build tree only).
# Set before add_subdirectory(Physics) so the Physics subdir and the later
# configure_file() call both see it.
set(PHYSICS_SPIRV_ROOT "${CMAKE_BINARY_DIR}/engine/Physics/spirv")

add_subdirectory(Asset)
add_subdirectory(Framework)
add_subdirectory(Physics)
add_subdirectory(Render)
add_subdirectory(UserInterface)

add_library(Engine SHARED 
    "MainClass.cpp"
    $<TARGET_OBJECTS:imgui>
    $<TARGET_OBJECTS:EngineLibFramework>
    $<TARGET_OBJECTS:EngineLibPhysics>
    $<TARGET_OBJECTS:EngineLibRender>
    $<TARGET_OBJECTS:EngineLibUserInterface>
)

# Widely used dependencies such as SDL and Vulkan are marked as public
target_link_libraries(Engine
    PUBLIC
    EngineLibHeaderInterface
    EngineLibExternalDependency
    EngineCore
    EngineRhi
    EngineAssetCore
)

# Dependencies only used in a small module are marked as private to avoid pollution
target_link_libraries(Engine 
    PRIVATE
    EngineLibFramework
    EngineLibPhysics
    EngineLibRender
    EngineLibUserInterface
)

# Copy third-party DLLs that are not natively built into the bin directory.
# SDL3 is an imported target from MSYS2; libktx overrides LIBRARY_OUTPUT_DIRECTORY.
add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3-shared>
    $<TARGET_FILE:ktx>
    $<TARGET_FILE:EngineCore>
    $<TARGET_FILE:EngineAssetCore>
    $<TARGET_FILE_DIR:Engine>
    COMMENT "Copying SDL3, libktx, and EngineCore DLLs to output directory"
)

# TODO: Use relative paths when install.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

# Setup meta parser for engine reflection
include(${ANROREFL_PARSER_DIR}/parser.cmake)
set(all_include_dirs)
get_include_directories_for_target(Engine all_include_dirs)

file(GLOB_RECURSE HEADERS "./*.h")
# Exclude the parts that are already separated
list(FILTER HEADERS EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADERS EXCLUDE REGEX "Tests/.*")
list(FILTER HEADERS EXCLUDE REGEX "Core/.*")
list(FILTER HEADERS EXCLUDE REGEX "Rhi/.*")
# Asset core types are registered by meta_asset_core in EngineAssetCore
list(FILTER HEADERS EXCLUDE REGEX "Asset/Asset\\.h")
list(FILTER HEADERS EXCLUDE REGEX "Asset/AssetRef\\.h")
filter_files_with_reflection_macros(HEADERS REFLECTION_SEARCH_HEADERS)

add_reflection_parser(
    target_name meta_engine
    reflection_search_files "${REFLECTION_SEARCH_HEADERS}"
    generated_code_dir "${ENGINE_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}" "${all_ext_include_dirs}"
    parent_projects meta_core_generation
)
add_dependencies(Engine meta_engine)
target_link_libraries(Engine PRIVATE meta_engine)

add_dependencies(Engine shader)
add_dependencies(Engine physics_shader)

add_dependencies(EngineLibFramework meta_engine)
add_dependencies(EngineLibPhysics meta_engine)
add_dependencies(EngineLibRender meta_engine)
add_dependencies(EngineLibUserInterface meta_engine)

# Doxygen support
find_package(Doxygen)
if(Doxygen_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
    # Only *.h files contain public interfaces
    set(DOXYGEN_FILE_PATTERNS *.h)
    # Expand reflection and attribute macros
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
    set(DOXYGEN_PREDEFINED
        "__declspec(x)="
        "__attribute__(x)="
        "REFL_SER_CLASS(...)="
        "REFL_SER_BODY(...)="
        "REFL_SER_BODY_OVERRIDE(...)="
        "REFL_SER_BODY_FINAL(...)="
        "REFL_SER_SIMPLE_STRUCT(...)="
    )
    set(DOXYGEN_EXPAND_AS_DEFINED
        "COPY_ENUM_VALUE"
    )

    doxygen_add_docs(
        engine_doxygen_doc
        ${CMAKE_CURRENT_SOURCE_DIR}/Asset/
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/
        ${CMAKE_CURRENT_SOURCE_DIR}/Framework/
        ${CMAKE_CURRENT_SOURCE_DIR}/Render/
        ${CMAKE_CURRENT_SOURCE_DIR}/UserInterface/
    )
else()
    message("Doxygen need to be installed to generate the doxygen documentation")
endif()

# CTest support
include(CTest)
add_subdirectory(Tests)
