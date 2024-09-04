#include "TestMaterial.h"

#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/PremadePipeline/DefaultPipeline.h"

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
        std::weak_ptr <RenderSystem> system, 
        const RenderPass & pass
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
    
        m_pipelines[0].first = std::make_unique <PremadePipeline::DefaultPipeline> (system);
        m_pipelines[0].second = std::make_unique <PipelineLayout> (system);
        auto & layout = *(m_pipelines[0].second.get());
        vk::DescriptorSetLayout per_camera_layout = system.lock()->GetGlobalConstantDescriptorPool().GetPerCameraConstantLayout().get();
        layout.CreatePipelineLayout({per_camera_layout}, {});
        m_pipelines[0].first->CreatePipeline(pass.GetSubpass(0), layout, {fragModule, vertModule});
    }
}
