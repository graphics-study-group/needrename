#ifndef EDITOR_WINDOW_MAINWINDOW_INCLUDED
#define EDITOR_WINDOW_MAINWINDOW_INCLUDED

#include <vector>
#include <memory>
#include <Editor/Widget/Widget.h>

namespace Editor
{
    class MainWindow
    {
    public:
        MainWindow();
        virtual ~MainWindow() = default;

        virtual void Render();

        void AddWidget(std::shared_ptr<Widget> widget);

    protected:
        static constexpr const char *k_main_window_widget_name = "##editor.main_window_widget";
        static constexpr const char *k_main_window_dockspace_name = "##editor.main_window_dockspace";

        std::vector<std::shared_ptr<Widget>> m_widgets{};
    };
}

#endif // EDITOR_WINDOW_MAINWINDOW_INCLUDED
