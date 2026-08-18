message("COMPILE PhysicsApp")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

add_library(PhysicsApp SHARED ${SOURCE})
target_include_directories(PhysicsApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PhysicsApp PUBLIC Engine)

set_target_properties(PhysicsApp PROPERTIES FOLDER engine_apps)
