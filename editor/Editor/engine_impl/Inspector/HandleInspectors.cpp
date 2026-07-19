#include "HandleInspectors.h"
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Handle.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/reflection.h>
#include <imgui.h>

namespace Editor {
    void ObjectHandleInspector::Inspect(const std::string &name, Engine::Reflection::Var var) {
        const Engine::ObjectHandle &handle = var.Get<Engine::ObjectHandle>();
        const auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        ImGui::Text(
            "%s: [GameObject]%s",
            name.c_str(),
            handle.IsValid() ? scene.GetGameObject(handle)->m_name.c_str() : "Invalid"
        );
    }

    void ComponentHandleInspector::Inspect(const std::string &name, Engine::Reflection::Var var) {
        const Engine::ComponentHandle &handle = var.Get<Engine::ComponentHandle>();
        const auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        std::string display_text = "Invalid";
        if (handle.IsValid()) {
            const auto *component = scene.GetComponent(handle);
            if (component) {
                const auto *go = component->GetParentGameObject();
                if (go) {
                    display_text =
                        go->m_name + " -> " + std::string(Engine::Reflection::GetTypeFromObject(*component)->GetName());
                }
            }
        }
        ImGui::Text("%s: [Component]%s", name.c_str(), display_text.c_str());
    }
} // namespace Editor
