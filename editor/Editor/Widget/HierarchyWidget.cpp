#include "HierarchyWidget.h"
#include <imgui.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>

namespace Editor
{
    HierarchyWidget::HierarchyWidget(const std::string &name) : Widget(name)
    {
    }

    HierarchyWidget::~HierarchyWidget()
    {
    }

    void HierarchyWidget::Render()
    {
        auto world = Engine::MainClass::GetInstance()->GetWorldSystem();
        bool selected_changed = false;
        if (ImGui::Begin(m_name.c_str()))
        {
            for (const auto &go : world->GetGameObjects())
            {
                auto selected = m_selected_game_object.lock();
                if (ImGui::Selectable(go->m_name.c_str(), selected == go))
                {
                    if (selected != go)
                    {
                        selected_changed = true;
                    }
                    m_selected_game_object = go;
                }
            }
        }
        ImGui::End();

        if (selected_changed)
        {
            m_OnGameObjectSelectedDelegate.Invoke(m_selected_game_object);
        }
    }
}
