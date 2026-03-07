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

        /// @brief Generate a GUID
        /// @return GUID
        inline GUID GenerateGUID() {
            return generateGUID(m_guid_gen);
        }

        void AddToLoadingQueue(const GUID &guid);
        void LoadAssetsInQueue();
        std::shared_ptr<Asset> LoadAssetImmediately(const GUID &guid);
        std::shared_ptr<Asset> GetAsset(const GUID &guid, bool load_if_not_loaded = true);
        bool IsAssetLoaded(const GUID &guid);
        void UnloadAsset(const GUID &guid);
        void UnloadUnusedAssets();

    protected:
        std::mt19937_64 m_guid_gen{std::random_device{}()};

        std::queue<GUID> m_loading_queue{};
        std::unordered_map<GUID, std::shared_ptr<Asset>> m_loaded_assets{};
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
