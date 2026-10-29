# Installable distribution: portable prefix layout, install components, and
# optional ZIP packaging.
#
# Layout:
#   bin/                  every executable and every shared library it loads
#   share/needrename/     the runtime data root (builtin_assets, projects,
#                         assets, spv, test_data, reflection)
#
# Components:
#   runtime        executables, shared libraries, built-in assets, physics SPIR-V
#   apps-data      project templates used by the applications
#   examples-data  sample content used by the example executables
#   test-data      test executables and their fixtures (ENGINE_INSTALL_TESTS=ON)
#   sdk            headers, import libraries, CMake package, reflection parser

include_guard(GLOBAL)

# --- Executables and shared libraries -------------------------------------
set(ENGINE_RUNTIME_TARGETS
    AnnoRefl
    EngineCore
    EngineRhi
    EngineAssetCore
    EnginePhysics
    EngineRender
    EngineFramework
    imgui
)
# Third-party shared libraries that the engine modules link at runtime too:
# ktx (texture transcoding), imgui, fastgltf (glTF import) and glslang (runtime
# shader compilation). All of them ship next to the executables.
foreach(candidate ktx fastgltf glslang EngineEditor PhysicsApp
                  external_resource_loading_example editor_run_game_example)
    if (TARGET ${candidate})
        list(APPEND ENGINE_RUNTIME_TARGETS ${candidate})
    endif()
endforeach()

install(
    TARGETS ${ENGINE_RUNTIME_TARGETS}
    RUNTIME DESTINATION bin COMPONENT runtime
    LIBRARY DESTINATION bin COMPONENT runtime
    ARCHIVE DESTINATION lib COMPONENT sdk
)

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
