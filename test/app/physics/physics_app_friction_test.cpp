// physics_app_friction_test.cpp — Contact friction in the real application
// configuration.
//
// `test/engine/headless/xpbd_contact_friction_test.cpp` drives a bare
// `PhysicsScene` + `XpbdGpuSolver` with its own configuration.  This test runs
// the same two scenarios through `PhysicsApp` in `AppMode::PhysicsOnly`, so the
// app's own configuration (50 position iterations, 10 velocity iterations,
// 2 substeps per step) and its own scene builder are what is under test:
//
//   * a box on a slope below the static friction limit must not creep, and
//   * a box on a slope above it must slide away.
//
// State is read back through `GetBodyState`, the app's public readback path.

#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cmath>
#include <iostream>

using namespace AppPhysics;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    constexpr float kMu = 0.5f;
    constexpr float kBoxHalf = 0.5f;

    /// A kinematic ramp tilted by `degrees` about Y whose top surface contains
    /// `anchor`, plus a box resting flat on it (1 mm into it, so the pair is in
    /// contact on the first substep).  Returns the box's BodyId.
    BodyId AddRampAndBox(PhysicsApp &app, const glm::vec3 &anchor, float degrees) {
        const glm::quat q = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));

        app.AddBox({
            .position = anchor + q * glm::vec3(0.0f, 0.0f, -0.2f),
            .rotation = q,
            .half_extents = {3.0f, 1.0f, 0.2f},
            .mass = 0.0f,
            .kinematic = true,
            .static_friction = kMu,
            .dynamic_friction = kMu,
            .color = "cyan",
        });

        return app.AddBox({
            .position = anchor + q * glm::vec3(0.0f, 0.0f, kBoxHalf - 0.001f),
            .rotation = q,
            .half_extents = {kBoxHalf, kBoxHalf, kBoxHalf},
            .mass = 1.0f,
            .static_friction = kMu,
            .dynamic_friction = kMu,
            .color = "red",
        });
    }

    /// Motion along the down-slope direction of a ramp tilted by `degrees`.
    float DownSlopeDistance(const glm::vec3 &from, const glm::vec3 &to, float degrees) {
        const glm::quat q = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::dot(to - from, q * glm::vec3(1.0f, 0.0f, 0.0f));
    }
} // namespace

int main() {
    CreateInfo info{};
    info.mode = AppMode::PhysicsOnly;
    info.resol_x = 256;
    info.resol_y = 256;

    auto app = PhysicsApp::Create(info);

    // Two ramps in one scene, far enough apart not to interact.  The analytic
    // static friction limit is atan(mu) = 26.57 degrees.
    constexpr float kHoldDegrees = 20.0f;
    constexpr float kSlideDegrees = 35.0f;
    const BodyId hold_box = AddRampAndBox(*app, glm::vec3(0.0f, 0.0f, 3.0f), kHoldDegrees);
    const BodyId slide_box = AddRampAndBox(*app, glm::vec3(0.0f, 20.0f, 3.0f), kSlideDegrees);

    app->CommitScene();

    // Settle: the initial millimetre of penetration relaxes during the first
    // few substeps, so the no-creep measurement starts once the box is at rest.
    for (int i = 0; i < 120; ++i) {
        app->Step();
    }
    const glm::vec3 hold_start = app->GetBodyState(hold_box).position;
    const glm::vec3 slide_start = app->GetBodyState(slide_box).position;

    for (int i = 0; i < 600; ++i) {
        app->Step();
    }

    const glm::vec3 hold_end = app->GetBodyState(hold_box).position;
    const glm::vec3 slide_end = app->GetBodyState(slide_box).position;

    const float hold_creep = glm::length(hold_end - hold_start);
    const float hold_creep_down_slope = DownSlopeDistance(hold_start, hold_end, kHoldDegrees);
    const float slide_distance = glm::length(slide_end - slide_start);
    const float slide_distance_down_slope = DownSlopeDistance(slide_start, slide_end, kSlideDegrees);

    std::cout << "app friction (mu = " << kMu << ", analytic limit " << glm::degrees(std::atan(kMu)) << " deg)"
              << std::endl;
    std::cout << "  " << kHoldDegrees << " deg ramp: moved " << hold_creep << " m (down-slope " << hold_creep_down_slope
              << " m) over 600 substeps" << std::endl;
    std::cout << "  " << kSlideDegrees << " deg ramp: moved " << slide_distance << " m (down-slope "
              << slide_distance_down_slope << " m) over 600 substeps, final speed "
              << glm::length(app->GetBodyState(slide_box).linear_velocity) << " m/s" << std::endl;

    Check(hold_creep < 1e-3f, "a box below the static limit does not creep (less than 1 mm over 600 substeps)");
    Check(slide_distance > 0.1f, "a box above the static limit slides away");

    if (g_failures == 0) {
        std::cout << "physics_app_friction_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_friction_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
