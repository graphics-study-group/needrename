#include "GUISystem.h"

#include "Render/AttachmentUtilsFunc.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/Swapchain.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    void GUISystem::CleanUp() {
        if (m_context != nullptr) {
            ImGui_ImplSDL3_Shutdown();
            if (ImGui::GetIO().BackendRendererUserData) {
                ImGui_ImplVulkan_Shutdown();
            }
            ImGui::DestroyContext(m_context);
        }
        m_context = nullptr;
    }

    GUISystem::GUISystem() {
    }

    GUISystem::~GUISystem() {
        CleanUp();
    }

    bool GUISystem::WantCaptureMouse() const {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool GUISystem::WantCaptureKeyboard() const {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void GUISystem::ProcessEvent(SDL_Event *event) const {
        ImGui_ImplSDL3_ProcessEvent(event);
    }

    void GUISystem::PrepareGUI() const {
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
    }

    void GUISystem::DrawGUI(
        const AttachmentUtils::AttachmentDescription &att, vk::Extent2D extent, GraphicsCommandBuffer &cb
    ) const {
        ImGui::Render();
        cb.BeginRendering(att, {}, extent, "GUI rendering pass");
        ImGui_ImplVulkan_RenderDrawData(
            ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cb.GetCommandBuffer()), nullptr
        );
        cb.EndRendering();
    }

    void GUISystem::Create(SDL_Window *window) {
        SDL_LogInfo(0, "Initializing GUI system with ImGui.");
        assert(m_context == nullptr && "Re-creating GUI system.");
        m_context = ImGui::CreateContext();
        assert(m_context && "Failed to create ImGui context.");
        ImGui_ImplSDL3_InitForVulkan(window);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    void GUISystem::CreateVulkanBackend(RenderSystem & render_system, vk::Format color_attachment_format) {

        const auto &swapchain = render_system.GetSwapchain();
        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = render_system.getInstance();
        info.PhysicalDevice = render_system.GetPhysicalDevice();
        info.Device = render_system.getDevice();
        info.Queue = render_system.getQueueInfo().graphicsQueue;
        info.DescriptorPool = render_system.GetGlobalConstantDescriptorPool().get();
        info.ImageCount = swapchain.GetFrameCount();
        info.MinImageCount = info.ImageCount;
        info.UseDynamicRendering = true;
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        std::array<vk::Format, 1> formats = {
            {color_attachment_format == vk::Format::eUndefined ? render_system.GetSwapchain().COLOR_FORMAT_VK
                                                               : color_attachment_format}
        };
        VkPipelineRenderingCreateInfoKHR pipeline{static_cast<VkPipelineRenderingCreateInfoKHR>(
            vk::PipelineRenderingCreateInfo{0, formats, vk::Format::eUndefined, vk::Format::eUndefined}
        )};
        info.PipelineRenderingCreateInfo = pipeline;

        if (!ImGui_ImplVulkan_Init(&info)) SDL_LogCritical(0, "Failed to initialize Vulkan backend for ImGui.");
    }
    ImGuiContext *GUISystem::GetCurrentContext() const {
        assert(this->m_context);
        return this->m_context;
    }
} // namespace Engine
