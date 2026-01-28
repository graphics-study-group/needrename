#include "InspectorWidget.h"
#include <Core/guid.h>
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/reflection.h>
#include <imgui.h>
#include <iostream>
#include <unordered_map>

namespace Editor {
    InspectorWidget::InspectorWidget(const std::string &name) : Widget(name) {
        LoadAvailableComponentTypes();
    }

    InspectorWidget::~InspectorWidget() {
    }

    void InspectorWidget::Render() {
        auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        if (ImGui::Begin(m_name.c_str())) {
            switch (m_inspector_mode) {
            case InspectorMode::kInspectorModeGameObject: {
                auto game_object = scene.GetGameObject(std::any_cast<ObjectHandle>(m_inspected_object));
                if (!game_object) {
                    ImGui::Text("No GameObject selected");
                    break;
                }
                ImGui::Text((std::string("<GameObject>") + game_object->m_name).c_str());
                ImGui::Separator();
                unsigned int component_idx = 0;
                for (auto component_handle : game_object->m_components) {
                    auto component = scene.GetComponent(component_handle);
                    ImGui::PushID(component_idx++);
                    auto component_type = Engine::Reflection::GetTypeFromObject(*component);
                    if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, "<%s>", component_type->GetName().c_str())) {
                        Engine::Reflection::Var component_var(component_type, component);
                        unsigned int field_idx = 0;
                        for (auto &[name, field] : component_type->GetAllFields()) {
                            ImGui::PushID(field_idx++);
                            this->InspectVar(name, field->GetVar(component_var.GetDataPtr()));
                            ImGui::PopID();
                        }
                        for (auto &[name, array_field] : component_type->GetAllArrayFields()) {
                            ImGui::PushID(field_idx++);
                            auto array_var = component_var.GetArrayMember(name);
                            if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_None)) {
                                size_t array_size = array_var.GetSize();
                                for (unsigned int i = 0; i < array_size; ++i) {
                                    ImGui::PushID(i);
                                    this->InspectVar("[" + std::to_string(i) + "]", array_var.GetElement(i));
                                    ImGui::PopID();
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }

                ImGui::Separator();
                if (ImGui::Button("Add Component")) {
                    ImGui::OpenPopup("AddComponentPopup");
                }
                if (ImGui::BeginPopup("AddComponentPopup")) {
                    for (const auto &component_type_name : m_component_types) {
                        if (ImGui::MenuItem(component_type_name.c_str())) {
                            auto component_type = Engine::Reflection::GetType(component_type_name);
                            if (component_type) {
                                scene.CreateComponent(*game_object, *component_type);
                            }
                        }
                    }
                    ImGui::EndPopup();
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

    void InspectorWidget::SetSelectedGameObject(ObjectHandle game_object) {
        if (!game_object.IsValid()) {
            m_inspector_mode = InspectorMode::kInspectorModeGameObject;
            m_inspected_object = game_object;
        } else {
            m_inspector_mode = InspectorMode::kInspectorModeNone;
            m_inspected_object = {};
        }
    }

    void InspectorWidget::LoadAvailableComponentTypes() {
        m_component_types.clear();
        const auto &registered_types = Engine::Reflection::Type::s_name_index_map;
        auto component_type = Engine::Reflection::GetType("Engine::Component");
        assert(component_type && component_type->IsReflectable() && "Component type must be registered");
        for (const auto &[type_name, type_index] : registered_types) {
            auto type = Engine::Reflection::GetType(type_name);
            if (type->IsDerivedFrom(component_type)) {
                m_component_types.push_back(type->GetName());
            }
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
        } else if (var.GetType()->GetTypeKind() == Engine::Reflection::Type::TypeKind::Enum) {
            auto enum_type = std::dynamic_pointer_cast<const Engine::Reflection::EnumType>(var.GetType());
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
            } else {
                ImGui::Text("%s: <Invalid Enum Type>", name.c_str());
            }
        } else if (var.GetType()->IsReflectable()) {
            if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, name.c_str())) {
                unsigned int field_idx = 0;
                for (auto &[name, field] : var.GetType()->GetAllFields()) {
                    ImGui::PushID(field_idx++);
                    this->InspectVar(name, field->GetVar(var.GetDataPtr()));
                    ImGui::PopID();
                }
                for (auto &[name, array_field] : var.GetType()->GetAllArrayFields()) {
                    ImGui::PushID(field_idx++);
                    auto array_var = var.GetArrayMember(name);
                    if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_None)) {
                        size_t array_size = array_var.GetSize();
                        for (unsigned int i = 0; i < array_size; ++i) {
                            ImGui::PushID(i);
                            this->InspectVar("[" + std::to_string(i) + "]", array_var.GetElement(i));
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
