#ifndef ASSET_ASSET_INCLUDED
#define ASSET_ASSET_INCLUDED

#include <string>
#include <filesystem>
#include <Core/guid.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class AssetManager;
    /// @brief Base class for all assets.
    class REFL_SER_CLASS(REFL_BLACKLIST) Asset
    {
        REFL_SER_BODY(Asset)
    public:
        Asset();
        virtual ~Asset();

        /// @brief Get the path to the asset file
        /// @return path to the asset file
        virtual std::filesystem::path GetAssetPath();

        /// @brief Get the path to the asset meta file
        /// @return path to the asset meta file
        virtual std::filesystem::path GetMetaPath();

        /// @brief Save the asset to the archive. Only used for automatic serialization when it is a member of another class. Only save the GUID of the asset
        virtual void save_to_archive(Serialization::Archive& archive) const;
        /// @brief Load the asset from the archive. Only used for automatic serialization when it is a member of another class. Only load the GUID of the asset
        virtual void load_from_archive(Serialization::Archive& archive);

        /// @brief Save the asset to the archive. It will call generated save function __serialization_save__(). Save all the data of the asset. Usually called by AssetManager
        virtual void save_asset_to_archive(Serialization::Archive& archive) const;
        /// @brief Load the asset from the archive. It will call generated load function __serialization_load__(). Load all the data of the asset. Usually called by AssetManager
        virtual void load_asset_from_archive(Serialization::Archive& archive);

        virtual void Unload();

        inline bool IsValid() const { return m_valid; }
        inline GUID GetGUID() const { return m_guid; }
        inline void SetGUID(GUID guid) { m_guid = guid; }

    protected:
        REFL_SER_DISABLE bool m_valid = false;
        GUID m_guid;

        inline void SetValid(bool valid) { m_valid = valid; }
    };
}

#endif // ASSET_ASSET_INCLUDED
