#include "SceneBuilder.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetRef.h>
#include <Core/Math/Transform.h>
#include <Framework/Component/RenderComponent/StaticMeshComponent.h>
#include <Framework/Component/physics/CollisionShapeComponent.h>
#include <Framework/Component/physics/PhysicsConstraintComponent.h>
#include <Framework/Component/physics/RigidBodyComponent.h>
#include <Framework/Object/GameObject.h>
#include <Framework/World/Handle.h>
#include <Framework/World/Scene.h>

#include <array>
#include <stdexcept>

namespace AppPhysics {

    using namespace Engine;

    namespace {
        constexpr std::array<const char *, 8> kKnownColors{
            "red", "green", "blue", "yellow", "cyan", "magenta", "orange", "white"
        };
    } // namespace

    // ── Physics assembly ────────────────────────────────────────────────

    PhysicsBuilder::PhysicsBuilder(Engine::Scene &scene, Engine::GameObject &root) : m_scene(scene), m_root(root) {
    }

    Engine::GameObject &PhysicsBuilder::AddRigidBody(
        const glm::vec3 &position,
        const glm::quat &rotation,
        float mass,
        bool kinematic,
        float static_friction,
        float dynamic_friction,
        float restitution
    ) {
        Engine::GameObject &parent = m_scene.CreateGameObject();
        parent.SetParent(m_root.GetHandle());
        {
            Transform t;
            t.SetPosition(position);
            t.SetRotation(rotation);
            parent.SetTransform(t);
        }

        auto &rb = parent.AddComponent<Engine::RigidBodyComponent>();
        rb.m_mass = mass;
        rb.m_is_kinematic = kinematic;
        rb.m_static_friction = static_friction;
        rb.m_dynamic_friction = dynamic_friction;
        rb.m_restitution = restitution;

        return parent;
    }

    void PhysicsBuilder::AddBoxCollision(Engine::GameObject &parent, const glm::vec3 &half_extents) {
        Engine::GameObject &collision_child = m_scene.CreateGameObject();
        collision_child.SetParent(parent.GetHandle());
        Transform t;
        collision_child.SetTransform(t);

        auto &shape = collision_child.AddComponent<Engine::CollisionShapeComponent>();
        shape.m_shape_type = Engine::CollisionShapeType::Box;
        shape.m_feature = half_extents;
    }

    void PhysicsBuilder::AddSphereCollision(Engine::GameObject &parent, float radius) {
        Engine::GameObject &collision_child = m_scene.CreateGameObject();
        collision_child.SetParent(parent.GetHandle());
        Transform t;
        collision_child.SetTransform(t);

        auto &shape = collision_child.AddComponent<Engine::CollisionShapeComponent>();
        shape.m_shape_type = Engine::CollisionShapeType::Sphere;
        shape.m_feature = glm::vec3(radius, 0.0f, 0.0f);
    }

    void PhysicsBuilder::AddCylinderCollision(Engine::GameObject &parent, float radius, float half_height) {
        Engine::GameObject &collision_child = m_scene.CreateGameObject();
        collision_child.SetParent(parent.GetHandle());
        Transform t;
        collision_child.SetTransform(t);

        auto &shape = collision_child.AddComponent<Engine::CollisionShapeComponent>();
        shape.m_shape_type = Engine::CollisionShapeType::Cylinder;
        shape.m_feature = glm::vec3(radius, half_height, 0.0f);
    }

    void PhysicsBuilder::AddFixedJoint(
        Engine::GameObject &obj1, Engine::GameObject &obj2, const FixedJointParams &params
    ) {
        auto &constraint = obj1.AddComponent<Engine::PhysicsConstraintComponent>();
        Engine::FixedJointDef def{};
        def.m_obj2_handle = obj2.GetHandle();
        def.m_compliance = params.compliance;
        constraint.m_joints.push_back(def);
    }

    void PhysicsBuilder::AddHingeJoint(
        Engine::GameObject &obj1, Engine::GameObject &obj2, const HingeJointParams &params
    ) {
        auto &constraint = obj1.AddComponent<Engine::PhysicsConstraintComponent>();
        Engine::HingeJointDef def{};
        def.m_obj2_handle = obj2.GetHandle();
        def.m_hinge_axis_obj1 = glm::normalize(params.axis_obj1);
        def.m_hinge_anchor_obj1 = params.anchor_obj1;
        def.m_compliance = params.compliance;
        constraint.m_joints.push_back(def);
    }

    // ── Scene builder ───────────────────────────────────────────────────

    SceneBuilder::SceneBuilder(
        Engine::Scene &scene, Engine::FileSystemDatabase &adb, Engine::GameObject &root, bool with_visuals
    ) : m_scene(scene), m_adb(adb), m_physics(scene, root), m_with_visuals(with_visuals) {
        // Load the builtin meshes once — all instances share them. Skipped when
        // visuals are disabled (headless form).
        if (m_with_visuals) {
            m_cube_mesh = adb.GetNewAssetRef(Engine::AssetPath{"builtin://mesh/cube.asset"});
            m_sphere_mesh = adb.GetNewAssetRef(Engine::AssetPath{"builtin://mesh/sphere.asset"});
            m_cylinder_mesh = adb.GetNewAssetRef(Engine::AssetPath{"builtin://mesh/cylinder.asset"});
        }
    }

