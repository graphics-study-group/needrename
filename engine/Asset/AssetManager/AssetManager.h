#ifndef ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
#define ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED

#include <Core/guid.h>
#include <filesystem>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine {
    class Asset;
    class AssetRef;

    class AssetManager : public std::enable_shared_from_this<AssetManager> {
    public:
        AssetManager() = default;
        virtual ~AssetManager() = default;

        /***************** Initialization Functions ****************/

        /// @brief Load built-in assets
        void LoadBuiltinAssets();

        /// @brief Load asset list from project
        void LoadProject();

        /***************** Asset (GUID) Query And Modification ****************/

        /// @brief Generate a GUID
        /// @return GUID
        inline GUID GenerateGUID() {
            return generateGUID(m_guid_gen);
        }

        /// @brief Load an external resource, copy to the project asset directory
        /// @param resourcePath Path to the external resource
        /// @param path_in_project Path to the output asset file
        void ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project);

        /// @brief Add an asset guid to the system.
        /// @param guid the guid of the asset
        /// @param path the in-project path of the asset
        void AddAsset(const GUID &guid, const std::filesystem::path &path);

        /// @brief Get the in-project path to the asset
        /// @param guid GUID of the asset
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(GUID guid) const;

        /// @brief Get the in-project path to the asset
        /// @param asset Asset class pointer
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(const std::shared_ptr<Asset> &asset) const;

        /// @brief Get an unloaded AssetRef of the given path
        /// @param path the in-project path of the asset
        /// @return an unloaded AssetRef if the path exist, which only contains the GUID. nullptr otherwise
        std::shared_ptr<AssetRef> GetNewAssetRef(const std::filesystem::path &path);

        /***************** Asset Loading ****************/

        void AddToLoadingQueue(std::shared_ptr<AssetRef> asset);

        void LoadAssetsInQueue();
        std::shared_ptr<Asset> LoadAssetImmediately(const GUID &guid);
        void LoadAssetImmediately(std::shared_ptr<AssetRef> asset_ref);

    protected:
        std::mt19937_64 m_guid_gen{std::random_device{}()};

        /// @brief GUID to asset path map
        std::unordered_map<GUID, std::string, GUIDHash> m_assets_map{};
        std::unordered_map<std::string, GUID> m_path_to_guid{};

        std::queue<std::shared_ptr<AssetRef>> m_loading_queue{};
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
