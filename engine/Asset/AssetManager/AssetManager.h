#ifndef ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
#define ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED

#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <filesystem>
#include <tiny_obj_loader.h>
#include "Core/guid.h"
#include "Asset/Asset.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class AssetManager : public std::enable_shared_from_this<AssetManager>
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

        /// @brief Generate a GUID
        /// @return GUID
        inline GUID GenerateGUID() { return generateGUID(m_guid_gen); }

        inline std::filesystem::path GetProjectPath() const { return m_projectPath; }
        inline std::filesystem::path GetAssetsDirectory() const { return m_projectPath / "assets"; }

        void AddAsset(const GUID &guid, const std::filesystem::path &path);

    protected:
        std::mt19937_64 m_guid_gen{std::random_device{}()};

        std::filesystem::path m_projectPath;
        /// @brief GUID to asset path map
        std::unordered_map<GUID, std::filesystem::path, GUIDHash> m_assets;
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
