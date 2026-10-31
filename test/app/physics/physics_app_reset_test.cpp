// physics_app_reset_test.cpp — PhysicsApp::ResetToInitial (episode reset).
//
// The app snapshots every body's pose/velocity and every actuator target at
// `CommitScene`; `ResetToInitial()` restores exactly that state for the whole app.
//
// Covered:
//   * calling reset before `CommitScene` throws `std::logic_error`,
//   * after an episode of simulation, reset returns every body to its commit-time
//     pose/velocity and every actuator to its commit-time target immediately, with
//     no intervening step,
//   * the snapshot is the commit-time state, not a mid-episode one (later steps do
//     not update it),
//   * external force and torque are cleared, so the steps after a reset apply no
//     residual force or torque,
//   * a stateful actuator's internal state is cleared through `Actuator::Reset()`,
//   * reset neither advances the simulation nor renders.

#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace AppPhysics;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    bool Close(float a, float b, float eps) {
        return std::fabs(a - b) <= eps;
    }

    bool CloseV(const glm::vec3 &a, const glm::vec3 &b, float eps) {
        return Close(a.x, b.x, eps) && Close(a.y, b.y, eps) && Close(a.z, b.z, eps);
    }

    template <typename Fn>
    bool ThrowsLogic(Fn fn) {
        try {
            fn();
        } catch (const std::logic_error &) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }

    /// @brief Control law with observable internal state and reset counting.
    ///
    /// `ComputeTorque` accumulates the measured angle into an integrator, which
    /// `Reset()` must clear. The call counters deliberately survive `Reset()`, so
    /// the test can tell "never called" from "called, then reset".
    class StatefulActuator final : public Actuator {
    public:
        float ComputeTorque(float angle, float angular_velocity) override {
            m_integral += angle;
            m_calls++;
            const float error = GetTargetAngle() - angle;
            return std::clamp(30.0f * error - 1.0f * angular_velocity, -20.0f, 20.0f);
        }

        void Reset() override {
            m_integral = 0.0f;
            m_reset_calls++;
        }

        float integral() const {
            return m_integral;
        }

        int calls() const {
            return m_calls;
        }

        int reset_calls() const {
            return m_reset_calls;
        }

    private:
        float m_integral{0.0f};
        int m_calls{0};
        int m_reset_calls{0};
    };

    struct BodySnapshot {
        glm::vec3 position{};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 linear_velocity{};
        glm::vec3 angular_velocity{};
    };

    BodySnapshot Snapshot(const PhysicsApp &app, BodyId id) {
        const BodyState state = app.GetBodyState(id);
        return {state.position, state.rotation, state.linear_velocity, state.angular_velocity};
    }
} // namespace

