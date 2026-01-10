#include <glslang/Public/ShaderLang.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>

#include <Asset/Shader/ShaderCompiler.h>
#include <cmake_config.h>

int main() {
    glslang::InitializeProcess();

    std::ifstream file(ENGINE_BUILTIN_ASSETS_DIR "/shaders/blinn_phong.frag.0.glsl");
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file!\n";
        return -1;
    }
    std::string shader_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<uint32_t> spirv;
    if (Engine::ShaderCompiler::CompileGLSLtoSPV(shader_code, EShLangFragment, spirv)) {
        std::cout << "Shader compiled successfully! SPIR-V size: " << spirv.size() << " words\n";
    } else {
        std::cerr << "Shader compilation failed!\n";
    }

    glslang::FinalizeProcess();
    return 0;
}
