#include "GUISystem.h"

#include "Render/AttachmentUtilsFunc.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <iostream>

namespace Engine {

    struct GUISystem::impl {
        ImGuiContext *m_context{nullptr};

        struct VulkanResources {
            static constexpr std::array<vk::DescriptorPoolSize, 11> DESCRIPTOR_POOL_SIZES{
                vk::DescriptorPoolSize{ vk::DescriptorType::eSampler, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eSampledImage, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eUniformTexelBuffer, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eStorageTexelBuffer, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBufferDynamic, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBufferDynamic, 1000 },
                vk::DescriptorPoolSize{ vk::DescriptorType::eInputAttachment, 1000 }
            };
            vk::UniqueDescriptorPool descriptor_pool {};
        } vkr {};

        void CleanUp() {
            if (m_context != nullptr) {
                ImGui_ImplSDL3_Shutdown();
                if (ImGui::GetIO().BackendRendererUserData) {
                    ImGui_ImplVulkan_Shutdown();
                }
                ImGui::DestroyContext(m_context);
            }
            m_context = nullptr;
            // This might not be necessary.
            vkr.descriptor_pool.reset(nullptr);
        }
    };

    GUISystem::GUISystem() : pimpl(std::make_unique<impl>()) {
    }

    GUISystem::~GUISystem() {
        std::cerr << "GUI System deconstructed" << std::endl;
        pimpl->CleanUp();
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
        cb.BeginRendering(att, {}, extent, "GUI rendering pass");
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(
            ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cb.GetCommandBuffer()), nullptr
        );
        cb.EndRendering();
    }

    void GUISystem::DrawGUI(vk::CommandBuffer cb) const noexcept {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(
            ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cb), nullptr
        );
    }

    void GUISystem::Create(SDL_Window *window) {
        SDL_LogInfo(0, "Initializing GUI system with ImGui.");
        assert(pimpl->m_context == nullptr && "Re-creating GUI system.");
        pimpl->m_context = ImGui::CreateContext();
        assert(pimpl->m_context && "Failed to create ImGui context.");
        ImGui_ImplSDL3_InitForVulkan(window);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    void GUISystem::CreateVulkanBackend(
        RenderSystem & render_system,
        vk::Format color_attachment_format,
        uint8_t samples
    ) {

        if(pimpl->vkr.descriptor_pool) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Recreating Vulkan backend for GUI subsystem.");
        } else {
            vk::DescriptorPoolCreateInfo dpci{
                vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                1000,
                pimpl->vkr.DESCRIPTOR_POOL_SIZES
            };
            pimpl->vkr.descriptor_pool = render_system.GetDevice().createDescriptorPoolUnique(dpci);
        }

        const auto &swapchain = render_system.GetSwapchain();
        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = render_system.GetDeviceInterface().GetInstance();
        info.PhysicalDevice = render_system.GetDeviceInterface().GetPhysicalDevice();
        info.Device = render_system.GetDevice();
        info.Queue = render_system.GetDeviceInterface().GetQueueInfo().graphicsQueue;
        info.DescriptorPool = pimpl->vkr.descriptor_pool.get();
        info.ImageCount = swapchain.GetFrameCount();
        info.MinImageCount = info.ImageCount;
        info.UseDynamicRendering = true;

        std::array<vk::Format, 1> formats = {
            {color_attachment_format == vk::Format::eUndefined ? render_system.GetSwapchain().GetColorFormat()
                                                               : color_attachment_format}
        };
        VkPipelineRenderingCreateInfoKHR pipeline{static_cast<VkPipelineRenderingCreateInfoKHR>(
            vk::PipelineRenderingCreateInfo{0, formats, vk::Format::eUndefined, vk::Format::eUndefined}
        )};

        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline;

        if (!ImGui_ImplVulkan_Init(&info)) {
            // This should not happen as ImGui_ImplVulkan_Init only returns true.
            SDL_LogCritical(0, "Failed to initialize Vulkan backend for ImGui.");
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR, 
                "Critical Error",
                "Cannot initialize Vulkan backend for ImGUI.\n"
                "This is an unrecoverable error and the program will now terminate.",
                nullptr);
            std::terminate();
        }
    }
    ImGuiContext *GUISystem::GetCurrentContext() const {
        assert(pimpl->m_context);
        return pimpl->m_context;
    }
    void GUISystem::ResetColorAttachmentFormat(
        vk::Format format,
        uint8_t samples
    ) noexcept {
        VkPipelineRenderingCreateInfoKHR prci{
            static_cast<VkPipelineRenderingCreateInfoKHR>(
                vk::PipelineRenderingCreateInfo{
                    0,
                    {format},
                    vk::Format::eUndefined,
                    vk::Format::eUndefined
                }
            )
        };
        ImGui_ImplVulkan_PipelineInfo pi{
            .RenderPass = nullptr,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            .PipelineRenderingCreateInfo = prci
        };
        ImGui_ImplVulkan_CreateMainPipeline(&pi);
    }
} // namespace Engine
