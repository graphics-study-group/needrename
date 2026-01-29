#ifndef ASSET_SCENE_SCENEASSET_H
#define ASSET_SCENE_SCENEASSET_H

#include <Asset/Asset.h>
#include <Framework/world/Scene.h>
#include <Reflection/macros.h>
#include <memory>

namespace Engine {
    namespace Serialization {
        class Archive;
    }
    class GameObject;
    class Scene;

    /**
     * @brief SceneAsset is an asset that contains many GameObjects and Components in a scene.
     * TODO: It uses a runtime Scene to store GameObjects and Components. This is not good. 
     * Objects and Components can't duplicated.
     * 
     * Consider using Reflection System to store every Component fields instead of Serialization System.
     * like this: <Fieldname, Value>
     * Consider using internal file Handle to store every ObjectHandle and ComponentHandle.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) SceneAsset : public Asset {
        REFL_SER_BODY(SceneAsset)
    public:
        SceneAsset() = default;
        SceneAsset(std::shared_ptr<Scene> scene);
        virtual ~SceneAsset() = default;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        std::shared_ptr<Scene> m_scene{};
    };
} // namespace Engine

#endif // ASSET_SCENE_SCENEASSET_H
