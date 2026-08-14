#include "HandleInspectors.h"
#include <Framework/Component/Component.h>
#include <Framework/MainClass.h>
#include <Framework/Object/GameObject.h>
#include <Framework/World/Handle.h>
#include <Framework/World/Scene.h>
#include <Framework/World/WorldSystem.h>

#include <AnnoRefl/reflection.h>
#include <imgui.h>

namespace Editor {
    void ObjectHandleInspector::Inspect(const std::string &name, AnnoRefl::Var var) {
        const Engine::ObjectHandle &handle = var.Get<Engine::ObjectHandle>();
        const auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        ImGui::Text(
            "%s: [GameObject]%s",
            name.c_str(),
            handle.IsValid() ? scene.GetGameObject(handle)->m_name.c_str() : "Invalid"
        );
    }

    void ComponentHandleInspector::Inspect(const std::string &name, AnnoRefl::Var var) {
        const Engine::ComponentHandle &handle = var.Get<Engine::ComponentHandle>();
        const auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        std::string display_text = "Invalid";
        if (handle.IsValid()) {
            const auto *component = scene.GetComponent(handle);
            if (component) {
                const auto *go = component->GetParentGameObject();
                if (go) {
                    display_text =
                        go->m_name + " -> " + std::string(AnnoRefl::GetTypeFromObject(*component)->GetName());
                }
            }
        }
        ImGui::Text("%s: [Component]%s", name.c_str(), display_text.c_str());
    }
} // namespace Editor
