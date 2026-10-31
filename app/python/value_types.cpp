/**
 * @file value_types.cpp
 *
 * @brief Python bindings for the PhysicsApp value types: glm math types, enums,
 * descriptors, state structs, and URDF import results.
 */

#include "binding_support.h"

#include <PhysicsApp.h>

#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

using namespace nb::literals;

namespace NeedrenameBindings {
    namespace {
        using namespace AppPhysics;

        /// @brief Readable repr for a glm vector: `Vec3(x, y, z)`.
        template <typename Vec>
        std::string VecRepr(const char *name, const Vec &v) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "%s(%g, %g, %g)", name, v.x, v.y, v.z);
            return buffer;
        }
    } // namespace

    void RegisterGlmTypes(nb::module_ &m) {
        nb::class_<glm::vec3>(m, "Vec3", "A 3-component float vector.")
            .def(
                "__init__",
                [](glm::vec3 *self, float x, float y, float z) { new (self) glm::vec3(x, y, z); },
                "x"_a = 0.0f,
                "y"_a = 0.0f,
                "z"_a = 0.0f
            )
            .def(
                "__init__",
                [](glm::vec3 *self, nb::sequence values) {
                    const auto size = nb::len(values);
                    if (size > 3) {
                        throw std::invalid_argument("Vec3 accepts at most 3 values");
                    }
                    glm::vec3 v(0.0f);
                    for (size_t i = 0; i < size; ++i) {
                        v[i] = nb::cast<float>(values[i]);
                    }
                    new (self) glm::vec3(v);
                }
            )
            .def_rw("x", &glm::vec3::x)
            .def_rw("y", &glm::vec3::y)
            .def_rw("z", &glm::vec3::z)
            .def("__repr__", [](const glm::vec3 &v) { return VecRepr("Vec3", v); })
            .def("__iter__", [](const glm::vec3 &v) { return nb::iter(nb::make_tuple(v.x, v.y, v.z)); });

        nb::class_<glm::vec4>(m, "Vec4", "A 4-component float vector.")
            .def(
                "__init__",
                [](glm::vec4 *self, float x, float y, float z, float w) { new (self) glm::vec4(x, y, z, w); },
                "x"_a = 0.0f,
                "y"_a = 0.0f,
                "z"_a = 0.0f,
                "w"_a = 0.0f
            )
            .def(
                "__init__",
                [](glm::vec4 *self, nb::sequence values) {
                    const auto size = nb::len(values);
                    if (size > 4) {
                        throw std::invalid_argument("Vec4 accepts at most 4 values");
                    }
                    glm::vec4 v(0.0f);
                    for (size_t i = 0; i < size; ++i) {
                        v[i] = nb::cast<float>(values[i]);
                    }
                    new (self) glm::vec4(v);
                }
            )
            .def_rw("x", &glm::vec4::x)
            .def_rw("y", &glm::vec4::y)
            .def_rw("z", &glm::vec4::z)
            .def_rw("w", &glm::vec4::w)
            .def(
                "__repr__",
                [](const glm::vec4 &v) {
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), "Vec4(%g, %g, %g, %g)", v.x, v.y, v.z, v.w);
                    return std::string(buffer);
                }
            )
            .def("__iter__", [](const glm::vec4 &v) { return nb::iter(nb::make_tuple(v.x, v.y, v.z, v.w)); });

        // The native glm constructor order is (w, x, y, z); the Python form
        // follows the documented storage order (xyzw) so it matches the
        // readback arrays and the `SetBodyValue(Rotation)` layout.
        nb::class_<glm::quat>(m, "Quat", "A rotation quaternion, constructed as (x, y, z, w).")
            .def(
                "__init__",
                [](glm::quat *self, float x, float y, float z, float w) { new (self) glm::quat(w, x, y, z); },
                "x"_a = 0.0f,
                "y"_a = 0.0f,
                "z"_a = 0.0f,
                "w"_a = 1.0f
            )
            .def_prop_rw(
                "x", [](const glm::quat &q) { return q.x; }, [](glm::quat &q, float v) { q.x = v; }
            )
            .def_prop_rw(
                "y", [](const glm::quat &q) { return q.y; }, [](glm::quat &q, float v) { q.y = v; }
            )
            .def_prop_rw(
                "z", [](const glm::quat &q) { return q.z; }, [](glm::quat &q, float v) { q.z = v; }
            )
            .def_prop_rw(
                "w", [](const glm::quat &q) { return q.w; }, [](glm::quat &q, float v) { q.w = v; }
            )
            .def("__repr__", [](const glm::quat &q) {
                char buffer[160];
                snprintf(buffer, sizeof(buffer), "Quat(x=%g, y=%g, z=%g, w=%g)", q.x, q.y, q.z, q.w);
                return std::string(buffer);
            });
    }

    void RegisterValueTypes(nb::module_ &m) {
        nb::enum_<AppMode>(m, "AppMode", "Execution form of a physics app, fixed at creation time.")
            .value("physics_only", AppMode::PhysicsOnly, "Physics only: no rendering, no window, no camera.")
            .value("offscreen", AppMode::Offscreen, "Physics plus offscreen rendering, no window.")
            .value("windowed", AppMode::Windowed, "Physics plus rendering into a window.");

        nb::enum_<BodyField>(m, "BodyField", "Per-body writable field selector for `set_body_value`.")
            .value("position", BodyField::Position)
            .value("rotation", BodyField::Rotation)
            .value("linear_velocity", BodyField::LinearVelocity)
            .value("angular_velocity", BodyField::AngularVelocity)
            .value("external_force", BodyField::ExternalForce)
            .value("external_torque", BodyField::ExternalTorque);

        m.attr("INVALID_BODY_ID") = INVALID_BODY_ID;
        m.attr("INVALID_JOINT_ID") = INVALID_JOINT_ID;

        AggregateBinding<CreateInfo>(m, "CreateInfo")
            .field("mode", &CreateInfo::mode)
            .field("resol_x", &CreateInfo::resol_x)
            .field("resol_y", &CreateInfo::resol_y)
            .field("window_title", &CreateInfo::window_title)
            .field("background_color", &CreateInfo::background_color);

        AggregateBinding<BoxDesc>(m, "BoxDesc")
            .field("position", &BoxDesc::position)
            .field("rotation", &BoxDesc::rotation)
            .field("half_extents", &BoxDesc::half_extents)
            .field("mass", &BoxDesc::mass)
            .field("kinematic", &BoxDesc::kinematic)
            .field("static_friction", &BoxDesc::static_friction)
            .field("dynamic_friction", &BoxDesc::dynamic_friction)
            .field("restitution", &BoxDesc::restitution)
            .field("color", &BoxDesc::color);

        AggregateBinding<SphereDesc>(m, "SphereDesc")
            .field("position", &SphereDesc::position)
            .field("rotation", &SphereDesc::rotation)
            .field("radius", &SphereDesc::radius)
            .field("mass", &SphereDesc::mass)
            .field("kinematic", &SphereDesc::kinematic)
            .field("static_friction", &SphereDesc::static_friction)
            .field("dynamic_friction", &SphereDesc::dynamic_friction)
            .field("restitution", &SphereDesc::restitution)
            .field("color", &SphereDesc::color);

        AggregateBinding<CylinderDesc>(m, "CylinderDesc")
            .field("position", &CylinderDesc::position)
            .field("rotation", &CylinderDesc::rotation)
            .field("radius", &CylinderDesc::radius)
            .field("half_height", &CylinderDesc::half_height)
            .field("mass", &CylinderDesc::mass)
            .field("kinematic", &CylinderDesc::kinematic)
            .field("static_friction", &CylinderDesc::static_friction)
            .field("dynamic_friction", &CylinderDesc::dynamic_friction)
            .field("restitution", &CylinderDesc::restitution)
            .field("color", &CylinderDesc::color);

        AggregateBinding<FixedJointParams>(m, "FixedJointParams").field("compliance", &FixedJointParams::compliance);

        AggregateBinding<HingeJointParams>(m, "HingeJointParams")
            .field("axis_obj1", &HingeJointParams::axis_obj1)
            .field("anchor_obj1", &HingeJointParams::anchor_obj1)
            .field("compliance", &HingeJointParams::compliance);

        AggregateBinding<DirectionalLightParams>(m, "DirectionalLightParams")
            .field("direction", &DirectionalLightParams::direction)
            .field("color", &DirectionalLightParams::color)
            .field("intensity", &DirectionalLightParams::intensity)
            .field("cast_shadow", &DirectionalLightParams::cast_shadow);

        AggregateBinding<BodyState>(m, "BodyState")
            .field("position", &BodyState::position)
            .field("rotation", &BodyState::rotation)
            .field("linear_velocity", &BodyState::linear_velocity)
            .field("angular_velocity", &BodyState::angular_velocity);

        AggregateBinding<JointState>(m, "JointState")
            .field("angle", &JointState::angle)
            .field("angular_velocity", &JointState::angular_velocity);

        AggregateBinding<JointLimits>(m, "JointLimits")
            .field("lower", &JointLimits::lower)
            .field("upper", &JointLimits::upper)
            .field("effort", &JointLimits::effort)
            .field("velocity", &JointLimits::velocity);

        AggregateBinding<JointBodyPair>(m, "JointBodyPair")
            .field("parent", &JointBodyPair::parent)
            .field("child", &JointBodyPair::child)
            .field("id", &JointBodyPair::id);

        // `urdf_path` is a filesystem path in C++; Python passes a str or any
        // os.PathLike through the filesystem caster.
        AggregateBinding<UrdfImportConfig>(m, "UrdfImportConfig")
            .field("urdf_path", &UrdfImportConfig::urdf_path)
            .field("position", &UrdfImportConfig::position)
            .field("rotation", &UrdfImportConfig::rotation)
            .field("static_friction", &UrdfImportConfig::static_friction)
            .field("dynamic_friction", &UrdfImportConfig::dynamic_friction)
            .field("restitution", &UrdfImportConfig::restitution);

        nb::class_<UrdfImportResult>(m, "UrdfImportResult", "Result of importing a URDF robot: name to id maps.")
            .def_prop_ro(
                "link_bodies",
                [](const UrdfImportResult &result) {
                    nb::dict out;
                    for (const auto &[name, body] : result.link_bodies) {
                        out[nb::str(name.c_str())] = body;
                    }
                    return out;
                }
            )
            .def_prop_ro("joint_bodies", [](const UrdfImportResult &result) {
                nb::dict out;
                for (const auto &[name, pair] : result.joint_bodies) {
                    out[nb::str(name.c_str())] = pair;
                }
                return out;
            });
    }
} // namespace NeedrenameBindings
