#ifndef ENGINE_ASSET_SHADER_SHADERCOMPILER_H
#define ENGINE_ASSET_SHADER_SHADERCOMPILER_H

#include <cstdint>
#include <glslang/Public/ShaderLang.h>
#include <string>
#include <vector>

namespace Engine::ShaderCompiler {
    bool CompileGLSLtoSPV(const std::string &glsl_code, EShLanguage shader_type, std::vector<uint32_t> &spirv);
}

#endif // ENGINE_ASSET_SHADER_SHADERCOMPILER_H
