#ifndef RESOURCE_ASSETMANAGER_ASSETMANAGER_INCLUDED
#define RESOURCE_ASSETMANAGER_ASSETMANAGER_INCLUDED

#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <filesystem>
#include <tiny_obj_loader.h>
#include "Core/guid.h"
#include "Asset/Asset.h"

namespace Engine
{
    class AssetManager
    {
    public:
        AssetManager() = default;
        virtual ~AssetManager() = default;

        /// @brief Load asset list from a project directory
        /// @param path Path to the project directory
        void LoadProject(std::filesystem::path path);

        /// @brief Load an external resource, copy to the project asset directory
        /// @param resourcePath Path to the external resource
        /// @param path_in_project Path to the output asset file
        void LoadExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project);

        /// @brief Save an asset modified in the engine editor to the project asset directory
        /// @tparam AssetType
        /// @param asset asset to save
        /// @param path path to save the asset
        template <typename AssetType>
        void SaveAsset(AssetType asset, std::filesystem::path path);

        /// @brief Get the path to the asset file
        /// @param guid GUID of the asset
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(GUID guid) const;

        /// @brief Get the path to the asset file
        /// @param asset Asset class pointer
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(const std::shared_ptr<Asset> &asset) const;

        inline std::filesystem::path GetProjectPath() const { return m_projectPath; }
        inline std::filesystem::path GetAssetsDirectory() const { return m_projectPath / "assets"; }

    protected:
        static std::mt19937_64 m_guid_gen;

        std::filesystem::path m_projectPath;
        /// @brief GUID to asset path map
        std::unordered_map<GUID, std::filesystem::path, GUIDHash> m_assets;

        /// @brief Generate a GUID
        /// @return GUID
        inline GUID GenerateGUID() { return generateGUID(m_guid_gen); }

        void AddAsset(const GUID &guid, const std::filesystem::path &path);

    private:
        /// @brief Load an external obj resource, copy to the project asset directory, and create a meta file
        /// @param path path to the external obj resource
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        void LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

        /// @brief Load an external material resource, read from .mtl file associated with an obj file. create a meta file at @param path_in_project
        /// @param texture_path_guid_map to mark the texture files that have been loaded, prevent loading the same texture multiple times
        /// @param parent_directory path to the parent directory of the obj file
        /// @param material material data read from tinyobjloader
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        /// @param guid to store the GUID of the material
        void LoadObjMaterialResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const tinyobj::material_t &material, const std::filesystem::path &path_in_project, GUID &guid);

        /// @brief Load an external texture resource, read from .mtl file associated with an obj file. create a meta file at @param path_in_project
        /// @param texture_path_guid_map to mark the texture files that have been loaded, prevent loading the same texture multiple times
        /// @param parent_directory path to the parent directory of the obj file
        /// @param filename file name of the texture file
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        /// @param guid to store the GUID of the texture
        /// @return true if the texture is loaded successfully
        bool LoadObjTextureResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const std::string &filename, const std::filesystem::path &path_in_project, GUID &guid);
    };
} // namespace Engine

#endif // RESOURCE_ASSETMANAGER_ASSETMANAGER_INCLUDED