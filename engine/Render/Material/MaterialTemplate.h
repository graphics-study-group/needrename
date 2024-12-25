#ifndef RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
#define RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/AttachmentUtils.h"

namespace Engine {
    class MaterialInstance;
    class Pipeline;
    class PipelineLayout;

    class MaterialTemplate {
    public:
        void ConstructFromJson(const char * json);

        MaterialInstance * CreateMaterial();

        const Pipeline * GetPipeline(uint32_t pass_index = 0);
        const PipelineLayout * GetPipelineLayout(uint32_t pass_index = 0);
        vk::DescriptorSet GetDescriptorSet(uint32_t pass_index = 0) const;

        AttachmentUtils::AttachmentOp GetDSAttachmentOperation(uint32_t pass_index = 0);
        AttachmentUtils::AttachmentOp GetColorAttachmentOperation(uint32_t index, uint32_t pass_index = 0);
    };
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
