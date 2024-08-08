#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace Engine
{
    class RendererComponent;
    class CameraComponent;

    struct QueueFamilyIndices
    {
        std::optional <uint32_t> graphics {};

        bool isComplete() {
            return graphics.has_value();
        }
    };

    class RenderSystem
    {
    public:

        RenderSystem();
        // ~RenderSystem();

        void Render();
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        void SetActiveCamera(std::shared_ptr <CameraComponent>);
        
        
    protected:

        static QueueFamilyIndices FillQueueFamily(const vk::PhysicalDevice & device);

        void CreateInstance(const vk::ApplicationInfo & appInfo);
        bool CheckValidationLayer();
        vk::PhysicalDevice SelectPhysicalDevice() const;
        void CreateLogicalDevice(const vk::PhysicalDevice & selectedPhysicalDevice);

        static constexpr std::string_view validation_layer_name = "VK_LAYER_KHRONOS_validation";

        // TODO: data: mesh, texture, light
        std::vector <std::shared_ptr<RendererComponent>> m_components {};
        std::shared_ptr <CameraComponent> m_active_camera {};

        vk::UniqueInstance m_instance{};
        vk::UniqueDevice m_device{};
        vk::Queue m_graphicsQueue{};
    };
}

#endif // RENDER_RENDERSYSTEM_INCLUDED
