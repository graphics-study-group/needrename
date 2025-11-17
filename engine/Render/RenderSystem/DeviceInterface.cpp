#include "DeviceInterface.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

namespace Engine::RenderSystemState {
    struct DeviceInterface::impl {

        static constexpr const char * VALIDATION_LAYER_NAME{"VK_LAYER_KHRONOS_validation"};
        static constexpr std::array <const char *, 1> DEVICE_EXTENSION_NAMES{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        vk::UniqueInstance instance{};
        vk::UniqueSurfaceKHR surface{};
        vk::PhysicalDeviceMemoryProperties physical_device_memory_properties{};
        vk::PhysicalDevice physical_device{};
        vk::UniqueDevice device{};

        // Cached info
        QueueFamilyIndices queue_families{};
        SwapchainSupport swapchain_support{};

        /**
         * @brief Check whether the validation layer exists for instance creation.
         */
        static bool InstanceCheckValidationLayer() {
            auto layers = vk::enumerateInstanceLayerProperties();
            for (const auto &layer : layers) {
                if (strcmp(layer.layerName, VALIDATION_LAYER_NAME) == 0) return true;
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Validation layer %s not available.", VALIDATION_LAYER_NAME);
            return false;
        }

        /**
         * @brief Create a new instance.
         */
        void CreateInstance(const DeviceConfiguration & cfg) {
            vk::ApplicationInfo appInfo{
                cfg.application_name.c_str(), cfg.application_version, "NEEDRENAME", VK_MAKE_VERSION(0, 1, 0), VK_API_VERSION_1_3
            };

            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating Vulkan instance.");
            const char *const *pExt;
            uint32_t extCount;
            pExt = SDL_Vulkan_GetInstanceExtensions(&extCount);

            std::vector<const char *> extensions;
            for (uint32_t i = 0; i < extCount; i++) {
                extensions.push_back(pExt[i]);
            }
#ifndef NDEBUG
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%u vulkan extensions requested.", extCount);
            for (uint32_t i = 0; i < extCount; i++) {
                SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "\t%s", pExt[i]);
            }

            vk::InstanceCreateInfo instInfo{vk::InstanceCreateFlags{}, &appInfo, {}, extensions};
#ifndef NDEBUG
            if (InstanceCheckValidationLayer()) {
                instInfo.enabledLayerCount = 1;
                instInfo.ppEnabledLayerNames = &(VALIDATION_LAYER_NAME);
            } else {
                instInfo.enabledLayerCount = 0;
            }
#else
            instInfo.enabledLayerCount = 0;
#endif
            instance = vk::createInstanceUnique(instInfo);
            cfg.dynamic_dispatcher->init(instance.get());
        }

        /**
         * @brief Create a new vulkan surface with the help of SDL.
         */
        void CreateSurface(const DeviceConfiguration & cfg) {
            assert(this->instance);
    
            vk::SurfaceKHR surface;
            int ret = SDL_Vulkan_CreateSurface(cfg.window, instance.get(), nullptr, (VkSurfaceKHR *)&surface);

            if (ret < 0) {
                SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create native surface, %s.", SDL_GetError());
                return;
            }

            // Pass the instance to it to assure successful deletion
            this->surface = vk::UniqueSurfaceKHR(surface, instance.get());
        }

        /**
         * @brief Fill up a struct containing usable queue family indices.
         */
        QueueFamilyIndices FillQueueFamilyIndices(vk::PhysicalDevice pd) {
            assert(surface);
    
            QueueFamilyIndices q;
            auto queueFamilyProps = pd.getQueueFamilyProperties();
            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "\tInspecting queue families");
            for (size_t i = 0; i < queueFamilyProps.size(); i++) {
                const auto &prop = queueFamilyProps[i];

                bool isGeneralQueueFamily = (prop.queueFlags & vk::QueueFlagBits::eGraphics)
                                            && (prop.queueFlags & vk::QueueFlagBits::eTransfer)
                                            && (prop.queueFlags & vk::QueueFlagBits::eCompute);
                bool supportPresenting = pd.getSurfaceSupportKHR(i, surface.get());

                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    std::format(
                        "\t\tQueue family {} {}{}{}({} present)",
                        i,
                        prop.queueFlags & vk::QueueFlagBits::eGraphics ? "Graphics " : "",
                        prop.queueFlags & vk::QueueFlagBits::eTransfer ? "Transfer " : "",
                        prop.queueFlags & vk::QueueFlagBits::eCompute ? "Compute " : "",
                        supportPresenting ? "Can" : "Cannot"
                    )
                        .c_str()
                );

                if (isGeneralQueueFamily) {
                    if (!q.graphics.has_value()) q.graphics = i;
                }
                if (supportPresenting) {
                    if (!q.present.has_value()) q.present = i;
                }
            }
            return q;
        }

