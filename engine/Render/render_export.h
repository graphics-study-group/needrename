#ifndef RENDER_EXPORT_H
#define RENDER_EXPORT_H

// RENDER_STATIC marks a target that compiles these sources into itself (the
// native shader tooling) instead of linking the EngineRender DLL: no
// import/export annotations are emitted there.
#if defined(_WIN32) && !defined(RENDER_STATIC)
#ifdef RENDER_DLL_EXPORTS
#define RENDER_API __declspec(dllexport)
#else
#define RENDER_API __declspec(dllimport)
#endif
#else
#define RENDER_API
#endif

#endif
