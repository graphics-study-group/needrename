#ifndef ASSET_EXPORT_H
#define ASSET_EXPORT_H

#ifdef _WIN32
#ifdef ASSET_CORE_DLL_EXPORTS
#define ASSET_CORE_API __declspec(dllexport)
#else
#define ASSET_CORE_API __declspec(dllimport)
#endif
#else
#define ASSET_CORE_API
#endif

#endif
