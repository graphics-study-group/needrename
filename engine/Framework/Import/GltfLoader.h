#ifndef FRAMEWORK_IMPORT_GLTFLOADER_INCLUDED
#define FRAMEWORK_IMPORT_GLTFLOADER_INCLUDED

#include "Framework/framework_export.h"
#include "ImportTypes.h"

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    class FRAMEWORK_API GltfLoader {
    public:
        GltfLoader();
        virtual ~GltfLoader() = default;

        /**
         * @brief Load an external glTF resource and serialize it into project assets.
         * @param path Path to the external glTF/GLB resource.
         * @param path_in_project Output directory relative to the project asset directory.
         */
        void LoadGltfResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

        /**
         * @brief Load a glTF resource to runtime assets only (no asset file generated).
         * @param path Path to the external glTF/GLB resource.
         * @return Import result that contains created runtime asset refs.
         */
        ImportResult LoadGltfInMemory(const std::filesystem::path &path);

    private:
        AssetManager *m_asset_manager{nullptr};
        FileSystemDatabase *m_database{nullptr};
    };
} // namespace Engine

#endif // FRAMEWORK_IMPORT_GLTFLOADER_INCLUDED
