#ifndef FUNCTIONAL_SDLWINDOW_INCLUDED
#define FUNCTIONAL_SDLWINDOW_INCLUDED

#include <SDL3/SDL.h>
#include <string>
#include <memory>
#include "consts.h"
#include <Render/Pipeline/RenderTargetBinding.h>

namespace Engine
{
    class AllocatedImage2D;
    class RenderSystem;

    /// A wrapper of SDL_Window
    /// Note that memory is managed manually
    class SDLWindow
    {
    public:
        SDLWindow(const char *, int, int, Uint32);

        SDLWindow(const SDLWindow &) = delete;
        SDLWindow(SDLWindow &&) = delete;
        SDLWindow &operator=(const SDLWindow &) = delete;
        SDLWindow &operator=(SDLWindow &&) = delete;

        virtual ~SDLWindow();

        void CreateRenderTargetBinding(std::shared_ptr<RenderSystem> render_system);
        const RenderTargetBinding &GetRenderTargetBinding() const;

        /// Get the underlying pointer of this window
        SDL_Window *GetWindow();

    protected:
        SDL_Window *m_window{nullptr};
        RenderTargetBinding m_render_target_binding{};
    
    private:
        std::shared_ptr<Engine::AllocatedImage2D> m_color_image{};
    };
}
#endif // FUNCTIONAL_SDLWINDOW_INCLUDED
