#ifndef FRAMEWORK_EXPORT_H
#define FRAMEWORK_EXPORT_H

#ifdef _WIN32
#ifdef FRAMEWORK_DLL_EXPORTS
#define FRAMEWORK_API __declspec(dllexport)
#else
#define FRAMEWORK_API __declspec(dllimport)
#endif
#else
#define FRAMEWORK_API
#endif

#endif
