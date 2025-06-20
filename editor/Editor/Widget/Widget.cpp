#include "Widget.h"
#include <imgui.h>

namespace Editor
{
    Widget::Widget(const std::string &name) : m_name(name)
    {
    }

    void Widget::Render()
    {
        if (ImGui::Begin(m_name.c_str()))
        {
        }
        ImGui::End();
    }
}
