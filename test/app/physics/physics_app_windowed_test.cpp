#include <Core/FileSystem/DataRoot.h>
#include <PhysicsApp.h>

#include <cassert>
#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace AppPhysics;

namespace {
    constexpr uint32_t kScreenWidth = 1024;
    constexpr uint32_t kScreenHeight = 768;

    // ── Scene builders relocated from example/physics_example ───────────
    void AddDoublePendulum(PhysicsApp &app, const glm::vec3 &anchor) {
        constexpr float kSphereRadius = 0.1f;
        constexpr float kBoxHalfX = 0.05f;
        constexpr float kBoxHalfY = 0.05f;
        constexpr float kBoxHalfZ = 0.6f;
        constexpr float kCylRadius = 0.2f;
        constexpr float kCylHalfH = 0.5f;
        constexpr float kGap = 0.1f;
        constexpr glm::vec3 kHingeAxis(0.0f, 1.0f, 0.0f);

        BodyId sphere = app.AddSphere({
            .position = anchor,
            .radius = kSphereRadius,
            .mass = 0.0f,
            .kinematic = true,
            .color = "white",
        });

        float box1_x = anchor.x - kSphereRadius - kGap - kBoxHalfZ;
        BodyId box1 = app.AddBox({
            .position = glm::vec3(box1_x, anchor.y, anchor.z),
            .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            .half_extents = glm::vec3(kBoxHalfX, kBoxHalfY, kBoxHalfZ),
            .mass = 1.0f,
            .color = "red",
        });

        float box2_x = box1_x - kBoxHalfZ - kGap - kBoxHalfZ;
        BodyId box2 = app.AddBox({
            .position = glm::vec3(box2_x, anchor.y, anchor.z),
            .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            .half_extents = glm::vec3(kBoxHalfX, kBoxHalfY, kBoxHalfZ),
            .mass = 1.0f,
            .color = "green",
        });

        float cyl_x = box2_x - kBoxHalfZ - kGap - kCylRadius;
        BodyId cylinder = app.AddCylinder({
            .position = glm::vec3(cyl_x, anchor.y, anchor.z),
            .radius = kCylRadius,
            .half_height = kCylHalfH,
            .mass = 5.0f,
            .color = "blue",
        });

        app.AddHingeJoint(sphere, box1, {.axis_obj1 = kHingeAxis, .anchor_obj1 = {0.0f, 0.0f, 0.0f}});
        app.AddHingeJoint(box1, box2, {.axis_obj1 = kHingeAxis, .anchor_obj1 = {0.0f, 0.0f, -kBoxHalfZ - 0.5f * kGap}});
        app.AddFixedJoint(box2, cylinder, {.compliance = 0.0f});
    }

    // ── URDF robot scene (A1) ────────────────────────────────────────
    void AddUrdfRobotScene(PhysicsApp &app, glm::vec3 global_position, glm::quat global_rotation) {
        const std::filesystem::path urdf = Engine::AssetsDir() / "a1_description/urdf/a1.urdf";

        UrdfImportConfig cfg;
        cfg.urdf_path = urdf;
        cfg.position = global_position;
        cfg.rotation = global_rotation;
        cfg.static_friction = 0.5f;
        cfg.dynamic_friction = 0.5f;
        cfg.restitution = 0.0f;

        UrdfImportResult res = app.LoadUrdf(cfg);

        // Links with <inertial> become bodies.
        assert(res.link_bodies.count("trunk") == 1);
        assert(res.link_bodies.count("FR_thigh") == 1);
        // Links without <inertial> are absent.
        assert(res.link_bodies.count("base") == 0);
        assert(res.link_bodies.count("FR_thigh_shoulder") == 0);

        // Joints with bodies on both ends appear, in URDF parent/child order.
        assert(res.joint_bodies.count("FR_thigh_joint") == 1);
        assert(res.joint_bodies["FR_thigh_joint"].parent == res.link_bodies["FR_hip"]);
        assert(res.joint_bodies["FR_thigh_joint"].child == res.link_bodies["FR_thigh"]);
        // Joints with a bodyless end are absent.
        assert(res.joint_bodies.count("floating_base") == 0);
    }

