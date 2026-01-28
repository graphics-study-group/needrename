#ifndef ASSET_SCENE_SCENEASSET_H
#define ASSET_SCENE_SCENEASSET_H

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <memory>

namespace Engine {
    namespace Serialization {
        class Archive;
    }
    class GameObject;
    class Scene;

    class SceneAsset : public Asset {
    public:
        SceneAsset() = default;
        SceneAsset(std::unique_ptr<Scene> &&scene);
        virtual ~SceneAsset() = default;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

    protected:
        std::unique_ptr<Scene> m_scene{};
    };
} // namespace Engine

#endif // ASSET_SCENE_SCENEASSET_H
