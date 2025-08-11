#ifndef RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED
#define RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED

#include "Render/VkWrapper.tcc"
#include <glm.hpp>

namespace vk {
    class Device;
}

namespace Engine {
    class RenderSystem;

    namespace ConstantData {
        struct PerCameraStruct {
            glm::mat4 view_matrix;
            glm::mat4 proj_matrix;
        };

        class PerCameraConstantLayout : public VkWrapperIndependent<vk::UniqueDescriptorSetLayout> {
        public:
            static constexpr uint32_t PER_CAMERA_SET_NUMBER = 1;
            void Create(vk::Device device);
        };
    } // namespace ConstantData
} // namespace Engine

#endif // RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED
