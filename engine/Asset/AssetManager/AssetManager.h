#ifndef ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
#define ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED

#include <Core/guid.h>
#include <filesystem>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine {
    class Asset;
    class AssetRef;

    /**
     * @brief The Asset Management system is responsible for handling various types of game assets,
     * such as textures, models, materials, and game object prefabs.
     * It ensures that these assets are efficiently loaded, managed, and utilized during the game's runtime.
     */
    class AssetManager : public std::enable_shared_from_this<AssetManager> {
    public:
        AssetManager() = default;
        virtual ~AssetManager() = default;

        /**
         * @brief Add a loaded asset to the manager.
         * @param asset The asset to add.
         * @note The asset may not be stored on the disk.
         */
        void AddLoadedAsset(std::unique_ptr<Asset> asset);

        /**
         * @brief Create a new asset of type T.
         * @param args The constructor arguments for the asset.
         * @return T* The newly created asset.
         * @note The asset is added to the manager immediately.
         * @note The asset will not be stored on the disk.
         */
        template <typename T, typename... Args>
        T *CreateAsset(Args... args) {
            auto asset = std::make_unique<T>(std::forward<Args>(args)...);
            GUID guid = asset->GetGUID();
            AddLoadedAsset(std::move(asset));
            return dynamic_cast<T *>(GetAsset(guid));
        }

        /**
         * @brief Add an asset to the loading queue.
         * @param guid The GUID of the asset to add.
         * @note If the asset is already in the queue, it is ignored.
         */
        void AddToLoadingQueue(const GUID &guid);

        /**
         * @brief Load assets in the queue.
         * @note This method will block until all are loaded.
         */
        void LoadAssetsInQueue();

        /**
         * @brief Load an asset immediately from the disk.
         * @param guid The GUID of the asset to load.
         * @return Asset* The loaded asset.
         */
        Asset *LoadAssetImmediately(const GUID &guid);

        /**
         * @brief Get an asset from the manager. If the asset is not loaded, it returns nullptr.
         * @param guid The GUID of the asset to get.
         * @return Asset* The asset. nullptr if the asset is not loaded.
         */
        Asset *GetAsset(const GUID &guid);

        /**
         * @brief Check if an asset is loaded.
         * @param guid The GUID of the asset to check.
         * @return bool True if the asset is loaded, false otherwise.
         */
        bool IsAssetLoaded(const GUID &guid);

        /**
         * @brief Unload an asset from the manager.
         * @param guid The GUID of the asset to unload.
         * @note If the asset is not loaded, it is ignored.
         */
        void UnloadAsset(const GUID &guid);

        /**
         * @brief Unload all unused assets from the manager.
         * @note This method will block until all are unloaded.
         */
        void UnloadUnusedAssets();

        /**
         * @brief Increment the reference count of an asset.
         * @param guid The GUID of the asset to increment.
         */
        void IncrementRefCount(const GUID &guid);

        /**
         * @brief Decrement the reference count of an asset.
         * @param guid The GUID of the asset to decrement.
         */
        void DecrementRefCount(const GUID &guid);

    protected:
        std::queue<GUID> m_loading_queue{};
        std::unordered_set<GUID> m_in_loading_queue{};

        std::unordered_map<GUID, std::unique_ptr<Asset>> m_loaded_assets{};
        std::unordered_map<GUID, unsigned int> m_asset_ref_count{};
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ASSET_ASSETMANAGER_ASSETMANAGER_INCLUDED
