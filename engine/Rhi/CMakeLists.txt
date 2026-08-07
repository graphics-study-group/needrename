file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE RHI_HEADERS "./*.h")

# Phase 1: OBJECT library merged into engine.dll (Rhi is not yet a standalone
# DLL; the RenderSystem constructor dependencies are still present and will be
# removed in Phase 3). Phase 4 converts this back to a SHARED library.
# NOTE: GPU_CONTEXT_DLL_EXPORTS must NOT be defined here — a dllexport symbol
# anywhere in the link disables MinGW ld's default --export-all-symbols, which
# would strip MainClass and other engine symbols from the export table.
# Phase 4 defines it again when Rhi becomes its own DLL.
add_library(EngineLibRhi OBJECT ${SOURCE})
target_link_libraries(EngineLibRhi
    PRIVATE
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
target_link_libraries(EngineLibRhi
    PUBLIC
    spirv-cross-cpp
    vma
)
