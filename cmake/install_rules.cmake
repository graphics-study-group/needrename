# Installable distribution: portable prefix layout, install components, the
# external-consumption SDK package, and optional ZIP packaging.
#
# Layout:
#   bin/                  every executable and every shared library it loads
#   share/needrename/     the runtime data root (builtin_assets, projects,
#                         assets, spirv, test_data, reflection)
#   include/              SDK headers (engine, editor, reflection, vendored)
#   lib/                  import/static libraries and lib/cmake/needrename
#
# Components:
#   runtime        executables, shared libraries, built-in assets, physics SPIR-V
#   apps-data      project templates used by the applications
#   examples-data  sample content used by the example executables
#   test-data      test executables and their fixtures (ENGINE_INSTALL_TESTS=ON)
#   sdk            headers, libraries for linking, CMake package, reflection parser

include_guard(GLOBAL)
include(CMakePackageConfigHelpers)

set(ENGINE_SDK_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/needrename")
set(ENGINE_SDK_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/needrename")

# --- Exported target set ---------------------------------------------------
# Everything the in-tree `Engine` interface transitively passes to a consumer.
set(ENGINE_EXPORT_INTERFACE_TARGETS
    Engine
    EngineLibHeaderInterface
    EngineDepGlm
    EngineDepJson
    EngineDepSdl
    EngineDepVulkan
    EngineDepVulkanExport
    EngineDepImgui
    glm
    glm-header-only
    json
    vma
)

set(ENGINE_EXPORT_LIBRARY_TARGETS
    AnnoRefl
    EngineCore
    EngineRhi
    EngineAssetCore
    EnginePhysics
    EngineRender
    EngineFramework
    imgui
    glslang
    fastgltf
)
foreach(candidate EngineEditor PhysicsApp)
    if (TARGET ${candidate})
        list(APPEND ENGINE_EXPORT_LIBRARY_TARGETS ${candidate})
    endif()
endforeach()

install(TARGETS ${ENGINE_EXPORT_INTERFACE_TARGETS} EXPORT needrenameTargets)
install(
    TARGETS ${ENGINE_EXPORT_LIBRARY_TARGETS}
    EXPORT needrenameTargets
    RUNTIME DESTINATION bin COMPONENT runtime
    LIBRARY DESTINATION bin COMPONENT runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk
)

# Third-party shared libraries the engine modules load at runtime but that are
# not part of the public link interface.
set(ENGINE_RUNTIME_ONLY_TARGETS)
foreach(candidate ktx fastgltf)
    if (TARGET ${candidate})
        list(APPEND ENGINE_RUNTIME_ONLY_TARGETS ${candidate})
    endif()
endforeach()
if (ENGINE_RUNTIME_ONLY_TARGETS)
    install(
        TARGETS ${ENGINE_RUNTIME_ONLY_TARGETS}
        RUNTIME DESTINATION bin COMPONENT runtime
        LIBRARY DESTINATION bin COMPONENT runtime
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk
    )
endif()

# Example executables.
set(ENGINE_EXAMPLE_EXECUTABLES)
foreach(candidate external_resource_loading_example editor_run_game_example)
    if (TARGET ${candidate})
        list(APPEND ENGINE_EXAMPLE_EXECUTABLES ${candidate})
    endif()
endforeach()
if (ENGINE_EXAMPLE_EXECUTABLES)
    install(TARGETS ${ENGINE_EXAMPLE_EXECUTABLES} RUNTIME DESTINATION bin COMPONENT runtime)
endif()

# SDL3 is an imported target; its shared runtime has to travel with the
# executables. The Vulkan loader and driver are deliberately not bundled.
install(
    IMPORTED_RUNTIME_ARTIFACTS SDL3::SDL3-shared
    LIBRARY DESTINATION bin
    RUNTIME DESTINATION bin
    COMPONENT runtime
)

# --- Data components ------------------------------------------------------
# builtin_assets is staged and installed by the top-level CMakeLists (mode
# dependent); physics SPIR-V is installed by engine/Physics.

# Project templates: an application materialises its working project from here.
install(
    DIRECTORY "${PROJECTS_DIR}/"
    DESTINATION share/needrename/projects
    COMPONENT apps-data
    PATTERN "CMakeLists.txt" EXCLUDE
)

# Sample content for the examples. Documentation screenshots never ship.
install(
    DIRECTORY "${ASSETS_DIR}/"
    DESTINATION share/needrename/assets
    COMPONENT examples-data
    PATTERN "img" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
)

# --- SDK headers ----------------------------------------------------------
install(
    DIRECTORY "${ENGINE_SOURCE_DIR}/"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
    PATTERN "*.inl"
    PATTERN "*.tpp"
    PATTERN "Tests" EXCLUDE
)

install(
    DIRECTORY "${ENGINE_EDITOR_DIR}/Editor/"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}/Editor"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
    PATTERN "*.inl"
)

# Vendored headers the engine's public headers expose.
install(
    DIRECTORY "${THIRD_PARTY_DIR}/AnnoRefl/include/"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
    PATTERN "*.tpp"
    PATTERN "*.inl"
)

install(
    DIRECTORY "${THIRD_PARTY_DIR}/glm/"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}/glm"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
    PATTERN "*.inl"
)

install(
    DIRECTORY "${THIRD_PARTY_DIR}/json/nlohmann/"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}/json/nlohmann"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.hpp"
)

