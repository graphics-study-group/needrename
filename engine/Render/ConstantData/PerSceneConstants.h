#ifndef RENDER_CONSTANTDATA_PERSCENECONSTANTS_INCLUDED
#define RENDER_CONSTANTDATA_PERSCENECONSTANTS_INCLUDED

#include <glm.hpp>
#include "Render/VkWrapper.tcc"

namespace Engine {
    class RenderSystem;

    namespace ConstantData {
        /**
         * Access this uniform struct in shader like this:
```glsl
layout(std140, set = 0, binding = 0) uniform PerSceneUniform {
    uint light_count;
    vec4 light_source[8];
    vec4 light_color[8];
};
```
         */
        struct PerSceneStruct {
            static constexpr size_t MAX_LIGHT_SOURCES_PER_SCENE = 8;
            uint32_t light_count;
            // Light source in world coordinate. The last component is unused.
            alignas(16) glm::vec4 light_source[MAX_LIGHT_SOURCES_PER_SCENE];
            // Light color in RGB multiplied by its strength. The last component is unused.
            alignas(16) glm::vec4 light_color[MAX_LIGHT_SOURCES_PER_SCENE];
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
