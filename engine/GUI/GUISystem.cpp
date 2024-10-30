#include "GUISystem.h"

#include "Render/RenderSystem.h"
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

    void GUISystem::Create(SDL_Window * window)
    {
        SDL_LogInfo(0, "Initializing GUI system with ImGui.");
        assert(m_context == nullptr && "Re-creating GUI system.");
        m_context = ImGui::CreateContext();
        assert(m_context && "Failed to create ImGui context.");
        ImGui_ImplSDL3_InitForVulkan(window);

        auto system = m_render_system.lock();
        const auto & swapchain = system->GetSwapchain();
        VkFormat swapchain_format = static_cast<VkFormat>(swapchain.GetImageFormat().format);

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

        VkPipelineRenderingCreateInfoKHR pipeline {};
        pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        pipeline.pNext = nullptr;
        pipeline.colorAttachmentCount = 1;
        pipeline.pColorAttachmentFormats = &swapchain_format;
        pipeline.depthAttachmentFormat = static_cast<VkFormat>(ImageUtils::GetVkFormat(swapchain.DEPTH_FORMAT));
        pipeline.viewMask = 0;
        info.PipelineRenderingCreateInfo = pipeline;

        assert(ImGui_ImplVulkan_Init(&info) && "Failed to initialize Vulkan backend.");
    }
}
