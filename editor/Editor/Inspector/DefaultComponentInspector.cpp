#include "DefaultComponentInspector.h"
#include "ComponentInspectorRegistry.h"
#include "DefaultVarInspector.h"
#include <Editor/EditorMainClass.h>
#include <Framework/Component/Component.h>

#include <AnnoRefl/Type.h>
#include <AnnoRefl/reflection.h>
#include <imgui.h>

namespace Editor {
    void InspectComponent(Engine::Component &component) {
        auto &registry = EditorMainClass::GetInstance()->GetComponentInspectorRegistry();
        auto component_type = AnnoRefl::GetTypeFromObject(component);
        auto *inspector = registry.Find(std::string(component_type->GetName()));
        if (inspector) {
            inspector->Inspect(component);
        } else {
            DefaultInspectComponent(component);
        }
    }

    void DefaultInspectComponent(Engine::Component &component) {
        auto component_type = AnnoRefl::GetTypeFromObject(component);
        if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, "<%s>", component_type->GetName().c_str())) {
            AnnoRefl::Var component_var(component_type, &component);
            unsigned int field_idx = 0;
            for (auto &[name, field] : component_type->GetAllFields()) {
                ImGui::PushID(static_cast<int>(field_idx++));
                InspectVar(name, field->GetVar(component_var.GetDataPtr()));
                ImGui::PopID();
            }
            for (auto &[name, array_field] : component_type->GetAllArrayFields()) {
                ImGui::PushID(static_cast<int>(field_idx++));
                auto array_var = component_var.GetArrayMember(name);
                if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_None)) {
                    size_t array_size = array_var.GetSize();
                    for (unsigned int i = 0; i < array_size; ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        InspectVar("[" + std::to_string(i) + "]", array_var.GetElement(i));
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
} // namespace Editor
