#include "HierarchyWidget.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/Scene/SceneAsset.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Framework/component/RenderComponent/LightComponent.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>

namespace {
    bool ContainsCaseInsensitive(const std::string &text, const std::string &pattern) {
        if (pattern.empty()) {
            return true;
        }

        auto to_lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        std::string lower_text(text.size(), '\0');
        std::string lower_pattern(pattern.size(), '\0');
        std::transform(text.begin(), text.end(), lower_text.begin(), to_lower);
        std::transform(pattern.begin(), pattern.end(), lower_pattern.begin(), to_lower);
        return lower_text.find(lower_pattern) != std::string::npos;
    }

    bool LessCaseInsensitive(const std::string &a, const std::string &b) {
        const size_t min_len = std::min(a.size(), b.size());
        for (size_t i = 0; i < min_len; ++i) {
            const char ac = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
            const char bc = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
            if (ac != bc) {
                return ac < bc;
            }
        }
        return a.size() < b.size();
    }
} // namespace

namespace Editor {
    HierarchyWidget::HierarchyWidget(const std::string &name) : Widget(name) {
    }

    HierarchyWidget::~HierarchyWidget() {
    }

    void HierarchyWidget::Render() {
        auto &adb = *std::dynamic_pointer_cast<Engine::FileSystemDatabase>(
            Engine::MainClass::GetInstance()->GetAssetDatabase()
        );
        auto &scene = Engine::MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
        bool selected_changed = false;
        ObjectHandle need_remove_go;
        ObjectHandle need_rename_go;

        if (ImGui::Begin(m_name.c_str())) {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    const char *raw = static_cast<const char *>(payload->Data);
                    if (raw && raw[0] != '\0') {
                        try {
                            auto asset_ref = adb.GetNewAssetRef(Engine::AssetPath(adb, std::filesystem::path(raw)));
                            auto *scene_asset = asset_ref.as<Engine::SceneAsset>();
                            scene_asset->AddToScene(scene);
                            scene.FlushCmdQueue();
                            SDL_LogInfo(
                                SDL_LOG_CATEGORY_APPLICATION, "Dropped SceneAsset added from Hierarchy widget: %s", raw
                            );
                        } catch (const std::exception &e) {
                            SDL_LogWarn(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "Dropped asset ignored on Hierarchy widget (%s): %s",
                                raw,
                                e.what()
                            );
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Top toolbar: add-button (opens popup) + search box
            if (ImGui::Button("+")) {
                ImGui::OpenPopup("HierarchyAddMenu");
            }
            if (ImGui::BeginPopup("HierarchyAddMenu")) {
                if (ImGui::MenuItem("Create Empty")) {
                    auto &go = scene.CreateGameObject();
                    go.m_name = "New GameObject";
                }
                if (ImGui::BeginMenu("Create Light")) {
                    if (ImGui::MenuItem("Directional Light")) {
                        auto &go = scene.CreateGameObject();
                        go.m_name = "Directional Light";
                        auto &comp = go.AddComponent<Engine::LightComponent>();
                        comp.m_type = Engine::LightType::Directional;
                        comp.m_cast_shadow = true;
                    }
                    if (ImGui::MenuItem("Point Light")) {
                        auto &go = scene.CreateGameObject();
                        go.m_name = "Point Light";
                        auto &comp = go.AddComponent<Engine::LightComponent>();
                        comp.m_type = Engine::LightType::Point;
                        comp.m_cast_shadow = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Create Mesh")) {
                    if (ImGui::MenuItem("Cube")) {
                        auto &go = scene.CreateGameObject();
                        go.m_name = "Cube";
                        auto &comp = go.AddComponent<Engine::StaticMeshComponent>();
                        comp.m_mesh_asset = adb.GetNewAssetRef(Engine::AssetPath(adb, "~/mesh/cube.asset"));
                        comp.m_material_assets.push_back(
                            adb.GetNewAssetRef(Engine::AssetPath(adb, "~/materials/solid_color_dark_grey.asset"))
                        );
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        auto &go = scene.CreateGameObject();
                        go.m_name = "Sphere";
                        auto &comp = go.AddComponent<Engine::StaticMeshComponent>();
                        comp.m_mesh_asset = adb.GetNewAssetRef(Engine::AssetPath(adb, "~/mesh/sphere.asset"));
                        comp.m_material_assets.push_back(
                            adb.GetNewAssetRef(Engine::AssetPath(adb, "~/materials/solid_color_dark_grey.asset"))
                        );
                    }
                    ImGui::EndMenu();
                }
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

            const auto &game_objects = scene.GetGameObjects();
            std::unordered_map<uint32_t, Engine::GameObject *> go_by_id;
            go_by_id.reserve(game_objects.size());
            for (const auto &go : game_objects) {
                go_by_id[go->GetHandle().GetID()] = go.get();
            }

            // Prefer frame-local cache first so repeated tree lookups are O(1).
            auto get_go = [&](ObjectHandle handle) -> Engine::GameObject * {
                if (!handle.IsValid()) {
                    return nullptr;
                }
                auto it = go_by_id.find(handle.GetID());
                if (it != go_by_id.end()) {
                    return it->second;
                }
                return scene.GetGameObject(handle);
            };

            // Clear stale UI state when selected/renaming object was deleted this frame.
            if (m_selected_game_object.IsValid() && get_go(m_selected_game_object) == nullptr) {
                m_selected_game_object.Reset();
                selected_changed = true;
            }
            if (m_renaming_game_object.IsValid() && get_go(m_renaming_game_object) == nullptr) {
                m_renaming_game_object.Reset();
                m_rename_buffer.clear();
            }

            auto render_context_menu = [&](Engine::GameObject &go) {
                if (ImGui::BeginPopupContextItem("hierarchy_context")) {
                    if (ImGui::MenuItem("Rename")) {
                        m_renaming_game_object = go.GetHandle();
                        m_rename_buffer = go.m_name;
                    }
                    if (ImGui::MenuItem("Delete")) {
                        need_remove_go = go.GetHandle();
                    }
                    ImGui::EndPopup();
                }
            };

            auto render_rename_input = [&](Engine::GameObject &go) {
                char rename_buffer[256];
                std::snprintf(rename_buffer, sizeof(rename_buffer), "%s", m_rename_buffer.c_str());
                if (ImGui::InputText(
                        "##rename_input",
                        rename_buffer,
                        sizeof(rename_buffer),
                        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue
                    )) {
                    m_rename_buffer = rename_buffer;
                    need_rename_go = go.GetHandle();
                    m_renaming_game_object.Reset();
                }
                if (!ImGui::IsItemActive() && ImGui::IsItemDeactivatedAfterEdit()) {
                    m_rename_buffer = rename_buffer;
                    need_rename_go = go.GetHandle();
                    m_renaming_game_object.Reset();
                }
            };

            const bool in_search_mode = !m_search.empty();
            if (in_search_mode) {
                // Unity-like search mode: show only matches in a flat, alphabetically sorted list.
                std::vector<Engine::GameObject *> matches;
                for (const auto &go : game_objects) {
                    if (ContainsCaseInsensitive(go->m_name, m_search)) {
                        matches.push_back(go.get());
                    }
                }
                std::sort(
                    matches.begin(), matches.end(), [](const Engine::GameObject *lhs, const Engine::GameObject *rhs) {
                        return LessCaseInsensitive(lhs->m_name, rhs->m_name);
                    }
                );

                for (Engine::GameObject *go : matches) {
                    ImGui::PushID(static_cast<int>(go->GetHandle().GetID()));
                    if (m_renaming_game_object == go->GetHandle()) {
                        render_rename_input(*go);
                    } else {
                        if (ImGui::Selectable(go->m_name.c_str(), m_selected_game_object == go->GetHandle())) {
                            if (m_selected_game_object != go->GetHandle()) {
                                selected_changed = true;
                            }
                            m_selected_game_object = go->GetHandle();
                        }
                    }
                    render_context_menu(*go);
                    ImGui::PopID();
                }
            } else {
                // Tree mode: start from roots and DFS through valid children handles.
                std::vector<ObjectHandle> root_handles;
                root_handles.reserve(game_objects.size());
                for (const auto &go : game_objects) {
                    const ObjectHandle parent_handle = go->GetParent();
                    if (!parent_handle.IsValid()) {
                        root_handles.push_back(go->GetHandle());
                    }
                }

                std::unordered_set<uint32_t> visited;
                std::function<void(ObjectHandle)> render_tree_node;
                render_tree_node = [&](ObjectHandle handle) {
                    Engine::GameObject *go = get_go(handle);
                    if (go == nullptr) {
                        return;
                    }

                    if (visited.find(handle.GetID()) != visited.end()) {
                        return;
                    }
                    visited.insert(handle.GetID());

                    // Skip dangling children to keep hierarchy robust under queued deletions.
                    std::vector<ObjectHandle> valid_children;
                    for (ObjectHandle child_handle : go->GetChildren()) {
                        if (get_go(child_handle) != nullptr) {
                            valid_children.push_back(child_handle);
                        }
                    }
                    const bool has_child = !valid_children.empty();

                    ImGui::PushID(static_cast<int>(handle.GetID()));
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                               | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (!has_child) {
                        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    }
                    if (m_selected_game_object == handle) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }

                    bool opened = false;
                    if (m_renaming_game_object == handle) {
                        opened = ImGui::TreeNodeEx("##tree_item", flags, "%s", "");
                        const bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
                        if (clicked && m_selected_game_object != handle) {
                            selected_changed = true;
                            m_selected_game_object = handle;
                        }
                        render_context_menu(*go);
                        ImGui::SameLine();
                        render_rename_input(*go);
                    } else {
                        opened = ImGui::TreeNodeEx("##tree_item", flags, "%s", go->m_name.c_str());
                        const bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
                        if (clicked) {
                            if (m_selected_game_object != handle) {
                                selected_changed = true;
                            }
                            m_selected_game_object = handle;
                        }
                        render_context_menu(*go);
                    }

                    if (has_child && opened) {
                        for (ObjectHandle child_handle : valid_children) {
                            render_tree_node(child_handle);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };

                for (ObjectHandle root_handle : root_handles) {
                    render_tree_node(root_handle);
                }
            }

            // Hotkeys for selected game object
            if (m_selected_game_object.IsValid()) {
                Engine::GameObject *selected_go = get_go(m_selected_game_object);
                if (selected_go == nullptr) {
                    m_selected_game_object.Reset();
                    selected_changed = true;
                } else {
                    // F2 to rename
                    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                        m_renaming_game_object = m_selected_game_object;
                        m_rename_buffer = selected_go->m_name;
                    }
                    // Delete to remove
                    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                        need_remove_go = m_selected_game_object;
                    }
                }
            }
        }
        ImGui::End();

        if (need_remove_go.IsValid()) {
            if (m_selected_game_object == need_remove_go) {
                m_selected_game_object.Reset();
                selected_changed = true;
            }
            if (m_renaming_game_object == need_remove_go) {
                m_renaming_game_object.Reset();
                m_rename_buffer.clear();
            }
            scene.RemoveGameObject(need_remove_go);
        }
        if (need_rename_go.IsValid()) {
            if (Engine::GameObject *rename_go = scene.GetGameObject(need_rename_go)) {
                rename_go->m_name = m_rename_buffer;
            }
        }

        if (selected_changed) {
            m_OnGameObjectSelectedDelegate.Invoke(m_selected_game_object);
        }
    }
} // namespace Editor
