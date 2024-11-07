#include "BlinnPhong.h"

#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/PremadePipeline/ConfigurablePipeline.h"
#include "Render/Memory/Image2DTexture.h"

#include <fstream>

inline std::vector <char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) throw std::runtime_error("failed to open file!");

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

namespace Engine {
    std::vector<vk::DescriptorSetLayoutBinding> BlinnPhong::GetBindings()
    {
        vk::DescriptorSetLayoutBinding binding_texture {
            0, 
            vk::DescriptorType::eCombinedImageSampler,
            1,
            vk::ShaderStageFlagBits::eFragment,
            { &m_sampler.get() }
        };
        vk::DescriptorSetLayoutBinding binding_uniform {
            1,
            vk::DescriptorType::eUniformBuffer,
            1,
            vk::ShaderStageFlagBits::eFragment,
            {}
        };
        return std::vector<vk::DescriptorSetLayoutBinding>{binding_texture, binding_uniform};
    }

    BlinnPhong::BlinnPhong(std::weak_ptr<RenderSystem> system): Material(system), fragModule(system), vertModule(system), m_uniform_buffer(system)
    {
        std::vector <char> shaderData = readFile("shader/blinn_phong.frag.spv");
        fragModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()), 
            shaderData.size(), 
            ShaderModule::ShaderType::Fragment
        );
                
        shaderData = readFile("shader/blinn_phong.vert.spv");
        vertModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()),
            shaderData.size(), 
            ShaderModule::ShaderType::Vertex
        );

        vk::SamplerCreateInfo sinfo;
        sinfo.magFilter = sinfo.minFilter = vk::Filter::eNearest;
        sinfo.addressModeU = sinfo.addressModeV = sinfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        m_sampler = system.lock()->getDevice().createSamplerUnique(sinfo);

        m_passes.resize(1);
        m_passes[0].pipeline = std::make_unique <ConfigurablePipeline> (system);
        m_passes[0].pipeline_layout = std::make_unique <PipelineLayout> (system);

        auto & manager = system.lock()->GetMaterialDescriptorManager();
        
        vk::DescriptorSetLayout set_layout = manager.NewDescriptorSetLayout("BlinnPhong", GetBindings());
        m_passes[0].material_descriptor_set_layout = set_layout;

        auto descriptor_set = manager.AllocateDescriptorSet(set_layout);
        m_passes[0].material_descriptor_set = descriptor_set;

        auto & pipeline_layout = *(m_passes[0].pipeline_layout.get());
        pipeline_layout.CreateWithDefault(set_layout);
        m_passes[0].pipeline->SetPipelineConfiguration(pipeline_layout, {fragModule, vertModule});

        // Create uniform buffer and write descriptor
        m_uniform_buffer.Create(Buffer::BufferType::Uniform, sizeof(UniformData));
        m_mapped_buffer = m_uniform_buffer.Map();
        vk::DescriptorBufferInfo dbinfo {
            m_uniform_buffer.GetBuffer(), 0, sizeof(UniformData)
        };
        vk::WriteDescriptorSet write {
            m_passes[0].material_descriptor_set,
            1,
            0,
            1,
            vk::DescriptorType::eUniformBuffer,
            {},
            {&dbinfo}
        };
        m_system.lock()->getDevice().updateDescriptorSets({write}, {});
    }

    const Pipeline *BlinnPhong::GetPipeline(uint32_t pass_index)
    {
        auto pipeline = m_passes[pass_index].pipeline.get();
        if (!pipeline->get()) {
            pipeline->CreatePipeline();
        }
        return pipeline;
    }

    void BlinnPhong::UpdateTexture(const AllocatedImage2DTexture &texture)
    {
        // Write texture descriptor
        vk::DescriptorImageInfo image_info {
            m_sampler.get(),
            texture.GetImageView(),
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        vk::WriteDescriptorSet write {
            m_passes[0].material_descriptor_set,
            0,
            0,
            1,
            vk::DescriptorType::eCombinedImageSampler,
            {&image_info}
        };
        m_system.lock()->getDevice().updateDescriptorSets({write}, {});
    }

    void BlinnPhong::UpdateUniform(const UniformData &uniform)
    {
        assert(m_mapped_buffer);
        memcpy(m_mapped_buffer, &uniform, sizeof uniform);
        m_uniform_buffer.Flush();
    }
}
