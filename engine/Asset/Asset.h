#ifndef ASSET_ASSET_INCLUDED
#define ASSET_ASSET_INCLUDED

#include <string>
#include <filesystem>
#include "Core/guid.h"

namespace Engine
{
    class AssetManager;
    /// @brief Base class for all assets.
    class Asset
    {
    public:
        Asset(std::weak_ptr <AssetManager> m_manager);
        virtual ~Asset();

        /// @brief Load asset from file to the memory
        virtual void Load();

        /// @brief Unload asset from memory
        virtual void Unload();

        /// @brief Get the path to the asset file
        /// @return path to the asset file
        virtual std::filesystem::path GetAssetPath();

        /// @brief Get the path to the asset meta file
        /// @return path to the asset meta file
        virtual std::filesystem::path GetMetaPath();

        inline bool IsValid() const { return m_valid; }
        inline GUID GetGUID() const { return m_guid; }
        inline void SetGUID(GUID guid) { m_guid = guid; }

    protected:
        std::weak_ptr <AssetManager> m_manager;
        bool m_valid = false;
        GUID m_guid;
    };
}

#endif // ASSET_ASSET_INCLUDED