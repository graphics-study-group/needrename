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
        auto hierachy_widget = std::make_shared<HierarchyWidget>("Hierarchy");
        AddWidget(hierachy_widget);
        auto project_widget = std::make_shared<ProjectWidget>("Project");
        AddWidget(project_widget);
        auto inspector_widget = std::make_shared<InspectorWidget>("Inspector");
        AddWidget(inspector_widget);
    }

    void MainWindow::Render()
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        int windowFlags = ImGuiWindowFlags_NoDecoration        // Disable title-bar, resizing, scrollbars, and collapsing
                          | ImGuiWindowFlags_NoMove            // Disable user moving the window
                          | ImGuiWindowFlags_NoScrollWithMouse // Disable scrolling with mouse
                          | ImGuiWindowFlags_NoBackground      // Disable drawing background color (WindowBg, etc.) and outside border
                          | ImGuiWindowFlags_NoSavedSettings   // Never load/save settings in .ini file
                          | ImGuiWindowFlags_MenuBar           // Has a menu-bar
                          | ImGuiWindowFlags_NoBringToFrontOnFocus // Disable bringing this window to front when focused
                          | ImGuiWindowFlags_NoInputs             // No mouse, keyboard/gamepad navigation within the window
                          | ImGuiWindowFlags_NoDocking         // Disable docking of this window
            ;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);            // No border
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); // No padding
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);              // No roundin
        ImGui::Begin(k_main_window_widget_name, 0, windowFlags);
        ImGui::PopStyleVar(3);

        // Create a dockspace if it doesn't exist yet
        ImGuiID dockspaceID = ImGui::GetID(k_main_window_dockspace_name);
        if (!ImGui::DockBuilderGetNode(dockspaceID))
        {
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::DockSpace(dockspaceID);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::End();

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
