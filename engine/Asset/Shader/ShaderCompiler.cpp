#include "ShaderCompiler.h"
#include "ShaderIncluder.h"

#include <SDL3/SDL.h>
#include <SPIRV/GlslangToSpv.h>

#include <MainClass.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/DeviceInterface.h>

namespace {
    /**
     * @brief Read whole file into a string.
     */
    std::string read_file(std::filesystem::path path) {

        constexpr auto read_size = std::size_t(4096);
        auto stream = std::ifstream(path);
        stream.exceptions(std::ios_base::badbit);

        if (!stream) {
            throw std::ios_base::failure("file does not exist");
        }
        
        auto out = std::string();
        auto buf = std::string(read_size, '\0');
        while (stream.read(& buf[0], read_size)) {
            out.append(buf, 0, stream.gcount());
        }
        out.append(buf, 0, stream.gcount());
        return out;
    }
}

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
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        shader.setDebugInfo(false);
        shader.setStringsWithLengths(shaderStrings, nullptr, 1);

        DirStackFileIncluder includer;
        includer.setLocalPath(shader_directory);
        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(&m_built_in_resource, 110, false, messages, includer)) {
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
    bool ShaderCompiler::CompileGLSLtoSPV(
        std::vector<uint32_t> &spirv,
        const std::filesystem::path &shader_abs_path,
        bool emit_debug_info
    ) {
        assert(shader_abs_path.is_absolute());

        std::string shader_code = read_file(shader_abs_path);

        std::array <const char *, 1> shader_codes = {shader_code.c_str()};
        auto shader_abs_path_str = shader_abs_path.generic_string();
        std::array <const char *, 1> shader_filename = {shader_abs_path_str.c_str()};

        auto filename = shader_abs_path.filename().generic_string();
        
        EShLanguage shader_type;
        if (filename.find("vert") != filename.npos) {
            shader_type = EShLangVertex;
        } else if (filename.find("frag") != filename.npos) {
            shader_type = EShLangFragment;
        } else if (filename.find("comp") != filename.npos) {
            shader_type = EShLangCompute;
        } else {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                std::format("Failed to infer shader type for shader {}", shader_abs_path.generic_string()).c_str()
            );
            return false;
        }

        /**
         * To generate non-semantic debug source info you need the following settings:
         * - Proper versioning of input, client and target;
         * - Enable debug options for SPIR-V generator (`emitNonSemanticShaderDebugInfo`, etc.);
         * - Enable debug options for GLSL frontend (`setDebugInfo`);
         * - Set up filenames correctly (`setStringsWithLengthsAndNames`);
         * - Enable Debug Message (`EShMsgDebugInfo`).
         */

        glslang::TShader shader(shader_type);
        shader.setEnvInput(glslang::EShSourceGlsl, shader_type, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        shader.setDebugInfo(emit_debug_info);
        shader.setStringsWithLengthsAndNames(shader_codes.data(), nullptr, shader_filename.data(), 1);

        DirStackFileIncluder includer;
        includer.setLocalPath(shader_abs_path.parent_path().lexically_normal().generic_string());
        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
        if (emit_debug_info) {
            messages = (EShMessages)(messages | EShMsgDebugInfo);
        }
        if (!shader.parse(&m_built_in_resource, 110, false, messages, includer)) {
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

        {
            auto option = glslang::SpvOptions{};
            auto intermediate = program.getIntermediate(shader_type);
            if (emit_debug_info) {
                option.emitNonSemanticShaderDebugInfo = true;
                option.emitNonSemanticShaderDebugSource = true;
                option.generateDebugInfo = true;
                option.stripDebugInfo = false;
                option.optimizeSize = false;
                option.disableOptimizer = true;
            }
            glslang::GlslangToSpv(*intermediate, spirv, &option);
        }
        return true;
    }
} // namespace Engine
