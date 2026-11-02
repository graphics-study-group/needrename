#include "Render/Asset/Shader/ShaderCompiler.h"

#include <stdexcept>

namespace Engine {
    // The shape that ships baked SPIR-V. There is no GLSL compiler here and no glslang to build
    // one from, so the class exists to keep MainClass's interface intact and to say, once and
    // with a message, that a caller asking it to compile has taken the wrong path. The engine
    // loads `.spv` assets instead, which is what the bake produces.

    ShaderCompiler::ShaderCompiler(const std::filesystem::path &project_assets_path) {
        m_project_assets_path = project_assets_path;
    }

    ShaderCompiler::~ShaderCompiler() = default;

    bool ShaderCompiler::CompileGLSLtoSPV(
        std::vector<uint32_t> &, const std::string &, EShLanguage, const std::filesystem::path &
    ) {
        throw std::logic_error(
            "ShaderCompiler: this build has no runtime GLSL compiler. Shaders ship as SPIR-V; "
            "bake them with shader_baker and load the .spv asset instead."
        );
    }

    bool ShaderCompiler::CompileGLSLtoSPV(std::vector<uint32_t> &, const std::filesystem::path &, bool) {
        throw std::logic_error(
            "ShaderCompiler: this build has no runtime GLSL compiler. Shaders ship as SPIR-V; "
            "bake them with shader_baker and load the .spv asset instead."
        );
    }
} // namespace Engine
