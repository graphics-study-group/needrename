#include "InspectorWidget.h"
#include <Core/guid.h>
#include <Editor/Inspector/DefaultComponentInspector.h>
#include <Editor/Inspector/DefaultVarInspector.h>
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Handle.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <AnnoRefl/reflection.h>
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
                auto text = std::string("<GameObject>") + game_object->m_name;
                ImGui::Text("%s", text.c_str());
                ImGui::Separator();
                unsigned int component_idx = 0;
                for (auto component_handle : game_object->m_components) {
                    auto component = scene.GetComponent(component_handle);
                    ImGui::PushID(component_idx++);
                    InspectComponent(*component);
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
                            auto component_type = AnnoRefl::GetType(component_type_name);
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
        if (game_object.IsValid()) {
            m_inspector_mode = InspectorMode::kInspectorModeGameObject;
            m_inspected_object = game_object;
        } else {
            m_inspector_mode = InspectorMode::kInspectorModeNone;
            m_inspected_object = {};
        }
    }

    void InspectorWidget::LoadAvailableComponentTypes() {
        m_component_types.clear();
        const auto &registered_types = AnnoRefl::Type::s_name_index_map;
        auto component_type = AnnoRefl::GetType("Engine::Component");
        assert(component_type && component_type->IsReflectable() && "Component type must be registered");
        for (const auto &[type_name, type_index] : registered_types) {
            auto type = AnnoRefl::GetType(type_name);
            if (type->IsDerivedFrom(component_type)) {
                m_component_types.push_back(type->GetName());
            }
        }
    }
} // namespace Editor
