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
set_target_properties(_physicsapp PROPERTIES FOLDER engine_apps)

# Installation: a wheel build (scikit-build-core sets SKBUILD) installs the
# extension straight into the package directory (`wheel.install-dir`), beside the
# Python sources and the runtime/data it resolves at import. A plain engine
# install keeps it under the data root instead of the prefix root.
if (SKBUILD)
    install(TARGETS _physicsapp LIBRARY DESTINATION . COMPONENT runtime)
else()
    install(TARGETS _physicsapp LIBRARY DESTINATION share/needrename/python COMPONENT runtime)
endif()

# No output-directory override: the extension lands in the project's normal
# per-configuration output directory (`<build>/bin/<Config>`), next to the engine
# DLLs it links, so Debug and Release never overwrite each other and a config can
# never mix with the other configuration's DLLs. A development run imports the
# package sources straight from `app/python/src` (`PYTHONPATH`); the package's
# `__init__.py` then picks the extension up from beside the engine DLLs. Nothing
# is staged or copied next to the sources. See README.md.

