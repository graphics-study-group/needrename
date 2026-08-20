#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdlib>
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

    // AddTemplateScene(*app, glm::vec3(0.0f, 5.0f, 0.0f));
    AddTemplateScene2(*app, glm::vec3(0.0f, 0.0f, 0.0f));

    app->AddDirectionalLight({.direction = {0.0f, 0.0f, -1.0f}, .intensity = 1.5f, .cast_shadow = true});
    app->AddDirectionalLight({.direction = {1.0f, 1.0f, -1.0f}, .intensity = 1.0f, .cast_shadow = true});

    app->SetCameraPose(glm::vec3(6.0f, -5.0f, 4.0f), glm::vec3(0.0f, 0.0f, 3.0f));

    app->CommitScene();

    if (finite) {
        app->Resume();
    }

    uint32_t frames = 0;
    while (!app->ShouldQuit()) {
        app->Step();
        app->RenderNextFrame();
        if (finite && ++frames >= frame_count) {
            break;
        }
    }

    return 0;
}
