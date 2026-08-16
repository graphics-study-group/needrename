#include "DefaultVarInspector.h"
#include "VarInspectorRegistry.h"
#include <Editor/EditorMainClass.h>

#include <AnnoRefl/Type.h>
#include <AnnoRefl/reflection.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <imgui.h>
#include <utility>

#include <cstring>

namespace Editor {
    void InspectVar(const std::string &name, AnnoRefl::Var var) {
        auto &registry = EditorMainClass::GetInstance()->GetVarInspectorRegistry();
        auto *inspector = registry.Find(std::string(var.GetType()->GetName()));
        if (inspector) {
            inspector->Inspect(name, std::move(var));
        } else {
            DefaultInspectVar(name, std::move(var));
        }
    }

    void DefaultInspectVar(const std::string &name, AnnoRefl::Var var) {
        if (var.GetType()->GetName() == "int") {
            int value = var.Get<int>();
            ImGui::InputInt(name.c_str(), &value);
            var.Set(value);
        } else if (var.GetType()->GetName() == "float") {
            float value = var.Get<float>();
            ImGui::InputFloat(name.c_str(), &value);
            var.Set(value);
        } else if (var.GetType()->GetName() == "std::string") {
            std::string value = var.Get<std::string>();
            char buffer[256];
            std::strncpy(buffer, value.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';
            ImGui::InputText(name.c_str(), buffer, sizeof(buffer));
            var.Set(std::string(buffer));
        } else if (var.GetType()->GetName() == "bool") {
            bool value = var.Get<bool>();
            ImGui::Checkbox(name.c_str(), &value);
            var.Set(value);
        } else if (var.GetType()->GetName() == "glm::vec3") {
            glm::vec3 value = var.Get<glm::vec3>();
            ImGui::DragFloat3(name.c_str(), &value[0], 0.1f);
            var.Set(value);
        } else if (var.GetType()->GetName() == "glm::quat") {
            glm::quat value = var.Get<glm::quat>();
            ImGui::DragFloat4(name.c_str(), &value[0], 0.01f);
            var.Set(glm::normalize(value));
        } else if (var.GetType()->GetTypeKind() == AnnoRefl::Type::TypeKind::Enum) {
            auto enum_type = std::dynamic_pointer_cast<const AnnoRefl::EnumType>(var.GetType());
            if (enum_type) {
                std::string current_value = std::string(var.GetEnumString());
                if (ImGui::BeginCombo(name.c_str(), current_value.c_str())) {
                    for (auto value : enum_type->GetEnumValues()) {
                        std::string item_text = std::string(enum_type->to_string(value));
                        bool is_selected = (item_text == current_value);
                        if (ImGui::Selectable(item_text.c_str(), is_selected)) {
                            var.SetEnumFromString(item_text);
                            current_value = item_text;
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        } else if (var.GetType()->IsReflectable()) {
            if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, "%s", name.c_str())) {
                unsigned int field_idx = 0;
                for (auto &[name, field] : var.GetType()->GetAllFields()) {
                    ImGui::PushID(static_cast<int>(field_idx++));
                    InspectVar(name, field->GetVar(var.GetDataPtr()));
                    ImGui::PopID();
                }
                for (auto &[name, array_field] : var.GetType()->GetAllArrayFields()) {
                    ImGui::PushID(static_cast<int>(field_idx++));
                    auto array_var = var.GetArrayMember(name);
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
    }
} // namespace Editor
