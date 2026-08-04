#ifndef CORE_EXPORT_H
#define CORE_EXPORT_H

#ifdef _WIN32
#ifdef CORE_DLL_EXPORTS
#define CORE_API __declspec(dllexport)
#else
#define CORE_API __declspec(dllimport)
#endif
#else
#define CORE_API
#endif

#endif
