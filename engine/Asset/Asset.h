#ifndef ASSET_ASSET_INCLUDED
#define ASSET_ASSET_INCLUDED

#include <string>
#include <filesystem>
#include <Core/guid.h>
#include <Reflection/macros.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class AssetManager;
    /// @brief Base class for all assets.
    class REFL_SER_CLASS(REFL_WHITELIST) Asset : public std::enable_shared_from_this<Asset>
    {
        REFL_SER_BODY(Asset)
    public:
        REFL_ENABLE Asset();
        virtual ~Asset();

        /// @brief Get the path to the asset file
        /// @return path to the asset file
        virtual std::filesystem::path GetAssetPath();

        /// @brief Get the path to the asset meta file
        /// @return path to the asset meta file
        virtual std::filesystem::path GetMetaPath();

        /// @brief Not allowed
        virtual void save_to_archive(Serialization::Archive &archive) const;
        /// @brief Not allowed
        virtual void load_from_archive(Serialization::Archive &archive);

        /// @brief Save the asset to the archive. It will call generated save function _SERIALIZATION_SAVE_(). Save all the data of the asset. Usually called by AssetManager
        virtual void save_asset_to_archive(Serialization::Archive &archive) const;
        /// @brief Load the asset from the archive. It will call generated load function _SERIALIZATION_LOAD_(). Load all the data of the asset. Usually called by AssetManager
        virtual void load_asset_from_archive(Serialization::Archive &archive);

        REFL_ENABLE GUID GetGUID() const;

    protected:
        GUID m_guid{};
    };
}

#pragma GCC diagnostic pop

#endif // ASSET_ASSET_INCLUDED
