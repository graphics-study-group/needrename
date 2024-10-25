#ifndef RENDER_CONSTANTDATA_PERSCENECONSTANTS_INCLUDED
#define RENDER_CONSTANTDATA_PERSCENECONSTANTS_INCLUDED

#include <glm.hpp>
#include "Render/VkWrapper.tcc"
#include "Render/Memory/Buffer.h"

namespace Engine {
    class RenderSystem;

    namespace ConstantData {
        struct PerSceneStruct {
            glm::vec4 light_source;
            glm::vec4 light_color;
        };

        class PerSceneConstantLayout : public VkWrapperIndependent<vk::UniqueDescriptorSetLayout> {
        protected:
            static constexpr std::array <vk::DescriptorSetLayoutBinding, 1> BINDINGS = {
                vk::DescriptorSetLayoutBinding{
                    0,
                    vk::DescriptorType::eUniformBuffer,
                    1,
                    vk::ShaderStageFlagBits::eAllGraphics
                }
            };
        public:
            static constexpr uint32_t PER_SCENE_SET_NUMBER = 0;
            void Create(vk::Device device);
        };
    }
}

#endif // RENDER_CONSTANTDATA_PERSCENECONSTANTS_INCLUDED
