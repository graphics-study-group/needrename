#include "MainWindow.h"
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

namespace Editor
{
    MainWindow::MainWindow()
    {
    }

    void MainWindow::Tick(float dt)
    {
        for (auto &widget : m_widgets)
        {
            widget->Tick(dt);
        }

        // 窗口的 ID 和 标题
        ImGuiID dockspaceID = ImGui::GetID("##ui.dock_space");
        const char *UI_DOCK_WINDOW = "##ui.dock_window";
        const char *UI_PROJECT_BOX = "Project##ui.project";
        const char *UI_PROPERTY_BOX = "Property##ui.property";
        const char *UI_TOOL_BOX = "Tools##ui.tools";
        const char *UI_MESSAGE_BOX = "Message##ui.message";
        const char *UI_LOG_BOX = "Log##ui.log";
        const char *UI_VIEW_BOX = "##ui.view";

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        int windowFlags = ImGuiWindowFlags_NoDecoration            // 无标题栏、不可改变大小、无滚动条、不可折叠
                          | ImGuiWindowFlags_NoMove                // 不可移动
                          | ImGuiWindowFlags_NoBackground          // 无背景（背景透明）
                          | ImGuiWindowFlags_MenuBar               // 菜单栏
                          | ImGuiWindowFlags_NoDocking             // 不可停靠
                          | ImGuiWindowFlags_NoBringToFrontOnFocus // 无法设置前台和聚焦
                          | ImGuiWindowFlags_NoNavFocus            // 无法通过键盘和手柄聚焦
            ;

        // 压入样式设置
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);            // 无边框
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); // 无边界
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);              // 无圆角
        // ImGui::SetNextWindowBgAlpha(0.0f); // 窗口 alpha 为 0，同样可以不显示背景

        ImGui::Begin(UI_DOCK_WINDOW, 0, windowFlags); // 开始停靠窗口
        ImGui::PopStyleVar(3);                        // 弹出样式设置

        // 创建停靠空间
        // 判断是否有根节点，防止一直重建
        if (!ImGui::DockBuilderGetNode(dockspaceID))
        {
            // 移除根节点
            ImGui::DockBuilderRemoveNode(dockspaceID);

            // 创建根节点
            ImGuiID root = ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_None);

            // 设置根节点位置大小
            ImGui::DockBuilderSetNodePos(root, {0.0f, 0.0f});
            ImGui::DockBuilderSetNodeSize(root, ImGui::GetWindowSize());

            // 分割停靠空间

            // 根节点分割左节点
            ImGuiID leftNode = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.25f, nullptr, &root);

            // 根节点分割右节点
            ImGuiID rightNode = ImGui::DockBuilderSplitNode(root, ImGuiDir_Right, 0.25f / 0.75f, nullptr, &root);

            // 根节点分割下节点
            ImGuiID bottomNode = ImGui::DockBuilderSplitNode(root, ImGuiDir_Down, 0.25f, nullptr, &root);

            // 左节点分割上下节点
            ImGuiID leftTopNode, leftBottomNode;
            ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Up, 0.5f, &leftTopNode, &leftBottomNode);

            // 设置节点属性

            // 禁止其他窗口/节点分割根节点
            // ImGui::DockBuilderGetNode(dockspaceID)->LocalFlags |= ImGuiDockNodeFlags_NoDockingSplit;

            // 设置分割到最后的根节点隐藏标签栏
            ImGui::DockBuilderGetNode(root)->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;

            // 设置节点停靠窗口
            ImGui::DockBuilderDockWindow(UI_PROJECT_BOX, leftTopNode);     // 左上节点
            ImGui::DockBuilderDockWindow(UI_PROPERTY_BOX, leftBottomNode); // 左下节点
            ImGui::DockBuilderDockWindow(UI_TOOL_BOX, rightNode);          // 右边节点

            ImGui::DockBuilderDockWindow(UI_MESSAGE_BOX, bottomNode); // 下面节点同时停靠两个窗口
            ImGui::DockBuilderDockWindow(UI_LOG_BOX, bottomNode);

            ImGui::DockBuilderDockWindow(UI_VIEW_BOX, root); // 观察窗口平铺“客户区”，停靠的节点是 CentralNode

            // 结束停靠设置
            ImGui::DockBuilderFinish(dockspaceID);

            // 设置焦点窗口
            ImGui::SetWindowFocus(UI_VIEW_BOX);
        }

        // 创建停靠空间
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::End(); // 结束停靠窗口

        // 工程框
        if (ImGui::Begin(UI_PROJECT_BOX))
        {
            ImGui::LabelText("label", "text");
            ImGui::Button("button");
        }
        ImGui::End();

        // 属性框
        if (ImGui::Begin(UI_PROPERTY_BOX))
        {
            ImGui::LabelText("label", "text");
            ImGui::Button("button");
        }
        ImGui::End();

        // 工具框
        if (ImGui::Begin(UI_TOOL_BOX))
        {
            ImGui::LabelText("label", "text");
            ImGui::Button("button");

            if (ImGui::Button("Reset docking"))
            {
                // 移除根节点，布局会自动重建
                ImGui::DockBuilderRemoveNode(dockspaceID);
            }
        }
        ImGui::End();

        // 消息框
        if (ImGui::Begin(UI_MESSAGE_BOX))
        {
            ImGui::LabelText("label", "text");
            ImGui::Button("button");
        }
        ImGui::End();

        // 日志框
        if (ImGui::Begin(UI_LOG_BOX))
        {
            ImGui::LabelText("label", "text");
            ImGui::Button("button");
        }
        ImGui::End();

        // 观察窗口，背景设置透明，窗口后面就能进行本地 API 的绘制
        // 压入样式设置
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);            // 无边框
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); // 无边界
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);              // 无圆角
        // ImGui::SetNextWindowBgAlpha(0.0f); // 窗口 alpha 为 0，同样可以不显示背景

        if (ImGui::Begin(UI_VIEW_BOX, 0, ImGuiWindowFlags_NoBackground))
        { // 无背景窗口
            // ImGui::Image(color_att_id, ImVec2(1280, 720));
        }
        ImGui::End();
        ImGui::PopStyleVar(3); // 弹出样式设置
    }

    void MainWindow::AddWidget(std::shared_ptr<Widget> widget)
    {
        m_widgets.push_back(widget);
    }
}
