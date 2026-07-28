#ifndef ANROREFL_EXPORT_INCLUDED
#define ANROREFL_EXPORT_INCLUDED

#ifdef _WIN32
  #ifdef ANROREFL_DLL_EXPORTS
    #define ANROREFL_API __declspec(dllexport)
  #else
    #define ANROREFL_API __declspec(dllimport)
  #endif
#else
  #define ANROREFL_API
#endif

#endif
