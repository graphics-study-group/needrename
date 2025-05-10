#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <fstream>

#include <cmake_config.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Render/RenderSystem.h>
#include <Render/AttachmentUtils.h>
#include <Render/Memory/Image2DTexture.h>
#include <Render/Memory/Buffer.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/RenderSystem/Swapchain.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Framework/world/WorldSystem.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Input/Input.h>
#include <GUI/GUISystem.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

using namespace Engine;

int main()
{
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Editor"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto rsys = cmc->GetRenderSystem();
    auto asset_manager = cmc->GetAssetManager();
    auto world = cmc->GetWorldSystem();
    auto gui = std::make_shared<GUISystem>(rsys);
    gui->Create(cmc->GetWindow()->GetWindow());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    Engine::AllocatedImage2D color{rsys}, depth{rsys};
    color.Create(1920, 1080, Engine::ImageUtils::ImageType::ColorGeneral, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
    depth.Create(1920, 1080, Engine::ImageUtils::ImageType::DepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
    color_att.image = color.GetImage();
    color_att.image_view = color.GetImageView();
    color_att.load_op = vk::AttachmentLoadOp::eClear;
    color_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_att.image = depth.GetImage();
    depth_att.image_view = depth.GetImageView();
    depth_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_att.store_op = vk::AttachmentStoreOp::eDontCare;

    vk::SamplerCreateInfo sci{};
    sci.magFilter = sci.minFilter = vk::Filter::eNearest;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = vk::SamplerAddressMode::eRepeat;
    vk::Sampler sampler = rsys->getDevice().createSampler(sci);
    ImTextureID color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(sampler, color_att.image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    Engine::AllocatedImage2D color_editor{rsys}, depth_editor{rsys};
    color_editor.Create(1920, 1080, Engine::ImageUtils::ImageType::ColorAttachment, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
    depth_editor.Create(1920, 1080, Engine::ImageUtils::ImageType::DepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);

    Engine::AttachmentUtils::AttachmentDescription color_editor_att, depth_editor_att;
    color_editor_att.image = color_editor.GetImage();
    color_editor_att.image_view = color_editor.GetImageView();
    color_editor_att.load_op = vk::AttachmentLoadOp::eClear;
    color_editor_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_editor_att.image = depth_editor.GetImage();
    depth_editor_att.image_view = depth_editor.GetImageView();
    depth_editor_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_editor_att.store_op = vk::AttachmentStoreOp::eDontCare;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    Uint64 FPS_TIMER = 0;
    while (!onQuit)
    {
        Uint64 current_time = SDL_GetTicksNS();
        float dt = (current_time - FPS_TIMER) * 1e-9f;
        FPS_TIMER = current_time;

        asset_manager->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                onQuit = true;
                break;
            }
            gui->ProcessEvent(&event);
            if (gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
                continue;
            if (gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
                continue;
            cmc->GetInputSystem()->ProcessEvent(&event);
        }

        cmc->GetInputSystem()->Update(dt);
        world->Tick(dt);

        ImGui_ImplSDL3_NewFrame();

        // Draw
        rsys->StartFrame();
        RenderCommandBuffer &cb = rsys->GetCurrentCommandBuffer();

        cb.Begin();
        {
            cb.BeginRendering(color_att, depth_att, color.GetExtent());
            rsys->DrawMeshes();
            // TODO: Event Process ????
            // ImGui_ImplVulkan_NewFrame();
            // ImGui::NewFrame();
            // gui->DrawGUI(cb);
            cb.EndRendering();
        }

        cb.InsertAttachmentBarrier(RenderCommandBuffer::AttachmentBarrierType::ColorAttachmentRAW, color_att.image);

        {
            vk::Extent2D extent2{rsys->GetSwapchain().GetExtent()};
            cb.BeginRendering(color_editor_att, depth_editor_att, extent2);
            ImGui_ImplVulkan_NewFrame();

            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable; // 设置停靠空间

            ImGui::NewFrame();

            // 窗口的 ID 和 标题
            ImGuiID dockspaceID         = ImGui::GetID("##ui.dock_space");
            const char* UI_DOCK_WINDOW  = "##ui.dock_window";
            const char* UI_PROJECT_BOX  = "Project##ui.project";
            const char* UI_PROPERTY_BOX = "Property##ui.property";
            const char* UI_TOOL_BOX     = "Tools##ui.tools";
            const char* UI_MESSAGE_BOX  = "Message##ui.message";
            const char* UI_LOG_BOX      = "Log##ui.log";
            const char* UI_VIEW_BOX     = "##ui.view";

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
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
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) { // 判断是否开启停靠
                // 判断是否有根节点，防止一直重建
                if (!ImGui::DockBuilderGetNode(dockspaceID)) {
                    // 移除根节点
                    ImGui::DockBuilderRemoveNode(dockspaceID);

                    // 创建根节点
                    ImGuiID root = ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_None);

                    // 设置根节点位置大小
                    ImGui::DockBuilderSetNodePos(root, { 0.0f, 0.0f });
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

                    ImGui::DockBuilderDockWindow(UI_MESSAGE_BOX, bottomNode);      // 下面节点同时停靠两个窗口
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
            }
            ImGui::End(); // 结束停靠窗口

            // 工程框
            if (ImGui::Begin(UI_PROJECT_BOX)) {
                ImGui::LabelText("label", "text");
                ImGui::Button("button");
            }
            ImGui::End();

            // 属性框
            if (ImGui::Begin(UI_PROPERTY_BOX)) {
                ImGui::LabelText("label", "text");
                ImGui::Button("button");
            }
            ImGui::End();

            // 工具框
            if (ImGui::Begin(UI_TOOL_BOX)) {
                ImGui::LabelText("label", "text");
                ImGui::Button("button");

                if (ImGui::Button("重置布局")) {
                    // 移除根节点，布局会自动重建
                    ImGui::DockBuilderRemoveNode(dockspaceID);
                }
            }
            ImGui::End();

            // 消息框
            if (ImGui::Begin(UI_MESSAGE_BOX)) {
                ImGui::LabelText("label", "text");
                ImGui::Button("button");
            }
            ImGui::End();

            // 日志框
            if (ImGui::Begin(UI_LOG_BOX)) {
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

            if (ImGui::Begin(UI_VIEW_BOX, 0, ImGuiWindowFlags_NoBackground)) { // 无背景窗口
                ImGui::Image(color_att_id, ImVec2(1280, 720));
                // 获取窗口坐标
                // ImVec2 pos  = ImGui::GetWindowPos();
                // ImVec2 size = ImGui::GetWindowSize();

                // ImGui::Text("position: %0.2f, %0.2f", pos.x, pos.y);
                // ImGui::Text("size: %0.2f, %0.2f", size.x, size.y);

                // 记录下视口位置给本地 API 使用
                // g_viewportPos  = glm::ivec2(pos.x, size.y);
                // g_viewportSize = glm::ivec2(size.x, size.y);
            }
            ImGui::End();
            ImGui::PopStyleVar(3); // 弹出样式设置

            gui->DrawGUI(cb);
            cb.EndRendering();
        }

        cb.End();
        cb.Submit();

        rsys->GetFrameManager().StageCopyComposition(color_editor.GetImage());
        rsys->CompleteFrame();
    }
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
