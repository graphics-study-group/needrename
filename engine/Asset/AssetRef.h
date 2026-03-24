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

    class REFL_SER_CLASS(REFL_WHITELIST) AssetRef final {
        REFL_SER_BODY(AssetRef)
    public:
        REFL_ENABLE AssetRef();
        REFL_ENABLE AssetRef(GUID guid);
        REFL_ENABLE AssetRef(const Asset *asset);
        ~AssetRef();

        /// @brief Save the asset to the archive. Only used for automatic serialization when it is a member of another
        /// class. Only save the GUID of the asset
        void save_to_archive(Serialization::Archive &archive) const;
        /// @brief Load the asset from the archive. Only used for automatic serialization when it is a member of another
        /// class. Only load the GUID of the asset
        void load_from_archive(Serialization::Archive &archive);

        REFL_ENABLE void Acquire(bool async_load);
        REFL_ENABLE void Release();
        REFL_ENABLE bool IsAcquired() const;
        REFL_ENABLE bool IsValid() const;
        REFL_ENABLE GUID GetGUID() const;

        template <AssetClass T>
        T *as(bool async_load = false);

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
            Acquire(async_load);
        }
        auto asset = TryGetAsset();
        if (!asset) return nullptr;
        auto ret = dynamic_cast<T *>(asset);
        if (!ret) throw std::runtime_error("AssetRef::as: Asset is not of type T");
        return ret;
    }
} // namespace Engine

#endif // ASSET_ASSETREF_INCLUDED
