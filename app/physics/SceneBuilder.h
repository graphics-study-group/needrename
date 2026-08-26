#ifndef APPPHYSICS_SCENEBUILDER_H
#define APPPHYSICS_SCENEBUILDER_H

#include "PhysicsApp.h"

#include <Asset/AssetRef.h>
#include <Framework/World/Handle.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <random>
#include <string>
#include <vector>

namespace Engine {
    class GameObject;
    class Scene;
    class FileSystemDatabase;
    class AssetRef;
} // namespace Engine

namespace AppPhysics {

    /**
     * @brief Physics assembly layer: creates rigid bodies, collision shapes and
     * joints.
     *
     * Deliberately free of Asset types (AssetRef / FileSystemDatabase): it only
     * touches the component/GameObject world. This is the layer a future
     * headless mode keeps.
     */
    class PhysicsBuilder {
    public:
        /**
         * @brief Construct a physics assembly bound to a scene.
         *
         * @param scene Scene to create physics objects in.
         * @param root  Root GameObject all created bodies are parented to.
         */
        PhysicsBuilder(Engine::Scene &scene, Engine::GameObject &root);

        /**
         * @brief Create a parent GameObject carrying a RigidBodyComponent.
         *
         * The body is positioned and oriented in world space, parented to the
         * root, and configured with mass, kinematics, friction and restitution.
         *
         * @param position         World-space position.
         * @param rotation         World-space orientation.
         * @param mass             Mass of the body.
         * @param kinematic        True for a kinematic (non-dynamic) body.
         * @param static_friction  Static friction coefficient.
         * @param dynamic_friction Dynamic friction coefficient.
         * @param restitution      Restitution coefficient.
         * @return Reference to the created parent GameObject.
         */
        Engine::GameObject &AddRigidBody(
            const glm::vec3 &position,
            const glm::quat &rotation,
            float mass,
            bool kinematic,
            float static_friction,
            float dynamic_friction,
            float restitution
        );

        /**
         * @brief Attach a box collision shape to a body as a collision child.
         *
         * Creates a child GameObject with a box CollisionShapeComponent whose
         * feature is the half-extents.
         *
         * @param parent       Body GameObject to attach the shape to.
         * @param half_extents Box half-extents along X, Y and Z.
         */
        void AddBoxCollision(Engine::GameObject &parent, const glm::vec3 &half_extents);

        /**
         * @brief Attach a sphere collision shape to a body as a collision child.
         *
         * @param parent Body GameObject to attach the shape to.
         * @param radius Sphere radius.
         */
        void AddSphereCollision(Engine::GameObject &parent, float radius);

        /**
         * @brief Attach a cylinder collision shape to a body as a collision child.
         *
         * @param parent      Body GameObject to attach the shape to.
         * @param radius      Cylinder radius.
         * @param half_height Cylinder half-height along Z.
         */
        void AddCylinderCollision(Engine::GameObject &parent, float radius, float half_height);

        /**
         * @brief Add a fixed joint owned by obj1 and referencing obj2.
         *
         * obj2's handle is recorded on a PhysicsConstraintComponent on obj1; the
         * initial relative transform is derived at Awake time.
         *
         * @param obj1   Owning body (hosts the constraint component).
         * @param obj2   Referenced body.
         * @param params Joint parameters.
         */
        void AddFixedJoint(Engine::GameObject &obj1, Engine::GameObject &obj2, const FixedJointParams &params);

        /**
         * @brief Add a hinge joint owned by obj1 and referencing obj2.
         *
         * The hinge axis and anchor are expressed in obj1's local frame. obj2's
         * local values are derived automatically at Awake time.
         *
         * @param obj1   Owning body (hosts the constraint component).
         * @param obj2   Referenced body.
         * @param params Joint parameters (axis, anchor, compliance).
         */
        void AddHingeJoint(Engine::GameObject &obj1, Engine::GameObject &obj2, const HingeJointParams &params);

    private:
        Engine::Scene &m_scene;
        Engine::GameObject &m_root;
    };

    /**
     * @brief Scene builder for the physics app.
     *
     * Composition of the physics assembly (PhysicsBuilder) and the
     * visualization assembly (builtin meshes + color-string materials).
     * Add methods return an opaque BodyId that identifies the created parent
     * GameObject.
     */
    class SceneBuilder {
    public:
        /**
         * @brief Construct a scene builder.
         *
         * When `with_visuals` is false the builtin meshes are not loaded and
         * bodies get no mesh visualization children and no color material
         * resolution (physics assembly unchanged) — the headless form.
         *
         * @param scene       Scene to create objects in.
         * @param adb         Asset database used to resolve builtin meshes/materials.
         * @param root        Root GameObject all created objects are parented to.
         * @param with_visuals Load visualization children/materials (default true).
         */
        SceneBuilder(
            Engine::Scene &scene, Engine::FileSystemDatabase &adb, Engine::GameObject &root, bool with_visuals = true
        );