    void AddTemplateScene(PhysicsApp &app, const glm::vec3 &global_offset) {
        app.AddBox({
            .position = glm::vec3(0.0f, 0.0f, 0.49f) + global_offset,
            .half_extents = {0.5f, 0.5f, 0.5f},
            .mass = 1.0f,
            .color = "red",
        });
        app.AddBox({
            .position = glm::vec3(0.0f, -0.7f, 2.0f) + global_offset,
            .rotation = glm::angleAxis(glm::radians(44.0f), glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))),
            .half_extents = {0.2f, 0.5f, 0.2f},
            .mass = 1.0f,
            .color = "green",
        });
        app.AddSphere(
            {.position = glm::vec3(1.2f, 0.0f, 7.0f) + global_offset, .radius = 0.5f, .mass = 1.0f, .color = "green"}
        );
        app.AddCylinder(
            {.position = glm::vec3(0.0f, 1.2f, 6.0f) + global_offset,
             .radius = 0.2f,
             .half_height = 1.0f,
             .mass = 1.0f,
             .color = "blue"}
        );
        app.AddSphere(
            {.position = glm::vec3(-1.0f, -0.5f, 8.0f) + global_offset, .radius = 0.4f, .mass = 2.0f, .color = "yellow"}
        );
        app.AddCylinder(
            {.position = glm::vec3(2.0f, -1.0f, 9.0f) + global_offset,
             .radius = 0.8f,
             .half_height = 0.3f,
             .mass = 0.5f,
             .color = "cyan"}
        );
        app.AddCylinder({
            .position = glm::vec3(-2.0f, 1.0f, 10.0f) + global_offset,
            .rotation = glm::angleAxis(glm::radians(90.0f), glm::normalize(glm::vec3(0.5f, 0.0f, 1.0f))),
            .radius = 0.3f,
            .half_height = 0.6f,
            .mass = 0.3f,
            .color = "magenta",
        });
        app.AddBox({
            .position = glm::vec3(0.5f, 2.0f, 4.0f) + global_offset,
            .rotation = glm::angleAxis(glm::radians(25.0f), glm::normalize(glm::vec3(0.3f, 0.2f, 0.7f))),
            .half_extents = {0.5f, 0.5f, 0.5f},
            .mass = 1.5f,
            .color = "orange",
        });

        constexpr int n = 6;
        const glm::vec3 brick_size(0.5f, 0.8f, 0.5f);
        const glm::vec3 offset = glm::vec3(5.0f, 0.7f, 0.0f) + global_offset;
        for (int i = 0; i < n; ++i) {
            const glm::vec3 start_pos = glm::vec3(0.0f, -0.5f * brick_size.y * n, brick_size.z * (i + 0.5f)) + offset;
            for (int j = 0; j < n - i; ++j) {
                app.AddBox({
                    .position = start_pos + glm::vec3(0.0f, brick_size.y * (j + 0.5f * i), 0.0f),
                    .half_extents = brick_size * 0.5f,
                    .mass = 0.2f,
                    .color = "blue",
                });
            }
        }

        AddDoublePendulum(app, glm::vec3(6.5f, 0.0f, 4.5f) + global_offset);
        AddUrdfRobotScene(app, glm::vec3(6.0f, 0.0f, 1.5f) + global_offset, glm::quat(0.0, 0.0, 0.0, 1.0));
    }

    // ── Contact friction debugging scene ──────────────────────────────
    // A row of kinematic ramps at angles around the static friction limit
    // `tan(theta) = mu_s`, each carrying an identical box placed at rest on it.
    // Both friction coefficients of the ramp and of the box are the same value,
    // so the analytic hold limit of a row with `mu` is `atan(mu)` degrees: the
    // boxes below it hold, the ones above it slide away.  This is the visual
    // counterpart of `test/engine/headless/xpbd_contact_friction_test.cpp`,
    // whose Scenario E measures the same transition numerically.
    //
    // Run the executable directly (no frame-count argument) and press SPACE to
    // resume; every ramp is labelled by its angle in the console banner.
    void AddFrictionRamp(PhysicsApp &app, const glm::vec3 &anchor, float degrees, float mu) {
        constexpr float kRampHalfThickness = 0.2f;
        constexpr float kBoxHalf = 0.5f;

        // A rotation about +Y tips the ramp's top surface down towards +x, so the
        // boxes slide towards +x.
        const glm::quat q = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));

        // The ramp is placed so that its top surface contains `anchor`.
        app.AddBox({
            .position = anchor + q * glm::vec3(0.0f, 0.0f, -kRampHalfThickness),
            .rotation = q,
            .half_extents = {2.5f, 1.0f, kRampHalfThickness},
            .mass = 0.0f,
            .kinematic = true,
            .static_friction = mu,
            .dynamic_friction = mu,
            .color = "cyan",
        });

        // The box rests flat on that surface, 1 mm into it so the pair is in
        // contact on the very first substep.
        app.AddBox({
            .position = anchor + q * glm::vec3(0.0f, 0.0f, kBoxHalf - 0.001f),
            .rotation = q,
            .half_extents = {kBoxHalf, kBoxHalf, kBoxHalf},
            .mass = 1.0f,
            .static_friction = mu,
            .dynamic_friction = mu,
            .color = "red",
        });
    }

    void AddFrictionSlopeScene(PhysicsApp &app, const glm::vec3 &global_offset, float mu) {
        const float kAngles[] = {20.0f, 26.0f, 27.0f, 28.0f, 29.0f, 40.0f, 60.0f};
        const uint32_t kCount = static_cast<uint32_t>(sizeof(kAngles) / sizeof(kAngles[0]));

        std::cout << "friction slope scene: mu = " << mu << ", analytic hold limit " << glm::degrees(std::atan(mu))
                  << " deg" << std::endl;
        for (uint32_t i = 0; i < kCount; ++i) {
            AddFrictionRamp(app, global_offset + glm::vec3(0.0f, static_cast<float>(i) * 3.0f, 3.0f), kAngles[i], mu);
            std::cout << "  ramp " << i << " at y = " << (global_offset.y + static_cast<float>(i) * 3.0f) << ": "
                      << kAngles[i] << " deg" << std::endl;
        }
    }

    void AddTemplateScene2(PhysicsApp &app, const glm::vec3 &global_offset) {
        constexpr float kWallHalfHeight = 3.0f;
        constexpr float kWallHalfSize = 15.0f;

        app.AddBox({
            .position = glm::vec3(-kWallHalfSize, 0.0f, kWallHalfHeight) + global_offset,
            .half_extents = {0.5f, kWallHalfSize, kWallHalfHeight},
            .mass = 1.0f,
            .kinematic = true,
            .color = "blue",
        });
        app.AddBox({
            .position = glm::vec3(kWallHalfSize, 0.0f, kWallHalfHeight) + global_offset,
            .half_extents = {0.5f, kWallHalfSize, kWallHalfHeight},
            .mass = 1.0f,
            .kinematic = true,
            .color = "blue",
        });
        app.AddBox({
            .position = glm::vec3(0.0f, -kWallHalfSize, kWallHalfHeight) + global_offset,
            .half_extents = {kWallHalfSize, 0.5f, kWallHalfHeight},
            .mass = 1.0f,
            .kinematic = true,
            .color = "blue",
        });
        app.AddBox({
            .position = glm::vec3(0.0f, kWallHalfSize, kWallHalfHeight) + global_offset,
            .half_extents = {kWallHalfSize, 0.5f, kWallHalfHeight},
            .mass = 1.0f,
            .kinematic = true,
            .color = "blue",
        });

        constexpr int n = 9;
        constexpr int m = 15;
        constexpr float offset = 2.2f;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                for (int k = 0; k < m; k++) {
                    int type = rand() % 3;
                    glm::vec3 pos =
                        glm::vec3((-(n + 1) / 2 + i) * offset, (-(n + 1) / 2 + j) * offset, ((n + 1) / 2 + k) * offset)
                        + global_offset;
                    glm::quat rot = glm::angleAxis(
                        glm::radians((float)(rand() % 360)),
                        glm::normalize(glm::vec3(rand() % 100, rand() % 100, rand() % 100))
                    );
                    switch (type) {
                    case 0:
                        app.AddBox(
                            {.position = pos,
                             .rotation = rot,
                             .half_extents = {0.5f, 0.7f, 0.5f},
                             .mass = 1.0f,
                             .color = "red"}
                        );
                        break;
                    case 1:
                        app.AddSphere(
                            {.position = pos, .rotation = rot, .radius = 0.5f, .mass = 1.0f, .color = "green"}
                        );
                        break;
                    default:
                        app.AddCylinder(
                            {.position = pos,
                             .rotation = rot,
                             .radius = 0.5f,
                             .half_height = 0.5f,
                             .mass = 1.0f,
                             .color = "blue"}
                        );
                        break;
                    }
                }
            }
        }
    }
} // namespace

