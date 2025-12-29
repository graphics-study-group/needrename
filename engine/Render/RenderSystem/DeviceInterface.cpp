#include "DeviceInterface.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Engine::RenderSystemState {
    struct DeviceInterface::impl {

        static constexpr const char * VALIDATION_LAYER_NAME{"VK_LAYER_KHRONOS_validation"};
        static constexpr std::array <const char *, 2> DEVICE_EXTENSION_NAMES{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
        };

        vk::UniqueInstance instance{};
        vk::UniqueSurfaceKHR surface{};
        vk::PhysicalDeviceMemoryProperties physical_device_memory_properties{};
        vk::PhysicalDeviceProperties physical_device_properties{};
        vk::PhysicalDevice physical_device{};
        vk::UniqueDevice device{};

        // Queue families
        struct QueueFamilies {
            std::optional<uint32_t> graphics {};
            std::optional<uint32_t> graphics_present {};
            std::optional<uint32_t> async_compute {};
            std::optional<uint32_t> async_compute_present {};
            std::optional<uint32_t> async_transfer {};

            /**
             * Only async transfer needs this.
             * > Queues supporting graphics and/or compute operations must report (1,1,1) 
             * > in minImageTransferGranularity, meaning that there are no additional 
             * > restrictions on the granularity of image transfer operations for these queues.
             */
            std::tuple<uint32_t, uint32_t, uint32_t> async_transfer_granularity {};

            bool is_complete() const noexcept {
                return graphics.has_value() && graphics_present.has_value();
            }
        } queue_families {};
        

        // Cached info
        QueueInfo queues{};

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
            
            static_assert(
                std::is_same<decltype(VULKAN_HPP_DEFAULT_DISPATCHER), vk::detail::DispatchLoaderDynamic>::value,
                "Vulkan-Hpp loader is not configured to be dynamic.");
            if (cfg.dynamic_dispatcher) {
                cfg.dynamic_dispatcher->init(
                    reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())
                );
            } else {
                VULKAN_HPP_DEFAULT_DISPATCHER.init(
                    reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())
                );
            }

            vk::ApplicationInfo appInfo{
                cfg.application_name.c_str(),
                cfg.application_version,
                "NEEDRENAME",
                VK_MAKE_VERSION(0, 1, 0),
                VK_API_VERSION_1_3
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
            if (cfg.dynamic_dispatcher) {
                cfg.dynamic_dispatcher->init(instance.get());
            } else {
                VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());
            }
                
        }

        /**
         * @brief Create a new vulkan surface with the help of SDL.
         */
        void CreateSurface(const DeviceConfiguration & cfg) {
            assert(this->instance);
    
            vk::SurfaceKHR surface;
            int ret = SDL_Vulkan_CreateSurface(cfg.window, instance.get(), nullptr, (VkSurfaceKHR *)&surface);

            if (ret < 0) {
                const std::string sdl_error{SDL_GetError()};
                SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create native surface, %s.", sdl_error.c_str());

                const std::string message_box_info{
                    std::format(
                        "Cannot create Vulkan surface: {}\n"
                        "This is an unrecoverable error and the program will now terminate.",
                        sdl_error
                    )
                };
                SDL_ShowSimpleMessageBox(
                    SDL_MESSAGEBOX_ERROR, 
                    "Critical Error",
                    message_box_info.c_str(),
                    cfg.window);
                std::terminate();
            }

            // Pass the instance to it to assure successful deletion
            this->surface = vk::UniqueSurfaceKHR(surface, instance.get());
        }

        /**
         * @brief Fill up a struct containing usable queue family indices.
         */
        QueueFamilies FillQueueFamilyIndices(vk::PhysicalDevice pd) {
            assert(surface);
    
            QueueFamilies q;

            auto queueFamilyProps = pd.getQueueFamilyProperties();
            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "\tInspecting queue families");

            for (size_t i = 0; i < queueFamilyProps.size(); i++) {
                const auto &prop = queueFamilyProps[i];

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
                    ).c_str()
                );

                if (prop.queueFlags & vk::QueueFlagBits::eGraphics) {
                    assert(pd.getSurfaceSupportKHR(i, surface.get()));
                    q.graphics = q.graphics_present = i;
                } else if (prop.queueFlags & vk::QueueFlagBits::eCompute) {
                    q.async_compute = i;
                    if (pd.getSurfaceSupportKHR(i, surface.get())) {
                        q.async_compute_present = i;
                    }
                } else if (prop.queueFlags & vk::QueueFlagBits::eTransfer) {
                    q.async_transfer = i;
                    q.async_transfer_granularity = std::tie(
                        prop.minImageTransferGranularity.width,
                        prop.minImageTransferGranularity.height,
                        prop.minImageTransferGranularity.depth
                    );
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
            if (!FillQueueFamilyIndices(pd).is_complete()) {
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
            physical_device_properties = physical_device.getProperties();

            queue_families = FillQueueFamilyIndices(physical_device);
        }

        /**
         * @brief Create the Vulkan device, and retrieve queues to submit command buffers to.
         */
        void CreateDevice(const DeviceConfiguration & cfg) {
            assert(physical_device);

            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating logical device.");

            // Find unique indices
            assert(queue_families.graphics.has_value() && queue_families.graphics_present.has_value());
            uint32_t graphics_queue = queue_families.graphics.value();
            std::array indices{
                queue_families.graphics.value(),
                queue_families.graphics_present.value(),
                queue_families.async_compute.value_or(graphics_queue),
                queue_families.async_compute_present.value_or(graphics_queue),
                queue_families.async_transfer.value_or(graphics_queue)
            };
            std::sort(indices.begin(), indices.end());
            auto end = std::unique(indices.begin(), indices.end());

            // Create DeviceQueueCreateInfo
            float priority = 1.0f;
            std::vector<vk::DeviceQueueCreateInfo> dqcs;
            dqcs.reserve(std::distance(indices.begin(), end));
            for (auto itr = indices.begin(); itr != end; ++itr) {
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

            vk::PhysicalDeviceVulkan12Features features12{};
            features12.timelineSemaphore = true;

            features13.pNext = &features12;
            pdf.pNext = &features13;
            dci.pNext = &pdf;

            // Fill up extensions
            dci.enabledExtensionCount = DEVICE_EXTENSION_NAMES.size();
            std::vector<const char *> extensions;
            for (const auto &extension : DEVICE_EXTENSION_NAMES) {
                extensions.push_back(extension);
            }
            dci.ppEnabledExtensionNames = extensions.data();

            device = physical_device.createDeviceUnique(dci);
            if (cfg.dynamic_dispatcher) {
                cfg.dynamic_dispatcher->init(device.get());
            } else {
                VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());
            }
            
        }

        /**
         * @brief Retrieve queues and create command pools to create
         * command buffers from.
         */
        void CreateCommandPool(const DeviceConfiguration & cfg) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Retreiving queues.");
            queues.graphicsQueue = device->getQueue(queue_families.graphics.value(), 0);
            queues.presentQueue = device->getQueue(queue_families.graphics_present.value(), 0);

            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating command pools.");
            vk::CommandPoolCreateInfo info{};
            info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            info.queueFamilyIndex = queue_families.graphics.value();
            queues.graphicsPool = device->createCommandPoolUnique(info);
            queues.graphicsOneTimePool = device->createCommandPoolUnique(info);

            info.queueFamilyIndex = queue_families.graphics_present.value();
            queues.presentPool = device->createCommandPoolUnique(info);
        }
    };

    DeviceInterface::DeviceInterface(DeviceConfiguration cfg) : pimpl(std::make_unique<impl>()) {
        assert(cfg.window);
        pimpl->CreateInstance(cfg);
        pimpl->CreateSurface(cfg);
        pimpl->GetPhysicalDevice(cfg);
        pimpl->CreateDevice(cfg);
        pimpl->CreateCommandPool(cfg);
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
    SwapchainSupport DeviceInterface::GetSwapchainSupport() const {
        return pimpl->FillSwapchainSupport(pimpl->physical_device);
    }
    const QueueInfo &DeviceInterface::GetQueueInfo() const {
        return pimpl->queues;
    }
    std::optional <uint32_t> DeviceInterface::GetQueueFamily(DeviceInterface::QueueFamilyType type) const noexcept {
        switch(type) {
            using enum DeviceInterface::QueueFamilyType;
            case GraphicsMain:
                assert(pimpl->queue_families.graphics.has_value());
                return pimpl->queue_families.graphics;
            case GraphicsPresent:
                assert(pimpl->queue_families.graphics_present.has_value());
                return pimpl->queue_families.graphics_present;
            case AsynchronousCompute:
                return pimpl->queue_families.async_compute;
            case AsynchronousComputePresent:
                return pimpl->queue_families.async_compute_present;
            case AsynchronousTransfer:
                return pimpl->queue_families.async_transfer;
        }
    }
    uint32_t DeviceInterface::QueryLimit(PhysicalDeviceLimitInteger limit) const {
        switch(limit) {
            using enum PhysicalDeviceLimitInteger;
            case MaxUniformBufferSize:
                return pimpl->physical_device_properties.limits.maxUniformBufferRange;
            case MaxStorageBufferSize:
                return pimpl->physical_device_properties.limits.maxStorageBufferRange;
            case UniformBufferOffsetAlignment:
                return pimpl->physical_device_properties.limits.minUniformBufferOffsetAlignment;
            case StorageBufferOffsetAlignment:
                return pimpl->physical_device_properties.limits.minStorageBufferOffsetAlignment;
            case AsyncTransferImageGranularityWidth:
                return std::get<0>(pimpl->queue_families.async_transfer_granularity);
            case AsyncTransferImageGranularityHeight:
                return std::get<1>(pimpl->queue_families.async_transfer_granularity);
            case AsyncTransferImageGranularityDepth:
                return std::get<2>(pimpl->queue_families.async_transfer_granularity);
        }
        return 0;
    }
    float DeviceInterface::QueryLimit(PhysicalDeviceLimitFloat limit) const {
        assert(!"Unimplemented.");
        return 0.0f;
    }
} // namespace Engine::RenderSystemState
