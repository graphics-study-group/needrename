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
                    // ImGui::Text("<%s>", component_type->GetName().c_str());
                    // ImGui::TableNextRow();
                    // ImGui::TableNextColumn();
                    ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_None;
                    // tree_flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;// Standard
                    // opening mode as we are likely to want to add selection afterwards tree_flags |=
                    // ImGuiTreeNodeFlags_NavLeftJumpsBackHere;  // Left arrow support tree_flags |=
                    // ImGuiTreeNodeFlags_SpanFullWidth;         // Span full width for easier mouse reach tree_flags |=
                    // ImGuiTreeNodeFlags_DrawLinesToNodes;      // Always draw hierarchy outlines
                    if (ImGui::TreeNodeEx("", tree_flags, "<%s>", component_type->GetName().c_str())) {
                        Engine::Reflection::Var component_var(component_type, component.get());
                        unsigned int field_idx = 0;
                        for (auto &[name, field] : component_type->GetFields()) {
                            ImGui::PushID(field_idx++);
                            this->InspectVar(name, field->GetVar(component_var));
                            ImGui::PopID();
                        }
                        // for (ExampleTreeNode* child : node->Childs)
                        //     DrawTreeNode(child);
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

    void InspectorWidget::InspectVar(const std::string &name, Engine::Reflection::Var var) {
        if (var.m_type->GetName() == "int") {
            int value = var.Get<int>();
            ImGui::InputInt(name.c_str(), &value);
            var.Set(value);
        } else if (var.m_type->GetName() == "float") {
            float value = var.Get<float>();
            ImGui::InputFloat(name.c_str(), &value);
            var.Set(value);
        } else if (var.m_type->GetName() == "std::string") {
            std::string value = var.Get<std::string>();
            char buffer[256];
            strncpy(buffer, value.c_str(), sizeof(buffer));
            ImGui::InputText(name.c_str(), buffer, sizeof(buffer));
            var.Set(std::string(buffer));
        } else if (var.m_type->GetName() == "bool") {
            bool value = var.Get<bool>();
            ImGui::Checkbox(name.c_str(), &value);
            var.Set(value);
        } else if (var.m_type->GetName() == "glm::vec3") {
            glm::vec3 value = var.Get<glm::vec3>();
            ImGui::InputFloat3(name.c_str(), &value[0]);
            var.Set(value);
        } else if (var.m_type->GetName() == "glm::quat") {
            glm::quat value = var.Get<glm::quat>();
            ImGui::InputFloat4(name.c_str(), &value[0]);
            var.Set(value);
        } else if (var.m_type->GetName().starts_with("std::vector<")) {
            if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_None)) {
                // auto &vec = var.Get<std::vector<std::any>>();
                // for (size_t i = 0; i < vec.size(); ++i)
                // {
                //     ImGui::PushID(i);
                //     std::string element_name = name + "[" + std::to_string(i) + "]";
                //     this->InspectVar(element_name, Engine::Reflection::Var(element_type, &vec[i]));
                //     ImGui::PopID();
                // }
                ImGui::TreePop();
            }
        } else if (var.m_type->m_reflectable) {
            if (ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_None, name.c_str())) {
                unsigned int field_idx = 0;
                for (auto &[name, field] : var.m_type->GetFields()) {
                    ImGui::PushID(field_idx++);
                    this->InspectVar(name, field->GetVar(var));
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
} // namespace Editor
