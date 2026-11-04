message("COMPILE PhysicsApp")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

# A browser has no shared libraries: Emscripten builds a SHARED target as a side module, and a
# side module linked into a wasm image resolves its symbols to null - the first call through one traps
# with RuntimeError: null function. The archive is what a static link needs, so the browser gets one.
if (EMSCRIPTEN)
    add_library(PhysicsApp STATIC ${SOURCE})
else()
    add_library(PhysicsApp SHARED ${SOURCE})
endif()
if(MSVC)
    # The app code has no dllexport annotations; export all symbols on
    # Windows so an import library is produced (Linux .so exports by default).
    set_target_properties(PhysicsApp PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_include_directories(PhysicsApp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/needrename/PhysicsApp>"
)
target_link_libraries(PhysicsApp PUBLIC Engine)

set_target_properties(PhysicsApp PROPERTIES FOLDER engine_apps)
