#ifndef RHI_EXPORT_H
#define RHI_EXPORT_H

#ifdef _WIN32
#ifdef RHI_DLL_EXPORTS
#define RHI_API __declspec(dllexport)
#else
#define RHI_API __declspec(dllimport)
#endif
#else
#define RHI_API
#endif

#endif
