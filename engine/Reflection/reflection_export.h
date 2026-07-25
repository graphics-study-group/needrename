#ifndef REFLECTION_EXPORT_H
#define REFLECTION_EXPORT_H

#ifdef _WIN32
  #ifdef REFLECTION_DLL_EXPORTS
    #define REFLECTION_API __declspec(dllexport)
  #else
    #define REFLECTION_API __declspec(dllimport)
  #endif
#else
  #define REFLECTION_API
#endif

#endif
