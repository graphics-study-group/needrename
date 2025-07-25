#include "Instance.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace Engine::RenderSystemState {
    void Instance::Create(const char *instance_name, const char *engine_name) {
        vk::ApplicationInfo appInfo{
            instance_name, VK_MAKE_VERSION(0, 1, 0), engine_name, VK_MAKE_VERSION(0, 1, 0), VK_API_VERSION_1_3};

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
        if (CheckValidationLayer()) {
            instInfo.enabledLayerCount = 1;
            instInfo.ppEnabledLayerNames = &(VALIDATION_LAYER_NAME);
        }
        else {
            instInfo.enabledLayerCount = 0;
        }
#else
        instInfo.enabledLayerCount = 0;
#endif
        this->m_handle = vk::createInstanceUnique(instInfo);
    }

    bool Instance::CheckValidationLayer() {
        auto layers = vk::enumerateInstanceLayerProperties();
        for (const auto &layer : layers) {
            if (strcmp(layer.layerName, VALIDATION_LAYER_NAME) == 0) return true;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Validation layer %s not available.", VALIDATION_LAYER_NAME);
        return false;
    }
} // namespace Engine::RenderSystemState
