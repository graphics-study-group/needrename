#ifndef ASSET_ASSETREF_INCLUDED
#define ASSET_ASSETREF_INCLUDED

#include <Core/guid.h>
#include <Reflection/macros.h>
#include <memory>
#include <optional>

namespace Engine {
    class Asset;
    class AssetManager;

    template <class T>
    concept AssetClass = std::is_base_of<Asset, T>::value;

    /**
     * @brief A reference to an asset. Stores the asset's GUID and an acquire flag.
     * The acquire flag is false by default, which means it do not count as a reference in the asset manager.
     * When the acquire flag is true, it counts as a reference in the asset manager.
     * And The asset will be loaded eagerly or asynchronously in the asset manager.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) AssetRef final {
        REFL_SER_BODY(AssetRef)
    public:
        REFL_ENABLE AssetRef();
        REFL_ENABLE AssetRef(GUID guid);
        REFL_ENABLE AssetRef(const Asset *asset);
        REFL_ENABLE AssetRef(const AssetRef &other);
        AssetRef &operator=(const AssetRef &other);
        ~AssetRef();

        /// @brief Save the asset to the archive. Only used for automatic serialization when it is a member of another
        /// class. Only save the GUID of the asset
        void save_to_archive(Serialization::Archive &archive) const;
        /// @brief Load the asset from the archive. Only used for automatic serialization when it is a member of another
        /// class. Only load the GUID of the asset
        void load_from_archive(Serialization::Archive &archive);

        /**
         * @brief Acquire the asset (increment reference count).
         * If the asset is not loaded, it will be loaded **eagerly**.
         */
        REFL_ENABLE void Acquire();

        /**
         * @brief Acquire the asset (increment reference count) asynchronously.
         * If the asset is not loaded, it will be loaded asynchronously.
         */
        REFL_ENABLE void AcquireAsync();

        /**
         * @brief Release the asset (decrement reference count).
         */
        REFL_ENABLE void Release();

        /**
         * @brief Check if the asset is acquired.
         * @return True if the asset is acquired, false otherwise.
         */
        REFL_ENABLE bool IsAcquired() const;

        /**
         * @brief Check if the asset guid is valid.
         * @return True if the asset guid is valid, false otherwise.
         */
        REFL_ENABLE bool IsValid() const;

        /**
         * @brief Get the asset guid.
         * @return The asset guid.
         */
        REFL_ENABLE GUID GetGUID() const;

        /**
         * @brief Get the asset as a specific type.
         * @param async_load Whether to load the asset asynchronously.
         * @return The asset as a specific type.
         */
        template <AssetClass T>
        T *as(bool async_load = false);

        /**
         * @brief Get the asset as a specific type.
         * @note The asset can't be acquired or loaded in this method.
         * @return The asset as a specific type.
         */
        template <AssetClass T>
        const T *cas() const;

    private:
        bool m_is_acquired{false};
        GUID m_guid{};

        Asset *TryGetAsset() const;
    };

    template <AssetClass T>
    const T *AssetRef::cas() const {
        if (!IsValid()) throw std::runtime_error("AssetRef::cas: AssetRef is not valid");
        auto asset = TryGetAsset();
        if (!asset) return nullptr;
        auto ret = dynamic_cast<const T *>(asset);
        if (!ret) throw std::runtime_error("AssetRef::cas: Asset is not of type T");
        return ret;
    }

    template <AssetClass T>
    T *AssetRef::as(bool async_load) {
        if (!IsValid()) throw std::runtime_error("AssetRef::as: AssetRef is not valid");
        if (!IsAcquired()) {
            if (async_load) {
                AcquireAsync();
            } else {
                Acquire();
            }
        }
        auto asset = TryGetAsset();
        if (!asset) return nullptr;
        auto ret = dynamic_cast<T *>(asset);
        if (!ret) throw std::runtime_error("AssetRef::as: Asset is not of type T");
        return ret;
    }
} // namespace Engine

#endif // ASSET_ASSETREF_INCLUDED
