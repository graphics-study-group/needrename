#ifndef ENGINE_GUI_GUISYSTEM_INCLUDED
#define ENGINE_GUI_GUISYSTEM_INCLUDED

#include <memory>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class RenderSystem;
    namespace AttachmentUtils{
        class AttachmentDescription;
    }
    class GraphicsCommandBuffer;

    class GUISystem {
    protected:
        std::weak_ptr <RenderSystem> m_render_system;
        ImGuiContext * m_context{nullptr};

        void CleanUp();
    
    public:
        /// @brief Construct a GUI system with render system
        /// @param render_system 
        /// Standard dependency injection pattern
        GUISystem(std::shared_ptr <RenderSystem> render_system);
        ~GUISystem();

        GUISystem(const GUISystem &) = delete;
        GUISystem(GUISystem &&) = delete;
        void operator= (const GUISystem &) = delete;
        void operator= (GUISystem &&) = delete; 

        void ProcessEvent(SDL_Event * event) const;
        void PrepareGUI() const;
        /**
         * @brief Record GUI rendering code on the given command buffer.
         * This member records a new render pass on the given command buffer with a single color attachment 
         * specified by the `attachment` parameter. If a previously used attachment is used again, synchronization 
         * barriers must be set up correctly.
         */
        void DrawGUI(const AttachmentUtils::AttachmentDescription & attachment, vk::Extent2D extent, GraphicsCommandBuffer & cb) const;
        void Create(SDL_Window * window, vk::Format color_attachment_format = vk::Format::eUndefined);
    };
}

#endif // ENGINE_GUI_GUISYSTEM_INCLUDED
