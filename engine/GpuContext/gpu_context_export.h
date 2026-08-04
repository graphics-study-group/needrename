#ifndef GPU_CONTEXT_EXPORT_H
#define GPU_CONTEXT_EXPORT_H

#ifdef _WIN32
#ifdef GPU_CONTEXT_DLL_EXPORTS
#define GPU_CONTEXT_API __declspec(dllexport)
#else
#define GPU_CONTEXT_API __declspec(dllimport)
#endif
#else
#define GPU_CONTEXT_API
#endif

#endif
