#ifndef ASSET_ASSETREF_INCLUDED
#define ASSET_ASSETREF_INCLUDED

#include <memory>
#include <Core/guid.h>
#include <Reflection/macros.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class Asset;
    class AssetManager;

    template <class T>
    concept AssetClass = std::is_base_of<Asset, T>::value;

    class REFL_SER_CLASS(REFL_WHITELIST) AssetRef : public std::enable_shared_from_this<AssetRef>
    {
        REFL_SER_BODY(AssetRef)
    public:
        REFL_ENABLE AssetRef();
        REFL_ENABLE AssetRef(GUID guid);
        REFL_ENABLE AssetRef(std::shared_ptr<Asset> asset);
        virtual ~AssetRef() = default;

        /// @brief Save the asset to the archive. Only used for automatic serialization when it is a member of another class. Only save the GUID of the asset
        virtual void save_to_archive(Serialization::Archive &archive) const;
        /// @brief Load the asset from the archive. Only used for automatic serialization when it is a member of another class. Only load the GUID of the asset
        virtual void load_from_archive(Serialization::Archive &archive);

        REFL_ENABLE virtual void Unload();

        REFL_ENABLE bool IsValid();
        REFL_ENABLE GUID GetGUID();

        template <AssetClass T>
        std::shared_ptr<T> as();

        template <AssetClass T>
        std::shared_ptr <const T> cas() const;

    protected:
        friend class AssetManager;

        std::shared_ptr<Asset> m_asset{};
        GUID m_guid{};
    };

    template <AssetClass T>
    std::shared_ptr<const T> AssetRef::cas() const
    {
        static_assert(std::is_base_of<Asset, T>::value, "T must be a derived class of Asset");
        return std::dynamic_pointer_cast<const T>(m_asset);
    }

    template <AssetClass T>
    std::shared_ptr<T> AssetRef::as()
    {
        static_assert(std::is_base_of<Asset, T>::value, "T must be a derived class of Asset");
        return std::dynamic_pointer_cast<T>(m_asset);
    }
}

#pragma GCC diagnostic pop

#endif // ASSET_ASSETREF_INCLUDED
