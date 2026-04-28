#include "Importer.h"
#include "FbxLoader.h"
#include "GltfLoader.h"
#include "ObjLoader.h"
#include <algorithm>
#include <cctype>
#include <memory>

namespace Engine::Importer {
    void ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project) {
        std::string extension = resourcePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (extension == ".obj") {
            auto loader = std::make_unique<ObjLoader>();
            loader->LoadObjResource(resourcePath, path_in_project);
        } else if (extension == ".fbx") {
            auto loader = std::make_unique<FbxLoader>();
            loader->LoadFbxResource(resourcePath, path_in_project);
        } else if (extension == ".gltf" || extension == ".glb") {
            auto loader = std::make_unique<GltfLoader>();
            loader->LoadGltfResource(resourcePath, path_in_project);
        } else {
            throw std::runtime_error("Unsupported file format: " + extension);
        }
    }
} // namespace Engine::Importer
