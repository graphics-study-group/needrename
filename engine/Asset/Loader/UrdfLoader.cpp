#include "UrdfLoader.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Loader/ImportSharedUtil.h>
#include <Asset/Scene/SceneAsset.h>
#include <Core/Math/Transform.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/component/physics/CollisionShapeComponent.h>
#include <Framework/component/physics/PhysicsConstraintComponent.h>
#include <Framework/component/physics/RigidBodyComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>

#include <SDL3/SDL.h>
#include <tinyxml2.h>

#include <functional>
#include <unordered_set>

namespace Engine {

    // ══════════════════════════════════════════════════════════
    //  Coordinate system conversion
    // ══════════════════════════════════════════════════════════

    glm::vec3 UrdfLoader::UrdfToEnginePos(const glm::vec3 &urdf) {
        return {-urdf.y, urdf.x, urdf.z};
    }

    glm::vec3 UrdfLoader::UrdfAxisToEngine(const glm::vec3 &urdf_axis) {
        return {-urdf_axis.y, urdf_axis.x, urdf_axis.z};
    }

    glm::quat UrdfLoader::UrdfRpyToEngineQuat(const glm::vec3 &rpy) {
        // URDF fixed-axis RPY: X→Y→Z order
        const glm::quat qx = glm::angleAxis(rpy.x, glm::vec3(1, 0, 0));
        const glm::quat qy = glm::angleAxis(rpy.y, glm::vec3(0, 1, 0));
        const glm::quat qz = glm::angleAxis(rpy.z, glm::vec3(0, 0, 1));
        const glm::quat q_urdf = qz * qy * qx;
        // Basis change: URDF frame → Engine frame (rotate +90° around Z)
        static const glm::quat q_convert = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
        return q_convert * q_urdf;
    }

    // ══════════════════════════════════════════════════════════
    //  Package URL resolution
    // ══════════════════════════════════════════════════════════

    std::filesystem::path UrdfLoader::ResolvePackageUrl(const std::string &url, const std::filesystem::path &urdf_dir) {
        const std::string prefix = "package://";
        if (url.compare(0, prefix.size(), prefix) != 0) {
            return url;
        }
        const std::string remainder = url.substr(prefix.size());
        const size_t slash = remainder.find('/');
        if (slash == std::string::npos) {
            return remainder;
        }
        // URDF is typically at <pkg>/urdf/<file>.urdf, meshes at <pkg>/meshes/<file>.dae
        std::filesystem::path pkg_dir = urdf_dir.parent_path().parent_path();
        return pkg_dir / remainder.substr(slash + 1);
    }

    // ══════════════════════════════════════════════════════════
    //  URDF XML parsing helpers
    // ══════════════════════════════════════════════════════════

    static UrdfGeometry ParseGeometry(tinyxml2::XMLElement *geom_elem) {
        UrdfGeometry geom;
        if (auto *box = geom_elem->FirstChildElement("box")) {
            geom.type = UrdfGeometryType::Box;
            const char *size_str = box->Attribute("size");
            if (size_str) {
                sscanf_s(size_str, "%f %f %f", &geom.box_size.x, &geom.box_size.y, &geom.box_size.z);
            }
        } else if (auto *sphere = geom_elem->FirstChildElement("sphere")) {
            geom.type = UrdfGeometryType::Sphere;
            sphere->QueryFloatAttribute("radius", &geom.sphere_radius);
        } else if (auto *cylinder = geom_elem->FirstChildElement("cylinder")) {
            geom.type = UrdfGeometryType::Cylinder;
            cylinder->QueryFloatAttribute("radius", &geom.cylinder_radius);
            cylinder->QueryFloatAttribute("length", &geom.cylinder_length);
        } else if (auto *mesh = geom_elem->FirstChildElement("mesh")) {
            geom.type = UrdfGeometryType::Mesh;
            const char *filename = mesh->Attribute("filename");
            if (filename) geom.mesh_filename = filename;
            const char *scale_str = mesh->Attribute("scale");
            if (scale_str) {
                sscanf_s(scale_str, "%f %f %f", &geom.mesh_scale.x, &geom.mesh_scale.y, &geom.mesh_scale.z);
            } else {
                geom.mesh_scale = glm::vec3(1.0f);
            }
        }
        return geom;
    }

