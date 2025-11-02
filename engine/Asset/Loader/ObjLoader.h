#ifndef ASSET_LOADER_OBJLOADER_INCLUDED
#define ASSET_LOADER_OBJLOADER_INCLUDED

#include <filesystem>
#include <memory>
#include <tiny_obj_loader.h>
#include <unordered_map>

namespace Engine {
    class AssetManager;
    class AssetDatabase;
    class MeshAsset;
    class MaterialAsset;

    class ObjLoader {
    public:
        ObjLoader();
        virtual ~ObjLoader() = default;

    public:
        /// @brief Load an external obj resource, copy to the project asset directory, and create a meta file
        /// @param path path to the external obj resource
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        void LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

        void LoadMeshAssetFromTinyObj(
            MeshAsset &mesh_asset, const tinyobj::attrib_t &attrib, const std::vector<tinyobj::shape_t> &shapes
        );
        void LoadMaterialAssetFromTinyObj(
            MaterialAsset &material_asset, const tinyobj::material_t &material, const std::filesystem::path &base_path
        );

    protected:
        std::weak_ptr<AssetManager> m_manager{};
        std::weak_ptr<AssetDatabase> m_database{};
    };
} // namespace Engine

#endif // ASSET_LOADER_OBJLOADER_INCLUDED
