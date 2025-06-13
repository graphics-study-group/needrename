#include "MainWindow.h"
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <Editor/Widget/HierarchyWidget.h>
#include <Editor/Widget/ProjectWidget.h>
#include <Editor/Widget/InspectorWidget.h>

namespace Editor
{
    MainWindow::MainWindow()
    {
        auto hierachy_widget = std::make_shared<HierarchyWidget>(k_hierarchy_widget_name);
        AddWidget(hierachy_widget);
        auto project_widget = std::make_shared<ProjectWidget>(k_project_widget_name);
        AddWidget(project_widget);
        auto inspector_widget = std::make_shared<InspectorWidget>(k_inspector_widget_name);
        hierachy_widget->m_OnGameObjectSelectedDelegate.AddDelegate(inspector_widget, &InspectorWidget::SetSelectedGameObject);
        AddWidget(inspector_widget);
    }

    void MainWindow::Render()
    {
        int windowFlags = ImGuiWindowFlags_NoDecoration            // Disable title-bar, resizing, scrollbars, and collapsing
                          | ImGuiWindowFlags_NoMove                // Disable user moving the window
                          | ImGuiWindowFlags_NoScrollWithMouse     // Disable scrolling with mouse
                          | ImGuiWindowFlags_NoBackground          // Disable drawing background color (WindowBg, etc.) and outside border
                          | ImGuiWindowFlags_MenuBar               // Has a menu-bar
                          | ImGuiWindowFlags_NoBringToFrontOnFocus // Disable bringing this window to front when focused
                          | ImGuiWindowFlags_NoDocking             // Disable docking of this window
            ;
        float current_height = 0.0f;

        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);            // No border
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); // No padding
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);              // No roundin
        ImGui::Begin(k_main_window_widget_name, 0, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID main_dockspace_id = ImGui::GetID(k_main_dockspace_name);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::BeginMenu("Layout"))
                {
                    if (ImGui::MenuItem("Reset"))
                    {
                        ImGui::DockBuilderRemoveNode(main_dockspace_id);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos());
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetWindowWidth(), 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        if (ImGui::Begin(k_control_widget_name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
        {
            // Center the buttons horizontally
            float button_width = 70.0f; // Approximate width of each button
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            int button_count = 5;
            float total_width = button_count * button_width + (button_count - 1) * spacing;
            float avail_width = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
            float offset_x = (avail_width - total_width) * 0.5f;
            if (offset_x > 0)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

            if (ImGui::Button("Start", ImVec2(button_width, 0)))
            {
                m_is_playing = true;
                m_is_paused = false;
                m_OnStart.Invoke();
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop", ImVec2(button_width, 0)))
            {
                m_is_playing = false;
                m_is_paused = false;
                m_OnStop.Invoke();
            }
            ImGui::SameLine();
            if (ImGui::Button("Pause", ImVec2(button_width, 0)))
            {
                if (m_is_playing && !m_is_paused)
                {
                    m_is_paused = true;
                    m_OnPause.Invoke();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Resume", ImVec2(button_width, 0)))
            {
                if (m_is_playing && m_is_paused)
                {
                    m_is_paused = false;
                    m_OnResume.Invoke();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Step", ImVec2(button_width, 0)))
            {
                if (m_is_playing && m_is_paused)
                {
                    m_OnStep.Invoke();
                }
            }
            current_height = ImGui::GetWindowSize().y;
            ImGui::End();
        }
        ImGui::PopStyleVar(2);

        // Create a dockspace if it doesn't exist yet
        if (!ImGui::DockBuilderGetNode(main_dockspace_id))
        {
            ImGui::DockBuilderRemoveNode(main_dockspace_id);
            ImGui::DockBuilderAddNode(main_dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(main_dockspace_id, viewport->WorkSize);

            ImGuiID dock_main_id = main_dockspace_id;
            ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
            ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);
            ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.25f, nullptr, &dock_main_id);

            ImGui::DockBuilderDockWindow(k_hierarchy_widget_name, dock_left);
            ImGui::DockBuilderDockWindow(k_project_widget_name, dock_bottom);
            ImGui::DockBuilderDockWindow(k_inspector_widget_name, dock_right);
            ImGui::DockBuilderDockWindow(k_game_widget_name, dock_main_id);

            ImGui::DockBuilderFinish(main_dockspace_id);
        }

        ImVec2 dockspace_pos = ImGui::GetCursorScreenPos();
        dockspace_pos.y += current_height;
        ImVec2 dockspace_size = ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - current_height);
        ImGui::SetNextWindowPos(dockspace_pos);
        ImGui::SetNextWindowSize(dockspace_size);

        int dockspace_widget_flag = ImGuiWindowFlags_NoDecoration            //
                                    | ImGuiWindowFlags_NoMove                //
                                    | ImGuiWindowFlags_NoScrollWithMouse     //
                                    | ImGuiWindowFlags_NoBackground          //
                                    | ImGuiWindowFlags_NoFocusOnAppearing    //
                                    | ImGuiWindowFlags_NoBringToFrontOnFocus //
            ;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        if (ImGui::BeginChild(k_main_dockspace_name, dockspace_size, ImGuiChildFlags_None, dockspace_widget_flag))
        {
            ImGui::DockSpace(main_dockspace_id);
            ImGui::EndChild();
        }
        ImGui::PopStyleVar(3);

        ImGui::End(); // main window

        for (auto &widget : m_widgets)
        {
            widget->Render();
        }
    }

    void MainWindow::AddWidget(std::shared_ptr<Widget> widget)
    {
        m_widgets.push_back(widget);
    }
}
