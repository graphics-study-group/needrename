#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
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

    // Build a scene with two dynamic bodies.
    const glm::vec3 box0(0.0f, 0.0f, 2.0f);
    const glm::vec3 sphere1(1.0f, 0.0f, 3.0f);
    BodyId b0 = app->AddBox({.position = box0, .half_extents = {0.5f, 0.5f, 0.5f}, .mass = 1.0f});
    BodyId b1 = app->AddSphere({.position = sphere1, .radius = 0.5f, .mass = 1.0f});

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
    auto view = app->GetBodyStates();
    Check(view.slot_indices.size() == 2, "slot_indices size == body count");
    Check(view.slot_indices[0] != view.slot_indices[1], "slot indices unique");
    Check(view.com_offsets.size() >= 2, "com_offsets sized to slots");
    Check(
        view.positions.size() >= 2 && view.rotations.size() >= 2 && view.linear_velocities.size() >= 2
            && view.angular_velocities.size() >= 2,
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

    // Position teleport applies on the next Step, then does NOT snap back.
    const glm::vec3 tp(2.0f, 3.0f, 8.0f);
    app->SetBodyValue(b1, BodyField::Position, glm::vec4(tp, 1.0f));
    Check(!CloseV(app->GetBodyState(b1).position, tp, 0.5f), "Set position not visible before Step");
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

    if (g_failures == 0) {
        std::cout << "physics_app_physics_only_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_physics_only_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
