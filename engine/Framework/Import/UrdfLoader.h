#ifndef FRAMEWORK_IMPORT_URDFLOADER_INCLUDED
#define FRAMEWORK_IMPORT_URDFLOADER_INCLUDED

#include "Framework/framework_export.h"
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
    class FRAMEWORK_API UrdfLoader {
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

        /**
         * @brief Parse a URDF XML file into an intermediate representation.
         *
         * @param urdf_path Path to the .urdf file on disk.
         * @return The parsed robot; empty `links`/`joints` indicate a parse
         * failure (details are logged).
         */
        UrdfRobot ParseUrdf(const std::filesystem::path &urdf_path);

        /**
         * @brief Build a robot's GameObject hierarchy into a caller-provided scene.
         *
         * Constructs one GameObject per link with rigid bodies, collision shapes,
         * joint constraints and (optionally) visual meshes, exactly mirroring the
         * URDF link/joint tree.
         *
         * The build does NOT create a temporary scene, does NOT flush the scene's
         * command queue, and does NOT save any asset — those remain the caller's
         * responsibility. `options.position`/`options.rotation` place the robot
         * (applied to the root link), and the friction/restitution coefficients
         * are written to every created `RigidBodyComponent`.
         *
         * @param robot       Parsed URDF robot to build.
         * @param scene       Target scene the hierarchy is built into.
         * @param root_parent Optional parent GameObject for the root link; pass
         *                    nullptr to leave the root link parentless.
         * @param options     Build options (placement, coefficients, visuals).
         * @return A map of physically realized links (link name → GameObject
         * handle) and joints (joint name → endpoint handles).
         */
        UrdfBuiltRobot BuildRobotScene(
            const UrdfRobot &robot, Scene &scene, GameObject *root_parent, const UrdfBuildOptions &options
        );

    private:
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

#endif // FRAMEWORK_IMPORT_URDFLOADER_INCLUDED