install(
    FILES "${THIRD_PARTY_DIR}/vma/vk_mem_alloc.h"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}/vma"
    COMPONENT sdk
)

install(
    DIRECTORY "${THIRD_PARTY_DIR}/imgui/"
    DESTINATION "${ENGINE_SDK_INCLUDE_DIR}/imgui"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "imgui*.h"
    PATTERN "imconfig*.h"
    PATTERN "backends/*.h"
)

# glslang keeps its own `<prefix>/include` install interface.
install(
    DIRECTORY "${THIRD_PARTY_DIR}/glslang/glslang/Public"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/glslang"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.h"
)
install(
    DIRECTORY "${THIRD_PARTY_DIR}/glslang/glslang/Include"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/glslang"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.h"
)

# fastgltf (imported by EngineFramework) declares its own `<prefix>/include`
# install interface, so its headers keep their `fastgltf/` layout there.
install(
    DIRECTORY "${THIRD_PARTY_DIR}/fastgltf/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT sdk
    FILES_MATCHING
    PATTERN "*.hpp"
)

# The physics application library's public headers.
if (TARGET PhysicsApp)
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/app/physics/"
        DESTINATION "${ENGINE_SDK_INCLUDE_DIR}/PhysicsApp"
        COMPONENT sdk
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
    )
endif()

# --- SDK reflection generation -------------------------------------------
# The parser scripts and templates, plus the per-module type lists (the
# reflection caches) consumer code generation reads as known engine types.
install(
    DIRECTORY "${THIRD_PARTY_DIR}/AnnoRefl/parser/"
    DESTINATION share/needrename/reflection/parser
    COMPONENT sdk
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)

foreach(module_cache IN ITEMS
        "${ENGINE_SOURCE_DIR}/Core/__generated__/meta_core"
        "${ENGINE_SOURCE_DIR}/Rhi/__generated__/meta_rhi"
        "${ENGINE_SOURCE_DIR}/Asset/__generated__/meta_asset_core"
        "${ENGINE_SOURCE_DIR}/Physics/__generated__/meta_physics"
        "${ENGINE_SOURCE_DIR}/Render/__generated__/meta_render"
        "${ENGINE_SOURCE_DIR}/Framework/__generated__/meta_framework"
        "${ENGINE_EDITOR_DIR}/__generated__/meta_editor")
    get_filename_component(module_cache_name "${module_cache}" NAME)
    install(
        FILES "${module_cache}/reflection_data.pkl"
        DESTINATION share/needrename/reflection/type_lists
        RENAME "${module_cache_name}.pkl"
        COMPONENT sdk
    )
endforeach()

# --- CMake package --------------------------------------------------------
# TODO: rename - the export set name, the `needrename::` namespace prefix, the
# package/config file names and the data-root directory name are rename points.
install(EXPORT needrenameTargets
    NAMESPACE needrename::
    FILE needrenameTargets.cmake
    DESTINATION "${ENGINE_SDK_CMAKE_DIR}"
    COMPONENT sdk
)

# TODO: rename - data-root directory name (also the NEEDRENAME_DATA_ROOT env var
# in Core/FileSystem/DataRoot.cpp).
set(NEEDRENAME_DATA_ROOT_REL "share/needrename")
# The relative name is substituted, not resolved, so the package stays
# relocatable: the config computes its prefix from its own location (see
# needrename-config.cmake.in). A PATH_VARS entry here would bake the
# configure-time CMAKE_INSTALL_PREFIX into the installed package.
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/needrename-config.cmake.in"
    "${PROJECT_BINARY_DIR}/needrename-config.cmake"
    INSTALL_DESTINATION "${ENGINE_SDK_CMAKE_DIR}"
)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/needrename-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${PROJECT_BINARY_DIR}/needrename-config.cmake"
        "${PROJECT_BINARY_DIR}/needrename-config-version.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/needrenameReflection.cmake"
    DESTINATION "${ENGINE_SDK_CMAKE_DIR}"
    COMPONENT sdk
)

# --- Tests ----------------------------------------------------------------
if (ENGINE_INSTALL_TESTS)
    get_property(engine_test_targets GLOBAL PROPERTY ENGINE_TEST_TARGETS)
    if (engine_test_targets)
        install(TARGETS ${engine_test_targets} RUNTIME DESTINATION bin COMPONENT test-data)
    endif()
    install(
        DIRECTORY "${TESTS_DIR}/"
        DESTINATION share/needrename/test_data
        COMPONENT test-data
        PATTERN "CMakeLists.txt" EXCLUDE
        PATTERN "__generated__" EXCLUDE
    )
endif()

# --- Packaging ------------------------------------------------------------
option(ENGINE_PACKAGE_ZIP "Produce a CPack ZIP archive of the install layout" ON)
if (ENGINE_PACKAGE_ZIP)
    if (WIN32)
        set(ENGINE_PACKAGE_PLATFORM "windows")
    elseif (APPLE)
        set(ENGINE_PACKAGE_PLATFORM "macos")
    else()
        set(ENGINE_PACKAGE_PLATFORM "linux")
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VENDOR "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${ENGINE_PACKAGE_PLATFORM}")
    # Unpacking the archive yields exactly the install layout, without an extra
    # top-level directory.
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
    include(CPack)
endif()
