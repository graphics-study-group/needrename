#include "HierarchyWidget.h"
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
        auto world = Engine::MainClass::GetInstance()->GetWorldSystem();
        bool selected_changed = false;
        std::shared_ptr<Engine::GameObject> need_remove_go = nullptr;
        std::shared_ptr<Engine::GameObject> need_rename_go = nullptr;

        if (ImGui::Begin(m_name.c_str())) {
            // Top toolbar: add-button (opens popup) + search box
            if (ImGui::Button("+")) {
                ImGui::OpenPopup("HierarchyAddMenu");
            }
            if (ImGui::BeginPopup("HierarchyAddMenu")) {
                if (ImGui::MenuItem("Create Empty GameObject")) {
                    auto go = world->CreateGameObject<Engine::GameObject>();
                    go->m_name = "New GameObject";
                    world->AddGameObjectToWorld(go);
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
            for (const auto &go : world->GetGameObjects()) {
                // Filter by search string if present
                if (!m_search.empty()) {
                    std::string name = go->m_name;
                    if (name.find(m_search) == std::string::npos) {
                        continue;
                    }
                }
                auto selected = m_selected_game_object.lock();
                auto renaming = m_renaming_game_object.lock();
                std::string label = go->m_name + "##hierarchy_item_" + std::to_string(index++);

                // Show InputText if this item is being renamed
                if (renaming == go) {
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
                        need_rename_go = go;
                        m_renaming_game_object.reset();
                    }
                    // Finish renaming when focus is lost
                    if (!ImGui::IsItemActive() && ImGui::IsItemDeactivatedAfterEdit()) {
                        m_rename_buffer = buf;
                        need_rename_go = go;
                        m_renaming_game_object.reset();
                    }
                    ImGui::PopID();
                } else {
                    // Show Selectable normally
                    if (ImGui::Selectable(label.c_str(), selected == go)) {
                        if (selected != go) {
                            selected_changed = true;
                        }
                        m_selected_game_object = go;
                    }
                    // Right-click context menu (with unique ID per item)
                    std::string context_id = "hierarchy_context_" + std::to_string(index - 1);
                    if (ImGui::BeginPopupContextItem(context_id.c_str())) {
                        if (ImGui::MenuItem("Rename")) {
                            m_renaming_game_object = go;
                            m_rename_buffer = go->m_name;
                        }
                        if (ImGui::MenuItem("Delete")) {
                            need_remove_go = go;
                        }
                        ImGui::EndPopup();
                    }
                }
            }
            // Hotkeys for selected game object
            auto selected = m_selected_game_object.lock();
            if (selected) {
                // F2 to rename
                if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    m_renaming_game_object = selected;
                    m_rename_buffer = selected->m_name;
                }
                // Delete to remove
                if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    need_remove_go = selected;
                }
            }
        }
        ImGui::End();

        if (need_remove_go) {
            if (m_selected_game_object.lock() == need_remove_go) {
                m_selected_game_object.reset();
                selected_changed = true;
            }
            world->RemoveGameObjectFromWorld(need_remove_go);
        }
        if (need_rename_go) {
            need_rename_go->m_name = m_rename_buffer;
        }

        if (selected_changed) {
            m_OnGameObjectSelectedDelegate.Invoke(m_selected_game_object);
        }
    }
} // namespace Editor
