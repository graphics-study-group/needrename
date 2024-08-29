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
        // C++ wrappers for Vulkan functions throw exceptions
        // So we don't need to do mundane error checking
        // Create instance
        
        this->m_instance.Create("no name", "no name");
        this->CreateSurface();

        m_selected_physical_device = RenderSystemState::PhysicalDevice::SelectPhysicalDevice(m_instance.get(), m_surface.get());
        this->CreateLogicalDevice();
        this->CreateSwapchain();

        // Create synchorization semaphores
        this->m_synch = std::make_unique<InFlightTwoStageSynchronization>(*this);
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

    vk::Instance RenderSystem::getInstance() const { return m_instance.get(); }
    vk::SurfaceKHR RenderSystem::getSurface() const { return m_surface.get(); }
    vk::Device RenderSystem::getDevice() const { return m_device.get(); }
    const RenderSystem::QueueInfo & RenderSystem::getQueueInfo() const { return m_queues; }
    const RenderSystem::SwapchainInfo &RenderSystem::getSwapchainInfo() const { return m_swapchain; }
    const Synchronization& RenderSystem::getSynchronization() const { return *m_synch; }
    RenderCommandBuffer & RenderSystem::GetGraphicsCommandBuffer(uint32_t frame_index) { return m_commandbuffers[frame_index]; }

    RenderCommandBuffer& RenderSystem::GetGraphicsCommandBufferWaitAndReset(uint32_t frame_index, uint64_t timeout) {
        m_device->waitForFences({m_synch->GetCommandBufferFence(frame_index)}, vk::True, timeout);
        m_commandbuffers[frame_index].Reset();
        return m_commandbuffers[frame_index];
    }

    uint32_t RenderSystem::GetNextImage(uint32_t frame_id, uint64_t timeout) {
        auto result = m_device->acquireNextImageKHR(
            m_swapchain.swapchain.get(), 
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
        std::array<vk::SwapchainKHR, 1> swapchains {m_swapchain.swapchain.get()};
        std::array<uint32_t, 1> frame_indices {in_flight_index};
        auto semaphores = m_synch->GetCommandBufferSigningSignals(in_flight_index);
        vk::PresentInfoKHR info{semaphores, swapchains, frame_indices};
        return m_queues.presentQueue.presentKHR(info);
    }

    void RenderSystem::WaitForIdle() const {
        m_device->waitIdle();
    }

    void RenderSystem::UpdateSwapchain() {
        this->WaitForIdle();
        this->CreateSwapchain();
    }

    std::tuple<vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR>
    RenderSystem::SelectSwapchainConfig(const SwapchainSupport &support) const {
        assert(!support.formats.empty());
        assert(!support.modes.empty());
        // Select surface format
        vk::SurfaceFormatKHR pickedFormat = support.formats[0];
        for (const auto & format : support.formats) {
            if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear 
                && format.format == vk::Format::eB8G8R8A8Srgb) {
                pickedFormat = format;
            }
        }
        // Select display mode
        vk::PresentModeKHR pickedMode = vk::PresentModeKHR::eFifo;
        for (const auto & mode : support.modes) {
            if (mode == vk::PresentModeKHR::eImmediate) {
                pickedMode = mode;
                break;
            }
        }
        if (pickedMode == vk::PresentModeKHR::eFifo) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Mailbox mode not supported, fall back to FIFO.");
        }

        // Measure extent
        vk::Extent2D extent{};
        if (support.capabilities.currentExtent.height != std::numeric_limits<uint32_t>::max()) {
            extent = support.capabilities.currentExtent;
        } else {
            uint32_t width, height;
            int w, h;
            SDL_GetWindowSizeInPixels(m_window.lock()->GetWindow(), &w, &h);
            width = static_cast<uint32_t>(w);
            height = static_cast<uint32_t>(h);
            extent = vk::Extent2D{width, height};

            extent.width = std::clamp(extent.width, 
                support.capabilities.minImageExtent.width,
                support.capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height, 
                support.capabilities.minImageExtent.height, 
                support.capabilities.maxImageExtent.height);

            if (extent.width != width || extent.height != height) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Extent clamped to (%u, %u).", extent.width, extent.height);
            }
        }
        return std::make_tuple(extent, pickedFormat, pickedMode);
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
        // Fill in selected configuration
        auto support = m_selected_physical_device.GetSwapchainSupport();
        auto [extent, format, mode] = SelectSwapchainConfig(support);

        uint32_t image_count = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, 
                "Requested %u images in swap chain, but only %u are allowed.", 
                image_count, support.capabilities.maxImageCount);
            image_count = support.capabilities.maxImageCount;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating a swapchain with %u images.", image_count);

        vk::SwapchainCreateInfoKHR info;
        info.surface = m_surface.get();
        info.minImageCount = image_count;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.presentMode = mode;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        info.preTransform = support.capabilities.currentTransform;
        // Disable alpha blending for framebuffers
        info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        info.clipped = vk::True;
        if (m_swapchain.swapchain) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Replacing old swap chain.");
            info.oldSwapchain = m_swapchain.swapchain.get();
        } else {
            info.oldSwapchain = nullptr;
        }

        auto indices = m_selected_physical_device.GetQueueFamilyIndices();
        std::vector <uint32_t> queues {indices.graphics.value(), indices.present.value()};
        if (indices.graphics != indices.present) {
            info.imageSharingMode = vk::SharingMode::eConcurrent;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queues.data();
        } else {
            info.imageSharingMode = vk::SharingMode::eExclusive;
        }
        
        m_swapchain.swapchain = m_device->createSwapchainKHRUnique(info);
        m_swapchain.images = m_device->getSwapchainImagesKHR(m_swapchain.swapchain.get());
        m_swapchain.format = format;
        m_swapchain.extent = extent;

        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, 
            "Retreiving image views for %llu swap chain images.", 
            m_swapchain.images.size()
        );
        m_swapchain.imageViews.clear();
        m_swapchain.imageViews.resize(m_swapchain.images.size());
        for (size_t i = 0; i < m_swapchain.images.size(); i++) {
            vk::ImageViewCreateInfo info;
            info.image = m_swapchain.images[i];
            info.viewType = vk::ImageViewType::e2D;
            info.format = m_swapchain.format.format;
            info.components.r = vk::ComponentSwizzle::eIdentity;
            info.components.g = vk::ComponentSwizzle::eIdentity;
            info.components.b = vk::ComponentSwizzle::eIdentity;
            info.components.a = vk::ComponentSwizzle::eIdentity;
            info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            m_swapchain.imageViews[i] = m_device->createImageViewUnique(info);
        }

        // Allocate command buffer for each image in the swap chain
        // Do note that frame id in flight doesn't equal to frame buffer index
        // However, creating more frames in flight than images is useless.
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating command buffers.");
        m_commandbuffers.clear();
        m_commandbuffers.resize(image_count);
        for (uint32_t i = 0; i < image_count; i++) {
            m_commandbuffers[i].CreateCommandBuffer(m_device.get(), m_queues.graphicsPool.get(), i);
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
