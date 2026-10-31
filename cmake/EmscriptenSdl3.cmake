# SDL3 for the browser build.
#
# There is no SDL3 config package on the host that an Emscripten build could
# use: the library has to be compiled for wasm, and Emscripten supplies exactly
# that as a port. The port brings the headers into the sysroot and the
# JavaScript glue into the link, so both the compile and the link line have to
# request it.
#
# The imported targets mirror the names the desktop `find_package(SDL3)` call
# produces, so no other file has to know which shape is in use.

set(_emsdk_port_flag "--use-port=sdl3")

if (NOT TARGET SDL3::SDL3)
    add_library(SDL3::SDL3 INTERFACE IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES
        INTERFACE_COMPILE_OPTIONS "${_emsdk_port_flag}"
        INTERFACE_LINK_OPTIONS "${_emsdk_port_flag}"
    )
endif()

if (NOT TARGET SDL3::SDL3-shared)
    add_library(SDL3::SDL3-shared INTERFACE IMPORTED)
    set_target_properties(SDL3::SDL3-shared PROPERTIES
        INTERFACE_LINK_LIBRARIES SDL3::SDL3
    )
endif()
