# Native shader tooling: a small static library with exactly the sources the
# shader toolchain needs (GLSL compilation, SPIR-V reflection and rewriting,
# structured-buffer layout, WGSL/interface baking), plus the shader_baker
# executable on top of it. Nothing here links Engine, EngineRender or EngineRhi:
# the same sources are compiled into the runtime DLLs separately, with their own
# export annotations, and into this library for the tools.
#
# Every native build configures this subtree, before the engine, so every native
# preset builds its own shader_baker. Browser builds configure it never: they
# name a host baker (see the top-level host-baker resolution).

add_library(EngineShaderTooling STATIC
    ${ENGINE_SOURCE_DIR}/Render/Asset/Shader/ShaderCompiler.cpp
    ${ENGINE_SOURCE_DIR}/Render/Asset/Shader/ShaderIncluder.cpp
    ${ENGINE_SOURCE_DIR}/Rhi/Pipeline/SpirvShaderParameterLayout.cpp
    ${ENGINE_SOURCE_DIR}/Rhi/Buffer/StructuredBufferPlacer.cpp
    ${ENGINE_SOURCE_DIR}/Rhi/Buffer/StructuredBuffer.cpp
    spirv_edit.cpp
    wgsl_bake.cpp
)
target_include_directories(EngineShaderTooling PUBLIC "${ENGINE_SOURCE_DIR}")
target_compile_definitions(EngineShaderTooling
    PUBLIC
    # The tooling compiles GLSL like the Vulkan runtime does.
    ENGINE_SHADER_COMPILER_HAS_GLSLANG=1
    # Static-link shape: no DLL import/export annotations on Rhi/Render headers.
    RHI_STATIC=1
    RENDER_STATIC=1
    # The tooling has no runtime data root; the baker registers its include paths.
    ENGINE_SHADER_TOOLING=1
)
# The EngineDep* shims live in the engine subtree, which a host-tools configure
# never enters, so this target names the third-party dependencies directly.
target_link_libraries(EngineShaderTooling
    PUBLIC
    glm
    json
    SDL3::SDL3
    AnnoRefl
    glslang
    spirv-cross-cpp
)
set_target_properties(EngineShaderTooling PROPERTIES FOLDER tools)

# shader_baker rewrites the built-in shader asset archives from GLSL to
# embedded-SPIR-V form, bakes WGSL variants and interface descriptions, and
# compiles single GLSL sources (the physics pipeline) - all through the same
# compiler and reflection code the runtimes use.
add_executable(shader_baker
    main.cpp
)
target_link_libraries(shader_baker PRIVATE EngineShaderTooling)
# The tooling is a self-contained host tool: it never links Dawn and always
# keeps its default runtime, including in a Release-only Dawn build.
set_target_properties(EngineShaderTooling shader_baker PROPERTIES ENGINE_DEFAULT_RUNTIME ON)
# The baker links SDL3 shared; every place that invokes it (this tree's shader
# commands, a browser cross-build naming the host-tools baker) expects it to run
# in place, so its runtime DLLs land next to the executable.
add_custom_command(TARGET shader_baker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:shader_baker> $<TARGET_RUNTIME_DLLS:shader_baker>
    COMMAND_EXPAND_LISTS
)
set_target_properties(shader_baker PROPERTIES FOLDER tools)
