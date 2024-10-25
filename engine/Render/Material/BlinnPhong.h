#ifndef RENDER_MATERIAL_BLINNPHONG_INCLUDED
#define RENDER_MATERIAL_BLINNPHONG_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Memory/Buffer.h"

namespace Engine {
    class ShaderModule;
    class AllocatedImage2DTexture;

    class BlinnPhong : public Material {
    public:
        struct UniformData {
            glm::vec4 specular;
            glm::vec4 ambient;
        };
    private:
        vk::UniqueSampler m_sampler {};
        ShaderModule fragModule;
        ShaderModule vertModule;

        std::vector <vk::DescriptorSetLayoutBinding> GetBindings();

        Buffer m_uniform_buffer;
        std::byte * m_mapped_buffer{nullptr};
        UniformData m_uniform_data;
    public:

        BlinnPhong (std::weak_ptr <RenderSystem> system);

        const virtual Pipeline * GetPipeline(uint32_t pass_index) override;

        void UpdateTexture(const AllocatedImage2DTexture & texture);
        void UpdateUniform(const UniformData & uniform);
    };
}

#endif // RENDER_MATERIAL_BLINNPHONG_INCLUDED
