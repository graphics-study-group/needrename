// physics_app_joint_torque_test.cpp — PhysicsApp::SetJointTorque behavior.
//
// The torque channel is exercised on a two-body hinge in `AppMode::PhysicsOnly`:
// a free dynamic pair whose bodies have identical mass and inertia, so a truly
// equal-and-opposite torque pair must produce exactly opposite angular
// velocities (`omega_child = -omega_parent`) about the hinge axis.
//
// Covered:
//   * phase / handle / actuator-ownership error cases,
//   * torque accelerates the joint in the +axis direction,
//   * a once-set torque keeps applying on every step until overwritten,
//   * the actuator torque path still drives its joint and is not affected,
//   * a later `SetBodyValue(ExternalTorque)` on one body wins over the joint
//     torque for that body while the other body keeps its component,
//   * `GetActuator` returns the registered instance and an empty pointer for a
//     joint without one,
//   * `RemoveActuator` clears the joint's torque channel (the stale actuator
//     torque stops applying) and hands the joint back to `SetJointTorque`,
//   * registration, tuning, and removal are legal in the Drive phase and an
//     actuator added after `CommitScene` drives from the next `Step`.

#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

using namespace AppPhysics;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    constexpr float kTorque = 2.0f;
    constexpr float kHalf = 0.2f;
    constexpr float kMass = 1.0f;

    /// @brief Control law with no feedback: applies a constant torque, which
    /// makes a removed-actuator torque easy to observe.
    class ConstantTorqueActuator final : public Actuator {
    public:
        explicit ConstantTorqueActuator(float torque) : m_torque(torque) {
        }

        float ComputeTorque(float, float) override {
            return m_torque;
        }

    private:
        float m_torque;
    };

    /// Hinge assembly: two identical boxes stacked along Z, 4 cm apart, joined at
    /// the upper (child) box's center by a Z-axis hinge. `parent_kinematic` pins
    /// the lower body (actuator case); otherwise both are free dynamic bodies so
    /// an equal-and-opposite torque pair counter-rotates them exactly.
    struct Assembly {
        BodyId parent{INVALID_BODY_ID};
        BodyId child{INVALID_BODY_ID};
        JointId joint{INVALID_JOINT_ID};
    };

    Assembly AddHingeAssembly(PhysicsApp &app, const glm::vec3 &origin, bool parent_kinematic) {
        Assembly a;
        a.parent = app.AddBox({
            .position = origin + glm::vec3(0.0f, 0.0f, -0.25f),
            .half_extents = {kHalf, kHalf, kHalf},
            .mass = parent_kinematic ? 0.0f : kMass,
            .kinematic = parent_kinematic,
        });
        a.child = app.AddBox({
            .position = origin + glm::vec3(0.0f, 0.0f, 0.25f),
            .half_extents = {kHalf, kHalf, kHalf},
            .mass = kMass,
            .kinematic = false,
        });
        a.joint =
            app.AddHingeJoint(a.parent, a.child, {.axis_obj1 = {0.0f, 0.0f, 1.0f}, .anchor_obj1 = {0.0f, 0.0f, 0.5f}});
        return a;
    }
} // namespace

