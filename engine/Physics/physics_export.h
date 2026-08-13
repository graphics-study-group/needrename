#ifndef PHYSICS_EXPORT_H
#define PHYSICS_EXPORT_H

#ifdef _WIN32
#ifdef PHYSICS_DLL_EXPORTS
#define PHYSICS_API __declspec(dllexport)
#else
#define PHYSICS_API __declspec(dllimport)
#endif
#else
#define PHYSICS_API
#endif

#endif
