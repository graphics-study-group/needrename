#ifndef EDITOR_WINDOW_MAINWINDOW_INCLUDED
#define EDITOR_WINDOW_MAINWINDOW_INCLUDED

#include <vector>
#include <memory>
#include <Core/Delegate/MulticastDelegate.h>
#include <Editor/Widget/Widget.h>

namespace Editor
{
    class MainWindow
    {
    public:
        static constexpr const char *k_main_window_widget_name = "##editor.main_window_widget";
        static constexpr const char *k_main_dockspace_name = "MainDockSpace##editor.main_dockspace";
        static constexpr const char *k_game_widget_name = "Game##editor.game";
        static constexpr const char *k_scene_widget_name = "Scene##editor.scene";
        static constexpr const char *k_hierarchy_widget_name = "Hierarchy##editor.hierarchy";
        static constexpr const char *k_project_widget_name = "Project##editor.project";
        static constexpr const char *k_inspector_widget_name = "Inspector##editor.inspector";
        static constexpr const char *k_control_widget_name = "Control##editor.control";
        
    public:
        MainWindow();
        virtual ~MainWindow() = default;

        virtual void Render();

        void AddWidget(std::shared_ptr<Widget> widget);

    public:
        Engine::MulticastDelegate<> m_OnStart{};
        Engine::MulticastDelegate<> m_OnStop{};
        Engine::MulticastDelegate<> m_OnPause{};
        Engine::MulticastDelegate<> m_OnResume{};
        Engine::MulticastDelegate<> m_OnStep{};

    protected:
        std::vector<std::shared_ptr<Widget>> m_widgets{};

        bool m_is_playing{false};
        bool m_is_paused{false};
    };
}

#endif // EDITOR_WINDOW_MAINWINDOW_INCLUDED
