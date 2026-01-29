#ifndef ASSET_SCENE_LEVELASSET_H
#define ASSET_SCENE_LEVELASSET_H

#include "SceneAsset.h"
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>
#include <vector>

namespace Engine {
    class Camera;
    class AssetRef;

    /**
     * @brief LevelAsset contains some extra config for the scene.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) LevelAsset : public SceneAsset {
        REFL_SER_BODY(LevelAsset)
    public:
        REFL_ENABLE LevelAsset() = default;
        REFL_ENABLE LevelAsset(std::shared_ptr<Scene> scene);
        ~LevelAsset() = default;

        REFL_SER_ENABLE std::shared_ptr<Camera> m_default_camera{};
        REFL_SER_ENABLE std::shared_ptr<AssetRef> m_skybox_material{};
    };
} // namespace Engine

#endif // ASSET_SCENE_LEVELASSET_H
