#include "Importer.h"
#include "ObjLoader.h"
#include <memory>

namespace Engine::Importer {
    void ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project) {
        std::string extension = resourcePath.extension().string();
        if (extension == ".obj") {
            auto loader = std::make_unique<ObjLoader>();
            loader->LoadObjResource(resourcePath, path_in_project);
        } else {
            throw std::runtime_error("Unsupported file format");
        }
    }
} // namespace Engine::Importer
