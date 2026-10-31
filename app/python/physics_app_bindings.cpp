/**
 * @file physics_app_bindings.cpp
 *
 * @brief Python bindings for `PhysicsApp` and the `Actuator` hierarchy.
 *
 * Design points implemented here (see the change's design.md):
 * - Blocking calls release the GIL (`step`, `render_next_frame`,
 *   `get_render_output`, `load_urdf`).
 * - Readback arrays are zero-copy numpy views over the engine's CPU readback
 *   memory. Each view holds the producing app object, so a live view keeps its
 *   backing memory alive; `step_id` makes staleness detectable.
 * - Actuators are registered by `shared_ptr`, so a Python subclass instance is
 *   registered directly (no adapter, no shadow state) and `get_actuator` returns
 *   the same Python object. Because `step` releases the GIL around
 *   `PhysicsApp::Step`, every `Actuator` virtual the trampoline overrides must
 *   acquire the GIL itself (see `PyActuator`).
 * - Exceptions are translated by `bindings.cpp`.
 */

#include "binding_support.h"

#include "Actuator.h"
#include "PhysicsApp.h"

#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/trampoline.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace nb::literals;

namespace NeedrenameBindings {
    namespace {
        using namespace AppPhysics;

        /// @brief Instance counter attribute maintained by `step`.
        constexpr const char *STEP_ID_ATTRIBUTE = "_step_id";

        /// @brief Number of steps this app object has completed so far.
        uint64_t StepIdOf(nb::handle app) {
            if (!nb::hasattr(app, STEP_ID_ATTRIBUTE)) {
                return 0;
            }
            return nb::cast<uint64_t>(app.attr(STEP_ID_ATTRIBUTE));
        }

        /**
         * @brief Python-facing batch state view: the engine spans plus the app
         * object that owns the readback memory.
         */
        struct BodyStatesViewBinding {
            BodyStatesView view;
            nb::object owner;
        };

        /// @brief Python-facing render output: pixels plus the owning app object.
        struct RenderOutputBinding {
            RenderOutput output;
            nb::object owner;
        };

        template <typename T>
        nb::ndarray<nb::numpy, const T, nb::ndim<1>> MakeArray1D(const T *data, size_t count, const nb::object &owner) {
            return nb::ndarray<nb::numpy, const T, nb::ndim<1>>(const_cast<T *>(data), {count}, owner);
        }

        template <typename T>
        nb::ndarray<nb::numpy, const T, nb::ndim<2>> MakeArray2D(
            const T *data, size_t rows, size_t columns, const nb::object &owner
        ) {
            return nb::ndarray<nb::numpy, const T, nb::ndim<2>>(const_cast<T *>(data), {rows, columns}, owner);
        }

        /**
         * @brief Trampoline so Python classes can implement `Actuator`.
         *
         * Rule for every future `Actuator` virtual: the override here MUST
         * acquire the GIL before calling into Python. `PhysicsApp::Step` runs
         * with the GIL released (the `step` binding releases it), so the
         * callback may run on a thread that does not hold it.
         */
        struct PyActuator : Actuator {
            NB_TRAMPOLINE(Actuator);

            float ComputeTorque(float angle, float angular_velocity) override {
                nb::gil_scoped_acquire acquire;
                NB_OVERRIDE_PURE_NAME("compute_torque", ComputeTorque, angle, angular_velocity);
            }

            void Reset() override {
                nb::gil_scoped_acquire acquire;
                NB_OVERRIDE_NAME("reset", Reset);
            }
        };
    } // namespace

