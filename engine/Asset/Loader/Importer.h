#ifndef ENGINE_ASSET_LOADER_IMPORTER_H
#define ENGINE_ASSET_LOADER_IMPORTER_H

#include <filesystem>

namespace Engine::Importer
{
    /// @brief Load an external resource, copy to the project asset directory.
    /// @param resourcePath Path to the external resource
    /// @param path_in_project Path to the output asset file
    void ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project);
}

#endif // ENGINE_ASSET_LOADER_IMPORTER_H
