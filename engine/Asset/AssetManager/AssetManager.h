#ifndef ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
#define ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED

#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <queue>
#include <filesystem>
#include <tiny_obj_loader.h>
#include <Core/guid.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class Asset;
    class AssetRef;

    class AssetManager : public std::enable_shared_from_this<AssetManager>
    {
    public:
        AssetManager() = default;
        virtual ~AssetManager() = default;

        /***************** Initialization Functions ****************/ 

        /// @brief Set the path to the built-in assets
        void SetBuiltinAssetPath(const std::filesystem::path &path);

        /// @brief Load built-in assets
        void LoadBuiltinAssets();

        /// @brief Load asset list from a project directory
        /// @param path Path to the project directory
        void LoadProject(std::filesystem::path path);

        /***************** Query Global Informations ****************/ 

        /// @brief Generate a GUID
        /// @return GUID
        inline GUID GenerateGUID() { return generateGUID(m_guid_gen); }

        inline std::filesystem::path GetProjectPath() const { return m_project_path; }
        inline std::filesystem::path GetAssetsDirectory() const { return m_project_path / "assets"; }

        /***************** Asset Data Base Query And Modification ****************/
        // TODO: Implement AssetDataBase to support multiple retrieval methods

        /// @brief Load an external resource, copy to the project asset directory
        /// @param resourcePath Path to the external resource
        /// @param path_in_project Path to the output asset file
        void ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project);

        /// @brief Add an asset guid to the system.
        /// @param guid the guid of the asset
        /// @param path the path of asset file relative to the project directory
        void AddAsset(const GUID &guid, const std::filesystem::path &path);

        /// @brief Get the path to the asset file
        /// @param guid GUID of the asset
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(GUID guid) const;

        /// @brief Get the path to the asset file
        /// @param asset Asset class pointer
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(const std::shared_ptr<Asset> &asset) const;

        /// @brief Get an unloaded AssetRef of the given path
        /// @param path the path of the asset file relative to the project directory
        /// @return an unloaded AssetRef if the path exist, which only contains the GUID. nullptr otherwise
        std::shared_ptr<AssetRef> GetNewAssetRef(const std::filesystem::path &path);

        /***************** Asset Loading ****************/

        void AddToLoadingQueue(std::shared_ptr<AssetRef> asset);

        void LoadAssetsInQueue();
        std::shared_ptr<Asset> LoadAssetImmediately(const GUID &guid);
        void LoadAssetImmediately(std::shared_ptr<AssetRef> asset_ref);

    protected:
        std::mt19937_64 m_guid_gen{std::random_device{}()};

        std::filesystem::path m_builtin_asset_path{};
        std::filesystem::path m_project_path{};

        /// @brief GUID to asset path map
        std::unordered_map<GUID, std::filesystem::path, GUIDHash> m_assets_map{};
        std::unordered_map<std::filesystem::path, GUID> m_path_to_guid{};

        std::queue<std::shared_ptr<AssetRef>> m_loading_queue{};
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
