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
    }

    ShaderCompiler::~ShaderCompiler() {
        glslang::FinalizeProcess();
    }

    bool ShaderCompiler::CompileGLSLtoSPV(
        std::vector<uint32_t> &spirv,
        const std::string &glsl_code,
        EShLanguage shader_type,
        const std::filesystem::path &shader_directory
    ) {
        const char *shaderStrings[1];
        shaderStrings[0] = glsl_code.c_str();

        glslang::TShader shader(shader_type);
        shader.setEnvInput(glslang::EShSourceGlsl, shader_type, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        shader.setStrings(shaderStrings, 1);

        DirStackFileIncluder includer;
        includer.setLocalPath(shader_directory);
        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(&m_built_in_resource, 100, false, messages, includer)) {
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
