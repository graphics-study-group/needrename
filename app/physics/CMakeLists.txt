message("COMPILE PhysicsApp")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

add_library(PhysicsApp SHARED ${SOURCE})
if(MSVC)
    # The app code has no dllexport annotations; export all symbols on
    # Windows so an import library is produced (Linux .so exports by default).
    set_target_properties(PhysicsApp PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_include_directories(PhysicsApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PhysicsApp PUBLIC Engine)

set_target_properties(PhysicsApp PROPERTIES FOLDER engine_apps)