        /**
         * @brief Add a physics box to the scene.
         *
         * Creates a parent GameObject (rigid body + collision box) and a scaled
         * mesh child using the color material.
         *
         * @param desc Box configuration.
         * @return BodyId identifying the created parent GameObject.
         */
        BodyId AddBox(const BoxDesc &desc);

        /**
         * @brief Add a physics sphere to the scene.
         *
         * Creates a parent GameObject (rigid body + collision sphere) and a
         * scaled mesh child using the color material.
         *
         * @param desc Sphere configuration.
         * @return BodyId identifying the created parent GameObject.
         */
        BodyId AddSphere(const SphereDesc &desc);

        /**
         * @brief Add a physics cylinder to the scene.
         *
         * Creates a parent GameObject (rigid body + collision cylinder) and a
         * scaled mesh child using the color material.
         *
         * @param desc Cylinder configuration.
         * @return BodyId identifying the created parent GameObject.
         */
        BodyId AddCylinder(const CylinderDesc &desc);

        /**
         * @brief Add a fixed joint between two bodies by BodyId.
         *
         * @param obj1   Owning body (hosts the constraint component).
         * @param obj2   Referenced body.
         * @param params Joint parameters.
         * @throws std::out_of_range when a BodyId is invalid.
         */
        void AddFixedJoint(BodyId obj1, BodyId obj2, const FixedJointParams &params);

        /**
         * @brief Add a hinge joint between two bodies by BodyId.
         *
         * @param obj1   Owning body (hosts the constraint component).
         * @param obj2   Referenced body.
         * @param params Joint parameters (axis, anchor, compliance).
         * @throws std::out_of_range when a BodyId is invalid.
         */
        void AddHingeJoint(BodyId obj1, BodyId obj2, const HingeJointParams &params);

        /**
         * @brief Resolve a BodyId to its parent GameObject.
         *
         * @param id BodyId returned by an Add method.
         * @return Reference to the corresponding parent GameObject.
         * @throws std::out_of_range when the id is invalid.
         */
        Engine::GameObject &GetBodyGameObject(BodyId id) const;

        /**
         * @brief Get the number of created bodies.
         *
         * @return Number of registered bodies (BodyIds 0..N-1 are valid).
         */
        uint32_t GetBodyCount() const;

        /**
         * @brief Register an existing body GameObject into the body registry.
         *
         * Appends the given handle to the internal body list and returns its
         * new BodyId. The referenced GameObject must already carry a
         * `RigidBodyComponent`; no scene construction is performed (used to
         * register URDF-imported bodies).
         *
         * @param handle ObjectHandle of the body GameObject.
         * @return The new BodyId.
         */
        BodyId RegisterExistingBody(Engine::ObjectHandle handle);

        /**
         * @brief Get the ObjectHandle of a created body.
         *
         * @param id BodyId returned by an Add method.
         * @return The body's ObjectHandle.
         * @throws std::out_of_range when the id is invalid.
         */
        Engine::ObjectHandle GetBodyHandle(BodyId id) const;

        /**
         * @brief Resolve a color string to a builtin solid color material asset.
         *
         * Empty string selects a random color; an invalid non-empty string
         * throws `std::invalid_argument`.
         *
         * @param color Color name (red/green/blue/yellow/cyan/magenta/orange/white),
         *              or empty for a random color.
         * @return AssetRef to the resolved builtin material.
         * @throws std::invalid_argument when the color is unknown.
         */
        Engine::AssetRef ResolveColor(std::string color) const;

    private:
        /**
         * @brief Register a created body and return its BodyId.
         *
         * @param parent Parent GameObject of the created body.
         * @return BodyId (index into the internal body list).
         */
        BodyId RegisterBody(Engine::GameObject &parent);

        Engine::Scene &m_scene;
        Engine::FileSystemDatabase &m_adb;

        PhysicsBuilder m_physics;

        bool m_with_visuals{true};

        // Visualization assembly state (Asset-dependent).
        Engine::AssetRef m_cube_mesh{};
        Engine::AssetRef m_sphere_mesh{};
        Engine::AssetRef m_cylinder_mesh{};

        mutable std::mt19937 m_rng{std::random_device{}()};

        // BodyId -> parent GameObject handle (BodyId is the vector index).
        std::vector<Engine::ObjectHandle> m_bodies{};
    };
} // namespace AppPhysics

#endif // APPPHYSICS_SCENEBUILDER_H
