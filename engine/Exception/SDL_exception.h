#ifndef SDL_EXCEPTION_H_INCLUDED
#define SDL_EXCEPTION_H_INCLUDED

#include "exception.h"

namespace Exception::SDLExceptions {
    class cant_init : public SDLException {};

    class cant_create_window : public SDLException {};

    class cant_create_renderer : public SDLException {};

    class cant_create_surface : public SDLException {};

    class cant_init_ttf : public SDLException {};

    class cant_open_ttf : public SDLException {};

    class cant_render_text : public SDLException {};

    class cant_load_glad : public SDLException {};

} // namespace Exception::SDLExceptions

#endif // SDL_EXCEPTION_H_INCLUDED
