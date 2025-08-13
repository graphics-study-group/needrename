#include "InspectorWidget.h"
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <MainClass.h>
#include <Reflection/reflection.h>
#include <imgui.h>
#include <unordered_map>

namespace Editor {
    InspectorWidget::InspectorWidget(const std::string &name) : Widget(name) {
    }

    InspectorWidget::~InspectorWidget() {
    }

    void InspectorWidget::Render() {
        if (ImGui::Begin(m_name.c_str())) {
            switch (m_inspector_mode) {
            case InspectorMode::kInspectorModeGameObject: {
                auto weak_game_object = std::any_cast<std::weak_ptr<Engine::GameObject>>(m_inspected_object);
                if (weak_game_object.expired()) {
                    ImGui::Text("No GameObject selected");
                    break;
                }
                auto game_object = weak_game_object.lock();
                ImGui::Text((std::string("<GameObject>") + game_object->m_name).c_str());
                ImGui::Separator();
                unsigned int component_idx = 0;
                for (const auto &component : game_object->m_components) {
                    ImGui::PushID(component_idx++);
                    auto component_type = Engine::Reflection::GetTypeFromObject(*component);
                    if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, "<%s>", component_type->GetName().c_str())) {
                        Engine::Reflection::Var component_var(component_type, component.get());
                        unsigned int field_idx = 0;
                        for (auto &[name, field] : component_type->GetFields()) {
                            ImGui::PushID(field_idx++);
                            this->InspectField(field, component_var);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }

                break;
            }
            case InspectorMode::kInspectorModeAsset: {
                // Handle asset inspection here
                break;
            }
            case InspectorMode::kInspectorModeNone:
                break;
            }
        }
        ImGui::End();
    }

    void InspectorWidget::SetSelectedGameObject(std::weak_ptr<Engine::GameObject> game_object) {
        if (!game_object.expired()) {
            m_inspector_mode = InspectorMode::kInspectorModeGameObject;
            m_inspected_object = game_object;
        } else {
            m_inspector_mode = InspectorMode::kInspectorModeNone;
            m_inspected_object = {};
        }
    }

    void InspectorWidget::InspectField(std::shared_ptr<const Engine::Reflection::Field> field, Engine::Reflection::Var var) {
        if (field->GetFieldType()->GetName().starts_with("std::vector<")) {
            auto array_var = var.GetArrayMember(field->GetName());
            size_t array_size = array_var.GetSize();
            if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, "Vector %s", field->GetName().c_str())) {
                for (size_t i = 0; i < array_size; ++i) {
                    ImGui::PushID(i);
                    this->InspectVar(field->GetName() + "[" + std::to_string(i) + "]", array_var.GetElement(i));
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        } else {
            this->InspectVar(field->GetName(), field->GetVar(var.GetDataPtr()));
        }
    }

    void InspectorWidget::InspectVar(const std::string &name, Engine::Reflection::Var var) {
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
            strncpy(buffer, value.c_str(), sizeof(buffer));
            ImGui::InputText(name.c_str(), buffer, sizeof(buffer));
            var.Set(std::string(buffer));
        } else if (var.GetType()->GetName() == "bool") {
            bool value = var.Get<bool>();
            ImGui::Checkbox(name.c_str(), &value);
            var.Set(value);
        } else if (var.GetType()->GetName() == "glm::vec3") {
            glm::vec3 value = var.Get<glm::vec3>();
            ImGui::InputFloat3(name.c_str(), &value[0]);
            var.Set(value);
        } else if (var.GetType()->GetName() == "glm::quat") {
            glm::quat value = var.Get<glm::quat>();
            ImGui::InputFloat4(name.c_str(), &value[0]);
            var.Set(value);
        } else if (var.GetType()->GetTypeKind() == Engine::Reflection::Type::TypeKind::Pointer) {
            auto pointer_type = std::dynamic_pointer_cast<const Engine::Reflection::PointerType>(var.GetType());
            if (pointer_type && pointer_type->GetPointedType()->GetName() == "Engine::AssetRef") {
                Engine::GUID asset_guid = var.GetPointedVar().InvokeMethod("GetGUID").Get<Engine::GUID>();
                ImGui::Text("%s: Asset GUID: %s", name.c_str(), asset_guid.toString().c_str());
            }
        } else if (var.GetType()->IsReflectable()) {
            if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, name.c_str())) {
                unsigned int field_idx = 0;
                for (auto &[name, field] : var.GetType()->GetFields()) {
                    ImGui::PushID(field_idx++);
                    this->InspectField(field, var);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
} // namespace Editor
