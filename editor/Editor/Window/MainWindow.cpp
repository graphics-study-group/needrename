#include "MainWindow.h"

namespace Editor
{
    MainWindow::MainWindow()
    {
        m_widgets.push_back(std::make_shared<Widget>("Test"));
    }

    void MainWindow::Tick(float dt)
    {
        for (auto &widget : m_widgets)
        {
            widget->Tick(dt);
        }
    }
}
