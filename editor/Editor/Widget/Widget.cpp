#include "Widget.h"
#include <imgui.h>

namespace Editor
{
    Widget::Widget(const char *name) : m_name(name)
    {
    }

    void Widget::Tick(float dt)
    {
        if (ImGui::Begin(m_name))
        {
        }
        ImGui::End();
    }
}
