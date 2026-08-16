#ifndef RENDER_EXPORT_H
#define RENDER_EXPORT_H

#ifdef _WIN32
#ifdef RENDER_DLL_EXPORTS
#define RENDER_API __declspec(dllexport)
#else
#define RENDER_API __declspec(dllimport)
#endif
#else
#define RENDER_API
#endif

#endif
