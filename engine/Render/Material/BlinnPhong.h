#ifndef RENDER_MATERIAL_BLINNPHONG_INCLUDED
#define RENDER_MATERIAL_BLINNPHONG_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Memory/Buffer.h"
#include "Render/Memory/Image2DTexture.h"

namespace Engine
{
    class ShaderModule;
    class AssetRef;

    class BlinnPhong : public Material
    {
    public:
        struct UniformData
        {
            // Specular color (rgb) with expoential shininess index (a)
            glm::vec4 specular;
            glm::vec4 ambient;
        };

    private:
        vk::UniqueSampler m_sampler{};
        ShaderModule fragModule;
        ShaderModule vertModule;

        std::vector<vk::DescriptorSetLayoutBinding> GetBindings();

        Buffer m_uniform_buffer;
        std::byte *m_mapped_buffer{nullptr};
        std::unique_ptr<AllocatedImage2DTexture> m_texture{};

    public:
        BlinnPhong(std::weak_ptr<RenderSystem> system, std::shared_ptr<AssetRef> asset);
        BlinnPhong(const BlinnPhong &) = delete;
        BlinnPhong operator=(const BlinnPhong &) = delete;

        const virtual Pipeline *GetPipeline(uint32_t pass_index) override;
        void CommitBuffer(TransferCommandBuffer &tcb);
        void UpdateUniform(const UniformData &uniform);
    };
}

#endif // RENDER_MATERIAL_BLINNPHONG_INCLUDED
