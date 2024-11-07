#include "BoneWeightVisualizer.h"

#include "Render/Pipeline/PremadePipeline/ConfigurablePipeline.h"

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
    std::vector<vk::DescriptorSetLayoutBinding> BoneWeightVisualizer::GetBindings()
    {
        vk::DescriptorSetLayoutBinding binding_uniform {
            0,
            vk::DescriptorType::eUniformBuffer,
            1,
            vk::ShaderStageFlagBits::eAllGraphics,
            {}
        };
        return std::vector<vk::DescriptorSetLayoutBinding>{binding_uniform};
    }
    BoneWeightVisualizer::BoneWeightVisualizer(std::weak_ptr<RenderSystem> system) 
    : Material(system), fragModule(system), vertModule(system), m_uniform_buffer(system)
    {
        std::vector <char> shaderData = readFile("shader/debug_fragment_color.frag.spv");
        fragModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()), 
            shaderData.size(), 
            ShaderModule::ShaderType::Fragment
        );
                
        shaderData = readFile("shader/bone_weight_visualizer.vert.spv");
        vertModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()),
            shaderData.size(), 
            ShaderModule::ShaderType::Vertex
        );

        m_passes.resize(1);
        m_passes[0].skinned_pipeline = std::make_unique <SkinnedConfigurablePipeline> (system);
        m_passes[0].skinned_pipeline_layout = std::make_unique <PipelineLayout> (system);

        auto & manager = system.lock()->GetMaterialDescriptorManager();
        vk::DescriptorSetLayout set_layout = manager.NewDescriptorSetLayout("BoneWeightVisualizer", GetBindings());
        auto descriptor_set = manager.AllocateDescriptorSet(set_layout);
        m_passes[0].descriptor_set = descriptor_set;

        auto & pipeline_layout = *(m_passes[0].skinned_pipeline_layout.get());
        pipeline_layout.CreateWithDefault(set_layout, true);

        auto ptr_pipeline = dynamic_cast<SkinnedConfigurablePipeline *>(m_passes[0].skinned_pipeline.get());
        ptr_pipeline->SetPipelineConfiguration(pipeline_layout, {fragModule, vertModule}, {});

        m_uniform_buffer.Create(Buffer::BufferType::Uniform, sizeof(UniformData));
        m_mapped_buffer = m_uniform_buffer.Map();
        vk::DescriptorBufferInfo dbinfo {
            m_uniform_buffer.GetBuffer(), 0, sizeof(UniformData)
        };
        vk::WriteDescriptorSet write {
            m_passes[0].descriptor_set,
            0,
            0,
            1,
            vk::DescriptorType::eUniformBuffer,
            {},
            {&dbinfo}
        };
        m_system.lock()->getDevice().updateDescriptorSets({write}, {});
    }

    const Pipeline *BoneWeightVisualizer::GetPipeline(uint32_t pass_index)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Requesting non-skinned pipeline for BoneWeightVisualizer.");
        return nullptr;
    }
    const Pipeline *BoneWeightVisualizer::GetSkinnedPipeline(uint32_t pass_index)
    {
        auto pipeline = m_passes[pass_index].skinned_pipeline.get();
        if (!pipeline->get()) {
            pipeline->CreatePipeline();
        }
        return pipeline;
    }

    void BoneWeightVisualizer::UpdateUniform(const UniformData &uniform)
    {
        assert(m_mapped_buffer);
        memcpy(m_mapped_buffer, &uniform, sizeof uniform);
        m_uniform_buffer.Flush();
    }
}