    void RegisterPhysicsApp(nb::module_ &m) {
        nb::class_<Actuator, PyActuator>(
            m,
            "Actuator",
            "Base class for a scalar joint control law.\n\n"
            "Derive from it in Python and override `compute_torque` to register a\n"
            "custom actuator with `PhysicsApp.add_actuator`. A stateful law also\n"
            "overrides `reset` to clear its internal state."
        )
            .def(nb::init<>())
            .def(
                "compute_torque",
                &Actuator::ComputeTorque,
                "angle"_a,
                "angular_velocity"_a,
                "Return the joint torque [N*m] for the measured joint state."
            )
            .def("reset", &Actuator::Reset, "Return the actuator to its freshly constructed control state.")
            .def("set_target_angle", &Actuator::SetTargetAngle, "target"_a, "Set the target joint angle [rad].")
            .def("get_target_angle", &Actuator::GetTargetAngle, "Return the target joint angle [rad].");

        nb::class_<PdActuator, Actuator>(
            m, "PdActuator", "Standard PD joint actuator: tau = kp * wrap(target - angle) - kd * omega."
        )
            .def(nb::init<float, float>(), "kp"_a = 25.0f, "kd"_a = 0.5f)
            .def("compute_torque", &PdActuator::ComputeTorque, "angle"_a, "angular_velocity"_a)
            .def_prop_rw("kp", &PdActuator::GetKp, &PdActuator::SetKp, "Proportional gain.")
            .def_prop_rw("kd", &PdActuator::GetKd, &PdActuator::SetKd, "Derivative gain.");

        nb::class_<DcMotorActuator, Actuator>(
            m, "DcMotorActuator", "PD core clipped by a DC-motor torque-speed envelope."
        )
            .def(
                nb::init<float, float, float, float, float, float>(),
                "kp"_a = 25.0f,
                "kd"_a = 0.5f,
                "stall_torque"_a = 33.5f,
                "no_load_speed"_a = 21.0f,
                "cont_torque"_a = 13.4f,
                "gear_ratio"_a = 1.0f
            )
            .def("compute_torque", &DcMotorActuator::ComputeTorque, "angle"_a, "angular_velocity"_a)
            .def_prop_rw("kp", &DcMotorActuator::GetKp, &DcMotorActuator::SetKp, "Proportional gain.")
            .def_prop_rw("kd", &DcMotorActuator::GetKd, &DcMotorActuator::SetKd, "Derivative gain.")
            .def_prop_rw(
                "stall_torque",
                &DcMotorActuator::GetStallTorque,
                &DcMotorActuator::SetStallTorque,
                "Stall torque [N*m] (motor side)."
            )
            .def_prop_rw(
                "no_load_speed",
                &DcMotorActuator::GetNoLoadSpeed,
                &DcMotorActuator::SetNoLoadSpeed,
                "No-load speed [rad/s] (motor side)."
            )
            .def_prop_rw(
                "cont_torque",
                &DcMotorActuator::GetContTorque,
                &DcMotorActuator::SetContTorque,
                "Continuous torque limit [N*m]."
            )
            .def_prop_rw("gear_ratio", &DcMotorActuator::GetGearRatio, &DcMotorActuator::SetGearRatio, "Gear ratio.");

        // The ndarray properties return a fresh handle each access. Property
        // getters default to `reference_internal`, which an array that already
        // carries an owner rejects, so they pin `move` explicitly.
        auto view_cls = nb::class_<BodyStatesViewBinding>(
            m,
            "BodyStatesView",
            "Zero-copy batch view over every body's readback state (structure of arrays).\n\n"
            "The arrays view the engine's CPU readback memory: they are not copies and\n"
            "their contents are only valid until the next `step()`. `step_id` reports the\n"
            "step the app is currently at, so a stored value detects staleness; call\n"
            "`.copy()` on an array to take a snapshot."
        );
        view_cls.def_prop_ro("step_id", [](const BodyStatesViewBinding &self) { return StepIdOf(self.owner); });
        view_cls.def_prop_ro(
            "slot_indices",
            [](const BodyStatesViewBinding &self) {
                return MakeArray1D(self.view.slot_indices.data(), self.view.slot_indices.size(), self.owner);
            },
            nb::rv_policy::automatic
        );
        view_cls.def_prop_ro(
            "com_offsets",
            [](const BodyStatesViewBinding &self) {
                return MakeArray2D(
                    reinterpret_cast<const float *>(self.view.com_offsets.data()),
                    self.view.com_offsets.size(),
                    3,
                    self.owner
                );
            },
            nb::rv_policy::automatic
        );
        view_cls.def_prop_ro(
            "positions",
            [](const BodyStatesViewBinding &self) {
                return MakeArray2D(
                    reinterpret_cast<const float *>(self.view.positions.data()),
                    self.view.positions.size(),
                    4,
                    self.owner
                );
            },
            nb::rv_policy::automatic
        );
        view_cls.def_prop_ro(
            "rotations",
            [](const BodyStatesViewBinding &self) {
                return MakeArray2D(
                    reinterpret_cast<const float *>(self.view.rotations.data()),
                    self.view.rotations.size(),
                    4,
                    self.owner
                );
            },
            nb::rv_policy::automatic
        );
        view_cls.def_prop_ro(
            "linear_velocities",
            [](const BodyStatesViewBinding &self) {
                return MakeArray2D(
                    reinterpret_cast<const float *>(self.view.linear_velocities.data()),
                    self.view.linear_velocities.size(),
                    4,
                    self.owner
                );
            },
            nb::rv_policy::automatic
        );
        view_cls.def_prop_ro(
            "angular_velocities",
            [](const BodyStatesViewBinding &self) {
                return MakeArray2D(
                    reinterpret_cast<const float *>(self.view.angular_velocities.data()),
                    self.view.angular_velocities.size(),
                    4,
                    self.owner
                );
            },
            nb::rv_policy::automatic
        );

        auto render_output_cls = nb::class_<RenderOutputBinding>(
            m,
            "RenderOutput",
            "Latest rendered frame: tight-packed RGBA8 pixels viewing the engine's\n"
            "staging memory, valid until the next frame capture."
        );
        render_output_cls.def_prop_ro("width", [](const RenderOutputBinding &self) { return self.output.width; });
        render_output_cls.def_prop_ro("height", [](const RenderOutputBinding &self) { return self.output.height; });
        render_output_cls.def_prop_ro("frame_id", [](const RenderOutputBinding &self) { return self.output.frame_id; });
        render_output_cls.def_prop_ro(
            "pixels",
            [](const RenderOutputBinding &self) {
                return nb::ndarray<nb::numpy, const uint8_t, nb::ndim<3>>(
                    const_cast<void *>(self.output.pixels), {self.output.height, self.output.width, 4}, self.owner
                );
            },
            nb::rv_policy::automatic
        );

        nb::class_<PhysicsApp>(
            m,
            "PhysicsApp",
            "Minimal entry point for GPU physics simulation.\n\n"
            "Lifecycle: `create` -> building phase (add bodies, joints, lights) ->\n"
            "`commit_scene` -> drive phase (`step`, `render_next_frame`, readback).\n"
            "Wrong-phase usage raises `RuntimeError`; invalid handles raise `IndexError`.",
            nb::dynamic_attr()
        )
            .def_static(
                "create",
                [](const CreateInfo &info) { return PhysicsApp::Create(info); },
                "info"_a = CreateInfo{},
                "Create a physics app with full initialization."
            )
            // ── Building phase ──────────────────────────────────────────
            .def("add_box", &PhysicsApp::AddBox, "desc"_a, "Add a physics box; returns its BodyId.")
            .def("add_sphere", &PhysicsApp::AddSphere, "desc"_a, "Add a physics sphere; returns its BodyId.")
            .def("add_cylinder", &PhysicsApp::AddCylinder, "desc"_a, "Add a physics cylinder; returns its BodyId.")
            .def(
                "add_fixed_joint",
                &PhysicsApp::AddFixedJoint,
                "obj1"_a,
                "obj2"_a,
                "params"_a = FixedJointParams{},
                "Add a fixed joint between two bodies."
            )
            .def(
                "add_hinge_joint",
                &PhysicsApp::AddHingeJoint,
                "obj1"_a,
                "obj2"_a,
                "params"_a = HingeJointParams{},
                "Add a hinge joint; returns its JointId."
            )
            .def(
                "add_actuator",
                [](PhysicsApp &app, JointId joint, std::shared_ptr<Actuator> actuator) {
                    app.AddActuator(joint, std::move(actuator));
                },
                "joint"_a,
                "actuator"_a,
                "Register an actuator on a joint (one per joint; the actuator owns the torque channel).\n\n"
                "Ownership is shared with the app: the object passed in is the object the app\n"
                "runs (`get_actuator(joint)` returns the same instance) and it stays alive while\n"
                "registered even if the caller drops its reference. Legal in both phases."
            )
            .def(
                "get_actuator",
                &PhysicsApp::GetActuator,
                "joint"_a,
                "Return the actuator registered on a joint, or None when it has none."
            )
            .def(
                "remove_actuator",
                &PhysicsApp::RemoveActuator,
                "joint"_a,
                "Detach a joint's actuator and clear its torque channel (handing the joint back to\n"
                "manual `set_joint_torque` control)."
            )
            .def(
                "set_target_angle",
                &PhysicsApp::SetTargetAngle,
                "joint"_a,
                "target"_a,
                "Set an actuator's target joint angle [rad]."
            )
            .def("get_joint_state", &PhysicsApp::GetJointState, "joint"_a, "Read a joint's angle and velocity.")
            .def("get_joint_limits", &PhysicsApp::GetJointLimits, "joint"_a, "Read a joint's recorded URDF limits.")
            .def(
                "load_urdf",
                &PhysicsApp::LoadUrdf,
                "config"_a,
                "Import a URDF robot; returns link/joint name to id maps.",
                nb::call_guard<nb::gil_scoped_release>()
            )
            .def(
                "add_directional_light",
                &PhysicsApp::AddDirectionalLight,
                "params"_a = DirectionalLightParams{},
                "Add a directional light."
            )
            .def(
                "set_camera_pose",
                &PhysicsApp::SetCameraPose,
                "position"_a,
                "look_target"_a,
                "Place the camera and aim it at a world-space point."
            )
            .def("commit_scene", &PhysicsApp::CommitScene, "Commit and freeze the scene; starts the drive phase.")
            // ── Drive phase ────────────────────────────────────────────
            .def(
                "reset_to_initial",
                &PhysicsApp::ResetToInitial,
                "Restore the whole app to the state recorded at `commit_scene`.\n\n"
                "Every body returns to its commit-time pose with the recorded velocities as\n"
                "integration seeds, external forces and torques are zeroed, and every\n"
                "actuator target is restored and its internal state cleared. The restored\n"
                "state is visible to state reads immediately and reaches the physics buffers\n"
                "on the next `step`; the reset itself neither steps nor renders."
            )
            .def(
                "step",
                [](nb::object self) {
                    auto &app = nb::cast<PhysicsApp &>(self);
                    {
                        nb::gil_scoped_release release;
                        app.Step();
                    }
                    const uint64_t next = StepIdOf(self) + 1;
                    nb::setattr(self, STEP_ID_ATTRIBUTE, nb::cast(next));
                },
                "Advance the physics simulation by one fixed step."
            )
            .def(
                "render_next_frame",
                &PhysicsApp::RenderNextFrame,
                "Render one frame, processing input and updating the camera.",
                nb::call_guard<nb::gil_scoped_release>()
            )
            .def("pause", &PhysicsApp::Pause, "Set the app-level pause flag.")
            .def("resume", &PhysicsApp::Resume, "Clear the app-level pause flag.")
            .def_prop_ro("is_paused", &PhysicsApp::IsPaused, "True while the app-level pause flag is set.")
            .def_prop_ro(
                "step_id", [](nb::object self) { return StepIdOf(self); }, "Number of steps completed so far."
            )
            .def_prop_ro("should_quit", &PhysicsApp::ShouldQuit, "True once a window-close event has been received.")
            .def("get_body_state", &PhysicsApp::GetBodyState, "id"_a, "Read one body's dynamic state.")
            .def(
                "get_body_states",
                [](nb::object self) {
                    auto &app = nb::cast<PhysicsApp &>(self);
                    return BodyStatesViewBinding{app.GetBodyStates(), self};
                },
                "Return the zero-copy batch view over every body's state."
            )
            .def(
                "set_body_value",
                &PhysicsApp::SetBodyValue,
                "id"_a,
                "field"_a,
                "value"_a,
                "Override one writable body field before the next step."
            )
            .def(
                "set_joint_torque",
                &PhysicsApp::SetJointTorque,
                "joint"_a,
                "torque"_a,
                "Apply an equal-and-opposite torque about a joint's hinge axis (joints without actuators)."
            )
            .def(
                "set_render_readback_enabled",
                &PhysicsApp::SetRenderReadbackEnabled,
                "enabled"_a,
                "Toggle recording of a CPU readback of the final render target."
            )
            .def(
                "get_render_output",
                [](nb::object self) {
                    auto &app = nb::cast<PhysicsApp &>(self);
                    RenderOutput output;
                    {
                        // Blocking wait on the GPU: release the GIL. The pixel view
                        // must be created with the GIL held, hence a scoped release
                        // instead of `nb::call_guard`.
                        nb::gil_scoped_release release;
                        output = app.GetRenderOutput();
                    }
                    return RenderOutputBinding{output, self};
                },
                "Block until the latest frame completes and return its pixels as a view."
            );
    }
} // namespace NeedrenameBindings
