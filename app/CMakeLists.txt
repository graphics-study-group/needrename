add_subdirectory(editor)
add_subdirectory(physics)

# The Python bindings are an application of the engine: they link PhysicsApp, so
# they are configured after the physics app target.
if (ENGINE_BUILD_PYTHON_BINDINGS)
    add_subdirectory(python)
endif()
