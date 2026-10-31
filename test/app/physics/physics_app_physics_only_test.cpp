// physics_app_physics_only_test.cpp — PhysicsApp in `AppMode::PhysicsOnly`.
//
// Covered: phase guards, initial readback, the batch SoA view, pause model,
// gravity integration, and the body-state write semantics of `SetBodyValue`:
//   * writes are visible to `GetBodyState` / `GetBodyStates` / `GetJointState`
//     before any step, and to actuator measurement inside the next step,
//   * a teleport applies on the next step and does not snap back,
//   * fields are not re-uploaded on later steps,
//   * a whole-field upload never disturbs bodies the caller did not write,
//   * external force/torque stay caller-managed (persist until zeroed).

#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

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

    template <typename Fn>
    bool ThrowsOutOfRange(Fn fn) {
        try {
            fn();
        } catch (const std::out_of_range &) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }

    /// @brief Zero-torque control law that records the joint state it measured.
    ///
    /// Applying no torque keeps the measured assembly undisturbed, so the recorded
    /// angles are a clean probe of what the actuator saw inside `Step`.
    class MeasuringActuator final : public Actuator {
    public:
        float ComputeTorque(float angle, float angular_velocity) override {
            m_angle = angle;
            m_angular_velocity = angular_velocity;
            return 0.0f;
        }

        float last_angle() const {
            return m_angle;
        }

    private:
        float m_angle{0.0f};
        float m_angular_velocity{0.0f};
    };
} // namespace

