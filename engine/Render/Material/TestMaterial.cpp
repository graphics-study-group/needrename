#include "TestMaterial.h"

#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderPass.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
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
    TestMaterial::TestMaterial (
        std::weak_ptr <RenderSystem> system
    ) : Material(system), fragModule(system), vertModule(system) {

        std::vector <char> shaderData = readFile("shader/debug_fragment_color.frag.spv");
        fragModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()), 
            shaderData.size(), 
            ShaderModule::ShaderType::Fragment
        );
                
        shaderData = readFile("shader/debug_vertex_trig.vert.spv");
        vertModule.CreateShaderModule(
            reinterpret_cast<std::byte*>(shaderData.data()),
            shaderData.size(), 
            ShaderModule::ShaderType::Vertex
        );
    
        m_passes.resize(1);
        m_passes[0].pipeline = std::make_unique <PremadePipeline::ConfigurablePipeline> (system);
        m_passes[0].pipeline_layout = std::make_unique <PipelineLayout> (system);
        auto & layout = *(m_passes[0].pipeline_layout.get());
        // layout.CreatePipelineLayout(GetGlobalDescriptorSetLayout(), {});
        layout.CreateWithDefault({});
        m_passes[0].pipeline->SetPipelineConfiguration(layout, {fragModule, vertModule});
    }

    const Pipeline *TestMaterial::GetPipeline(uint32_t pass_index)
    {
        auto pipeline = m_passes[pass_index].pipeline.get();
        if (!pipeline->get()) {
            pipeline->CreatePipeline();
        }
        return pipeline;
    }
}
