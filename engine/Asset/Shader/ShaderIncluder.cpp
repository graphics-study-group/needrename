#include "ShaderIncluder.h"
#include <cmake_config.h>
#include <fstream>

namespace Engine {
    using IncludeResult = glslang::TShader::Includer::IncludeResult;
    typedef char tUserDataElement;

    const std::unordered_map<std::filesystem::path, std::filesystem::path> ShaderIncluder::k_include_path_map{
        {"engine", ENGINE_BUILTIN_ASSETS_DIR "/shaders/include"}
    };

    ShaderIncluder::ShaderIncluder() {
    }

    ShaderIncluder::~ShaderIncluder() {
    }

    IncludeResult *ShaderIncluder::includeLocal(
        const char *headerName, const char *includerName, size_t inclusionDepth
    ) {
        return includeSystem(headerName, includerName, inclusionDepth);
    }

    IncludeResult *ShaderIncluder::includeSystem(const char *headerName, const char *, size_t) {
        std::filesystem::path real_path = std::filesystem::path(headerName);
        for (const auto &[key, path] : k_include_path_map) {
            if (real_path.lexically_normal().generic_string().starts_with(key.generic_string() + "/")) {
                real_path = path / real_path.lexically_relative(key);
                break;
            }
        }
        std::ifstream file(real_path);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            char *data = new tUserDataElement[content.size() + 1];
            std::memcpy(data, content.data(), content.size());
            data[content.size()] = '\0';
            return new IncludeResult(real_path.generic_string(), data, content.size(), data);
        }
        // Not found
        return nullptr;
    }

    void ShaderIncluder::releaseInclude(IncludeResult *result) {
        if (result != nullptr) {
            delete[] static_cast<tUserDataElement *>(result->userData);
            delete result;
        }
    }

} // namespace Engine