int main(int argc, char **argv) {
    // ctest path: a trailing frame-count runs that many frames then exits and
    // auto-resumes after commit. Manual path (no arg): runs indefinitely,
    // starting paused (SPACE resumes) — the former example UX.
    bool finite = argc > 1;
    uint32_t frame_count = finite ? static_cast<uint32_t>(std::stoul(argv[1])) : 0;

    CreateInfo info{};
    info.mode = AppMode::Windowed;
    info.resol_x = kScreenWidth;
    info.resol_y = kScreenHeight;
    info.window_title = "Physics App (windowed test)";

    auto app = PhysicsApp::Create(info);

    // Ground plane (kinematic, large flat box in XY, thin in Z). Z is up.
    app->AddBox({
        .position = {0.0f, 0.0f, -0.5f},
        .half_extents = {100.0f, 100.0f, 0.5f},
        .mass = 0.0f,
        .kinematic = true,
        .color = "white",
    });

    AddTemplateScene(*app, glm::vec3(0.0f, 5.0f, 0.0f));
    // AddTemplateScene2(*app, glm::vec3(0.0f, 0.0f, 0.0f));

    // Contact friction debugging scene: see AddFrictionSlopeScene.  It sits clear
    // of the template scene (which lives around y = 5) and the camera below
    // frames it.
    AddFrictionSlopeScene(*app, glm::vec3(0.0f, 18.0f, 0.0f), /*mu=*/0.5f);

    app->AddDirectionalLight({.direction = {0.0f, 0.0f, -1.0f}, .intensity = 1.0f, .cast_shadow = true});
    app->AddDirectionalLight({.direction = {1.0f, 1.0f, -1.0f}, .intensity = 0.7f, .cast_shadow = true});

    app->SetCameraPose(glm::vec3(15.0f, 27.0f, 11.0f), glm::vec3(0.0f, 27.0f, 3.0f));

    app->CommitScene();

    if (finite) {
        app->Resume();
    }

    uint32_t frames = 0;
    while (!app->ShouldQuit()) {
        if (!app->IsPaused()) {
            app->Step();
        }
        app->RenderNextFrame();
        if (finite && ++frames >= frame_count) {
            break;
        }
    }

    return 0;
}
