add_subdirectory(glm EXCLUDE_FROM_ALL)
set_target_properties(glm PROPERTIES FOLDER third_party)

add_subdirectory(json EXCLUDE_FROM_ALL)
set_target_properties(json PROPERTIES FOLDER third_party)

add_subdirectory(stb EXCLUDE_FROM_ALL)
set_target_properties(stb PROPERTIES FOLDER third_party)

add_subdirectory(tinyobjloader EXCLUDE_FROM_ALL)
set_target_properties(tinyobjloader PROPERTIES FOLDER third_party)

add_subdirectory(vma EXCLUDE_FROM_ALL)
set_target_properties(vma PROPERTIES FOLDER third_party)

# For some reasons the SPIRV-Cross in Vulkan SDK cannot be found by CMake...
set(SPIRV_CROSS_CLI OFF CACHE BOOL "Build the CLI binary. Requires SPIRV_CROSS_STATIC." FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "Enable SPIRV-Cross tests." FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "Enable HLSL target support." FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "Enable MSL target support." FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "Enable C API wrapper support in static library." FORCE)
add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
set_target_properties(spirv-cross-core PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-cpp PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-glsl PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-reflect PROPERTIES FOLDER third_party)
set_target_properties(spirv-cross-util PROPERTIES FOLDER third_party)

# Add ImGUI files
file(GLOB IMGUI_SOURCE 
    "${THIRD_PARTY_DIR}/imgui/*.cpp" 
    "${THIRD_PARTY_DIR}/imgui/backends/*_vulkan.cpp"
    "${THIRD_PARTY_DIR}/imgui/backends/*_sdl3.cpp"
)
message(DEBUG "Found ImGUI files: ${IMGUI_SOURCE}")
add_library(imgui OBJECT EXCLUDE_FROM_ALL ${IMGUI_SOURCE})
set_target_properties(imgui PROPERTIES FOLDER third_party)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
target_include_directories(
    imgui 
    SYSTEM PUBLIC 
    "${THIRD_PARTY_DIR}/imgui/"
    "${THIRD_PARTY_DIR}/imgui/backends/"
)

include(FetchContent)

# -----------------------------
# 1. Fetch SPIRV-Headers（SPIRV-Tools 依赖）
# -----------------------------
FetchContent_Declare(
    spirv_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG vulkan-sdk-1.4.335
    GIT_SUBMODULES "" # 确保拉取子模块
)
if(NOT spirv_headers_POPULATED)
    FetchContent_Populate(spirv_headers)
    set(SPIRV_HEADERS_ENABLE_TESTS OFF CACHE BOOL "Disable tests" FORCE)
    set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "Disable install" FORCE)
    add_subdirectory(${spirv_headers_SOURCE_DIR} ${spirv_headers_BINARY_DIR})
endif()

# -----------------------------
# 2. Fetch SPIRV-Tools
# -----------------------------
FetchContent_Declare(
    spirv_tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG v2025.5
)
FetchContent_GetProperties(spirv_tools)
if(NOT spirv_tools_POPULATED)
    FetchContent_Populate(spirv_tools)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static lib")
    set(SPIRV_TOOLS_BUILD_TESTS OFF CACHE BOOL "Disable tests")
    set(SPIRV_WERROR OFF CACHE BOOL "Disable warnings as errors")
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "Don't build spirv-tools executables")
    add_subdirectory(${spirv_tools_SOURCE_DIR} ${spirv_tools_BINARY_DIR})


    # file(GLOB SPIRV_TOOLS_SOURCE_FILES
    #     "${spirv_tools_SOURCE_DIR}/source/opt/*.cpp"
    # )
    # add_library(SPIRV-Tools-opt SHARED ${SPIRV_TOOLS_SOURCE_FILES})
    # target_include_directories(SPIRV-Tools-opt PUBLIC 
    #     "${spirv_tools_SOURCE_DIR}"
    #     "${spirv_tools_SOURCE_DIR}/include"
    # )
    # target_link_libraries(SPIRV-Tools-opt PUBLIC SPIRV-Headers)
    # target_include_directories(SPIRV-Tools-opt PRIVATE 
    #     "${spirv_headers_SOURCE_DIR}/include/spirv/unified1"
    # )
endif()

# -----------------------------
# 3. Fetch glslang
# -----------------------------
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 16.1.0
)
FetchContent_GetProperties(glslang)
if(NOT glslang_POPULATED)
    FetchContent_Populate(glslang)

    # 禁用不需要的功能，启用优化
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static lib")
    set(BUILD_TESTING OFF CACHE BOOL "Disable tests")
    set(ENABLE_HLSL OFF CACHE BOOL "Disable HLSL support")
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Don't build executables")
    set(ENABLE_OPT ON CACHE BOOL "Enable SPIR-V optimizer")
    set(ALLOW_EXTERNAL_SPIRV_TOOLS ON CACHE BOOL "Use our fetched SPIRV-Tools")

    add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR})
endif()
