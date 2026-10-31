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
//     torque for that body while the other body keeps its component.

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
    app->AddActuator(actuated.joint, std::make_unique<PdActuator>(20.0f, 1.0f));
    app->SetTargetAngle(actuated.joint, 0.5f);

    Check(manual.joint != INVALID_JOINT_ID && actuated.joint != INVALID_JOINT_ID, "joints created");

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

    if (g_failures == 0) {
        std::cout << "physics_app_joint_torque_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_joint_torque_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
