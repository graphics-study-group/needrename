#ifndef FRAMEWORK_SCENE_LEVELASSET_INCLUDED
#define FRAMEWORK_SCENE_LEVELASSET_INCLUDED

#include "Framework/framework_export.h"
#include <memory>
#include <vector>

#include <AnnoRefl/macros.h>

#include "SceneAsset.h"
#include <Asset/AssetRef.h>
#include <Framework/World/Handle.h>

namespace Engine {
    class Camera;

    /**
     * @brief LevelAsset contains some extra config for the scene.
     *
     * Note: Can not store any extra smart pointer in SceneAsset which pointed to some data in scene.
     * Because SceneAsset will be deserialized twice (one at loading asset, one at load GOs to scene)
     * It will break the process of deserializing smart pointer
     * Note 2: ObjectHandle and ComponentHandle can not be serialized out of m_archive in SceneAsset.
     */
    class FRAMEWORK_API REFL_SER_CLASS(REFL_WHITELIST) LevelAsset : public SceneAsset {
        REFL_SER_BODY_OVERRIDE(LevelAsset)
    public:
        REFL_ENABLE LevelAsset() = default;
        ~LevelAsset() = default;

        virtual void save_asset_to_archive(AnnoRefl::Archive &archive) const override;
        virtual void load_asset_from_archive(AnnoRefl::Archive &archive) override;

        void LoadToWorld();

        REFL_ENABLE ComponentHandle m_default_camera{};
        REFL_SER_ENABLE AssetRef m_skybox_material{};
    };
} // namespace Engine

#endif // FRAMEWORK_SCENE_LEVELASSET_INCLUDED
