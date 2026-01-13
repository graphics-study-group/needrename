#ifndef ENGINE_ASSET_SHADER_SHADERINCLUDER_H
#define ENGINE_ASSET_SHADER_SHADERINCLUDER_H

#pragma once

#include <filesystem>
#include <glslang/Public/ShaderLang.h>
#include <unordered_map>

namespace Engine {
    class ShaderIncluder : public glslang::TShader::Includer {
        static const std::unordered_map<std::filesystem::path, std::filesystem::path> k_include_path_map;

    public:
        ShaderIncluder();
        virtual ~ShaderIncluder();

        virtual IncludeResult *includeLocal(
            const char *headerName, const char *includerName, size_t inclusionDepth
        ) override;
        virtual IncludeResult *includeSystem(
            const char *headerName, const char *includerName, size_t inclusionDepth
        ) override;
        virtual void releaseInclude(IncludeResult *result) override;
    };

} // namespace Engine

#endif // ENGINE_ASSET_SHADER_SHADERINCLUDER_H
