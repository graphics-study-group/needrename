#include "RenderSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Render/Pipeline/CommandBuffer.h"

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

        // Create synchorization semaphores
        this->m_synch = std::make_unique<InFlightTwoStageSynch>(*this, 3);
        this->m_descriptor_pool.Create(shared_from_this(), 3);
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Vulkan initialization finished.");
    }

    RenderSystem::~RenderSystem() 
    {
        // Resources are released by RAII.
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Destroying other resources by RAII.");
    }

    void RenderSystem::Render() {
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp)
    {
        m_components.push_back(comp);
    }

    void RenderSystem::SetActiveCamera(std::shared_ptr <CameraComponent> cameraComponent)
    {
        m_active_camera = cameraComponent;
    }

    uint32_t RenderSystem::FindPhysicalMemory(uint32_t type, vk::MemoryPropertyFlags properties) {
        const auto & memory_properties = m_selected_physical_device.GetMemoryProperties();
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
            if ((type & (1 << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to find physical memory on GPU with type %u.", type);
        return 0;
    }

    void RenderSystem::WaitForFrameBegin(uint32_t frame_index, uint64_t timeout) {
        vk::Fence fence = getSynchronization().GetCommandBufferFence(frame_index);
        vk::Result waitFenceResult = getDevice().waitForFences({fence}, vk::True, timeout);
        if (waitFenceResult == vk::Result::eTimeout) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Timed out waiting for fence for frame id %u.", frame_index);
        }
        m_commandbuffers[frame_index].Reset();
        getDevice().resetFences({fence});
    }

    vk::Instance RenderSystem::getInstance() const { return m_instance.get(); }
    vk::SurfaceKHR RenderSystem::getSurface() const { return m_surface.get(); }
    vk::Device RenderSystem::getDevice() const { return m_device.get(); }
    const RenderSystem::QueueInfo & RenderSystem::getQueueInfo() const { return m_queues; }
    const RenderSystemState::Swapchain& RenderSystem::GetSwapchain() const { return m_swapchain; }
    const Synchronization& RenderSystem::getSynchronization() const { return *m_synch; }
    RenderCommandBuffer & RenderSystem::GetGraphicsCommandBuffer(uint32_t frame_index) { return m_commandbuffers[frame_index]; }
    const RenderSystemState::GlobalConstantDescriptorPool& RenderSystem::GetGlobalConstantDescriptorPool() const {
        return m_descriptor_pool;
    }

    uint32_t RenderSystem::GetNextImage(uint32_t frame_id, uint64_t timeout) {
        auto result = m_device->acquireNextImageKHR(
            m_swapchain.GetSwapchain(), 
            timeout, 
            m_synch->GetNextImageSemaphore(frame_id),
            nullptr
        );
        if (result.result == vk::Result::eTimeout) {
            SDL_LogError(0, "Timed out waiting for next frame.");
            return -1;
        }
        return result.value;
    }

    vk::Result RenderSystem::Present(uint32_t frame_index, uint32_t in_flight_index) {
        std::array<vk::SwapchainKHR, 1> swapchains { m_swapchain.GetSwapchain() };
        std::array<uint32_t, 1> frame_indices {in_flight_index};
        auto semaphores = m_synch->GetCommandBufferSigningSignals(in_flight_index);
        vk::PresentInfoKHR info{semaphores, swapchains, frame_indices};
        return m_queues.presentQueue.presentKHR(info);
    }

    void RenderSystem::EnableDepthTesting() {
        m_swapchain.EnableDepthTesting(this->shared_from_this());
    }

    void RenderSystem::WritePerCameraConstants(const ConstantData::PerCameraStruct& data, uint32_t in_flight_index) {
        std::byte * ptr = m_descriptor_pool.GetPerCameraConstantMemory(in_flight_index);
        std::memcpy(ptr, &data, sizeof data);
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

        vk::PhysicalDeviceFeatures pdf{};

        vk::DeviceCreateInfo dci{};
        dci.queueCreateInfoCount = static_cast<uint32_t>(dqcs.size());
        dci.pQueueCreateInfos = dqcs.data();
        dci.pEnabledFeatures = &pdf;

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

        // Allocate command buffer for each image in the swap chain
        // Do note that frame id in flight doesn't equal to frame buffer index
        // However, creating more frames in flight than images is useless.
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating command buffers.");
        size_t image_count = m_swapchain.GetImageViews().size();
        m_commandbuffers.clear();
        m_commandbuffers.resize(image_count);
        for (uint32_t i = 0; i < image_count; i++) {
            m_commandbuffers[i].CreateCommandBuffer(shared_from_this(), m_queues.graphicsPool.get(), i);
        }
    }

    void RenderSystem::CreateCommandPools(const QueueFamilyIndices & indices) 
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating command pools.");
        vk::CommandPoolCreateInfo info{};
        info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        info.queueFamilyIndex = indices.graphics.value();
        m_queues.graphicsPool = m_device->createCommandPoolUnique(info);

        info.queueFamilyIndex = indices.present.value();
        m_queues.presentPool = m_device->createCommandPoolUnique(info);
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
