#ifndef ENGINE_GUI_GUISYSTEM_INCLUDED
#define ENGINE_GUI_GUISYSTEM_INCLUDED

#include <memory>
#include <imgui.h>

typedef struct SDL_Window;

namespace Engine {
    class RenderSystem;

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

        void Create(SDL_Window * window);
    };
}

#endif // ENGINE_GUI_GUISYSTEM_INCLUDED
