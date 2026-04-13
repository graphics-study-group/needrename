#include "RenderSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/ResizableRTTManager.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/Camera.h"
#include "Render/Resource/RenderResourceManager.h"

#include <Core/Functional/SDLWindow.h>
#include <MainClass.h>
#include <UserInterface/GUISystem.h>

#include <iostream>

namespace Engine {
    struct RenderSystem::impl {
        impl(RenderSystem &parent, std::weak_ptr<SDLWindow> parent_window) :
            m_window(parent_window), m_allocator_state(parent), m_frame_manager(parent),
            m_render_resource_manager(parent), m_renderer_manager(parent), m_scene_data_manager(parent),
            m_camera_manager(parent), m_resizable_rtt_manger(parent) {

            };

        /// @brief Create a swap chain, possibly replace the older one.
        void CreateSwapchain();

        std::weak_ptr<SDLWindow> m_window;

        // Order of declaration effects destructing order!
        std::unique_ptr<RenderSystemState::DeviceInterface> m_device_interface{};
        std::unique_ptr<RenderSystemState::ImmutableResourceCache> m_immutable_resource_cache{};

        RenderSystemState::AllocatorState m_allocator_state;
        RenderSystemState::Swapchain m_swapchain{};
        RenderSystemState::FrameManager m_frame_manager;
        RenderSystemState::RenderResourceManager m_render_resource_manager;
        RenderSystemState::RendererManager m_renderer_manager;
        RenderSystemState::SceneDataManager m_scene_data_manager;
        RenderSystemState::CameraManager m_camera_manager;
        RenderSystemState::ResizableRTTManager m_resizable_rtt_manger;
    };

    RenderSystem::RenderSystem(std::weak_ptr<SDLWindow> parent_window) :
        pimpl(std::make_unique<RenderSystem::impl>(*this, parent_window)) {
    }

    void RenderSystem::Create() {
        assert(!this->pimpl->m_device_interface.get() && "Recreating render system");
        RenderSystemState::DeviceInterface::DeviceConfiguration cfg{
            .window = pimpl->m_window.lock()->GetWindow(),
            .application_name = "",
            .application_version = 0,
            .dynamic_dispatcher = nullptr
        };
        pimpl->m_device_interface = std::make_unique<RenderSystemState::DeviceInterface>(cfg);
        pimpl->m_immutable_resource_cache =
            std::make_unique<RenderSystemState::ImmutableResourceCache>(pimpl->m_device_interface->GetDevice());

        pimpl->CreateSwapchain();

        pimpl->m_allocator_state.Create();

        pimpl->m_frame_manager.Create();
        pimpl->m_scene_data_manager.Create();
        pimpl->m_camera_manager.Create();
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Vulkan initialization finished.");
    }

    RenderSystem::~RenderSystem() {
        // Resources are released by RAII.
        // SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Destroying other resources by RAII.");
        std::cerr << "Render system deconstructed" << std::endl;
    }

    void RenderSystem::CompleteFrame(
        const RenderTargetTexture &present_texture,
        MemoryAccessTypeImageBits last_access,
        uint32_t width,
        uint32_t height,
        uint32_t offset_x,
        uint32_t offset_y
    ) {
        if (pimpl->m_frame_manager.PresentToFramebuffer(
                present_texture.GetImage(), last_access, {width, height}, {(int32_t)offset_x, (int32_t)offset_y}
            )) {
            this->UpdateSwapchain();
        }
        pimpl->m_render_resource_manager.TickFrame();
    }

    void RenderSystem::CompleteFrame(
        const RenderTargetTexture &present_texture,
        uint32_t width,
        uint32_t height,
        uint32_t offset_x,
        uint32_t offset_y
    ) {
        this->CompleteFrame(
            present_texture, MemoryAccessTypeImageBits::ColorAttachmentWrite, width, height, offset_x, offset_y
        );
    }

    vk::Device RenderSystem::GetDevice() const {
        return pimpl->m_device_interface->GetDevice();
    }
    const RenderSystemState::DeviceInterface &RenderSystem::GetDeviceInterface() const {
        return *pimpl->m_device_interface;
    }

    const RenderSystemState::AllocatorState &RenderSystem::GetAllocatorState() const {
        return pimpl->m_allocator_state;
    }

    const RenderSystemState::Swapchain &RenderSystem::GetSwapchain() const {
        return pimpl->m_swapchain;
    }

    RenderSystemState::RenderResourceManager &RenderSystem::GetRenderResourceManager() {
        return pimpl->m_render_resource_manager;
    }

    RenderSystemState::FrameManager &RenderSystem::GetFrameManager() {
        return pimpl->m_frame_manager;
    }

    RenderSystemState::RendererManager &RenderSystem::GetRendererManager() {
        return pimpl->m_renderer_manager;
    }

    RenderSystemState::ImmutableResourceCache &RenderSystem::GetIRCache() {
        return *pimpl->m_immutable_resource_cache;
    }

    RenderSystemState::CameraManager &RenderSystem::GetCameraManager() {
        return pimpl->m_camera_manager;
    }

    RenderSystemState::SceneDataManager &RenderSystem::GetSceneDataManager() {
        return pimpl->m_scene_data_manager;
    }

    RenderSystemState::ResizableRTTManager &RenderSystem::GetResizableRTTManager() {
        return pimpl->m_resizable_rtt_manger;
    }

    void RenderSystem::WaitForIdle() const {
        pimpl->m_device_interface->GetDevice().waitIdle();
    }

    void RenderSystem::UpdateSwapchain() {
        this->WaitForIdle();
        pimpl->CreateSwapchain();
    }

    uint32_t RenderSystem::StartFrame() {
        auto fb = pimpl->m_frame_manager.StartFrame();
        GetCameraManager().FetchCameraData();
        GetCameraManager().UploadCameraData(GetFrameManager().GetFrameInFlight());

        GetSceneDataManager().FetchLightData();
        GetSceneDataManager().UploadSceneData(GetFrameManager().GetFrameInFlight());
        return fb;
    }

    void RenderSystem::impl::CreateSwapchain() {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating swap chain.");

        uint32_t width, height;
        int w, h;
        SDL_GetWindowSizeInPixels(m_window.lock()->GetWindow(), &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
        vk::Extent2D expected_extent{width, height};

        m_swapchain.CreateSwapchain(*m_device_interface, expected_extent);
        m_resizable_rtt_manger.SetReferenceSize(w, h);
    }
} // namespace Engine
