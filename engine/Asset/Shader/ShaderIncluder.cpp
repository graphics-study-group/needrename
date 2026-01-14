#include "ShaderIncluder.h"

#include <filesystem>
#include <cmake_config.h>
#include <MainClass.h>
#include <Asset/AssetDatabase/FileSystemDatabase.h>

namespace Engine {
    DirStackFileIncluder::DirStackFileIncluder() {
        // Automatically add the project asset directory to system paths
        if (auto main_instance = MainClass::GetInstance()) {
            if (auto asset_db = main_instance->GetAssetDatabase()) {
                if (auto fs_db = std::dynamic_pointer_cast<FileSystemDatabase>(asset_db)) {
                    addSystemPath(fs_db->GetAssetsDirectory());
                }
            }
        }
        // Add engine built-in asset directory to system paths
        addSystemPath(ENGINE_BUILTIN_ASSETS_DIR "/shaders/include");
    }

    glslang::TShader::Includer::IncludeResult *DirStackFileIncluder::includeLocal(
        const char *headerName, const char *includerName, size_t inclusionDepth
    ) {
        auto result = readLocalPath(headerName, includerName, static_cast<int>(inclusionDepth));
        if (result != nullptr) {
            return result;
        }
        return readSystemPath(headerName);
    }

    glslang::TShader::Includer::IncludeResult *DirStackFileIncluder::includeSystem(
        const char *headerName, const char * /*includerName*/, size_t /*inclusionDepth*/
    ) {
        return readSystemPath(headerName);
    }

    void DirStackFileIncluder::releaseInclude(IncludeResult *result) {
        if (result != nullptr) {
            delete[] static_cast<tUserDataElement *>(result->userData);
            delete result;
        }
    }

    std::set<std::string> DirStackFileIncluder::getIncludedFiles() {
        return includedFiles;
    }

    DirStackFileIncluder::~DirStackFileIncluder() {
    }

    DirStackFileIncluder::IncludeResult *DirStackFileIncluder::readLocalPath(
        const char *headerName, const char * /*includerName*/, int depth
    ) {
        if (this->localPath != std::filesystem::path()) {
            // Discard popped include directories, and
            // initialize when at parse-time first level.
            directoryStack.resize(depth);
            if (depth == 1) {
                directoryStack.back() = this->localPath;
            }

            std::filesystem::path headerPath(headerName);
            // Find a directory that works, using a reverse search of the include stack.
            for (auto it = directoryStack.rbegin(); it != directoryStack.rend(); ++it) {
                std::filesystem::path candidate = (*it / headerPath).lexically_normal();
                std::ifstream file(candidate, std::ios_base::binary | std::ios_base::ate);
                if (file) {
                    directoryStack.push_back(candidate.parent_path().lexically_normal());

                    std::filesystem::path normalized = candidate.lexically_normal();
                    std::string normalizedStr = normalized.generic_string();

                    includedFiles.insert(normalizedStr);
                    return newIncludeResult(normalizedStr, file, static_cast<int>(file.tellg()));
                }
            }
        }
        return nullptr;
    }

    DirStackFileIncluder::IncludeResult *DirStackFileIncluder::readSystemPath(const char * headerName) const {
        for (const auto & systemPath : systemPaths) {
            std::filesystem::path candidate = (systemPath / std::filesystem::path(headerName)).lexically_normal();
            std::ifstream file(candidate, std::ios_base::binary | std::ios_base::ate);
            if (file) {
                std::filesystem::path normalized = candidate.lexically_normal();
                std::string normalizedStr = normalized.generic_string();
                return newIncludeResult(normalizedStr, file, static_cast<int>(file.tellg()));
            }
        }
        return nullptr;
    }

    DirStackFileIncluder::IncludeResult *DirStackFileIncluder::newIncludeResult(
        const std::string &path, std::ifstream &file, int length
    ) const {
        char *content = new tUserDataElement[length];
        file.seekg(0, file.beg);
        file.read(content, length);
        return new IncludeResult(path, content, length, content);
    }

    void DirStackFileIncluder::setLocalPath(const std::filesystem::path &path) {
        localPath = path;
    }

    void DirStackFileIncluder::addSystemPath(const std::filesystem::path &path) {
        systemPaths.push_back(path);
    }
} // namespace Engine
