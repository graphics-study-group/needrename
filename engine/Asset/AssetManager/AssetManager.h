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

        void AddLoadedAsset(std::unique_ptr<Asset> asset);
        template <typename T, typename... Args>
        T *CreateAsset(Args... args) {
            auto asset = std::make_unique<T>(std::forward<Args>(args)...);
            GUID guid = asset->GetGUID();
            AddLoadedAsset(std::move(asset));
            return dynamic_cast<T *>(GetAsset(guid));
        }

        void AddToLoadingQueue(const GUID &guid);
        void LoadAssetsInQueue();
        Asset *LoadAssetImmediately(const GUID &guid);
        Asset *GetAsset(const GUID &guid);
        bool IsAssetLoaded(const GUID &guid);
        void UnloadAsset(const GUID &guid);
        void UnloadUnusedAssets();
        void IncrementRefCount(const GUID &guid);
        void DecrementRefCount(const GUID &guid);

    protected:
        std::mt19937_64 m_guid_gen{std::random_device{}()};

        std::queue<GUID> m_loading_queue{};
        std::unordered_map<GUID, std::unique_ptr<Asset>> m_loaded_assets{};
        std::unordered_map<GUID, unsigned int> m_asset_ref_count{};
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
