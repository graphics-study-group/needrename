#include "HierarchyWidget.h"
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace Editor {
    HierarchyWidget::HierarchyWidget(const std::string &name) : Widget(name) {
    }

    HierarchyWidget::~HierarchyWidget() {
    }

    void HierarchyWidget::Render() {
        auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        bool selected_changed = false;
        ObjectHandle need_remove_go;
        ObjectHandle need_rename_go;

        if (ImGui::Begin(m_name.c_str())) {
            // Top toolbar: add-button (opens popup) + search box
            if (ImGui::Button("+")) {
                ImGui::OpenPopup("HierarchyAddMenu");
            }
            if (ImGui::BeginPopup("HierarchyAddMenu")) {
                if (ImGui::MenuItem("Create Empty GameObject")) {
                    auto &go = scene.CreateGameObject();
                    go.m_name = "New GameObject";
                }
                ImGui::MenuItem("Create Light");
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            // Search input fills remaining width
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::PushItemWidth(avail);
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", m_search.c_str());
            if (ImGui::InputTextWithHint("##hierarchy_search", "Search...", buf, sizeof(buf))) {
                m_search = buf;
            }
            ImGui::PopItemWidth();
            ImGui::Separator();

            uint32_t index = 0;
            for (const auto &go : scene.GetGameObjects()) {
                // Filter by search string if present
                if (!m_search.empty()) {
                    std::string name = go->m_name;
                    if (name.find(m_search) == std::string::npos) {
                        continue;
                    }
                }
                std::string label = go->m_name + "##hierarchy_item_" + std::to_string(index++);

                // Show InputText if this item is being renamed
                if (m_renaming_game_object == go->GetHandle()) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "%s", m_rename_buffer.c_str());
                    ImGui::PushID(go.get());
                    if (ImGui::InputText(
                            "##rename_input",
                            buf,
                            sizeof(buf),
                            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue
                        )) {
                        // Finish renaming when Enter is pressed
                        m_rename_buffer = buf;
                        need_rename_go = go->GetHandle();
                        m_renaming_game_object.Reset();
                    }
                    // Finish renaming when focus is lost
                    if (!ImGui::IsItemActive() && ImGui::IsItemDeactivatedAfterEdit()) {
                        m_rename_buffer = buf;
                        need_rename_go = go->GetHandle();
                        m_renaming_game_object.Reset();
                    }
                    ImGui::PopID();
                } else {
                    // Show Selectable normally
                    if (ImGui::Selectable(label.c_str(), m_selected_game_object == go->GetHandle())) {
                        if (m_selected_game_object != go->GetHandle()) {
                            selected_changed = true;
                        }
                        m_selected_game_object = go->GetHandle();
                    }
                    // Right-click context menu (with unique ID per item)
                    std::string context_id = "hierarchy_context_" + std::to_string(index - 1);
                    if (ImGui::BeginPopupContextItem(context_id.c_str())) {
                        if (ImGui::MenuItem("Rename")) {
                            m_renaming_game_object = go->GetHandle();
                            m_rename_buffer = go->m_name;
                        }
                        if (ImGui::MenuItem("Delete")) {
                            need_remove_go = go->GetHandle();
                        }
                        ImGui::EndPopup();
                    }
                }
            }
            // Hotkeys for selected game object
            if (m_selected_game_object.IsValid()) {
                // F2 to rename
                if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    m_renaming_game_object = m_selected_game_object;
                    m_rename_buffer = scene.GetGameObjectRef(m_selected_game_object).m_name;
                }
                // Delete to remove
                if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    need_remove_go = m_selected_game_object;
                }
            }
        }
        ImGui::End();

        if (need_remove_go.IsValid()) {
            if (m_selected_game_object == need_remove_go) {
                m_selected_game_object.Reset();
                selected_changed = true;
            }
            scene.RemoveGameObject(need_remove_go);
        }
        if (need_rename_go.IsValid()) {
            scene.GetGameObjectRef(need_rename_go).m_name = m_rename_buffer;
        }

        if (selected_changed) {
            m_OnGameObjectSelectedDelegate.Invoke(m_selected_game_object);
        }
    }
} // namespace Editor
