#include "RenderSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Pipeline/CommandBuffer.h"
#include <MainClass.h>
#include <GUI/GUISystem.h>

namespace Engine
{
    RenderSystem::RenderSystem(
        std::weak_ptr <SDLWindow> parent_window
    ) : m_window(parent_window)
    {
    }

    void RenderSystem::Create() {
        assert(!this->m_instance.get() || "Recreating render system");
        // C++ wrappers for Vulkan functions throw exceptions
        // So we don't need to do mundane error checking
        // Create instance
        this->m_instance.Create("no name", "no name");
        this->CreateSurface();

        m_selected_physical_device = RenderSystemState::PhysicalDevice::SelectPhysicalDevice(m_instance.get(), m_surface.get());
        this->CreateLogicalDevice();
        this->CreateSwapchain();

        this->m_allocator_state.Create(shared_from_this());

        this->EnableDepthTesting();

        this->m_render_target_setup = std::make_unique<RenderTargetSetup>(shared_from_this());
        this->m_render_target_setup->CreateFromSwapchain();
        this->m_render_target_setup->SetClearValues({
            vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
            vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
        });

        // Create synchorization semaphores
        this->m_descriptor_pool.Create(shared_from_this(), 3);
        this->m_material_descriptor_manager.Create(shared_from_this());
        this->m_material_registry.Create(shared_from_this());
        this->m_frame_manager.Create(shared_from_this());
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Vulkan initialization finished.");
    }

    RenderSystem::~RenderSystem() 
    {
        // Resources are released by RAII.
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Destroying other resources by RAII.");
    }

    void RenderSystem::DrawMeshes(uint32_t inflight, uint32_t pass)
    {
        RenderCommandBuffer & cb = this->GetGraphicsCommandBuffer(inflight);

        // Write camera transforms
        std::byte * camera_ptr = this->GetGlobalConstantDescriptorPool().GetPerCameraConstantMemory(inflight);
        ConstantData::PerCameraStruct camera_struct;
        if (m_active_camera) {
            camera_struct = {
                m_active_camera->GetViewMatrix(), 
                m_active_camera->GetProjectionMatrix()
            };
            
        } else {
            camera_struct = {
                glm::mat4{1.0f}, 
                glm::mat4{1.0f}
            };
        }
        std::memcpy(camera_ptr, &camera_struct, sizeof camera_struct);
        
        vk::Extent2D extent {this->GetSwapchain().GetExtent()};
        vk::Rect2D scissor{{0, 0}, extent};
        cb.SetupViewport(extent.width, extent.height, scissor);
        for (const auto & component : m_components) {
            glm::mat4 model_matrix = component->GetWorldTransform().GetTransformMatrix();
            auto down_casted_ptr = std::dynamic_pointer_cast<MeshComponent>(component);
            if (down_casted_ptr == nullptr) {
                continue;
            }

            const auto & materials = down_casted_ptr->GetMaterials();
            const auto & meshes = down_casted_ptr->GetSubmeshes();

            assert(materials.size() == meshes.size());
            for (size_t id = 0; id < materials.size(); id++){
                cb.BindMaterial(*materials[id], pass);
                cb.DrawMesh(*meshes[id], model_matrix);
            }
        }
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp)
    {
        m_components.push_back(comp);
    }

    void RenderSystem::ClearComponent()
    {
        m_components.clear();
    }

    void RenderSystem::SetActiveCamera(std::shared_ptr <CameraComponent> cameraComponent)
    {
        m_active_camera = cameraComponent;
    }

    vk::Instance RenderSystem::getInstance() const { return m_instance.get(); }
    vk::SurfaceKHR RenderSystem::getSurface() const { return m_surface.get(); }
    vk::Device RenderSystem::getDevice() const { return m_device.get(); }
    vk::PhysicalDevice RenderSystem::GetPhysicalDevice() const { return m_selected_physical_device.get(); }
    const RenderSystemState::AllocatorState &RenderSystem::GetAllocatorState() const { return m_allocator_state; }
    const RenderSystem::QueueInfo &RenderSystem::getQueueInfo() const { return m_queues; }
    const RenderSystemState::Swapchain& RenderSystem::GetSwapchain() const { return m_swapchain; }
    RenderCommandBuffer & RenderSystem::GetGraphicsCommandBuffer(uint32_t frame_index) { return m_frame_manager.GetCommandBuffers()[frame_index]; }
    const RenderSystemState::GlobalConstantDescriptorPool& RenderSystem::GetGlobalConstantDescriptorPool() const {
        return m_descriptor_pool;
    }
    RenderSystemState::MaterialDescriptorManager& RenderSystem::GetMaterialDescriptorManager() {
        return m_material_descriptor_manager;
    }

