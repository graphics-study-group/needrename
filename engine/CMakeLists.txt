###########################################
# Common interface and dependency shims   #
###########################################

# Header files are shared across all engine modules; expose the engine source
# root as a single INTERFACE so every module can include its peers.
add_library(EngineLibHeaderInterface INTERFACE)
target_include_directories(EngineLibHeaderInterface INTERFACE ${ENGINE_SOURCE_DIR})

# Granular external dependency targets - each DLL links only what it needs.
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

###########################################
# Module DLLs (in dependency order)       #
###########################################

# Root directory for physics shader SPIR-V artefacts (build tree only).
# Set before add_subdirectory(Physics) so the Physics subdir and the later
# configure_file() call both see it.
set(PHYSICS_SPIRV_ROOT "${CMAKE_BINARY_DIR}/engine/Physics/spirv")

add_subdirectory(Core)
add_subdirectory(Rhi)
add_subdirectory(Asset)
add_subdirectory(Physics)
add_subdirectory(Render)
add_subdirectory(Framework)

###########################################
# Engine aggregate interface              #
###########################################

# Consumers link `Engine` only; it forwards to every module DLL and its headers.
add_library(Engine INTERFACE)
target_link_libraries(Engine
    INTERFACE
    EngineLibHeaderInterface
    EngineDepGlm
    EngineDepJson
    EngineDepSdl
    EngineDepVulkan
    EngineDepImgui
    AnnoRefl
    EngineCore
    EngineRhi
    EngineAssetCore
    EnginePhysics
    EngineRender
    EngineFramework
)

###########################################
# Build-tree configuration                #
###########################################

# TODO: Use relative paths when install.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

###########################################
# Documentation                           #
###########################################

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
    )
else()
    message("Doxygen need to be installed to generate the doxygen documentation")
endif()


