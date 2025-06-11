#include "ProjectWidget.h"
#include <imgui.h>
#include <MainClass.h>

namespace Editor
{
    ProjectWidget::ProjectWidget(const std::string &name) : Widget(name)
    {
    }

    ProjectWidget::~ProjectWidget()
    {
    }

    void ProjectWidget::Render()
    {
        if (ImGui::Begin(m_name.c_str()))
        {
        }
        ImGui::End();
    }
}
