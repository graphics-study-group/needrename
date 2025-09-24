#ifndef ASSET_MATERIAL_MATERIALLIBRARYASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALLIBRARYASSET_INCLUDED

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_unordered_map.h>
#include <Asset/Asset.h>
#include <Asset/AssetRef.h>

#include <unordered_map>

namespace Engine {
    /**
     * @brief Asset class for `MaterialLibrary`.
     * 
     * `MaterialLibrary` acts as a bundle of different materials
     * (i.e. pipelines) that can be dispatched by tags and the type
     * of the mesh to be drawn.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) MaterialLibraryAsset : public Asset {
        REFL_SER_BODY(MaterialLibraryAsset)
    public:
        using MeshTypeBundle = std::unordered_map <uint32_t, std::shared_ptr<AssetRef>>;
        using TagBundle = std::unordered_map <std::string, MeshTypeBundle>;

        REFL_ENABLE MaterialLibraryAsset() = default;
        virtual ~MaterialLibraryAsset() = default;

        REFL_SER_ENABLE TagBundle material_bundle;
        REFL_SER_ENABLE std::string m_name{};
    };
}

#endif // ASSET_MATERIAL_MATERIALLIBRARYASSET_INCLUDED
