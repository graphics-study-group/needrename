#ifndef GPU_CONTEXT_EXPORT_H
#define GPU_CONTEXT_EXPORT_H

// Phase 1 (OBJECT merge): Rhi symbols live inside engine.dll, which is linked
// with MinGW's default --export-all-symbols (auto-import handles consumers).
// A dllimport attribute here would turn class members into import references,
// stripping them from the export table. The macro therefore expands empty
// unless GPU_CONTEXT_DLL_EXPORTS is defined (Phase 4: Rhi becomes a standalone
// DLL again and this file returns to the dllexport/dllimport pair).

#ifdef _WIN32
#ifdef GPU_CONTEXT_DLL_EXPORTS
#define GPU_CONTEXT_API __declspec(dllexport)
#else
#define GPU_CONTEXT_API
#endif
#else
#define GPU_CONTEXT_API
#endif

#endif