#ifndef RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED
#define RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED

#include <glm.hpp>
#include <vulkan/vulkan_handles.hpp>

namespace vk {
    class DescriptorSetLayout;
    class Device;
}

namespace Engine {
    class RenderSystem;

    namespace ConstantData {
        struct PerCameraStruct {
            glm::mat4 view_matrix;
            glm::mat4 proj_matrix;
        };

        class PerCameraConstantLayout {
            vk::UniqueDescriptorSetLayout m_handle;
        public:
            static constexpr uint32_t PER_CAMERA_SET_NUMBER = 1;
            void Create(vk::Device device);

            vk::DescriptorSetLayout get() const noexcept {
                return m_handle.get();
            }
        };
    } // namespace ConstantData
} // namespace Engine

#endif // RENDER_CONSTANTDATA_PERCAMERACONSTANTS_INCLUDED
