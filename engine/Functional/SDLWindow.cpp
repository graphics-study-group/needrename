#include "SDLWindow.h"
#include "Exception/exception.h"

namespace Engine
{
    SDLWindow::SDLWindow(const char *title, int w, int h,
                         Uint32 flags) : width(w), height(h)
    {
        // ctor
        this->blockMouseEvent = false;
        this->blockKeyboardEvent = false;

        this->window = SDL_CreateWindow(title, w, h, flags);
        if (!this->window)
            throw Exception::SDLExceptions::cant_create_window();
    }

    SDLWindow::SDLWindow(SDLWindow && other) : window(other.window), width(other.width), height(other.height) {
        other.window = nullptr;
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

    bool SDLWindow::DispatchEvents(SDL_Event &event)
    {
        // SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Dispatching an event : %u\n", event.type);
        switch (event.type)
        {
        default:
            break;
        }
        return true;
    }

    bool SDLWindow::BeforeEventLoop()
    {
        return true;
    }

    bool SDLWindow::AfterEventLoop()
    {
        bool ret = true;
        for (auto item : this->postProcs)
            if (item() == false)
            {
                ret = false;
                break;
            }

        return ret;
    }

    int SDLWindow::GetHeight() const
    {
        return height;
    }

    int SDLWindow::GetWidth() const
    {
        return width;
    }

} // namespace Engine
