# Both apps are backend-neutral: the editor reaches the backend only through the RHI seam and the
# GUI system, and the physics app owns no backend type at all. Each is built for whichever backend
# the tree selects.
add_subdirectory(editor)
add_subdirectory(physics)

# The Python bindings are an application of the engine: they link PhysicsApp, so
# they are configured after the physics app target.
if (ENGINE_BUILD_PYTHON_BINDINGS)
    add_subdirectory(python)
endif()