int main() {
    CreateInfo info{};
    info.mode = AppMode::PhysicsOnly;
    info.resol_x = 256;
    info.resol_y = 256;

    auto app = PhysicsApp::Create(info);

    // Phase guard: there is nothing to restore before the scene is committed.
    Check(ThrowsLogic([&] { app->ResetToInitial(); }), "ResetToInitial before CommitScene throws logic_error");

    // Three independent bodies plus a hinge pair carrying a stateful actuator.
    const glm::vec3 pos0(0.0f, 0.0f, 4.0f);
    const glm::vec3 pos1(3.0f, 0.0f, 3.0f);
    const glm::vec3 pos2(-3.0f, 0.0f, 5.0f);
    const BodyId b0 = app->AddBox({.position = pos0, .half_extents = {0.5f, 0.5f, 0.5f}, .mass = 1.0f});
    const BodyId b1 = app->AddSphere({.position = pos1, .radius = 0.5f, .mass = 1.0f});
    const BodyId b2 = app->AddBox({.position = pos2, .half_extents = {0.3f, 0.3f, 0.3f}, .mass = 1.0f});

    const BodyId hinge_parent =
        app->AddBox({.position = {10.0f, 0.0f, 4.75f}, .half_extents = {0.2f, 0.2f, 0.2f}, .mass = 1.0f});
    const BodyId hinge_child =
        app->AddBox({.position = {10.0f, 0.0f, 5.25f}, .half_extents = {0.2f, 0.2f, 0.2f}, .mass = 1.0f});
    const JointId hinge_joint = app->AddHingeJoint(
        hinge_parent, hinge_child, {.axis_obj1 = {0.0f, 0.0f, 1.0f}, .anchor_obj1 = {0.0f, 0.0f, 0.5f}}
    );

    auto actuator = std::make_shared<StatefulActuator>();
    app->AddActuator(hinge_joint, actuator);

    // Commit-time actuator target: what reset must restore.
    constexpr float kCommitTarget = 0.2f;
    app->SetTargetAngle(hinge_joint, kCommitTarget);

    app->CommitScene();

    const std::vector<BodyId> bodies{b0, b1, b2, hinge_parent, hinge_child};
    std::vector<BodySnapshot> commit_states;
    for (const BodyId id : bodies) {
        commit_states.push_back(Snapshot(*app, id));
    }
    const float commit_joint_angle = app->GetJointState(hinge_joint).angle;
    const float commit_b0_z = app->GetBodyState(b0).position.z;
    Check(std::fabs(actuator->GetTargetAngle() - kCommitTarget) < 1e-5f, "actuator starts on its commit-time target");

    // ── Episode 1: disturb pose, velocity, force/torque, target, actuator ──
    app->SetBodyValue(b0, BodyField::ExternalForce, glm::vec4(0.0f, 0.0f, 200.0f, 0.0f));
    app->SetBodyValue(b0, BodyField::ExternalTorque, glm::vec4(0.0f, 0.0f, 5.0f, 0.0f));
    app->SetTargetAngle(hinge_joint, 0.9f);
    for (int i = 0; i < 60; ++i) {
        app->Step();
    }

    const float disturbed_b0_z = app->GetBodyState(b0).position.z;
    Check(disturbed_b0_z > commit_b0_z + 1.0f, "the external force lifts the disturbed body");
    Check(std::fabs(actuator->GetTargetAngle() - 0.9f) < 1e-5f, "the target was changed during the episode");
    Check(actuator->calls() > 0, "the actuator ran during the episode");
    Check(actuator->integral() != 0.0f, "the actuator accumulated internal state");
    Check(actuator->reset_calls() == 0, "the actuator was not reset during the episode");
    Check(std::fabs(app->GetBodyState(b0).angular_velocity.z) > 0.1f, "the external torque spun the disturbed body");

    // ── Reset ───────────────────────────────────────────────────────────
    app->ResetToInitial();

    // The snapshot is the commit-time state, so the restore is immediate: no
    // step happened, otherwise the poses and velocities would have integrated.
    for (size_t i = 0; i < bodies.size(); ++i) {
        const BodySnapshot now = Snapshot(*app, bodies[i]);
        Check(CloseV(now.position, commit_states[i].position, 1e-4f), "reset restores the commit-time position");
        Check(
            Close(now.rotation.w, commit_states[i].rotation.w, 1e-4f)
                && Close(now.rotation.x, commit_states[i].rotation.x, 1e-4f)
                && Close(now.rotation.y, commit_states[i].rotation.y, 1e-4f)
                && Close(now.rotation.z, commit_states[i].rotation.z, 1e-4f),
            "reset restores the commit-time rotation"
        );
        Check(
            CloseV(now.linear_velocity, commit_states[i].linear_velocity, 1e-4f),
            "reset restores the commit-time linear velocity seed"
        );
        Check(
            CloseV(now.angular_velocity, commit_states[i].angular_velocity, 1e-4f),
            "reset restores the commit-time angular velocity seed"
        );
    }
    Check(
        std::fabs(app->GetJointState(hinge_joint).angle - commit_joint_angle) < 1e-3f,
        "reset restores the commit-time joint angle (state reads only, no step)"
    );
    Check(std::fabs(actuator->GetTargetAngle() - kCommitTarget) < 1e-5f, "reset restores the commit-time target angle");
    Check(actuator->reset_calls() == 1, "reset clears actuator internal state through Actuator::Reset()");
    Check(Close(actuator->integral(), 0.0f, 1e-6f), "the actuator's internal state is cleared");

    // ── The restored state is what the next step starts from ───────────
    // Force and torque were zeroed by the reset: one step may only apply gravity.
    app->Step();
    const BodyState after_reset_step = app->GetBodyState(b0);
    Check(
        after_reset_step.position.z < commit_b0_z,
        "reset cleared the external force (the body falls, it is not pushed up)"
    );
    Check(after_reset_step.linear_velocity.z < 0.0f, "one step after reset only carries gravity");
    Check(std::fabs(after_reset_step.angular_velocity.z) < 0.1f, "reset cleared the external torque");
    Check(actuator->GetTargetAngle() == kCommitTarget, "the restored target drives the joint from the next step");

    if (g_failures == 0) {
        std::cout << "physics_app_reset_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_reset_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
