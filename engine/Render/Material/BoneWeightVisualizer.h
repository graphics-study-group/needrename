#ifndef RENDER_MATERIAL_BONEWEIGHTVISUALIZER_INCLUDED
#define RENDER_MATERIAL_BONEWEIGHTVISUALIZER_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Memory/Buffer.h"

namespace Engine {
    class ShaderModule;

    class BoneWeightVisualizer : public Material {
    public:
        struct UniformData {
            uint32_t bone_index;
        };
    private:
        ShaderModule fragModule;
        ShaderModule vertModule;

        std::vector <vk::DescriptorSetLayoutBinding> GetBindings();

        Buffer m_uniform_buffer;
        std::byte * m_mapped_buffer{nullptr};
        UniformData m_uniform_data {};
    public:

        BoneWeightVisualizer (std::weak_ptr <RenderSystem> system);

        const virtual Pipeline * GetPipeline(uint32_t pass_index) override;
        const virtual Pipeline * GetSkinnedPipeline(uint32_t pass_index) override;

        void UpdateUniform(const UniformData & uniform);
    };
}

#endif // RENDER_MATERIAL_BONEWEIGHTVISUALIZER_INCLUDED
