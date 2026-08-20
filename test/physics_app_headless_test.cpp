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
    info.mode = AppMode::Headless;
    info.resol_x = 256;
    info.resol_y = 256;

    auto app = PhysicsApp::Create(info);

    // Reading before CommitScene throws logic_error.
    Check(ThrowsLogic([&] { (void)app->GetBodyState(0); }), "GetBodyState before commit throws");
    Check(ThrowsLogic([&] { (void)app->GetBodyStates(); }), "GetBodyStates before commit throws");

    // Render-only APIs throw in headless mode (allowed before commit too).
    Check(ThrowsLogic([&] { app->SetCameraPose({0, 0, 0}, {1, 0, 0}); }), "SetCameraPose throws headless");
    Check(ThrowsLogic([&] { app->AddDirectionalLight({}); }), "AddDirectionalLight throws headless");

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

    // Render-readback APIs throw in headless mode.
    Check(ThrowsLogic([&] { app->RenderNextFrame(); }), "RenderNextFrame throws headless");
    Check(ThrowsLogic([&] { (void)app->GetRenderOutput(); }), "GetRenderOutput throws headless");
    Check(ThrowsLogic([&] { app->SetRenderReadbackEnabled(true); }), "SetRenderReadbackEnabled(true) throws headless");

    // Advance physics; the dynamic body falls under gravity (Z-down). The
    // simulation starts paused (SetSimulationEnabled(false)), so resume first.
    app->Resume();
    app->Step();
    BodyState after = app->GetBodyState(b0);
    std::cout << "box0.z: " << box0.z << " ---> " << after.position.z << std::endl;
    Check(after.position.z < box0.z, "position falls under gravity after Step");
    Check(!app->ShouldQuit(), "ShouldQuit false in headless mode");

    if (g_failures == 0) {
        std::cout << "physics_app_headless_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_headless_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