    static void ParseOrigin(tinyxml2::XMLElement *elem, glm::vec3 &xyz, glm::vec3 &rpy) {
        if (auto *origin = elem->FirstChildElement("origin")) {
            const char *xyz_str = origin->Attribute("xyz");
            if (xyz_str) {
                sscanf_s(xyz_str, "%f %f %f", &xyz.x, &xyz.y, &xyz.z);
            }
            const char *rpy_str = origin->Attribute("rpy");
            if (rpy_str) {
                sscanf_s(rpy_str, "%f %f %f", &rpy.x, &rpy.y, &rpy.z);
            }
        }
    }

    static std::optional<UrdfInertial> ParseInertial(tinyxml2::XMLElement *link_elem) {
        auto *inertial_elem = link_elem->FirstChildElement("inertial");
        if (!inertial_elem) return std::nullopt;

        UrdfInertial inertial;
        ParseOrigin(inertial_elem, inertial.origin_xyz, inertial.origin_rpy);

        if (auto *mass_elem = inertial_elem->FirstChildElement("mass")) {
            mass_elem->QueryFloatAttribute("value", &inertial.mass);
        }
        if (auto *inertia_elem = inertial_elem->FirstChildElement("inertia")) {
            inertia_elem->QueryFloatAttribute("ixx", &inertial.ixx);
            inertia_elem->QueryFloatAttribute("ixy", &inertial.ixy);
            inertia_elem->QueryFloatAttribute("ixz", &inertial.ixz);
            inertia_elem->QueryFloatAttribute("iyy", &inertial.iyy);
            inertia_elem->QueryFloatAttribute("iyz", &inertial.iyz);
            inertia_elem->QueryFloatAttribute("izz", &inertial.izz);
        }
        return inertial;
    }

    static void ParseVisuals(tinyxml2::XMLElement *link_elem, std::vector<UrdfPosedElement> &visuals) {
        for (auto *vis = link_elem->FirstChildElement("visual"); vis; vis = vis->NextSiblingElement("visual")) {
            UrdfPosedElement v;
            ParseOrigin(vis, v.origin_xyz, v.origin_rpy);
            if (auto *geom = vis->FirstChildElement("geometry")) {
                v.geometry = ParseGeometry(geom);
            }
            visuals.push_back(v);
        }
    }

    static void ParseCollisions(tinyxml2::XMLElement *link_elem, std::vector<UrdfPosedElement> &collisions) {
        for (auto *col = link_elem->FirstChildElement("collision"); col; col = col->NextSiblingElement("collision")) {
            UrdfPosedElement c;
            ParseOrigin(col, c.origin_xyz, c.origin_rpy);
            if (auto *geom = col->FirstChildElement("geometry")) {
                c.geometry = ParseGeometry(geom);
            }
            collisions.push_back(c);
        }
    }

