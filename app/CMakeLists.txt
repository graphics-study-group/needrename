# Both apps are backend-neutral: the editor reaches the backend only through the RHI seam and the
# GUI system, and the physics app owns no backend type at all. Each is built for whichever backend
# the tree selects.
add_subdirectory(editor)
add_subdirectory(physics)
