file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibCore
    OBJECT
    ${SOURCE}
)

if(WIN32)
    set(UUID_LIBRARIES rpcrt4)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    
    set(UUID_LIBRARIES ${UUID_LIBRARIES} ${UUID_LDFLAGS})
    include_directories(${UUID_INCLUDE_DIRS})
endif()

target_link_libraries(EngineLibCore 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
    ${UUID_LIBRARIES}
)