    UrdfRobot UrdfLoader::ParseUrdf(const std::filesystem::path &urdf_path) {
        UrdfRobot robot;
        robot.name = urdf_path.stem().string();

        tinyxml2::XMLDocument doc;
        tinyxml2::XMLError err = doc.LoadFile(urdf_path.string().c_str());
        if (err != tinyxml2::XML_SUCCESS) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "UrdfLoader: Failed to parse %s: %s",
                urdf_path.string().c_str(),
                doc.ErrorStr()
            );
            return robot;
        }

        auto *robot_elem = doc.FirstChildElement("robot");
        if (!robot_elem) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION, "UrdfLoader: No <robot> element in %s", urdf_path.string().c_str()
            );
            return robot;
        }

        const char *robot_name = robot_elem->Attribute("name");
        if (robot_name) robot.name = robot_name;

        // <material> definitions
        for (auto *mat = robot_elem->FirstChildElement("material"); mat; mat = mat->NextSiblingElement("material")) {
            UrdfMaterial m;
            const char *mat_name = mat->Attribute("name");
            if (mat_name) m.name = mat_name;
            if (auto *color = mat->FirstChildElement("color")) {
                const char *rgba = color->Attribute("rgba");
                if (rgba) {
                    sscanf_s(rgba, "%f %f %f %f", &m.color_rgba.r, &m.color_rgba.g, &m.color_rgba.b, &m.color_rgba.a);
                }
            }
            robot.materials.push_back(m);
        }

        // <link> elements
        for (auto *link = robot_elem->FirstChildElement("link"); link; link = link->NextSiblingElement("link")) {
            UrdfLink l;
            const char *link_name = link->Attribute("name");
            if (link_name) l.name = link_name;

            l.inertial = ParseInertial(link);
            ParseVisuals(link, l.visuals);
            ParseCollisions(link, l.collisions);

            robot.links.push_back(l);
        }

        // <joint> elements
        for (auto *joint = robot_elem->FirstChildElement("joint"); joint; joint = joint->NextSiblingElement("joint")) {
            UrdfJoint j;
            const char *jname = joint->Attribute("name");
            if (jname) j.name = jname;

            const char *jtype = joint->Attribute("type");
            if (jtype) {
                std::string type_str(jtype);
                if (type_str == "fixed") j.type = UrdfJointType::Fixed;
                else if (type_str == "revolute") j.type = UrdfJointType::Revolute;
                else if (type_str == "continuous") j.type = UrdfJointType::Continuous;
                else if (type_str == "prismatic") j.type = UrdfJointType::Prismatic;
                else if (type_str == "floating") j.type = UrdfJointType::Floating;
                else j.type = UrdfJointType::Unknown;
            }

            if (auto *parent = joint->FirstChildElement("parent")) {
                const char *plink = parent->Attribute("link");
                if (plink) j.parent_link = plink;
            }
            if (auto *child = joint->FirstChildElement("child")) {
                const char *clink = child->Attribute("link");
                if (clink) j.child_link = clink;
            }

            ParseOrigin(joint, j.origin_xyz, j.origin_rpy);

            if (auto *axis = joint->FirstChildElement("axis")) {
                const char *axyz = axis->Attribute("xyz");
                if (axyz) sscanf_s(axyz, "%f %f %f", &j.axis.x, &j.axis.y, &j.axis.z);
            }

            if (auto *dynamics = joint->FirstChildElement("dynamics")) {
                dynamics->QueryFloatAttribute("damping", &j.damping);
                dynamics->QueryFloatAttribute("friction", &j.friction);
            }

            if (auto *limit = joint->FirstChildElement("limit")) {
                limit->QueryFloatAttribute("lower", &j.limit_lower);
                limit->QueryFloatAttribute("upper", &j.limit_upper);
                limit->QueryFloatAttribute("effort", &j.limit_effort);
                limit->QueryFloatAttribute("velocity", &j.limit_velocity);
            }

            robot.joints.push_back(j);
        }

        // Determine root link
        std::unordered_set<std::string> child_links;
        for (const auto &j : robot.joints) {
            child_links.insert(j.child_link);
        }
        for (const auto &l : robot.links) {
            if (child_links.find(l.name) == child_links.end()) {
                robot.root_link_name = l.name;
                break;
            }
        }

        return robot;
    }

    // ══════════════════════════════════════════════════════════
    //  Collision object handle collection
    // ══════════════════════════════════════════════════════════

    std::vector<ObjectHandle> UrdfLoader::CollectCollisionObjectHandles(GameObject &root, Scene &scene) {
        std::vector<ObjectHandle> result;
        std::function<void(GameObject &)> collect = [&](GameObject &go) {
            for (ComponentHandle ch : go.m_components) {
                if (dynamic_cast<CollisionShapeComponent *>(scene.GetComponent(ch))) {
                    result.push_back(go.GetHandle());
                    break;
                }
            }
            for (ObjectHandle child_h : go.GetChildren()) {
                if (auto *child = scene.GetGameObject(child_h)) {
                    collect(*child);
                }
            }
        };
        collect(root);
        return result;
    }

    // ══════════════════════════════════════════════════════════
    //  Geometry → CollisionShape feature helper
    // ══════════════════════════════════════════════════════════

    static void ApplyGeometryToShape(CollisionShapeComponent &shape, const UrdfGeometry &geom) {
        switch (geom.type) {
        case UrdfGeometryType::Box:
            shape.m_shape_type = CollisionShapeType::Box;
            shape.m_feature = glm::vec3(geom.box_size.x * 0.5f, geom.box_size.y * 0.5f, geom.box_size.z * 0.5f);
            break;
        case UrdfGeometryType::Sphere:
            shape.m_shape_type = CollisionShapeType::Sphere;
            shape.m_feature = glm::vec3(geom.sphere_radius, 0.0f, 0.0f);
            break;
        case UrdfGeometryType::Cylinder:
            shape.m_shape_type = CollisionShapeType::Cylinder;
            shape.m_feature = glm::vec3(geom.cylinder_radius, geom.cylinder_length * 0.5f, 0.0f);
            break;
        case UrdfGeometryType::Mesh:
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "UrdfLoader: Skipping mesh collision (Phase 2)");
            break;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  SceneAsset builder
    // ══════════════════════════════════════════════════════════

    void UrdfLoader::BuildAndSaveSceneAsset(
        const UrdfRobot &robot, const std::filesystem::path &path_in_project, AssetManager &am, FileSystemDatabase &db
    ) {
        auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();

        static const std::vector<const char *> kBuiltinMaterials = {
            "~/materials/solid_color_red.asset",
            "~/materials/solid_color_blue.asset",
            "~/materials/solid_color_green.asset",
            "~/materials/solid_color_orange.asset",
            "~/materials/solid_color_white.asset",
            "~/materials/solid_color_dark_grey.asset",
            "~/materials/solid_color_cyan.asset",
            "~/materials/solid_color_magenta.asset",
            "~/materials/solid_color_yellow.asset",
        };

        // ── 1. Create one GO per link ──
        std::unordered_map<std::string, GameObject *> name_to_go;
        for (const auto &link : robot.links) {
            auto &go = temp_scene.CreateGameObject();
            go.m_name = link.name;
            name_to_go[link.name] = &go;
        }

        // ── 2. Set parent-child from joints ──
        for (const auto &joint : robot.joints) {
            auto *parent_go = name_to_go[joint.parent_link];
            auto *child_go = name_to_go[joint.child_link];
            if (!parent_go || !child_go) continue;

            Transform local;
            local.SetPosition(UrdfToEnginePos(joint.origin_xyz));
            local.SetRotation(UrdfRpyToEngineQuat(joint.origin_rpy));
            child_go->SetTransform(local);
            child_go->SetParent(parent_go->GetHandle());
        }

        // ── 3. RigidBodyComponent for links with <inertial> ──
        for (const auto &link : robot.links) {
            if (!link.inertial.has_value()) continue;
            auto *go = name_to_go[link.name];
            if (!go) continue;

            auto &rb = go->AddComponent<RigidBodyComponent>();
            const auto &inertial = *link.inertial;
            rb.m_mass = inertial.mass;
            rb.m_use_manual_inertia = true;
            rb.m_manual_inertia_diag = glm::vec3(inertial.ixx, inertial.iyy, inertial.izz);
            rb.m_manual_inertia_offdiag = glm::vec3(inertial.ixy, inertial.ixz, inertial.iyz);
        }

        // ── 4. CollisionShapeComponent ──
        for (const auto &link : robot.links) {
            auto *go = name_to_go[link.name];
            if (!go) continue;

            for (const auto &col : link.collisions) {
                bool has_offset = glm::length(col.origin_xyz) > 1e-6f || glm::length(col.origin_rpy) > 1e-6f;
                if (has_offset) {
                    auto &sub_go = temp_scene.CreateGameObject();
                    sub_go.m_name = link.name + "_collision";
                    Transform local;
                    local.SetPosition(UrdfToEnginePos(col.origin_xyz));
                    local.SetRotation(UrdfRpyToEngineQuat(col.origin_rpy));
                    sub_go.SetTransform(local);
                    sub_go.SetParent(go->GetHandle());

                    auto &shape = sub_go.AddComponent<CollisionShapeComponent>();
                    shape.m_center = glm::vec3(0.0f);
                    shape.m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    ApplyGeometryToShape(shape, col.geometry);
                } else {
                    auto &shape = go->AddComponent<CollisionShapeComponent>();
                    shape.m_center = UrdfToEnginePos(col.origin_xyz);
                    shape.m_rotation = UrdfRpyToEngineQuat(col.origin_rpy);
                    ApplyGeometryToShape(shape, col.geometry);
                }
            }
        }

        // ── 5. PhysicsConstraintComponent (one per parent link) ──
        // Collect all joints per parent link into a single component.
        std::unordered_map<GameObject *, PhysicsConstraintComponent *> parent_constraints;
        for (const auto &joint : robot.joints) {
            auto *parent_go = name_to_go[joint.parent_link];
            auto *child_go = name_to_go[joint.child_link];
            if (!parent_go || !child_go) continue;

            if (joint.type == UrdfJointType::Fixed) {
                auto *&constraint = parent_constraints[parent_go];
                if (!constraint) {
                    constraint = &parent_go->AddComponent<PhysicsConstraintComponent>();
                }
                FixedJointDef fixed;
                fixed.m_obj2_handle = child_go->GetHandle();
                fixed.m_compliance = 0.0f;
                constraint->m_joints.push_back(fixed);
            } else if (joint.type == UrdfJointType::Revolute || joint.type == UrdfJointType::Continuous) {
                auto *&constraint = parent_constraints[parent_go];
                if (!constraint) {
                    constraint = &parent_go->AddComponent<PhysicsConstraintComponent>();
                }
                HingeJointDef hinge;
                hinge.m_obj2_handle = child_go->GetHandle();
                hinge.m_compliance = 0.0f;

                const glm::vec3 eng_axis = UrdfAxisToEngine(joint.axis);
                hinge.m_obj1_local_aligned_axis = eng_axis;
                hinge.m_obj2_local_aligned_axis = eng_axis;

                hinge.m_obj1_local_attach_point = UrdfToEnginePos(joint.origin_xyz);
                hinge.m_obj2_local_attach_point = glm::vec3(0.0f);

                constraint->m_joints.push_back(hinge);
            } else if (joint.type == UrdfJointType::Prismatic || joint.type == UrdfJointType::Floating) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "UrdfLoader: Skipping unsupported joint type for '%s'",
                    joint.name.c_str()
                );
            }
        }

        // ── 6. Parent-child collision filtering ──
        for (const auto &joint : robot.joints) {
            auto *parent_go = name_to_go[joint.parent_link];
            auto *child_go = name_to_go[joint.child_link];
            if (!parent_go || !child_go) continue;

            auto parent_collision_gos = CollectCollisionObjectHandles(*parent_go, temp_scene);
            auto child_collision_gos = CollectCollisionObjectHandles(*child_go, temp_scene);

            std::function<void(GameObject &, const std::vector<ObjectHandle> &)> add_ignores =
                [&](GameObject &go, const std::vector<ObjectHandle> &handles) {
                    for (ComponentHandle ch : go.m_components) {
                        if (auto *shape = dynamic_cast<CollisionShapeComponent *>(temp_scene.GetComponent(ch))) {
                            for (ObjectHandle h : handles) {
                                shape->m_ignore_collision_objects.push_back(h);
                            }
                        }
                    }
                    for (ObjectHandle child_h : go.GetChildren()) {
                        if (auto *cgo = temp_scene.GetGameObject(child_h)) {
                            add_ignores(*cgo, handles);
                        }
                    }
                };

            add_ignores(*parent_go, child_collision_gos);
            add_ignores(*child_go, parent_collision_gos);
        }

        // ── 7. StaticMeshComponent (render via collision geometry) ──
        // Since DAE/STL mesh import is not yet implemented, we render using
        // <collision> geometry so that robot shapes are always visible.
        // Builtin mesh base sizes:
        //   cube:     2×2×2 centered  → scale = half_extents
        //   sphere:   radius 1m       → scale = (r, r, r)
        //   cylinder: radius 1m, h=2m, Z-up → scale = (r, r, half_h)
        for (const auto &link : robot.links) {
            auto *go = name_to_go[link.name];
            if (!go || link.collisions.empty()) continue;

            size_t hash = std::hash<std::string>{}(link.name);
            const char *mat_path = kBuiltinMaterials[hash % kBuiltinMaterials.size()];
            auto mat_ref = db.GetNewAssetRef(AssetPath(db, mat_path));

            for (const auto &col : link.collisions) {
                AssetPath mesh_path(db, "");
                glm::vec3 mesh_scale(1.0f);
                switch (col.geometry.type) {
                case UrdfGeometryType::Box:
                    mesh_path = AssetPath(db, "~/mesh/cube.asset");
                    mesh_scale = glm::vec3(
                        col.geometry.box_size.x * 0.5f, col.geometry.box_size.y * 0.5f, col.geometry.box_size.z * 0.5f
                    );
                    break;
                case UrdfGeometryType::Sphere:
                    mesh_path = AssetPath(db, "~/mesh/sphere.asset");
                    mesh_scale = glm::vec3(col.geometry.sphere_radius);
                    break;
                case UrdfGeometryType::Cylinder:
                    mesh_path = AssetPath(db, "~/mesh/cylinder.asset");
                    mesh_scale = glm::vec3(
                        col.geometry.cylinder_radius, col.geometry.cylinder_radius, col.geometry.cylinder_length * 0.5f
                    );
                    break;
                case UrdfGeometryType::Mesh:
                    SDL_LogInfo(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "UrdfLoader: Skipping mesh collision '%s' for link '%s' (Phase 2)",
                        col.geometry.mesh_filename.c_str(),
                        link.name.c_str()
                    );
                    continue;
                }

                auto mesh_ref = db.GetNewAssetRef(mesh_path);

                // Always place render geometry on a child GO so scale applies independently.
                auto &render_go = temp_scene.CreateGameObject();
                render_go.m_name = link.name + "_render";

                Transform local;
                local.SetPosition(UrdfToEnginePos(col.origin_xyz));
                local.SetRotation(UrdfRpyToEngineQuat(col.origin_rpy));
                local.SetScale(mesh_scale);
                render_go.SetTransform(local);
                render_go.SetParent(go->GetHandle());

                auto &mc = render_go.AddComponent<StaticMeshComponent>();
                mc.m_mesh_asset = mesh_ref;
                mc.m_material_assets = {mat_ref};
                mc.m_is_eagerly_loaded = true;
            }
        }

        // ── 8. Flush and save ──
        temp_scene.FlushCmdQueue();

        auto *scene_asset = am.CreateAsset<SceneAsset>();
        scene_asset->SaveFromScene(temp_scene);
        detail::import_shared::SaveAsset(db, *scene_asset, path_in_project, "GO_" + robot.name);
    }

    // ══════════════════════════════════════════════════════════
    //  Public API
    // ══════════════════════════════════════════════════════════

    UrdfLoader::UrdfLoader() {
        m_asset_manager = MainClass::GetInstance()->GetAssetManager();
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    }

    UrdfLoader::~UrdfLoader() = default;

    void UrdfLoader::LoadUrdfResource(
        const std::filesystem::path &urdf_path, const std::filesystem::path &path_in_project
    ) {
        auto am = m_asset_manager.lock();
        auto db = m_database.lock();
        if (!am || !db) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "UrdfLoader: AssetManager or Database unavailable");
            return;
        }

        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "UrdfLoader: Loading %s → %s",
            urdf_path.string().c_str(),
            path_in_project.string().c_str()
        );

        UrdfRobot robot = ParseUrdf(urdf_path);

        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "UrdfLoader: Parsed robot '%s' with %u links, %u joints",
            robot.name.c_str(),
            static_cast<unsigned>(robot.links.size()),
            static_cast<unsigned>(robot.joints.size())
        );

        BuildAndSaveSceneAsset(robot, path_in_project, *am, *db);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "UrdfLoader: Saved SceneAsset as GO_%s.asset", robot.name.c_str());
    }

} // namespace Engine
