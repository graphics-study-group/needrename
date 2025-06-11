#include "InspectorWidget.h"
#include <imgui.h>
#include <MainClass.h>

namespace Editor
{
    InspectorWidget::InspectorWidget(const std::string &name) : Widget(name)
    {
    }

    InspectorWidget::~InspectorWidget()
    {
    }

    void InspectorWidget::Render()
    {
        if (ImGui::Begin(m_name.c_str()))
        {
        }
        ImGui::End();
    }
}
