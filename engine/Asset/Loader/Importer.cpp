#include "Importer.h"
#include "AssimpLoader.h"
#include <algorithm>
#include <cctype>
#include <memory>

namespace Engine::Importer {
    void ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project) {
        std::string extension = resourcePath.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
        );
        if (extension == ".obj" || extension == ".fbx") {
            auto loader = std::make_unique<AssimpLoader>();
            loader->LoadResource(resourcePath, path_in_project);
        } else {
            throw std::runtime_error("Unsupported file format: " + extension);
        }
    }
} // namespace Engine::Importer
