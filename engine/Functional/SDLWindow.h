#ifndef FUNCTIONAL_SDLWINDOW_INCLUDED
#define FUNCTIONAL_SDLWINDOW_INCLUDED

#include <SDL3/SDL.h>
#include <string>
#include <memory>
#include "consts.h"
#include <Render/AttachmentUtils.h>

namespace Engine
{
    class AllocatedImage2D;

    struct WindowAttachmentDescription
    {
        Engine::AttachmentUtils::AttachmentDescription color;
        Engine::AttachmentUtils::AttachmentDescription depth;
        vk::Extent2D extent;
    };

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

        std::shared_ptr<WindowAttachmentDescription> GetAttachmentDescription();

        /// Get the underlying pointer of this window
        SDL_Window *GetWindow();

    protected:
        SDL_Window *m_window{nullptr};
        std::shared_ptr<WindowAttachmentDescription> m_attachment_description{};

    private:
        std::shared_ptr<Engine::AllocatedImage2D> m_color_image{};
        std::shared_ptr<Engine::AllocatedImage2D> m_depth_image{};
    };
}
#endif // FUNCTIONAL_SDLWINDOW_INCLUDED
