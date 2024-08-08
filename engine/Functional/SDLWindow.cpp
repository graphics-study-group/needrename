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
        if (this->window == nullptr)
            throw Exception::SDLExceptions::cant_create_window();
        this->glcontext = nullptr;
        this->renderer = nullptr;
    }

    SDLWindow::~SDLWindow()
    {
        if (this->renderer != nullptr)
            SDL_DestroyRenderer(this->renderer);
        SDL_GL_DeleteContext(this->glcontext);
        SDL_DestroyWindow(this->window);
    }

    SDL_Window *SDLWindow::GetWindow()
    {
        return this->window;
    }

    SDL_Renderer *SDLWindow::GetRenderer()
    {
        return this->renderer;
    }

    [[deprecated("SDL_CreateRenderer is deprecated.")]]
    void SDLWindow::CreateRenderer()
    {
        if (this->renderer != nullptr)
            SDL_DestroyRenderer(this->renderer);

        this->renderer = SDL_CreateRenderer(this->window, "opengl");
        if (this->renderer == nullptr)
            throw Exception::SDLExceptions::cant_create_renderer();
        this->glcontext = SDL_GL_CreateContext(this->window);
    }

    void SDLWindow::SetIcon(SDL_Surface *surface, bool freeSurface)
    {
        SDL_SetWindowIcon(this->window, surface);
        if (freeSurface)
            SDL_DestroySurface(surface);
    }

    bool SDLWindow::OnClickOverall(SDL_Event &event)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Clicking (%d, %d)",
                     event.button.x, event.button.y);
        return true;
    }

    bool SDLWindow::OnDrawOverall(bool forcedRedraw)
    {
        if (this->renderer == nullptr)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "onDrawOverall() is called but no renderer is available");
            return true;
        }
        return true;
    }

    bool SDLWindow::OnKeyOverall(SDL_Event &event)
    {
        // TODO: key event!!!!
        // if (event.type == SDL_KEYDOWN)
        // 	return keyFocus->onKeydown(event);
        // else if (event.type == SDL_KEYUP)
        // 	return keyFocus->onKeyup(event);

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "An event which is not a keyboard event is passed into onKeyOverall as a parameter");
        return true;
    }

    bool SDLWindow::DispatchEvents(SDL_Event &event)
    {
        // SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Dispatching an event : %u\n", event.type);
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (this->blockKeyboardEvent)
                break;
            return this->OnKeyOverall(event);
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (this->blockMouseEvent)
                break;
            return this->OnClickOverall(event);
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
            SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                           "Redrawn due to a window event");
            return this->OnDrawOverall(true);
            break;
        default:
            break;
        }
        return true;
    }

    bool SDLWindow::BeforeEventLoop()
    {
        this->OnDrawOverall();
        return true;
    }

    bool SDLWindow::AfterEventLoop()
    {
        SDL_GL_SwapWindow(this->window);

        bool ret = true;
        for (auto item : this->postProcs)
            if (item() == false)
            {
                ret = false;
                break;
            }

        return ret;
    }

    void SDLWindow::EnableMSAA(int samples)
    {
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