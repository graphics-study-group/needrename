#include "GUISystem.h"

#include "Render/RenderSystem.h"
#include "Render/Pipeline/CommandBuffer/RenderCommandBuffer.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

namespace Engine {
    void GUISystem::CleanUp()
    {
        if (m_context != nullptr) {
            ImGui_ImplSDL3_Shutdown();
            ImGui_ImplVulkan_Shutdown();
            ImGui::DestroyContext(m_context);
        }
        m_context = nullptr;
    }

    GUISystem::GUISystem(std::shared_ptr<RenderSystem> render_system) : m_render_system(render_system)
    {
    }

    GUISystem::~GUISystem()
    {
        CleanUp();
    }

    bool GUISystem::WantCaptureMouse() const
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool GUISystem::WantCaptureKeyboard() const
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void GUISystem::ProcessEvent(SDL_Event *event) const
    {
        ImGui_ImplSDL3_ProcessEvent(event);
    }

    void GUISystem::PrepareGUI() const
    {
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
    }

    void GUISystem::DrawGUI(RenderCommandBuffer &cb) const
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cb.get()), nullptr);
    }

    void GUISystem::Create(SDL_Window *window)
    {
        SDL_LogInfo(0, "Initializing GUI system with ImGui.");
        assert(m_context == nullptr && "Re-creating GUI system.");
        m_context = ImGui::CreateContext();
        assert(m_context && "Failed to create ImGui context.");
        ImGui_ImplSDL3_InitForVulkan(window);

        auto system = m_render_system.lock();
        const auto & swapchain = system->GetSwapchain();

        ImGui_ImplVulkan_InitInfo info {};
        /* info.CheckVkResultFn = [](VkResult result){
            vk::detail::resultCheck(static_cast<vk::Result>(result), "ImGui check: ");
        }; */
        info.Instance = system->getInstance();
        info.PhysicalDevice = system->GetPhysicalDevice();
        info.Device = system->getDevice();
        info.Queue = system->getQueueInfo().graphicsQueue;
        info.DescriptorPool = system->GetGlobalConstantDescriptorPool().get();
        info.ImageCount = swapchain.GetFrameCount();
        info.MinImageCount = info.ImageCount;
        info.UseDynamicRendering = true;
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineRenderingCreateInfoKHR pipeline{
            static_cast<VkPipelineRenderingCreateInfoKHR>(swapchain.GetPipelineRenderingCreateInfo())
        };
        info.PipelineRenderingCreateInfo = pipeline;

        if(!ImGui_ImplVulkan_Init(&info)) SDL_LogCritical(0, "Failed to initialize Vulkan backend for ImGui.");
    }
}
