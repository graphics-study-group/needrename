#include "SDLWindow.h"
#include "Exception/exception.h"

namespace Engine
{
    SDLWindow::SDLWindow(const char *title, int width, int height, Uint32 flags)
    {
        this->window = SDL_CreateWindow(title, width, height, flags);
        if (!this->window)
            throw Exception::SDLExceptions::cant_create_window();
    }

    SDLWindow::~SDLWindow()
    {
        Release();
    }

    void SDLWindow::Release() 
    {
        if (this->window) {
            SDL_DestroyWindow(this->window);
            this->window = nullptr;
        }
    }

    SDL_Window *SDLWindow::GetWindow()
    {
        return this->window;
    }

    void SDLWindow::SetIcon(SDL_Surface *surface, bool freeSurface)
    {
        SDL_SetWindowIcon(this->window, surface);
        if (freeSurface)
            SDL_DestroySurface(surface);
    }
} // namespace Engine
