#include <PhysicsApp.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace AppPhysics;

// physics_app_stability_test - long-run stability probe for the XPBD GPU solver.
//
// Builds the brick pyramid from the windowed test (ground plane + n-layer stack)
// and steps it headlessly, watching for the failure mode the windowed app shows
// after a while: a body's velocity suddenly jumping by orders of magnitude.
//
// Usage: physics_app_stability_test [steps] [layers] [threshold]
//   steps      number of Step() calls (default 600)
//   layers     pyramid layers, 6 = the windowed test's original scene (default 6)
//   threshold  |v| in m/s that counts as an anomaly (default 3.0)
//
// Exit code 0 = stable for the whole run, 1 = an anomaly was detected.

namespace {
    struct Snapshot {
        uint32_t step{0};
        std::vector<glm::vec4> positions;
        std::vector<glm::vec4> velocities;
        float max_speed{0.0f};
        uint32_t max_speed_slot{0};
    };

    void AddPyramid(PhysicsApp &app, int layers, const glm::vec3 &global_offset) {
        const glm::vec3 brick_size(0.5f, 0.8f, 0.5f);
        const glm::vec3 offset = glm::vec3(5.0f, 0.7f, 0.0f) + global_offset;
        for (int i = 0; i < layers; ++i) {
            const glm::vec3 start_pos =
                glm::vec3(0.0f, -0.5f * brick_size.y * layers, brick_size.z * (i + 0.5f)) + offset;
            for (int j = 0; j < layers - i; ++j) {
                app.AddBox({
                    .position = start_pos + glm::vec3(0.0f, brick_size.y * (j + 0.5f * i), 0.0f),
                    .half_extents = brick_size * 0.5f,
                    .mass = 0.2f,
                });
            }
        }
    }

    Snapshot Take(PhysicsApp &app, uint32_t step) {
        const BodyStatesView view = app.GetBodyStates();
        Snapshot s;
        s.step = step;
        for (size_t i = 0; i < view.slot_indices.size(); ++i) {
            const uint32_t slot = view.slot_indices[i];
            s.positions.push_back(view.positions[slot]);
            s.velocities.push_back(view.linear_velocities[slot]);
            const float speed = glm::length(glm::vec3(view.linear_velocities[slot]));
            if (speed > s.max_speed) {
                s.max_speed = speed;
                s.max_speed_slot = slot;
            }
        }
        return s;
    }

    void PrintSnapshot(const Snapshot &s) {
        std::cout << "--- step " << s.step << "  max|v| " << s.max_speed << " (slot " << s.max_speed_slot << ") ---\n";
        for (size_t i = 0; i < s.positions.size(); ++i) {
            const glm::vec4 &p = s.positions[i];
            const glm::vec4 &v = s.velocities[i];
            if (i == 0u || glm::length(glm::vec3(v)) > 0.5f || glm::length(glm::vec3(p) - glm::vec3(5.0f, 5.0f, 0.0f)) > 3.0f) {
                std::cout << "  slot " << i << "  p(" << p.x << ", " << p.y << ", " << p.z << ")  v(" << v.x << ", "
                          << v.y << ", " << v.z << ")\n";
            }
        }
    }
} // namespace

int main(int argc, char **argv) {
    uint32_t steps = 600;
    int layers = 6;
    float threshold = 3.0f;
    if (argc > 1) steps = static_cast<uint32_t>(std::stoul(argv[1]));
    if (argc > 2) layers = std::stoi(argv[2]);
    if (argc > 3) threshold = std::stof(argv[3]);

    CreateInfo info{};
    info.mode = AppMode::PhysicsOnly;
    info.resol_x = 256;
    info.resol_y = 256;

    auto app = PhysicsApp::Create(info);

    // Ground plane (kinematic, large flat box in XY, thin in Z). Z is up.
    app->AddBox({
        .position = {0.0f, 0.0f, -0.5f},
        .half_extents = {100.0f, 100.0f, 0.5f},
        .mass = 0.0f,
        .kinematic = true,
    });
    AddPyramid(*app, layers, glm::vec3(0.0f, 5.0f, 0.0f));
    app->CommitScene();
    app->Resume();

    std::vector<Snapshot> history;
    Snapshot onset;
    bool found = false;

    for (uint32_t step = 1; step <= steps; ++step) {
        app->Step();
        Snapshot s = Take(*app, step);
        std::cout << "step " << step << "  max|v| " << std::fixed << std::setprecision(4) << s.max_speed << " (slot "
                  << s.max_speed_slot << ")" << std::endl;

        if (!found) {
            history.push_back(s);
            if (history.size() > 4u) history.erase(history.begin());
            if (!(s.max_speed < threshold)) {
                onset = s;
                found = true;
            }
        } else {
            break;
        }
    }

    if (!found) {
        std::cout << "physics_app_stability_test PASSED: " << steps << " steps, " << layers
                  << "-layer pyramid, final max |v| " << (history.empty() ? 0.0f : history.back().max_speed) << std::endl;
        return 0;
    }

    std::cout << "\n===== ANOMALY: max|v| reached " << onset.max_speed << " at step " << onset.step << " =====\n";
    for (const Snapshot &s : history) {
        PrintSnapshot(s);
    }
    std::cout << "physics_app_stability_test FAILED: anomaly at step " << onset.step << " (max|v| " << onset.max_speed
              << ")" << std::endl;
    return 1;
}