    RenderSystemState::MaterialRegistry &RenderSystem::GetMaterialRegistry()
    {
        return m_material_registry;
    }

    TransferCommandBuffer & RenderSystem::GetTransferCommandBuffer() {
        m_device->resetCommandPool(m_queues.graphicsOneTimePool.get());
        return m_one_time_commandbuffer;
    }

    void RenderSystem::Render()
    {
        MainClass::GetInstance()->GetGUISystem()->PrepareGUI();
        
        uint32_t index = m_frame_manager.StartFrame();
        RenderCommandBuffer & cb = m_frame_manager.GetCommandBuffer();
        cb.Begin();
        vk::Extent2D extent {GetSwapchain().GetExtent()};
        cb.BeginRendering(*this->m_render_target_setup, extent, index);

        this->DrawMeshes(m_frame_manager.GetFrameInFlight());
        MainClass::GetInstance()->GetGUISystem()->DrawGUI(cb);

        cb.EndRendering();
        cb.End();
        cb.Submit();
        m_frame_manager.CompleteFrame();
    }

    void RenderSystem::EnableDepthTesting() {
        m_swapchain.EnableDepthTesting(this->shared_from_this());
    }

    void RenderSystem::WritePerCameraConstants(const ConstantData::PerCameraStruct& data, uint32_t in_flight_index) {
        std::byte * ptr = m_descriptor_pool.GetPerCameraConstantMemory(in_flight_index);
        std::memcpy(ptr, &data, sizeof data);
        m_descriptor_pool.FlushPerCameraConstantMemory(in_flight_index);
    }

    void RenderSystem::WaitForIdle() const {
        m_device->waitIdle();
    }

    void RenderSystem::UpdateSwapchain() {
        this->WaitForIdle();
        this->CreateSwapchain();
    }

    void RenderSystem::CreateLogicalDevice() {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating logical device.");

        auto indices = m_selected_physical_device.GetQueueFamilyIndices();
        // Find unique indices
        std::vector<uint32_t> indices_vector{indices.graphics.value(),
                                            indices.present.value()};
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
        vk::PhysicalDeviceVulkan13Features features13 {};
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
        this->m_queues.graphicsQueue =
            m_device->getQueue(indices.graphics.value(), 0);
        this->m_queues.presentQueue =
            m_device->getQueue(indices.present.value(), 0);
        this->CreateCommandPools(indices);
    }

    void RenderSystem::CreateSwapchain() 
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating swap chain.");
       
        uint32_t width, height;
        int w, h;
        SDL_GetWindowSizeInPixels(m_window.lock()->GetWindow(), &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
        vk::Extent2D expected_extent{width, height};

        m_swapchain.CreateSwapchain(m_selected_physical_device,
            m_device.get(),
            m_surface.get(),
            expected_extent
        );
    }

    void RenderSystem::CreateCommandPools(const QueueFamilyIndices & indices) 
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating command pools.");
        vk::CommandPoolCreateInfo info{};
        info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        info.queueFamilyIndex = indices.graphics.value();
        m_queues.graphicsPool = m_device->createCommandPoolUnique(info);
        m_queues.graphicsOneTimePool = m_device->createCommandPoolUnique(info);

        info.queueFamilyIndex = indices.present.value();
        m_queues.presentPool = m_device->createCommandPoolUnique(info);

        // Prepare a one-time transfer command buffer
        m_one_time_commandbuffer.Create(shared_from_this(), m_queues.graphicsOneTimePool.get(), m_queues.graphicsQueue);
    }

    void RenderSystem::CreateSurface() 
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating KHR surface.");

        vk::SurfaceKHR surface;
        int ret = SDL_Vulkan_CreateSurface(
            m_window.lock()->GetWindow(),
            m_instance.get(),
            nullptr,
            (VkSurfaceKHR *)&surface
            );

        if (ret < 0) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create native surface, %s.", SDL_GetError());
            return;
        }

        // Pass the instance to it to assure successful deletion
        m_surface = vk::UniqueSurfaceKHR(surface, m_instance.get());
    }
}  // namespace Engine
