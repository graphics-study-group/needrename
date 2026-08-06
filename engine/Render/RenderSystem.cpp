#include "RenderSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#include "Framework/component/RenderComponent/RendererComponent.h"
#include "GpuContext/AllocatorState.h"
#include "GpuContext/DeviceInterface.h"
#include "GpuContext/Structs.h"
#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/HeadlessPresentProvider.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/ResizableRTTManager.h"
#include "Render/RenderSystem/SwapchainPresentProvider.h"
#include "Render/Renderer/Camera.h"
#include "Render/Resource/AllRenderResourceManagers.h"

#include <Core/Functional/SDLWindow.h>
#include <MainClass.h>
#include <UserInterface/GUISystem.h>

#include <iostream>
#include <vulkan/vulkan.hpp>

namespace Engine {
    struct RenderSystem::impl {
        impl(RenderSystem &parent, std::weak_ptr<SDLWindow> parent_window) :
            m_window(parent_window), m_frame_manager(parent), m_renderer_manager(parent), m_scene_data_manager(parent),
            m_camera_manager(parent), m_resizable_rtt_manger(parent), m_material_instance_provider(parent),
            m_material_library_provider(parent), m_static_mesh_resource_provider(parent) {

            };

        std::weak_ptr<SDLWindow> m_window;

        // Order of declaration effects destructing order!
        std::unique_ptr<RenderSystemState::DeviceInterface> m_device_interface{};
        std::unique_ptr<RenderSystemState::ImmutableResourceCache> m_immutable_resource_cache{};

        std::unique_ptr<RenderSystemState::AllocatorState> m_allocator_state;
        std::unique_ptr<IPresentProvider> m_present_provider;
        RenderSystemState::FrameManager m_frame_manager;
        RenderSystemState::RendererManager m_renderer_manager;
        RenderSystemState::SceneDataManager m_scene_data_manager;
        RenderSystemState::CameraManager m_camera_manager;
        RenderSystemState::ResizableRTTManager m_resizable_rtt_manger;

        RenderSystemState::MaterialInstanceManager m_material_instance_provider;
        RenderSystemState::MaterialLibraryManager m_material_library_provider;
        RenderSystemState::StaticMeshResourceManager m_static_mesh_resource_provider;
    };

    RenderSystem::RenderSystem(std::weak_ptr<SDLWindow> parent_window) :
        pimpl(std::make_unique<RenderSystem::impl>(*this, parent_window)), m_resource_managers{
                                                                               &pimpl->m_material_instance_provider,
                                                                               &pimpl->m_material_library_provider,
                                                                               &pimpl->m_static_mesh_resource_provider
                                                                           } {
    }

    void RenderSystem::Create() {
        assert(!this->pimpl->m_device_interface.get() && "Recreating render system");

        bool is_headless = pimpl->m_window.expired();
        SDL_Window *sdl_window = is_headless ? nullptr : pimpl->m_window.lock()->GetWindow();

        RenderSystemState::DeviceInterface::DeviceConfiguration cfg{
            .window = sdl_window, .application_name = "", .application_version = 0, .dynamic_dispatcher = nullptr
        };
        pimpl->m_device_interface = std::make_unique<RenderSystemState::DeviceInterface>(cfg);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(pimpl->m_device_interface->GetInstance(), ::vkGetInstanceProcAddr);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(pimpl->m_device_interface->GetDevice());
        pimpl->m_immutable_resource_cache =
            std::make_unique<RenderSystemState::ImmutableResourceCache>(pimpl->m_device_interface->GetDevice());

        if (is_headless) {
            vk::Extent2D extent{1920, 1080};
            pimpl->m_present_provider = std::make_unique<RenderSystemState::HeadlessPresentProvider>(
                *pimpl->m_device_interface, extent, vk::Format::eR8G8B8A8Unorm, 3
            );
            pimpl->m_resizable_rtt_manger.SetReferenceSize(extent.width, extent.height);
        } else {
            uint32_t width, height;
            int w, h;
            SDL_GetWindowSizeInPixels(sdl_window, &w, &h);
            width = static_cast<uint32_t>(w);
            height = static_cast<uint32_t>(h);
            vk::Extent2D expected_extent{width, height};

            auto spp = std::make_unique<RenderSystemState::SwapchainPresentProvider>();
            spp->Initialize(*pimpl->m_device_interface, expected_extent);
            pimpl->m_present_provider = std::move(spp);
            pimpl->m_resizable_rtt_manger.SetReferenceSize(w, h);
        }

        pimpl->m_allocator_state = std::make_unique<RenderSystemState::AllocatorState>();
        pimpl->m_allocator_state->SetDeviceInterface(*pimpl->m_device_interface);
        pimpl->m_allocator_state->Create();

        pimpl->m_frame_manager.Create(*pimpl->m_present_provider);
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
        const RenderTargetTexture &present_texture, MemoryAccessTypeImageBits last_access
    ) {
        if (pimpl->m_frame_manager.SubmitFrame(present_texture, last_access)) {
            this->UpdateSwapchain();
        }

        pimpl->m_material_instance_provider.TickFrame();
        pimpl->m_material_library_provider.TickFrame();
        pimpl->m_static_mesh_resource_provider.TickFrame();
    }

    vk::Device RenderSystem::GetDevice() const {
        return pimpl->m_device_interface->GetDevice();
    }
    const RenderSystemState::DeviceInterface &RenderSystem::GetDeviceInterface() const {
        return *pimpl->m_device_interface;
    }

    const RenderSystemState::AllocatorState &RenderSystem::GetAllocatorState() const {
        return *pimpl->m_allocator_state;
    }

    IPresentProvider &RenderSystem::GetPresentProvider() {
        return *pimpl->m_present_provider;
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
        uint32_t width, height;
        int w, h;
        SDL_GetWindowSizeInPixels(pimpl->m_window.lock()->GetWindow(), &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
        pimpl->m_present_provider->Recreate({width, height});
        pimpl->m_resizable_rtt_manger.SetReferenceSize(w, h);
    }

    uint32_t RenderSystem::StartFrame() {
        auto fb = pimpl->m_frame_manager.StartFrame();
        if (fb == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date (e.g. window resize): recreate and retry once.
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Acquire returned out-of-date; recreating swapchain and retrying.");
            this->UpdateSwapchain();
            fb = pimpl->m_frame_manager.StartFrame();
        }
        if (fb == std::numeric_limits<uint32_t>::max()) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Acquire still out-of-date after recreation; skipping frame.");
            return fb;
        }

        GetCameraManager().FetchCameraData();
        GetCameraManager().UploadCameraData(GetFrameManager().GetFrameInFlight());

        GetSceneDataManager().FetchLightData();
        GetSceneDataManager().UploadSceneData(GetFrameManager().GetFrameInFlight());
        return fb;
    }

} // namespace Engine
