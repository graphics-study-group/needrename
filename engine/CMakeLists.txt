message("COMPILE engine library")
file(GLOB_RECURSE HEADER "./*.h")

list(FILTER HEADER EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADER EXCLUDE REGEX "Tests/.*")

# Header files and some deps are shared across these modules, so we use an interface to manage them
add_library(EngineLibHeaderInterface INTERFACE)
target_include_directories(EngineLibHeaderInterface INTERFACE ${ENGINE_SOURCE_DIR})
add_library(EngineLibExternalDependency INTERFACE)
target_link_libraries(EngineLibExternalDependency
    INTERFACE
    SDL3::SDL3
    Vulkan::Vulkan
    glm
    json
    imgui
    vma
    glslang
)
target_compile_definitions(EngineLibExternalDependency INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_definitions(EngineLibExternalDependency INTERFACE VULKAN_HPP_STORAGE_SHARED)
target_compile_definitions(EngineLibExternalDependency INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Root directory for physics shader SPIR-V artefacts (build tree only).
# Set before add_subdirectory(Physics) so the Physics subdir and the later
# configure_file() call both see it.
set(PHYSICS_SPIRV_ROOT "${CMAKE_BINARY_DIR}/engine/Physics/spirv")

add_subdirectory(Asset)
add_subdirectory(Core)
add_subdirectory(Framework)
add_subdirectory(Physics)
add_subdirectory(Reflection)
add_subdirectory(Render)
add_subdirectory(UserInterface)

add_library(engine SHARED 
    "MainClass.cpp"
    $<TARGET_OBJECTS:imgui>
    $<TARGET_OBJECTS:EngineLibAsset>
    $<TARGET_OBJECTS:EngineLibCore>
    $<TARGET_OBJECTS:EngineLibFramework>
    $<TARGET_OBJECTS:EngineLibPhysics>
    $<TARGET_OBJECTS:EngineLibReflection>
    $<TARGET_OBJECTS:EngineLibRender>
    $<TARGET_OBJECTS:EngineLibUserInterface>
)

add_dependencies(engine shader)
add_dependencies(engine physics_shader)

add_dependencies(EngineLibAsset meta_engine)
add_dependencies(EngineLibCore meta_engine)
add_dependencies(EngineLibFramework meta_engine)
add_dependencies(EngineLibPhysics meta_engine)
add_dependencies(EngineLibReflection meta_engine)
add_dependencies(EngineLibRender meta_engine)
add_dependencies(EngineLibUserInterface meta_engine)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/Reflection/
        ${CMAKE_CURRENT_SOURCE_DIR}/Render/
        ${CMAKE_CURRENT_SOURCE_DIR}/UserInterface/
    )
else()
    message("Doxygen need to be installed to generate the doxygen documentation")
endif()

# Widely used dependencies such as SDL and Vulkan are marked as public
target_link_libraries(engine
    PUBLIC
    EngineLibHeaderInterface
    EngineLibExternalDependency
)

# Dependencies only used in a small module are marked as private to avoid pollution
target_link_libraries(engine 
    PRIVATE
    EngineLibAsset
    EngineLibCore
    EngineLibFramework
    EngineLibPhysics
    EngineLibReflection
    EngineLibRender
    EngineLibUserInterface
)

# Copy third-party DLLs that are not natively built into the bin directory.
# SDL3 is an imported target from MSYS2; libktx overrides LIBRARY_OUTPUT_DIRECTORY.
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3-shared>
    $<TARGET_FILE:ktx>
    $<TARGET_FILE_DIR:engine>
    COMMENT "Copying SDL3 and libktx DLLs to output directory"
)

# TODO: Use relative paths when install.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

include(CTest)
add_subdirectory(Tests)
