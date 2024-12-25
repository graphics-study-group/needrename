    #ifndef RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
#define RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/AttachmentUtils.h"

namespace Engine {
    class MaterialInstance;
    class Pipeline;
    class PipelineLayout;

    /// @brief A factory class for instantiation of materials.
    /// Contains all public immutable data for a given type of materials, such as pipeline
    /// and its configurations, descriptor set layout and attachment operations.
    class MaterialTemplate {
    public:
        void ConstructFromJson(const char * json);

        MaterialInstance * CreateMaterial();

        const Pipeline * GetPipeline(uint32_t pass_index = 0) const;
        const PipelineLayout * GetPipelineLayout(uint32_t pass_index = 0) const;
        vk::DescriptorSetLayout GetDescriptorSetLayout(uint32_t pass_index = 0) const;
        vk::DescriptorSet AllocateDescriptorSet(uint32_t pass_index = 0);

        AttachmentUtils::AttachmentOp GetDSAttachmentOperation(uint32_t pass_index = 0);
        AttachmentUtils::AttachmentOp GetColorAttachmentOperation(uint32_t index, uint32_t pass_index = 0);
    };
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