    BodyId SceneBuilder::RegisterBody(Engine::GameObject &parent) {
        BodyId id = static_cast<BodyId>(m_bodies.size());
        m_bodies.push_back(parent.GetHandle());
        return id;
    }

    BodyId SceneBuilder::AddBox(const BoxDesc &desc) {
        Engine::GameObject &parent = m_physics.AddRigidBody(
            desc.position,
            desc.rotation,
            desc.mass,
            desc.kinematic,
            desc.static_friction,
            desc.dynamic_friction,
            desc.restitution
        );

        // Mesh child: visual cube (scale = half_extents maps the 2x2x2 cube).
        if (m_with_visuals) {
            Engine::GameObject &mesh_child = m_scene.CreateGameObject();
            mesh_child.SetParent(parent.GetHandle());
            Transform t;
            mesh_child.SetTransform(t);
            mesh_child.GetTransformRef().SetScale(desc.half_extents);

            auto &mc = mesh_child.AddComponent<Engine::StaticMeshComponent>();
            mc.m_mesh_asset = m_cube_mesh;
            mc.m_material_assets.push_back(ResolveColor(desc.color));
            mc.m_is_eagerly_loaded = true;
        }

        m_physics.AddBoxCollision(parent, desc.half_extents);

        return RegisterBody(parent);
    }

    BodyId SceneBuilder::AddSphere(const SphereDesc &desc) {
        Engine::GameObject &parent = m_physics.AddRigidBody(
            desc.position,
            desc.rotation,
            desc.mass,
            desc.kinematic,
            desc.static_friction,
            desc.dynamic_friction,
            desc.restitution
        );

        // Mesh child: visual sphere (scale = radius maps the 1m sphere).
        if (m_with_visuals) {
            Engine::GameObject &mesh_child = m_scene.CreateGameObject();
            mesh_child.SetParent(parent.GetHandle());
            Transform t;
            mesh_child.SetTransform(t);
            mesh_child.GetTransformRef().SetScale(glm::vec3(desc.radius));

            auto &mc = mesh_child.AddComponent<Engine::StaticMeshComponent>();
            mc.m_mesh_asset = m_sphere_mesh;
            mc.m_material_assets.push_back(ResolveColor(desc.color));
            mc.m_is_eagerly_loaded = true;
        }

        m_physics.AddSphereCollision(parent, desc.radius);

        return RegisterBody(parent);
    }

    BodyId SceneBuilder::AddCylinder(const CylinderDesc &desc) {
        Engine::GameObject &parent = m_physics.AddRigidBody(
            desc.position,
            desc.rotation,
            desc.mass,
            desc.kinematic,
            desc.static_friction,
            desc.dynamic_friction,
            desc.restitution
        );

        // Mesh child: visual cylinder (scale = (r, r, half_height) maps the
        // 1m-radius, 2m-height Z-up cylinder).
        if (m_with_visuals) {
            Engine::GameObject &mesh_child = m_scene.CreateGameObject();
            mesh_child.SetParent(parent.GetHandle());
            Transform t;
            mesh_child.SetTransform(t);
            mesh_child.GetTransformRef().SetScale(glm::vec3(desc.radius, desc.radius, desc.half_height));

            auto &mc = mesh_child.AddComponent<Engine::StaticMeshComponent>();
            mc.m_mesh_asset = m_cylinder_mesh;
            mc.m_material_assets.push_back(ResolveColor(desc.color));
            mc.m_is_eagerly_loaded = true;
        }

        m_physics.AddCylinderCollision(parent, desc.radius, desc.half_height);

        return RegisterBody(parent);
    }

    void SceneBuilder::AddFixedJoint(BodyId obj1, BodyId obj2, const FixedJointParams &params) {
        m_physics.AddFixedJoint(GetBodyGameObject(obj1), GetBodyGameObject(obj2), params);
    }

    void SceneBuilder::AddHingeJoint(BodyId obj1, BodyId obj2, const HingeJointParams &params) {
        m_physics.AddHingeJoint(GetBodyGameObject(obj1), GetBodyGameObject(obj2), params);
    }

    Engine::GameObject &SceneBuilder::GetBodyGameObject(BodyId id) const {
        if (id == INVALID_BODY_ID || id >= m_bodies.size()) {
            throw std::out_of_range("PhysicsApp: invalid BodyId");
        }
        return m_scene.GetGameObjectRef(m_bodies[id]);
    }

    uint32_t SceneBuilder::GetBodyCount() const {
        return static_cast<uint32_t>(m_bodies.size());
    }

    Engine::ObjectHandle SceneBuilder::GetBodyHandle(BodyId id) const {
        if (id == INVALID_BODY_ID || id >= m_bodies.size()) {
            throw std::out_of_range("PhysicsApp: invalid BodyId");
        }
        return m_bodies[id];
    }

    Engine::AssetRef SceneBuilder::ResolveColor(std::string color) const {
        if (color.empty()) {
            color = kKnownColors[m_rng() % kKnownColors.size()];
        }
        for (const char *name : kKnownColors) {
            if (color == name) {
                return m_adb.GetNewAssetRef(Engine::AssetPath{"builtin://materials/solid_color_" + color + ".asset"});
            }
        }
        throw std::invalid_argument("PhysicsApp: unknown color '" + color + "'");
    }
} // namespace AppPhysics
