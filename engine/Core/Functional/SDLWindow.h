#ifndef CORE_FUNCTIONAL_SDLWINDOW_INCLUDED
#define CORE_FUNCTIONAL_SDLWINDOW_INCLUDED

#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace vk {
    class Extent2D;
}

namespace Engine {
    class RenderTargetTexture;
    class RenderSystem;

    /// A wrapper of SDL_Window
    /// Note that memory is managed manually
    class SDLWindow {
    public:
        SDLWindow(const char *, int, int, Uint32);

        SDLWindow(const SDLWindow &) = delete;
        SDLWindow(SDLWindow &&) = delete;
        SDLWindow &operator=(const SDLWindow &) = delete;
        SDLWindow &operator=(SDLWindow &&) = delete;

        virtual ~SDLWindow();

        /// Get the underlying pointer of this window
        SDL_Window *GetWindow();

        std::pair<int, int> GetSize() const;

    protected:
        SDL_Window *m_window{nullptr};
    };
} // namespace Engine
#endif // CORE_FUNCTIONAL_SDLWINDOW_INCLUDED
