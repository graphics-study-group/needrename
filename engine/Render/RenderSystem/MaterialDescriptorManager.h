#ifndef RENDER_RENDERSYSTEM_PERMATERIALDESCRIPTORMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_PERMATERIALDESCRIPTORMANAGER_INCLUDED

#include <string>
#include <unordered_map>

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {
        /// @brief Manager for per material descriptor set layouts.
        /// This class is referred to only on material creation, and should not be on critical paths.
        class MaterialDescriptorManager {
        private:
            std::weak_ptr <RenderSystem> m_system {};
            vk::Device m_logical_device {};

            std::unordered_map <std::string, vk::UniqueDescriptorSetLayout> m_layouts {};

            static constexpr uint32_t MAX_SET_SIZE = 16;
            static constexpr std::array <vk::DescriptorPoolSize, 2> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 64},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 64}
            };

            vk::UniqueDescriptorPool m_descriptor_pool {};
        public:
            void Create(std::shared_ptr <RenderSystem> system);
            
            vk::DescriptorSetLayout NewDescriptorSetLayout(std::string name, const std::vector <vk::DescriptorSetLayoutBinding> & bindings);
            // vk::DescriptorSetLayout GetDescriptorSetLayout(std::string name);

            vk::DescriptorSet AllocateDescriptorSet(std::string name);
            vk::DescriptorSet AllocateDescriptorSet(vk::DescriptorSetLayout layout);
        };
    }
}

#endif // RENDER_RENDERSYSTEM_PERMATERIALDESCRIPTORMANAGER_INCLUDED
