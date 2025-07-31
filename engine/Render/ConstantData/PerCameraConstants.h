#ifndef RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED
#define RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED

#include <glm.hpp>
#include <vulkan/vulkan.hpp>
#include "Render/VkWrapper.tcc"

namespace Engine {
    class RenderSystem;

    namespace ConstantData {
        struct PerCameraStruct {
            glm::mat4 view_matrix;
            glm::mat4 proj_matrix;
        };

        class PerCameraConstantLayout : public VkWrapperIndependent<vk::UniqueDescriptorSetLayout> {
        protected:
            static constexpr std::array <vk::DescriptorSetLayoutBinding, 1> BINDINGS = {
                vk::DescriptorSetLayoutBinding{
                    0,
                    vk::DescriptorType::eUniformBufferDynamic,
                    1,
                    vk::ShaderStageFlagBits::eAllGraphics
                }
            };
        public:
            static constexpr uint32_t PER_CAMERA_SET_NUMBER = 1;
            void Create(vk::Device device);
        };
    }
}

#endif // RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED
