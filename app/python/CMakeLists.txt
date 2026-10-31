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

# Installation: a wheel build (scikit-build-core sets SKBUILD) installs the
# extension straight into the package directory (`wheel.install-dir`), beside the
# Python sources and the runtime/data it resolves at import. A plain engine
# install keeps it under the data root instead of the prefix root.
if (SKBUILD)
    install(TARGETS _physicsapp LIBRARY DESTINATION . COMPONENT runtime)
else()
    install(TARGETS _physicsapp LIBRARY DESTINATION share/needrename/python COMPONENT runtime)
endif()

# Dev-run package tree: the built `.pyd` lives next to the Python sources of the
# package, so `PYTHONPATH=<build>/python` makes `import needrename_sim` work
# against a build tree. The wheel stage (task 8.1) installs from the same layout.
set(NEEDRENAME_PYTHON_DEV_DIR "${CMAKE_BINARY_DIR}/python")
set(NEEDRENAME_PYTHON_DEV_PACKAGE "${NEEDRENAME_PYTHON_DEV_DIR}/needrename_sim")

set_target_properties(_physicsapp PROPERTIES
    FOLDER engine_apps
    LIBRARY_OUTPUT_DIRECTORY "${NEEDRENAME_PYTHON_DEV_PACKAGE}"
    RUNTIME_OUTPUT_DIRECTORY "${NEEDRENAME_PYTHON_DEV_PACKAGE}"
)
# Multi-config generators append the configuration name unless a per-config
# output directory is set; the package tree is configuration-independent.
foreach(CONFIG_NAME Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${CONFIG_NAME}" CONFIG_NAME_UPPER)
    set_target_properties(_physicsapp PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${NEEDRENAME_PYTHON_DEV_PACKAGE}"
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${NEEDRENAME_PYTHON_DEV_PACKAGE}"
    )
endforeach()

file(GLOB NEEDRENAME_PYTHON_SOURCES CONFIGURE_DEPENDS
    "${ENGINE_ROOT_DIR}/app/python/src/needrename_sim/*.py"
)
if (NEEDRENAME_PYTHON_SOURCES)
    add_custom_target(python_package_stage ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NEEDRENAME_PYTHON_DEV_PACKAGE}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NEEDRENAME_PYTHON_SOURCES} "${NEEDRENAME_PYTHON_DEV_PACKAGE}"
        DEPENDS ${NEEDRENAME_PYTHON_SOURCES}
        COMMENT "Staging the needrename_sim Python package for dev runs"
        VERBATIM
    )
    add_dependencies(_physicsapp python_package_stage)
endif()
