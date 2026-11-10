# Python bindings for PhysicsApp (nanobind).
#
# Produces the extension module `needrename_sim._physicsapp`, which the
# `needrename-sim` distribution package imports after resolving the engine DLLs
# and the runtime data root. The interpreter is the project's
# `Python3_EXECUTABLE` — the same one the reflection parser uses — so the
# binding and the parser can never disagree about the Python version.

# The reflection parser already searched Python3; search again so this directory
# works if it is included on its own.
if (NOT Python3_EXECUTABLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# nanobind requires the FindPython `Python::Interpreter` / `Python::Module`
# targets (the reflection parser uses Python3 instead). Pin FindPython to the
# project interpreter so both agree.
set(Python_EXECUTABLE "${Python3_EXECUTABLE}")
find_package(Python 3.10 COMPONENTS Interpreter Development.Module REQUIRED)

# nanobind is consumed from the third_party submodule (a CMake package sitting
# in its source tree), not from an installed Python wheel.
set(nanobind_DIR "${THIRD_PARTY_DIR}/nanobind/cmake")
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_physicsapp NB_STATIC
    bindings.cpp
    value_types.cpp
    physics_app_bindings.cpp
)
target_link_libraries(_physicsapp PRIVATE PhysicsApp)
set_target_properties(_physicsapp PROPERTIES FOLDER engine_apps/python)

set(PHYSICSAPP_STUB_FILE "$<TARGET_FILE_DIR:_physicsapp>/_physicsapp.pyi")
# The stub is regenerated every build: stubgen is cheap, its output path depends on
# the target's configuration directory, and an always-run target keeps ordering
# simple. The package assembly does not depend on the file - it depends on the
# extension target - so a stub rewrite alone does not reassemble the package; a
# bindings change relinks the extension and does.
add_custom_target(generate_python_stubs ALL
    COMMAND "${Python3_EXECUTABLE}" "${THIRD_PARTY_DIR}/nanobind/src/stubgen.py"
        -m _physicsapp
        -i "$<TARGET_FILE_DIR:_physicsapp>"
        -o "${PHYSICSAPP_STUB_FILE}"
    DEPENDS _physicsapp
    COMMENT "Generating Python type stubs for _physicsapp"
    VERBATIM
)
set_target_properties(generate_python_stubs PROPERTIES FOLDER engine_apps/python)

# Installation: a wheel build (scikit-build-core sets SKBUILD) installs the
# extension and its stub straight into the package directory (`wheel.install-dir`),
# beside the Python sources and the runtime/data it resolves at import. A plain
# engine install keeps them under the data root instead of the prefix root.
#
# `LIBRARY` carries the module on every platform: a Python extension is a MODULE
# library, and CMake installs that artifact kind through the library destination
# even where it is a `.pyd`. `COMPONENT` must directly follow the destination -
# adding a `RUNTIME` destination in between moves the component onto it and leaves
# the module in the "Unspecified" group, which is how it silently stopped being
# installed.
if (SKBUILD)
    install(TARGETS _physicsapp LIBRARY DESTINATION . COMPONENT runtime)
    install(FILES "${PHYSICSAPP_STUB_FILE}" DESTINATION . COMPONENT runtime)
else()
    install(TARGETS _physicsapp LIBRARY DESTINATION share/needrename/python COMPONENT runtime)
    install(FILES "${PHYSICSAPP_STUB_FILE}" DESTINATION share/needrename/python COMPONENT runtime)
endif()

# The development package is assembled by the install rules: install components
# `runtime` and `apps-data` into a prefix, then copy the Python package sources
# beside them. The prefix differs from the build tree's (`output/<config>`), whose
# package root is the prefix itself, so this is the one place that assembles a
# second layout. Nothing here decides which files a distribution carries - the
# install rules do - so the package cannot drift from an install.
set(PYTHON_DEV_ROOT "${CMAKE_BINARY_DIR}/python/$<CONFIG>")
set(PYTHON_DEV_PACKAGE_DIR "${PYTHON_DEV_ROOT}/needrename_sim")
# Stamps are files, targets are names: a name orders the assembly after the target
# on every generator, a `$<TARGET_FILE:...>` does not.
set(PYTHON_RUNTIME_DEPENDS
    ${ENGINE_STAGED_DATA_STAMPS}
    _physicsapp
    PhysicsApp
    EngineFramework
    EnginePhysics
    EngineRender
    EngineRhi
    EngineCore
    EngineAssetCore
    AnnoRefl
)
if (TARGET EngineEditor)
    # The runtime component installs it, so the assembly has to wait for it.
    list(APPEND PYTHON_RUNTIME_DEPENDS EngineEditor)
endif()
needrename_stage_runtime(
    TARGET python_runtime
    PREFIX "${PYTHON_DEV_PACKAGE_DIR}"
    COMPONENTS runtime apps-data
    EXTRA_COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/needrename_sim" "${PYTHON_DEV_PACKAGE_DIR}"
    DEPENDS ${PYTHON_RUNTIME_DEPENDS}
)
# The stub is installed by the runtime component, so it has to exist before the
# assembly runs; the assembly itself does not depend on its timestamp.
add_dependencies(python_runtime generate_python_stubs)

set(PYTHON_DEV_ENVIRONMENT
    --unset=NEEDRENAME_RUNTIME_DIR
    --unset=NEEDRENAME_DATA_ROOT
    --unset=NEEDRENAME_BINDINGS_DIR
    "PYTHONPATH=${PYTHON_DEV_ROOT}"
)

if (ENGINE_BUILD_TESTS)
    add_test(
        NAME python_bindings
        COMMAND "${CMAKE_COMMAND}" -E env ${PYTHON_DEV_ENVIRONMENT}
            "${Python3_EXECUTABLE}" -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    set_tests_properties(python_bindings PROPERTIES
        LABELS python
        TIMEOUT 1200
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_custom_target(python_test
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            -C "$<CONFIG>" -R "^python_bindings$"
        DEPENDS python_runtime
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Running Python binding tests for $<CONFIG>"
        USES_TERMINAL
        VERBATIM
    )
    set_target_properties(python_test PROPERTIES FOLDER engine_apps/python)
endif()

set(NEEDRENAME_PYTHON_EXAMPLE_URDF
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimal_robot.urdf"
    CACHE FILEPATH "URDF used by the Python example run targets"
)
function(needrename_add_python_example target script)
    add_custom_target(${target}
        COMMAND "${CMAKE_COMMAND}" -E env ${PYTHON_DEV_ENVIRONMENT}
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/${script}" ${ARGN}
        DEPENDS python_runtime
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running ${script} with the $<CONFIG> Python development runtime"
        USES_TERMINAL
        VERBATIM
    )
    set_target_properties(${target} PROPERTIES FOLDER engine_apps/python)
endfunction()

needrename_add_python_example(python_example_rl rl_loop_sketch.py
    "${NEEDRENAME_PYTHON_EXAMPLE_URDF}" --episodes 1 --steps 16
)
needrename_add_python_example(python_example_headless headless_render.py
    "${NEEDRENAME_PYTHON_EXAMPLE_URDF}" --frames 2 --width 160 --height 120
    --out "${CMAKE_BINARY_DIR}/python-examples/$<CONFIG>/frames"
)
needrename_add_python_example(python_example_windowed windowed_robot.py
    "${NEEDRENAME_PYTHON_EXAMPLE_URDF}"
)
needrename_add_python_example(python_example_a1_stand a1_stand.py)

