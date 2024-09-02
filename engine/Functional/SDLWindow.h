#ifndef FUNCTIONAL_SDLWINDOW_INCLUDED
#define FUNCTIONAL_SDLWINDOW_INCLUDED

#include <SDL3/SDL.h>
#include <list>
#include <string>
#include <memory>
#include <functional>

#include "consts.h"

namespace Engine
{
    /// A wrapper of SDL_Window
    /// Note that memory is managed manually
    class SDLWindow
    {
    public:
        SDLWindow(const char *, int, int, Uint32);

        SDLWindow(const SDLWindow &) = delete;
        SDLWindow(SDLWindow &&);
        SDLWindow & operator = (const SDLWindow &) = delete;
        SDLWindow & operator = (SDLWindow &&) = delete;

        /// @note Delete EVERY registered object before the destruction of this class !!
        virtual ~SDLWindow();

        /// @brief 
        void Release();

        /// Set the icon of this window
        void SetIcon(SDL_Surface *, bool = true);

        /// Get the underlying pointer of this window
        SDL_Window *GetWindow();

        /// Call this function in an event loop before processing any events
        /// @return TRUE if the event loop is to be continued
        virtual bool BeforeEventLoop();

        /// Call this function in an event loop after all events are processed
        /// @return TRUE if the event loop is to be continued
        virtual bool AfterEventLoop();

        /// Call this function to dispatch all events
        /// @return TRUE if the event loop is to be continued
        virtual bool DispatchEvents(SDL_Event &);

        [[deprecated("Does not consider HDPI displays. Use SDL_GetWindowSizeInPixels instead.")]]
        int GetHeight() const;

        [[deprecated("Does not consider HDPI displays. Use SDL_GetWindowSizeInPixels instead.")]]
        int GetWidth() const;

    protected:
        SDL_Window * window {nullptr};
        const int width, height;
        std::list<std::function<bool(void)>> postProcs {};
    private:
    };
}
#endif // FUNCTIONAL_SDLWINDOW_INCLUDED
