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

        void Tick(float dt);

    protected:
        std::vector<std::shared_ptr<Widget>> m_widgets;
    };
}

#endif // EDITOR_WINDOW_MAINWINDOW_INCLUDED
