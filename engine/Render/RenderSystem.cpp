#include "RenderSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/Instance.h"
#include "Render/RenderSystem/MaterialDescriptorManager.h"
#include "Render/RenderSystem/MaterialRegistry.h"
#include "Render/RenderSystem/PhysicalDevice.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/Camera.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include <Functional/SDLWindow.h>
#include <GUI/GUISystem.h>
#include <MainClass.h>

#include <iostream>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Engine {
    struct RenderSystem::impl {
        impl(RenderSystem &parent, std::weak_ptr<SDLWindow> parent_window) :
            m_window(parent_window), m_allocator_state(parent), m_frame_manager(parent) {

            };

        /// @brief Create a vk::SurfaceKHR.
        /// It should be called right after instance creation, before selecting a physical device.
        void CreateSurface();

        /// @brief Create a logical device from selected physical device.
        void CreateLogicalDevice();

        /// @brief Create a swap chain, possibly replace the older one.
        void CreateSwapchain();

        void CreateCommandPools();

        static constexpr const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";
        static constexpr std::array<std::string_view, 1> device_extension_name = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        // uint32_t m_in_flight_frame_id = 0;

        std::weak_ptr<SDLWindow> m_window;
        // TODO: data: mesh, texture, light
        std::vector<std::shared_ptr<RendererComponent>> m_components{};
        std::weak_ptr<Camera> m_active_camera{};

        RenderSystemState::PhysicalDevice m_selected_physical_device{};

        // Order of declaration effects destructing order!

        RenderSystemState::Instance m_instance{};
        vk::UniqueSurfaceKHR m_surface{};
        vk::UniqueDevice m_device{};

        QueueFamilyIndices m_queue_families{};
        QueueInfo m_queues{};
        RenderSystemState::AllocatorState m_allocator_state;
        RenderSystemState::Swapchain m_swapchain{};
        RenderSystemState::FrameManager m_frame_manager;
        RenderSystemState::GlobalConstantDescriptorPool m_descriptor_pool{};
        RenderSystemState::MaterialRegistry m_material_registry{};
    };

    RenderSystem::RenderSystem(std::weak_ptr<SDLWindow> parent_window) {
        this->pimpl = std::make_unique<RenderSystem::impl>(*this, parent_window);
    }

    void RenderSystem::Create() {
        assert(!this->pimpl->m_instance.get() || "Recreating render system");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())
        );
        pimpl->m_instance.Create("no name", "no name");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(pimpl->m_instance.get());
        pimpl->CreateSurface();

        pimpl->m_selected_physical_device =
            RenderSystemState::PhysicalDevice::SelectPhysicalDevice(pimpl->m_instance.get(), pimpl->m_surface.get());
        pimpl->CreateLogicalDevice();
        VULKAN_HPP_DEFAULT_DISPATCHER.init(pimpl->m_device.get());
        pimpl->CreateSwapchain();

        pimpl->m_allocator_state.Create();

        // Create synchorization semaphores
        pimpl->m_frame_manager.Create();
        pimpl->m_descriptor_pool.Create(shared_from_this(), pimpl->m_frame_manager.FRAMES_IN_FLIGHT);
        pimpl->m_material_registry.Create(shared_from_this());
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Vulkan initialization finished.");
    }

    RenderSystem::~RenderSystem() {
        // Resources are released by RAII.
        // SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Destroying other resources by RAII.");
        std::cerr << "Render system deconstructed" << std::endl;
    }

    void RenderSystem::DrawMeshes(uint32_t pass) {
        glm::mat4 view{1.0f}, proj{1.0f};
        auto camera = pimpl->m_active_camera.lock();
        if (camera) {
            view = camera->GetViewMatrix();
            proj = camera->GetProjectionMatrix();
        }
        DrawMeshes(view, proj, this->GetSwapchain().GetExtent(), pass);
    }

    void RenderSystem::DrawMeshes(
        const glm::mat4 &view_matrix, const glm::mat4 &projection_matrix, vk::Extent2D extent, uint32_t pass
    ) {
        GraphicsCommandBuffer cb = this->GetFrameManager().GetCommandBuffer();

        // Write camera transforms
        auto camera_ptr = this->GetGlobalConstantDescriptorPool().GetPerCameraConstantMemory(
            pimpl->m_frame_manager.GetFrameInFlight(), GetActiveCameraId()
        );
        ConstantData::PerCameraStruct camera_struct{view_matrix, projection_matrix};
        std::memcpy(camera_ptr, &camera_struct, sizeof camera_struct);

        vk::Rect2D scissor{{0, 0}, extent};
        cb.SetupViewport(extent.width, extent.height, scissor);
        for (const auto &component : pimpl->m_components) {
            glm::mat4 model_matrix = component->GetWorldTransform().GetTransformMatrix();
            auto down_casted_ptr = std::dynamic_pointer_cast<MeshComponent>(component);
            if (down_casted_ptr == nullptr) {
                continue;
            }

            const auto &materials = down_casted_ptr->GetMaterials();
            const auto &meshes = down_casted_ptr->GetSubmeshes();

            assert(materials.size() == meshes.size());
            for (size_t id = 0; id < materials.size(); id++) {
                cb.BindMaterial(*materials[id], pass);
                cb.DrawMesh(*meshes[id], model_matrix);
            }
        }
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp) {
        pimpl->m_components.push_back(comp);
    }

    void RenderSystem::ClearComponent() {
        pimpl->m_components.clear();
    }

    void RenderSystem::SetActiveCamera(std::weak_ptr<Camera> camera) {
        pimpl->m_active_camera = camera;
    }

    uint32_t RenderSystem::GetActiveCameraId() const {
        auto camera = pimpl->m_active_camera.lock();
        return camera ? camera->m_display_id : 0;
    }

    vk::Instance RenderSystem::getInstance() const {
        return pimpl->m_instance.get();
    }
    vk::SurfaceKHR RenderSystem::getSurface() const {
        return pimpl->m_surface.get();
    }
    vk::Device RenderSystem::getDevice() const {
        return pimpl->m_device.get();
    }
    vk::PhysicalDevice RenderSystem::GetPhysicalDevice() const {
        return pimpl->m_selected_physical_device.get();
    }
    const RenderSystemState::AllocatorState &RenderSystem::GetAllocatorState() const {
        return pimpl->m_allocator_state;
    }
    const RenderSystem::QueueFamilyIndices &RenderSystem::GetQueueFamilies() const {
        return pimpl->m_queue_families;
    }
    const RenderSystem::QueueInfo &RenderSystem::getQueueInfo() const {
        return pimpl->m_queues;
    }
    const RenderSystemState::Swapchain &RenderSystem::GetSwapchain() const {
        return pimpl->m_swapchain;
    }
    const RenderSystemState::GlobalConstantDescriptorPool &RenderSystem::GetGlobalConstantDescriptorPool() const {
        return pimpl->m_descriptor_pool;
    }

    RenderSystemState::MaterialRegistry &RenderSystem::GetMaterialRegistry() {
        return pimpl->m_material_registry;
    }

    RenderSystemState::FrameManager &RenderSystem::GetFrameManager() {
        return pimpl->m_frame_manager;
    }

    void RenderSystem::CompleteFrame() {
        if (pimpl->m_frame_manager.CompositeToFramebufferAndPresent()) {
            this->UpdateSwapchain();
        }
    }

    void RenderSystem::WritePerCameraConstants(const ConstantData::PerCameraStruct &data, uint32_t in_flight_index) {
        auto *ptr = pimpl->m_descriptor_pool.GetPerCameraConstantMemory(in_flight_index, GetActiveCameraId());
        std::memcpy(ptr, &data, sizeof data);
        pimpl->m_descriptor_pool.FlushPerCameraConstantMemory(in_flight_index, GetActiveCameraId());
    }

    void RenderSystem::WaitForIdle() const {
        pimpl->m_device->waitIdle();
    }

    void RenderSystem::UpdateSwapchain() {
        this->WaitForIdle();
        pimpl->CreateSwapchain();
    }

    uint32_t RenderSystem::StartFrame() {
        return pimpl->m_frame_manager.StartFrame();
    }

    void RenderSystem::impl::CreateLogicalDevice() {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating logical device.");

        m_queue_families = m_selected_physical_device.GetQueueFamilyIndices(m_surface.get());
        // Find unique indices
        std::vector<uint32_t> indices_vector{m_queue_families.graphics.value(), m_queue_families.present.value()};
        std::sort(indices_vector.begin(), indices_vector.end());
        auto end = std::unique(indices_vector.begin(), indices_vector.end());
        // Create DeviceQueueCreateInfo
        float priority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> dqcs;
        dqcs.reserve(std::distance(indices_vector.begin(), end));
        for (auto itr = indices_vector.begin(); itr != end; ++itr) {
            vk::DeviceQueueCreateInfo dqc;
            dqc.pQueuePriorities = &priority;
            dqc.queueCount = 1;
            dqc.queueFamilyIndex = *itr;
            dqcs.push_back(dqc);
        }
        dqcs.shrink_to_fit();

        vk::DeviceCreateInfo dci{};
        dci.queueCreateInfoCount = static_cast<uint32_t>(dqcs.size());
        dci.pQueueCreateInfos = dqcs.data();

        vk::PhysicalDeviceFeatures2 pdf{};
        vk::PhysicalDeviceVulkan13Features features13{};
        features13.dynamicRendering = true;
        features13.synchronization2 = true;
        pdf.pNext = &features13;
        dci.pNext = &pdf;

        // Fill up extensions
        dci.enabledExtensionCount = device_extension_name.size();
        std::vector<const char *> extensions;
        for (const auto &extension : device_extension_name) {
            extensions.push_back(extension.data());
        }
        dci.ppEnabledExtensionNames = extensions.data();

        // Validation layers are not used for logical devices.
        dci.enabledLayerCount = 0;

        m_device = m_selected_physical_device.get().createDeviceUnique(dci);

        SDL_LogInfo(0, "Retreiving queues.");
        this->m_queues.graphicsQueue = m_device->getQueue(m_queue_families.graphics.value(), 0);
        this->m_queues.presentQueue = m_device->getQueue(m_queue_families.present.value(), 0);
        this->CreateCommandPools();
    }

    void RenderSystem::impl::CreateSwapchain() {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating swap chain.");

        uint32_t width, height;
        int w, h;
        SDL_GetWindowSizeInPixels(m_window.lock()->GetWindow(), &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
        vk::Extent2D expected_extent{width, height};

        m_swapchain.CreateSwapchain(m_selected_physical_device, m_device.get(), m_surface.get(), expected_extent);
    }

    void RenderSystem::impl::CreateCommandPools() {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating command pools.");
        vk::CommandPoolCreateInfo info{};
        info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        info.queueFamilyIndex = m_queue_families.graphics.value();
        m_queues.graphicsPool = m_device->createCommandPoolUnique(info);
        m_queues.graphicsOneTimePool = m_device->createCommandPoolUnique(info);

        info.queueFamilyIndex = m_queue_families.present.value();
        m_queues.presentPool = m_device->createCommandPoolUnique(info);
    }

    void RenderSystem::impl::CreateSurface() {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating KHR surface.");

        vk::SurfaceKHR surface;
        int ret =
            SDL_Vulkan_CreateSurface(m_window.lock()->GetWindow(), m_instance.get(), nullptr, (VkSurfaceKHR *)&surface);

        if (ret < 0) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create native surface, %s.", SDL_GetError());
            return;
        }

        // Pass the instance to it to assure successful deletion
        m_surface = vk::UniqueSurfaceKHR(surface, m_instance.get());
    }
} // namespace Engine