        /**
         * @brief Fill up a struct indicating its capability to support
         * the desired swapchain.
         */
        SwapchainSupport FillSwapchainSupport(vk::PhysicalDevice pd) {
            assert(surface);
            SwapchainSupport support{
                pd.getSurfaceCapabilitiesKHR(surface.get()),
                pd.getSurfaceFormatsKHR(surface.get()),
                pd.getSurfacePresentModesKHR(surface.get())
            };
            return support;
        }

        /**
         * @brief Assign a score to the given physical device.
         * @return a score (higher the better). Negative if unusable.
         */
        int GetPhysicalDeviceSuitabilityScore(vk::PhysicalDevice pd) {
            int score{0};

            auto props = pd.getProperties();
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "\tInspecting %s.", props.deviceName.data());
            switch(props.deviceType) {
                case vk::PhysicalDeviceType::eDiscreteGpu:
                    score += 10;
                    break;
                case vk::PhysicalDeviceType::eIntegratedGpu:
                    score += 5;
                    break;
                default:
                    break;
            }

            // Check if all queue families are available
            if (!FillQueueFamilyIndices(pd).isComplete()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Cannot find complete queue family.");
                return -1;
            }

            // Check if swapchain is supported
            auto support = FillSwapchainSupport(pd);
            if (support.formats.empty() || support.modes.empty()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Cannot find suitable swapchain.");
                return -1;
            }

            // Check features
            auto device_features = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            auto features13 = device_features.get<vk::PhysicalDeviceVulkan13Features>();
            if (!(features13.dynamicRendering && features13.synchronization2)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "This physical device does not support needed Vulkan 1.3 features.");
                return -1;
            }

            // Check if all extensions are available
            std::unordered_set<std::string> required_extensions{};
            for (const auto &extension_name : DEVICE_EXTENSION_NAMES) {
                required_extensions.insert(extension_name);
            }

            auto extensions = pd.enumerateDeviceExtensionProperties();
            for (const auto &extension : extensions) {
                required_extensions.erase(extension.extensionName);
            }

            if (!required_extensions.empty()) {
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_RENDER,
                    "Cannot find all extensions, %llu extensions not found.",
                    required_extensions.size()
                );
                for (const auto &name : required_extensions) {
                    SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "\t%s", name.data());
                }
                return -1;
            }

            return score;
        }

        /**
         * @brief Get the physical device that supports needed Vulkan features.
         */
        void GetPhysicalDevice(const DeviceConfiguration & cfg) {
            assert(surface);

            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Selecting physical devices.");
            auto devices = instance->enumeratePhysicalDevices();
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Found %llu Vulkan devices.", devices.size());

            vk::PhysicalDevice selected_device;
            for (const auto &device : devices) {
                if (GetPhysicalDeviceSuitabilityScore(device) > 0) {
                    selected_device = device;
                    break;
                }
            }

            if (!selected_device) {
                SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Cannot select appropiate device.");
                SDL_ShowSimpleMessageBox(
                    SDL_MESSAGEBOX_ERROR, 
                    "Critical Error",
                    "None of your GPUs supports necessary Vulkan capabilities.\n"
                    "This is an unrecoverable error and the program will now terminate.",
                    cfg.window);
                std::terminate();
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Device %s selected.", selected_device.getProperties().deviceName.data());

            physical_device = selected_device;
            physical_device_memory_properties = physical_device.getMemoryProperties();

            queue_families = FillQueueFamilyIndices(physical_device);
            swapchain_support = FillSwapchainSupport(physical_device);
        }

        /**
         * @brief Create the Vulkan device.
         */
        void CreateDevice(const DeviceConfiguration & cfg) {
            assert(physical_device);
        }
    };

    DeviceInterface::DeviceInterface(DeviceConfiguration cfg) : pimpl(std::make_unique<impl>()) {
        pimpl->CreateInstance(cfg);
        pimpl->CreateSurface(cfg);
        pimpl->GetPhysicalDevice(cfg);
        pimpl->CreateDevice(cfg);
    }
    DeviceInterface::~DeviceInterface() = default;

    vk::Instance DeviceInterface::GetInstance() const {
        assert(pimpl->instance);
        return pimpl->instance.get();
    }
    vk::SurfaceKHR DeviceInterface::GetSurface() const {
        assert(pimpl->surface);
        return pimpl->surface.get();
    }
    vk::PhysicalDevice DeviceInterface::GetPhysicalDevice() const {
        assert(pimpl->physical_device);
        return pimpl->physical_device;
    }
    vk::Device DeviceInterface::GetDevice() const {
        assert(pimpl->device);
        return pimpl->device.get();
    }
    const QueueFamilyIndices &DeviceInterface::GetQueueFamilies() const {
        return pimpl->queue_families;
    }
    const SwapchainSupport &DeviceInterface::GetSwapchainSupport() const {
        return pimpl->swapchain_support;
    }
} // namespace Engine::RenderSystemState
