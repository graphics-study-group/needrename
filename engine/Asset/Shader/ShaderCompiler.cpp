#include "ShaderCompiler.h"
#include "ShaderIncluder.h"
#include <SDL3/SDL.h>
#include <SPIRV/GlslangToSpv.h>
#include <MainClass.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/DeviceInterface.h>

namespace Engine {
    ShaderCompiler::ShaderCompiler() {
        glslang::InitializeProcess();

        // const auto &di = MainClass::GetInstance()->GetRenderSystem()->GetDeviceInterface();
        // m_built_in_resource.maxUniformBufferSize = static_cast<int>(di.QueryLimit(DeviceInterface::PhysicalDeviceLimitInteger::MaxUniformBufferSize));
    }

    ShaderCompiler::~ShaderCompiler() {
        glslang::FinalizeProcess();
    }

    bool ShaderCompiler::CompileGLSLtoSPV(const std::string &glsl_code, EShLanguage shader_type, std::vector<uint32_t> &spirv) {
        const char *shaderStrings[1];
        shaderStrings[0] = glsl_code.c_str();

        glslang::TShader shader(shader_type);
        shader.setEnvInput(glslang::EShSourceGlsl, shader_type, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        shader.setStrings(shaderStrings, 1);

        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(&m_built_in_resource, 100, false, messages, m_includer)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "GLSL Parsing Failed:\n%s\n%s\n",
                shader.getInfoLog(),
                shader.getInfoDebugLog()
            );
            return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "GLSL Linking Failed:\n%s\n%s\n",
                program.getInfoLog(),
                program.getInfoDebugLog()
            );
            return false;
        }

        glslang::GlslangToSpv(*program.getIntermediate(shader_type), spirv);
        return true;
    }
} // namespace Engine
