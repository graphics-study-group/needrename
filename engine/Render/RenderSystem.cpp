#include "RenderSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Framework/component/RenderComponent/CameraComponent.h"

namespace Engine
{
    RenderSystem::RenderSystem()
    {
        // C++ wrappers for Vulkan functions throw exceptions
        // So we don't need to do mundane error checking
        // Create instance
        vk::ApplicationInfo appInfo;
        appInfo.pApplicationName = "no name";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "no name";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;
        this->CreateInstance(appInfo);

        auto physical = this->SelectPhysicalDevice();
        this->CreateLogicalDevice(physical);

        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Vulkan initialization finished.");
    }

    void RenderSystem::Render()
    {
        CameraContext cameraContext{};
        if (m_active_camera) {
            cameraContext = m_active_camera->CreateContext();
        } else {
            SDL_LogWarn(0, "No active camera.");
            cameraContext.projection_matrix = glm::identity<glm::mat4>();
            cameraContext.view_matrix = glm::identity<glm::mat4>();
        }
        for (auto comp : m_components) {
            comp->Draw(cameraContext);
        }
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp)
    {
        m_components.push_back(comp);
    }

    void RenderSystem::SetActiveCamera(std::shared_ptr <CameraComponent> cameraComponent)
    {
        m_active_camera = cameraComponent;
    }

    void RenderSystem::CreateInstance(const vk::ApplicationInfo &appInfo)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating Vulkan instance.");
        const char * const * pExt;
        uint32_t extCount;
        pExt = SDL_Vulkan_GetInstanceExtensions(&extCount);

        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%u vulkan extensions requested.", extCount);
        for (uint32_t i = 0; i < extCount; i++) {
            SDL_LogVerbose(0, "\t%s", pExt[i]);
        }

        vk::InstanceCreateInfo instInfo;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount = extCount;
        instInfo.ppEnabledExtensionNames = pExt;
        if (CheckValidationLayer()) {
            instInfo.enabledLayerCount = 1;
            instInfo.ppEnabledLayerNames = std::array<const char *, 1>{validation_layer_name.data()}.data();
        } else {
            instInfo.enabledLayerCount = 0;
        }
        this->m_instance = vk::createInstanceUnique(instInfo);
    }

    bool RenderSystem::CheckValidationLayer()
    {
        auto layers = vk::enumerateInstanceLayerProperties();
        for (const auto & layer : layers) {
            if(strcmp(layer.layerName, validation_layer_name.data()) == 0)
                return true;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Validation layer %s not available.", validation_layer_name.data());
        return false;
    }

    vk::PhysicalDevice RenderSystem::SelectPhysicalDevice() const
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Selecting physical devices.");
        auto devices = m_instance->enumeratePhysicalDevices();
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Found %llu Vulkan devices.", devices.size());

        auto select_pred = [] (const vk::PhysicalDevice & device) -> bool {
            if (!RenderSystem::FillQueueFamily(device).isComplete()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Cannot find graphics queue family.");
                return false;
            }
            return true;
        };

        const vk::PhysicalDevice * selected_device = nullptr;
        for (const auto & device : devices) {
            auto properties = device.getProperties();
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "\t Inspecting %s.", properties.deviceName.data());
            if (select_pred(device)) {
                selected_device = &device;
                break;
            }
        }
        
        if (!selected_device) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Cannot select appropiate device.");
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Device %s selected.", selected_device->getProperties().deviceName.data());
        return *selected_device;
    }

    void RenderSystem::CreateLogicalDevice(const vk::PhysicalDevice &selectedPhysicalDevice)
    {
        SDL_LogInfo(0, "Creating logical device.");

        vk::DeviceQueueCreateInfo dqc{};
        auto indices = FillQueueFamily(selectedPhysicalDevice);
        std::array <float, 1> priorities {1.0f};
        dqc.queueCount = 1;
        dqc.queueFamilyIndex = indices.graphics.value(); 
        dqc.pQueuePriorities = priorities.data();

        vk::PhysicalDeviceFeatures pdf{};

        vk::DeviceCreateInfo dci{};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &dqc;
        dci.pEnabledFeatures = &pdf;
        dci.enabledExtensionCount = 0;
        dci.enabledLayerCount = 0;

        m_device = selectedPhysicalDevice.createDeviceUnique(dci);

        SDL_LogInfo(0, "Retreiving queues.");
        this->m_graphicsQueue = m_device->getQueue(indices.graphics.value(), 0);
    }

    QueueFamilyIndices RenderSystem::FillQueueFamily(const vk::PhysicalDevice &device)
    {
        QueueFamilyIndices q;
        auto queueFamilyProps = device.getQueueFamilyProperties();
        for (size_t i = 0; i < queueFamilyProps.size(); i++) {
            const auto & prop = queueFamilyProps[i];
            if (prop.queueFlags & vk::QueueFlagBits::eGraphics) {
                q.graphics = i;
            }
        }
        return q;
    }
}
