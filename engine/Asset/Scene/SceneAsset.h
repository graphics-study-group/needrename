#ifndef ASSET_SCENE_SCENEASSET_H
#define ASSET_SCENE_SCENEASSET_H

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <memory>

namespace Engine {
    namespace Serialization {
        class Archive;
    }
    class Scene;

    /**
     * @brief SceneAsset is an asset that contains many GameObjects and Components in a scene.
     * It stores an Archive directly.
     * The data should be deserialized into Scene directly
     */
    class REFL_SER_CLASS(REFL_WHITELIST) SceneAsset : public Asset {
        REFL_SER_BODY(SceneAsset)
    public:
        SceneAsset() = default;
        virtual ~SceneAsset();

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        void SaveFromScene(const Scene &scene);
        void AddToScene(Scene &scene);

        std::unique_ptr<Serialization::Archive> m_archive{};
    };
} // namespace Engine

#endif // ASSET_SCENE_SCENEASSET_H