int main() {
    CreateInfo info{};
    info.mode = AppMode::PhysicsOnly;
    info.resol_x = 256;
    info.resol_y = 256;

    auto app = PhysicsApp::Create(info);

    // The manual-torque pair sits at the origin; the actuated assembly is far
    // away so the two never interact.
    const Assembly manual = AddHingeAssembly(*app, glm::vec3(0.0f, 0.0f, 0.0f), false);
    const Assembly actuated = AddHingeAssembly(*app, glm::vec3(20.0f, 0.0f, 0.0f), true);
    auto actuated_actuator = std::make_shared<PdActuator>(20.0f, 1.0f);
    app->AddActuator(actuated.joint, actuated_actuator);
    app->SetTargetAngle(actuated.joint, 0.5f);

    // Removal scenario: a constant-torque actuator on a free pair, so the stale
    // torque is observable as continued angular acceleration.
    const Assembly removable = AddHingeAssembly(*app, glm::vec3(40.0f, 0.0f, 0.0f), false);
    auto constant_actuator = std::make_shared<ConstantTorqueActuator>(kTorque);
    app->AddActuator(removable.joint, constant_actuator);

    // Drive-phase scenario: registered only after CommitScene.
    const Assembly drive_phase = AddHingeAssembly(*app, glm::vec3(60.0f, 0.0f, 0.0f), true);
    auto drive_phase_actuator = std::make_shared<PdActuator>(20.0f, 1.0f);
    drive_phase_actuator->SetTargetAngle(0.5f);

    Check(manual.joint != INVALID_JOINT_ID && actuated.joint != INVALID_JOINT_ID, "joints created");

    // ── Registration errors (Building phase) ───────────────────────────
    {
        bool threw = false;
        try {
            app->AddActuator(manual.joint, nullptr);
        } catch (const std::invalid_argument &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "AddActuator with a null actuator throws std::invalid_argument");
    }
    {
        bool threw = false;
        try {
            app->AddActuator(actuated.joint, std::make_shared<PdActuator>());
        } catch (const std::invalid_argument &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "AddActuator on an occupied joint throws std::invalid_argument");
    }
    {
        bool threw = false;
        try {
            app->AddActuator(12345u, std::make_shared<PdActuator>());
        } catch (const std::out_of_range &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "AddActuator with an invalid JointId throws std::out_of_range");
    }

    // ── Registered instance is retrievable ─────────────────────────────
    Check(app->GetActuator(actuated.joint) == actuated_actuator, "GetActuator returns the registered instance");
    Check(app->GetActuator(manual.joint) == nullptr, "GetActuator returns an empty pointer when there is none");
    {
        bool threw = false;
        try {
            (void)app->GetActuator(12345u);
        } catch (const std::out_of_range &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "GetActuator with an invalid JointId throws std::out_of_range");
    }
    {
        bool threw = false;
        try {
            app->RemoveActuator(manual.joint);
        } catch (const std::logic_error &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "RemoveActuator on a joint without an actuator throws std::logic_error");
    }
    {
        bool threw = false;
        try {
            app->RemoveActuator(12345u);
        } catch (const std::out_of_range &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "RemoveActuator with an invalid JointId throws std::out_of_range");
    }

    // ── Phase guard: before CommitScene ─────────────────────────────────
    {
        bool threw = false;
        try {
            app->SetJointTorque(manual.joint, kTorque);
        } catch (const std::logic_error &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "SetJointTorque before CommitScene throws std::logic_error");
    }

    app->CommitScene();

    // ── Handle and ownership errors ─────────────────────────────────────
    {
        bool threw = false;
        try {
            app->SetJointTorque(12345u, kTorque);
        } catch (const std::out_of_range &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "SetJointTorque with an invalid JointId throws std::out_of_range");
    }
    {
        bool threw = false;
        try {
            app->SetJointTorque(INVALID_JOINT_ID, kTorque);
        } catch (const std::out_of_range &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "SetJointTorque with INVALID_JOINT_ID throws std::out_of_range");
    }
    {
        bool threw = false;
        try {
            app->SetJointTorque(actuated.joint, kTorque);
        } catch (const std::logic_error &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "SetJointTorque on an actuated joint throws std::logic_error");
    }

    // ── Removal clears the torque channel ───────────────────────────────
    // The constant-torque actuator has been driving its joint; after removal the
    // joint keeps its angular velocity but stops accelerating.
    for (int i = 0; i < 3; ++i) {
        app->Step();
    }
    const float removable_omega_before = app->GetJointState(removable.joint).angular_velocity;
    Check(removable_omega_before > 0.0f, "the removable joint is driven by its actuator");

    app->RemoveActuator(removable.joint);
    Check(app->GetActuator(removable.joint) == nullptr, "RemoveActuator detaches the actuator");
    {
        bool threw = false;
        try {
            app->SetJointTorque(removable.joint, 0.0f);
        } catch (...) {
            threw = true;
        }
        Check(!threw, "SetJointTorque is legal again after RemoveActuator");
    }

    for (int i = 0; i < 3; ++i) {
        app->Step();
    }
    const float removable_omega_after = app->GetJointState(removable.joint).angular_velocity;
    Check(
        std::fabs(removable_omega_after - removable_omega_before)
            < 0.05f * std::max(std::fabs(removable_omega_before), 1e-6f),
        "the removed actuator's stale torque stops applying"
    );

    // ── Actuator path unchanged ─────────────────────────────────────────
    // The actuated joint reaches its target while the untouched manual joint
    // stays at rest (no manual-torque interference).
    for (int i = 0; i < 60; ++i) {
        app->Step();
    }
    const JointState actuated_state = app->GetJointState(actuated.joint);
    Check(actuated_state.angle > 0.1f, "a registered actuator still drives its joint");
    Check(
        std::fabs(app->GetJointState(manual.joint).angular_velocity) < 1e-3f,
        "no manual torque is applied to a joint that never received one"
    );

    // ── Torque accelerates the joint in the +axis direction ─────────────
    app->SetJointTorque(manual.joint, kTorque);
    app->Step();
    const float omega_1 = app->GetJointState(manual.joint).angular_velocity;
    Check(omega_1 > 0.0f, "positive torque accelerates the joint about +axis");

    // ── Persistence: the once-set torque keeps applying every step ──────
    app->Step();
    app->Step();
    const float omega_3 = app->GetJointState(manual.joint).angular_velocity;
    Check(omega_3 > 2.0f * omega_1, "a once-set torque keeps applying on every step");

    // ── Equal-and-opposite torques on the two bodies ────────────────────
    // Identical bodies => a truly opposite torque pair => opposite angular
    // velocities of equal magnitude.
    const float child_omega = app->GetBodyState(manual.child).angular_velocity.z;
    const float parent_omega = app->GetBodyState(manual.parent).angular_velocity.z;
    Check(child_omega > 0.0f, "child body spins in the +axis direction");
    Check(parent_omega < 0.0f, "parent body spins in the -axis direction");
    Check(
        std::fabs(child_omega + parent_omega) < 0.05f * std::max(std::fabs(child_omega), 1e-6f),
        "the opposite body torques are equal in magnitude"
    );

    // ── Zeroing the torque stops the acceleration ───────────────────────
    app->SetJointTorque(manual.joint, 0.0f);
    app->Step();
    const float omega_zeroed = app->GetJointState(manual.joint).angular_velocity;
    Check(
        std::fabs(omega_zeroed - omega_3) < 0.05f * std::fabs(omega_3),
        "setting the torque to zero stops the acceleration"
    );

    // ── A later SetBodyValue(ExternalTorque) wins for that body ─────────
    // Re-apply the pair, then zero only the child's channel: the child keeps its
    // angular velocity while the parent keeps accelerating on its component.
    app->SetJointTorque(manual.joint, kTorque);
    app->SetBodyValue(manual.child, BodyField::ExternalTorque, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    const float child_before = app->GetBodyState(manual.child).angular_velocity.z;
    const float parent_before = app->GetBodyState(manual.parent).angular_velocity.z;
    app->Step();
    app->Step();
    const float child_after = app->GetBodyState(manual.child).angular_velocity.z;
    const float parent_after = app->GetBodyState(manual.parent).angular_velocity.z;

    Check(
        std::fabs(child_after - child_before) < 0.05f * std::max(std::fabs(child_before), 1e-6f),
        "the later SetBodyValue write zeroes the child's component of the joint torque"
    );
    Check(parent_after < parent_before, "the parent keeps its component of the joint torque");

    // ── Drive-phase registration, tuning, and removal ───────────────────
    {
        bool threw = false;
        try {
            app->AddActuator(drive_phase.joint, drive_phase_actuator);
        } catch (...) {
            threw = true;
        }
        Check(!threw, "AddActuator is legal in the Drive phase");
    }
    Check(
        app->GetActuator(drive_phase.joint) == drive_phase_actuator,
        "a Drive-phase actuator is retrievable as the registered instance"
    );

    for (int i = 0; i < 60; ++i) {
        app->Step();
    }
    Check(
        app->GetJointState(drive_phase.joint).angle > 0.1f,
        "an actuator added after CommitScene drives its joint from the next Step"
    );

    drive_phase_actuator->SetKp(40.0f);
    Check(drive_phase_actuator->GetKp() == 40.0f, "the caller can tune the live instance");
    app->SetTargetAngle(drive_phase.joint, -0.3f);
    for (int i = 0; i < 80; ++i) {
        app->Step();
    }
    Check(
        std::fabs(app->GetJointState(drive_phase.joint).angle + 0.3f) < 0.05f,
        "the tuned Drive-phase actuator tracks its new target"
    );

    app->RemoveActuator(drive_phase.joint);
    Check(app->GetActuator(drive_phase.joint) == nullptr, "a Drive-phase actuator can be removed");
    {
        bool threw = false;
        try {
            app->SetJointTorque(drive_phase.joint, 0.0f);
        } catch (...) {
            threw = true;
        }
        Check(!threw, "removal in the Drive phase hands the joint back to SetJointTorque");
    }
    {
        bool threw = false;
        try {
            app->SetTargetAngle(drive_phase.joint, 0.0f);
        } catch (const std::logic_error &) {
            threw = true;
        } catch (...) {
        }
        Check(threw, "SetTargetAngle throws once the actuator is removed");
    }

    if (g_failures == 0) {
        std::cout << "physics_app_joint_torque_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_joint_torque_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
