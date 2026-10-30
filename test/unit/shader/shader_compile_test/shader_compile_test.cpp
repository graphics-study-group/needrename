#include <Core/FileSystem/DataRoot.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glslang/Public/ShaderLang.h>
#include <iostream>
#include <string>
#include <vector>

#include <Render/Asset/Shader/ShaderCompiler.h>

Engine::ShaderCompiler compiler;

bool Compile(std::filesystem::path path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file!\n";
        return false;
    }
    std::string shader_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<uint32_t> spirv;
    if (compiler.CompileGLSLtoSPV(spirv, shader_code, EShLangFragment, path.parent_path())) {
        std::cout << "Shader compiled successfully! SPIR-V size: " << spirv.size() << " words\n";
    } else {
        std::cerr << "Shader compilation failed!\n";
    }
    return true;
}

int main() {
    bool flag = true;
    flag &= Compile((Engine::TestDataDir() / "unit/shader/shader_compile_test/blinn_phong_test_shader.frag").string());
    flag &=
        Compile((Engine::TestDataDir() / "unit/shader/shader_compile_test/folder/local_include_test.frag").string());
    flag &= Compile(
        (Engine::TestDataDir() / "unit/shader/shader_compile_test/folder/multi_level_include_test.frag").string()
    );

    if (!flag) {
        std::cerr << "One or more shader compilations failed.\n";
        return 1;
    }
    return 0;
}
