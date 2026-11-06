#ifndef RHI_EXPORT_H
#define RHI_EXPORT_H

// RHI_STATIC marks a target that compiles these sources into itself (the native
// shader tooling) instead of linking the EngineRhi DLL: no import/export
// annotations are emitted there.
#if defined(_WIN32) && !defined(RHI_STATIC)
#ifdef RHI_DLL_EXPORTS
#define RHI_API __declspec(dllexport)
#else
#define RHI_API __declspec(dllimport)
#endif
#else
#define RHI_API
#endif

#endif
