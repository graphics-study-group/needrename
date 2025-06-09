#ifndef FUNCTIONAL_SDLWINDOW_INCLUDED
#define FUNCTIONAL_SDLWINDOW_INCLUDED

#include <SDL3/SDL.h>
#include <string>
#include <memory>
#include "consts.h"
#include <Render/Pipeline/RenderTargetBinding.h>

namespace Engine
{
    class Texture;
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
        vk::Extent2D GetExtent() const;
        const Texture &GetColorTexture() const noexcept;
        const Texture &GetDepthTexture() const noexcept;

        /// Get the underlying pointer of this window
        SDL_Window *GetWindow();

    protected:
        SDL_Window *m_window{nullptr};
        RenderTargetBinding m_render_target_binding{};
        std::shared_ptr<Texture> m_color_texture{};
        std::shared_ptr<Texture> m_depth_texture{};
    };
}
#endif // FUNCTIONAL_SDLWINDOW_INCLUDED
