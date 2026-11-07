#include <Core/FileSystem/DataRoot.h>
#include <PhysicsApp.h>

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

using namespace AppPhysics;

namespace {
    constexpr uint32_t kScreenWidth = 1280;
    constexpr uint32_t kScreenHeight = 720;
    constexpr uint32_t kLogEverySteps = 60;
    constexpr float kPhysicsTimeStep = 0.005f;

    // ── Tunable actuator parameters (edit these to tune standing) ────
    constexpr float kKp = 25.0f;
    constexpr float kKd = 0.5f;
    constexpr float kStallTorque = 33.5f;
    constexpr float kNoLoadSpeed = 21.0f;
    constexpr float kContTorque = 13.4f;
    constexpr float kGearRatio = 1.0f;

    // A1 default standing joint targets (hip ±0.1, thigh 0.8/1.0, calf −1.5).
    const std::map<std::string, float> kDefaultJointPos = {
        {"FR_hip_joint", -0.1f},
        {"FR_thigh_joint", 0.8f},
        {"FR_calf_joint", -1.5f},
        {"FL_hip_joint", 0.1f},
        {"FL_thigh_joint", 0.8f},
        {"FL_calf_joint", -1.5f},
        {"RR_hip_joint", -0.1f},
        {"RR_thigh_joint", 1.0f},
        {"RR_calf_joint", -1.5f},
        {"RL_hip_joint", 0.1f},
        {"RL_thigh_joint", 1.0f},
        {"RL_calf_joint", -1.5f},
    };
} // namespace

int main(int argc, char **argv) {
    // Finite frame count (argv[1]) = ctest regression run; no arg = interactive,
    // starts paused (SPACE resumes) for manual tuning.
    const bool finite = argc > 1;
    const uint32_t frame_count = finite ? static_cast<uint32_t>(std::stoul(argv[1])) : 0;

    CreateInfo info{};
    info.mode = AppMode::Windowed;
    info.resol_x = kScreenWidth;
    info.resol_y = kScreenHeight;
    info.window_title = "Physics App (A1 actuator stand)";
    // The low-inertia leg links make 60 Hz zero-order-held PD control unstable.
    info.physics_time_step = kPhysicsTimeStep;

    auto app = PhysicsApp::Create(info);

    // Ground plane (kinematic, large flat box in XY, thin in Z; Z is up).
    app->AddBox({
        .position = {0.0f, 0.0f, -0.5f},
        .half_extents = {100.0f, 100.0f, 0.5f},
        .mass = 0.0f,
        .kinematic = true,
        .color = "white",
    });

    // A1 robot just above the ground, in its default (zero-pose) configuration.
    const std::filesystem::path urdf = Engine::AssetsDir() / "a1_description/urdf/a1.urdf";
    UrdfImportConfig cfg;
    cfg.urdf_path = urdf;
    cfg.position = {0.0f, 0.0f, 0.85f};
    cfg.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    cfg.static_friction = 0.5f;
    cfg.dynamic_friction = 0.5f;
    cfg.restitution = 0.0f;
    UrdfImportResult res = app->LoadUrdf(cfg);

    // Register a DCMotor actuator on each leg joint and set its target.
    for (const auto &[name, target] : kDefaultJointPos) {
        const auto it = res.joint_bodies.find(name);
        if (it == res.joint_bodies.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "A1 stand: joint '%s' not found in loaded robot.", name.c_str());
            return 1;
        }
        const JointId jid = it->second.id;
        app->AddActuator(
            jid, std::make_shared<DcMotorActuator>(kKp, kKd, kStallTorque, kNoLoadSpeed, kContTorque, kGearRatio)
        );
        app->SetTargetAngle(jid, target);
    }

    app->AddDirectionalLight({.direction = {0.0f, 0.0f, -1.0f}, .intensity = 1.5f, .cast_shadow = true});
    app->SetCameraPose(glm::vec3(2.5f, -2.5f, 2.0f), glm::vec3(0.0f, 0.0f, 0.5f));

    app->CommitScene();

    if (finite) {
        app->Resume();
    }

    const bool check_standing = finite && frame_count >= 600;
    float min_trunk_up_z = 1.0f;
    float settle_peak_joint_speed = 0.0f;
    uint32_t steps = 0;
    uint32_t frames = 0;
    while (!app->ShouldQuit()) {
        if (!app->IsPaused()) {
            app->Step();
            ++steps;
            if (check_standing) {
                const glm::quat q = app->GetBodyState(res.link_bodies.at("trunk")).rotation;
                const float up_z = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
                min_trunk_up_z = std::min(min_trunk_up_z, up_z);
                if (steps > frame_count - 60) {
                    for (const auto &[name, target] : kDefaultJointPos) {
                        const JointState st = app->GetJointState(res.joint_bodies.at(name).id);
                        settle_peak_joint_speed =
                            std::max(settle_peak_joint_speed, std::fabs(st.angular_velocity));
                    }
                }
            }
            if (kLogEverySteps > 0 && (steps % kLogEverySteps == 0)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "--- step %u ---", steps);
                for (const auto &[name, target] : kDefaultJointPos) {
                    const JointState st = app->GetJointState(res.joint_bodies.at(name).id);
                    SDL_LogInfo(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "  %-16s target %+.3f  q %+.3f  err %+.3f",
                        name.c_str(),
                        target,
                        st.angle,
                        st.angle - target
                    );
                }
            }
        }
        app->RenderNextFrame();
        if (finite && ++frames >= frame_count) {
            break;
        }
    }

    if (check_standing) {
        float max_joint_error = 0.0f;
        for (const auto &[name, target] : kDefaultJointPos) {
            const float angle = app->GetJointState(res.joint_bodies.at(name).id).angle;
            max_joint_error = std::max(max_joint_error, std::fabs(angle - target));
        }
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "A1 stand: min up_z=%+.3f, settle peak speed=%.3f, max joint error=%.3f",
            min_trunk_up_z,
            settle_peak_joint_speed,
            max_joint_error
        );
        if (min_trunk_up_z < 0.0f || settle_peak_joint_speed > 2.0f || max_joint_error > 0.5f) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "A1 stand: robot did not stay upright and settle at its targets.");
            return 1;
        }
    }

    return 0;
}
