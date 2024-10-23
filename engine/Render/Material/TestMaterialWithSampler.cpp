#include "TestMaterialWithSampler.h"

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
    TestMaterialWithSampler::TestMaterialWithSampler (
        std::weak_ptr <RenderSystem> system,
        const AllocatedImage2DTexture & texture
    ) : Material(system), fragModule(system), vertModule(system) {

        std::vector <char> shaderData = readFile("shader/debug_fragment_sampler.frag.spv");
        fragModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()), 
            shaderData.size(), 
            ShaderModule::ShaderType::Fragment
        );
                
        shaderData = readFile("shader/debug_vertex_sampler.vert.spv");
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
        m_passes[0].pipeline = std::make_unique <PremadePipeline::ConfigurablePipeline> (system);
        m_passes[0].pipeline_layout = std::make_unique <PipelineLayout> (system);

        auto & manager = system.lock()->GetMaterialDescriptorManager();
        vk::DescriptorSetLayoutBinding binding {
            0, 
            vk::DescriptorType::eCombinedImageSampler,
            1,
            vk::ShaderStageFlagBits::eFragment,
            { &m_sampler.get() }
        };
        vk::DescriptorSetLayout set_layout = manager.NewDescriptorSetLayout("TestMaterial", {binding});
        auto descriptor_set = manager.AllocateDescriptorSet(set_layout);
        m_passes[0].descriptor_set = descriptor_set;

        auto & pipeline_layout = *(m_passes[0].pipeline_layout.get());
        pipeline_layout.CreateWithDefault({set_layout});
        m_passes[0].pipeline->SetPipelineConfiguration(pipeline_layout, {fragModule, vertModule});

        // Write texture descriptor
        vk::DescriptorImageInfo image_info {
            m_sampler.get(),
            texture.GetImageView(),
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        vk::WriteDescriptorSet write {
            descriptor_set,
            0,
            0,
            1,
            vk::DescriptorType::eCombinedImageSampler,
            {&image_info}
        };
        system.lock()->getDevice().updateDescriptorSets({write}, {});
    }
    const Pipeline *TestMaterialWithSampler::GetPipeline(uint32_t pass_index)
    {
        auto pipeline = m_passes[pass_index].pipeline.get();
        if (!pipeline->get()) {
            pipeline->CreatePipeline();
        }
        return pipeline;
    }
}
