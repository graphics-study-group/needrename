#ifndef ENGINE_ASSET_LOADER_URDFLOADER_H
#define ENGINE_ASSET_LOADER_URDFLOADER_H

#include <Framework/Import/UrdfTypes.h>
#include <Framework/World/Handle.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
    class AssetRef;
    class AssetManager;
    class FileSystemDatabase;
    class GameObject;
    class Scene;

    /**
     * @brief Imports URDF robot description files and produces SceneAsset prefabs.
     *
     * Parses URDF XML, builds a full GameObject hierarchy with rigid bodies,
     * collision shapes, joint constraints, and visual meshes, then serializes
     * the result as a SceneAsset.
     */
    class UrdfLoader {
    public:
        UrdfLoader();
        ~UrdfLoader();

        /**
         * @brief Load a URDF file and produce assets in the project.
         *
         * @param urdf_path Path to the .urdf file on disk.
         * @param path_in_project Project-relative directory for output assets.
         */
        void LoadUrdfResource(const std::filesystem::path &urdf_path, const std::filesystem::path &path_in_project);

    private:
        /// Parse a URDF XML file into an intermediate representation.
        UrdfRobot ParseUrdf(const std::filesystem::path &urdf_path);

        /// Resolve a package:// URI to an absolute filesystem path.
        std::filesystem::path ResolvePackageUrl(const std::string &url, const std::filesystem::path &urdf_dir);

        /// Build the full GameObject hierarchy from parsed URDF data and save as SceneAsset.
        void BuildAndSaveSceneAsset(
            const UrdfRobot &robot,
            const std::filesystem::path &path_in_project,
            AssetManager &am,
            FileSystemDatabase &db
        );

        /// Collect all ComponentHandles of CollisionShapeComponents in the given
        /// GameObject's subtree.
        static std::vector<ComponentHandle> CollectCollisionComponentHandles(GameObject &root, Scene &scene);

        AssetManager *m_asset_manager{nullptr};
        FileSystemDatabase *m_database{nullptr};
    };

} // namespace Engine

#endif // ENGINE_ASSET_LOADER_URDFLOADER_H