int main() {
    CreateInfo info{};
    info.mode = AppMode::PhysicsOnly;
    info.resol_x = 256;
    info.resol_y = 256;

    auto app = PhysicsApp::Create(info);

    // Reading before CommitScene throws logic_error.
    Check(ThrowsLogic([&] { (void)app->GetBodyState(0); }), "GetBodyState before commit throws");
    Check(ThrowsLogic([&] { (void)app->GetBodyStates(); }), "GetBodyStates before commit throws");
    Check(
        ThrowsLogic([&] { app->SetBodyValue(0, BodyField::Position, glm::vec4(0.0f)); }),
        "SetBodyValue before commit throws"
    );

    // Render-only APIs throw in physics_only mode (allowed before commit too).
    Check(ThrowsLogic([&] { app->SetCameraPose({0, 0, 0}, {1, 0, 0}); }), "SetCameraPose throws physics_only");
    Check(ThrowsLogic([&] { app->AddDirectionalLight({}); }), "AddDirectionalLight throws physics_only");

    // Build the scene: three independent dynamic bodies plus a hinge pair far
    // away, which carries the zero-torque measuring actuator.
    const glm::vec3 box0(0.0f, 0.0f, 2.0f);
    const glm::vec3 sphere1(1.0f, 0.0f, 3.0f);
    const glm::vec3 box2(-4.0f, 0.0f, 5.0f);
    BodyId b0 = app->AddBox({.position = box0, .half_extents = {0.5f, 0.5f, 0.5f}, .mass = 1.0f});
    BodyId b1 = app->AddSphere({.position = sphere1, .radius = 0.5f, .mass = 1.0f});
    BodyId b2 = app->AddBox({.position = box2, .half_extents = {0.5f, 0.5f, 0.5f}, .mass = 1.0f});

    const glm::vec3 hinge_axis(0.0f, 0.0f, 1.0f);
    BodyId hinge_parent =
        app->AddBox({.position = {20.0f, 0.0f, -0.25f}, .half_extents = {0.2f, 0.2f, 0.2f}, .mass = 1.0f});
    BodyId hinge_child =
        app->AddBox({.position = {20.0f, 0.0f, 0.25f}, .half_extents = {0.2f, 0.2f, 0.2f}, .mass = 1.0f});
    JointId hinge_joint =
        app->AddHingeJoint(hinge_parent, hinge_child, {.axis_obj1 = hinge_axis, .anchor_obj1 = {0.0f, 0.0f, 0.5f}});
    auto measuring = std::make_shared<MeasuringActuator>();
    app->AddActuator(hinge_joint, measuring);

    app->CommitScene();

    // Initial state is readable before the first step and matches setup.
    BodyState s0 = app->GetBodyState(b0);
    Check(CloseV(s0.position, box0, 0.01f), "initial position matches");
    Check(
        Close(s0.rotation.w, 1.0f, 1e-4f) && Close(s0.rotation.x, 0.0f, 1e-4f) && Close(s0.rotation.y, 0.0f, 1e-4f)
            && Close(s0.rotation.z, 0.0f, 1e-4f),
        "initial rotation is identity"
    );
    Check(CloseV(s0.linear_velocity, {0.0f, 0.0f, 0.0f}, 1e-3f), "initial linear velocity zero");
    Check(CloseV(s0.angular_velocity, {0.0f, 0.0f, 0.0f}, 1e-3f), "initial angular velocity zero");

    // Invalid BodyId throws out_of_range.
    Check(ThrowsOutOfRange([&] { (void)app->GetBodyState(INVALID_BODY_ID); }), "invalid id throws");
    Check(ThrowsOutOfRange([&] { (void)app->GetBodyState(9999); }), "out-of-range id throws");

    // Batch view: one entry per BodyId, unique slots, zero com offsets.
    constexpr size_t kBodyCount = 5;
    auto view = app->GetBodyStates();
    Check(view.slot_indices.size() == kBodyCount, "slot_indices size == body count");
    Check(view.slot_indices[0] != view.slot_indices[1], "slot indices unique");
    Check(view.com_offsets.size() >= kBodyCount, "com_offsets sized to slots");
    Check(
        view.positions.size() >= kBodyCount && view.rotations.size() >= kBodyCount
            && view.linear_velocities.size() >= kBodyCount && view.angular_velocities.size() >= kBodyCount,
        "SoA spans sized to slots"
    );
    Check(
        Close(view.com_offsets[view.slot_indices[0]].x, 0.0f, 1e-4f)
            && Close(view.com_offsets[view.slot_indices[0]].y, 0.0f, 1e-4f)
            && Close(view.com_offsets[view.slot_indices[0]].z, 0.0f, 1e-4f),
        "self-built body has zero com offset"
    );

    // Render-readback APIs throw in physics_only mode.
    Check(ThrowsLogic([&] { app->RenderNextFrame(); }), "RenderNextFrame throws physics_only");
    Check(ThrowsLogic([&] { (void)app->GetRenderOutput(); }), "GetRenderOutput throws physics_only");
    Check(
        ThrowsLogic([&] { app->SetRenderReadbackEnabled(true); }), "SetRenderReadbackEnabled(true) throws physics_only"
    );

    // Pause model: the app starts paused (flag), but Step() is unconditional and
    // advances physics regardless of the flag; Pause()/Resume() only flip the
    // flag and never disable the scene simulation.
    Check(app->IsPaused(), "app starts with paused flag set");
    const float z_paused0 = app->GetBodyState(b0).position.z;
    app->Step(); // Called while the paused flag is set — must still advance.
    Check(app->GetBodyState(b0).position.z < z_paused0, "Step advances physics while paused flag is set");
    Check(app->IsPaused(), "paused flag unchanged by Step");
    app->Pause();
    app->Step();
    Check(app->GetBodyState(b0).position.z < z_paused0, "Step advances physics while explicitly Paused");

    // Advance physics; the dynamic body falls under gravity (Z-down). The
    // simulation starts paused (paused flag set), so resume first.
    app->Resume();
    Check(!app->IsPaused(), "Resume clears paused flag");
    app->Step();
    BodyState after = app->GetBodyState(b0);
    std::cout << "box0.z: " << box0.z << " ---> " << after.position.z << std::endl;
    Check(after.position.z < box0.z, "position falls under gravity after Step");
    Check(!app->ShouldQuit(), "ShouldQuit false in physics_only mode");

    // ── SetBodyValue scenarios ──────────────────────────────────────────

    // Invalid BodyId throws out_of_range.
    Check(
        ThrowsOutOfRange([&] { app->SetBodyValue(9999, BodyField::Position, glm::vec4(0.0f)); }),
        "SetBodyValue invalid id throws"
    );

    // A write is visible to state reads immediately: no intervening Step.
    const glm::vec3 tp(2.0f, 3.0f, 8.0f);
    app->SetBodyValue(b1, BodyField::Position, glm::vec4(tp, 1.0f));
    Check(CloseV(app->GetBodyState(b1).position, tp, 1e-4f), "Set position visible before Step");
    auto written_view = app->GetBodyStates();
    Check(
        CloseV(glm::vec3(written_view.positions[written_view.slot_indices[b1]]), tp, 1e-4f),
        "batch view reflects the write immediately"
    );

    // ... and the teleport still applies on the next Step, without snapping back.
    app->Step();
    BodyState st = app->GetBodyState(b1);
    Check(CloseV(st.position, tp, 0.5f), "teleport applied on next Step");
    app->Step(); // no re-set
    BodyState st2 = app->GetBodyState(b1);
    Check(st2.position.z < tp.z, "position does not snap back to teleport value on later steps");

    // Velocity injection: set a +x velocity; subsequent steps move it +x.
    app->SetBodyValue(b1, BodyField::LinearVelocity, glm::vec4(6.0f, 0.0f, 0.0f, 0.0f));
    const float x0_vel = app->GetBodyState(b1).position.x;
    for (int i = 0; i < 20; ++i) {
        app->Step();
    }
    Check(app->GetBodyState(b1).position.x > x0_vel + 1.0f, "velocity injection moves body in +x");

    // A field is not re-uploaded on later steps: set velocity to zero once, then
    // gravity keeps it accumulating (not snapped back to zero each step).
    app->SetBodyValue(b1, BodyField::LinearVelocity, glm::vec4(0.0f));
    app->Step();
    app->Step();
    // Two steps of gravity => v.z ≈ -2*g*dt ≈ -0.33 if not re-uploaded (≈ -0.16 if re-uploaded each step).
    Check(app->GetBodyState(b1).linear_velocity.z < -0.2f, "velocity field not re-uploaded on later steps");

    // Force persists until caller zeroes it: teleport b0, clear velocity, apply
    // an upward force strong enough to overcome gravity.
    const glm::vec3 p0_f(0.0f, 0.0f, 10.0f);
    app->SetBodyValue(b0, BodyField::Position, glm::vec4(p0_f, 1.0f));
    app->SetBodyValue(b0, BodyField::LinearVelocity, glm::vec4(0.0f));
    const glm::vec3 up(0.0f, 0.0f, 20.0f);
    app->SetBodyValue(b0, BodyField::ExternalForce, glm::vec4(up, 0.0f));
    for (int i = 0; i < 40; ++i) {
        app->Step();
    }
    const float peak_z = app->GetBodyState(b0).position.z;
    Check(peak_z > p0_f.z + 0.5f, "persistent force lifts body against gravity");

    // Zero the force (and reset velocity) — caller-managed lifetime, no solver clearing.
    app->SetBodyValue(b0, BodyField::ExternalForce, glm::vec4(0.0f));
    app->SetBodyValue(b0, BodyField::LinearVelocity, glm::vec4(0.0f));
    for (int i = 0; i < 20; ++i) {
        app->Step();
    }
    const float after_zero_z = app->GetBodyState(b0).position.z;
    Check(after_zero_z < peak_z - 0.3f, "zeroed force lets body fall again (caller-managed lifetime)");

    // ── Whole-field upload does not disturb unwritten bodies ────────────
    // The Position upload covers every slot of the field, so it must carry the
    // untouched bodies' own simulated values: a zero-seeded upload would teleport
    // b0 and b2 to the origin here.
    app->Step();
    const glm::vec3 b0_before = app->GetBodyState(b0).position;
    const glm::vec3 b2_before = app->GetBodyState(b2).position;
    const glm::vec3 b1_target(9.0f, 9.0f, 9.0f);
    app->SetBodyValue(b1, BodyField::Position, glm::vec4(b1_target, 0.0f));
    app->Step();
    Check(CloseV(app->GetBodyState(b1).position, b1_target, 0.5f), "written body lands at the written position");
    const glm::vec3 b0_after = app->GetBodyState(b0).position;
    const glm::vec3 b2_after = app->GetBodyState(b2).position;
    Check(glm::length(b0_after - b0_before) < 0.5f, "unwritten body b0 continues from its own simulated state");
    Check(glm::length(b2_after - b2_before) < 0.5f, "unwritten body b2 continues from its own simulated state");
    Check(glm::length(b0_after) > 1.0f, "unwritten body b0 is not teleported to the origin");
    Check(glm::length(b2_after) > 1.0f, "unwritten body b2 is not teleported to the origin");

    // ── Joint and actuator reads see a pose write immediately ───────────
    const glm::vec3 parent_before = app->GetBodyState(hinge_parent).position;
    const float angle_before = app->GetJointState(hinge_joint).angle;
    const float written_angle = 0.4f;
    const glm::quat q_written = glm::angleAxis(written_angle, hinge_axis);
    app->SetBodyValue(hinge_child, BodyField::Rotation, glm::vec4(q_written.x, q_written.y, q_written.z, q_written.w));
    const float angle_after = app->GetJointState(hinge_joint).angle;
    Check(
        std::fabs(angle_after - angle_before - written_angle) < 0.05f,
        "GetJointState reflects the written pose before Step"
    );
    Check(
        CloseV(app->GetBodyState(hinge_parent).position, parent_before, 1e-4f),
        "writing the child's rotation leaves the parent body untouched"
    );

    // The actuator measures the joint at the start of the next step, so it must
    // see the written pose rather than the pre-write one.
    app->Step();
    Check(
        std::fabs(measuring->last_angle() - (angle_before + written_angle)) < 0.05f,
        "actuator measurement uses the written pose"
    );

    if (g_failures == 0) {
        std::cout << "physics_app_physics_only_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_physics_only_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
