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
if (SKBUILD)
    install(TARGETS _physicsapp LIBRARY DESTINATION . COMPONENT runtime)
    install(FILES "${PHYSICSAPP_STUB_FILE}" DESTINATION . COMPONENT runtime)
else()
    install(TARGETS _physicsapp LIBRARY DESTINATION share/needrename/python COMPONENT runtime)
    install(FILES "${PHYSICSAPP_STUB_FILE}" DESTINATION share/needrename/python COMPONENT runtime)
endif()

# The extension is linked in the normal per-configuration output directory next
# to its engine DLLs. The development package stages those artifacts through the
# same install components used by distributions, so its runtime inventory does
# not drift from the install layout.
set(PYTHON_DEV_ROOT "${CMAKE_BINARY_DIR}/python/$<CONFIG>")
set(PYTHON_DEV_PACKAGE_DIR "${PYTHON_DEV_ROOT}/needrename_sim")
set(PYTHON_RUNTIME_DEPENDENCIES generate_python_stubs builtin_assets_stage)
if (TARGET EngineEditor)
    list(APPEND PYTHON_RUNTIME_DEPENDENCIES EngineEditor)
endif()
add_custom_target(python_runtime ALL
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
        --config "$<CONFIG>" --prefix "${PYTHON_DEV_PACKAGE_DIR}" --component runtime
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
        --config "$<CONFIG>" --prefix "${PYTHON_DEV_PACKAGE_DIR}" --component apps-data
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/needrename_sim" "${PYTHON_DEV_PACKAGE_DIR}"
    DEPENDS ${PYTHON_RUNTIME_DEPENDENCIES}
    COMMENT "Staging the Python development runtime for $<CONFIG>"
    VERBATIM
)
set_target_properties(python_runtime PROPERTIES FOLDER engine_apps/python)

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

