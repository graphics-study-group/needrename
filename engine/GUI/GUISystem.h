#ifndef ENGINE_GUI_GUISYSTEM_INCLUDED
#define ENGINE_GUI_GUISYSTEM_INCLUDED

#include <memory>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class RenderSystem;
    class RenderCommandBuffer;

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
        void DrawGUI(RenderCommandBuffer & cb) const;
        void Create(SDL_Window * window, vk::Format color_attachment_format = vk::Format::eUndefined);
    };
}

#endif // ENGINE_GUI_GUISYSTEM_INCLUDED
