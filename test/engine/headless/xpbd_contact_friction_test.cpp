// xpbd_contact_friction_test.cpp — Contact friction coverage for the XPBD GPU
// solver: the analytic per-substep tangential velocity reduction, the
// mass / velocity-iteration / sliding-direction invariances, the stick-slip
// threshold of the position phase's static friction, and the detector's
// published contact count under a binding contact budget.
//
// The fixture drives a `PhysicsScene` with `XpbdGpuSolver` directly (no
// `PhysicsApp`), the same way `xpbd_reduce_capacity_test.cpp` does, so the
// scene's bodies, coefficients and the solver's configuration are all under the
// test's control.  One step is one substep here (`num_substep_perstep = 1`), so
// "per substep" and "per step" coincide and every measurement is a substep
// measurement.

#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSystem.h>
#include <Physics/Solver/XPBDGpuSolver.h>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Submission/SubmissionHelper.h>

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using namespace Engine;

namespace {

    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    void Info(const char *what) {
        std::cout << "  " << what << std::endl;
    }

    constexpr uint32_t kMaxBodies = 32u;
    constexpr float kGravity = 9.81f;
    constexpr float kTimeStep = 1.0f / 60.0f;
    constexpr uint32_t kSubsteps = 1u;
    constexpr float kDt = kTimeStep / static_cast<float>(kSubsteps);

    /// A box's solid-inertia descriptor, matching the fixture's box convention.
    void FillBoxInertia(RigidBodyComDescriptor &rb, glm::vec3 half, float mass) {
        const float ex = 2.0f * half.x, ey = 2.0f * half.y, ez = 2.0f * half.z;
        const float ixx = mass * (ey * ey + ez * ez) / 12.0f;
        const float iyy = mass * (ex * ex + ez * ez) / 12.0f;
        const float izz = mass * (ex * ex + ey * ey) / 12.0f;
        rb.inertia = glm::mat4(
            glm::vec4(ixx, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, iyy, 0.0f, 0.0f),
            glm::vec4(0.0f, 0.0f, izz, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
        const float inv_x = (ixx > 1e-12f) ? (1.0f / ixx) : 0.0f;
        const float inv_y = (iyy > 1e-12f) ? (1.0f / iyy) : 0.0f;
        const float inv_z = (izz > 1e-12f) ? (1.0f / izz) : 0.0f;
        rb.inverse_inertia = glm::mat4(
            glm::vec4(inv_x, 0.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, inv_y, 0.0f, 0.0f),
            glm::vec4(0.0f, 0.0f, inv_z, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    /// Quaternion for a rotation of `angle` about the world Y axis.
    glm::vec4 RotationAboutY(float angle) {
        return glm::vec4(0.0f, std::sin(0.5f * angle), 0.0f, std::cos(0.5f * angle));
    }

    /// Rotate `v` by the quaternion `q` (xyzw).
    glm::vec3 Rotate(const glm::vec4 &q, const glm::vec3 &v) {
        const glm::vec3 t = 2.0f * glm::cross(glm::vec3(q), v);
        return v + q.w * t + glm::cross(glm::vec3(q), t);
    }

    /**
     * @brief Headless `PhysicsScene` + `XpbdGpuSolver` harness.
     *
     * Owns its scene's submission helper and host-visible readback staging, and
     * mirrors the app's per-step order: upload the scene, record the solver into
     * one command buffer (with the resulting state copied back on the same
     * submission), then submit and wait.
     */
    struct Fixture {
        RenderSystem &rsys;
        PhysicsSystem &physics;
        PhysicsScene &scene;
        XpbdGpuSolver *solver = nullptr;
        std::unique_ptr<Rhi::SubmissionHelper> submission;
        std::unique_ptr<Rhi::DeviceBuffer> pos_staging;
        std::unique_ptr<Rhi::DeviceBuffer> linvel_staging;
        std::unique_ptr<Rhi::DeviceBuffer> angvel_staging;

        std::vector<RigidBodyComDescriptor> bodies;
        uint32_t shape_slots = 0u;
        uint32_t stepped_body_count = 0u;
        bool uploaded = false;

        Fixture(RenderSystem &r, PhysicsSystem &p, uint32_t id, const XpbdConfig &config) :
            rsys(r), physics(p), scene(p.CreateScene(id)) {
            scene.SetSimulationEnabled(true);
            submission = std::make_unique<Rhi::SubmissionHelper>(
                rsys.GetDeviceContext().GetDeviceInterface(),
                rsys.GetDeviceContext().GetAllocatorState(),
                rsys.GetDeviceContext().GetEpochTracker()
            );
            auto make_staging = [this](const char *name) {
                return Rhi::DeviceBuffer::CreateUnique(
                    rsys.GetAllocatorState(),
                    Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice},
                    static_cast<size_t>(kMaxBodies) * sizeof(glm::vec4),
                    name
                );
            };
            pos_staging = make_staging("XPBD friction pos");
            linvel_staging = make_staging("XPBD friction linvel");
            angvel_staging = make_staging("XPBD friction angvel");

            auto owned = std::make_unique<XpbdGpuSolver>(rsys.GetDeviceContext());
            owned->SetConfig(config);
            solver = owned.get();
            physics.RegisterSolver(id, std::move(owned));
        }

        uint32_t AddBox(
            glm::vec3 center,
            glm::vec4 rotation,
            glm::vec3 half,
            float mass,
            bool kinematic,
            float static_friction,
            float dynamic_friction,
            bool with_shape = true
        ) {
            const uint32_t body = scene.AllocateRigidBodySlot();
            RigidBodyComDescriptor rb{};
            rb.mass = mass;
            rb.is_kinematic = kinematic;
            rb.static_friction = static_friction;
            rb.dynamic_friction = dynamic_friction;
            rb.restitution = 0.0f;
            rb.center_world_position = glm::vec4(center, 1.0f);
            rb.center_world_rotation = rotation;
            FillBoxInertia(rb, half, mass);
            bodies.push_back(rb);
            scene.SubmitRigidBody(body, rb);

            if (!with_shape) {
                return body;
            }

            const uint32_t shape = scene.AllocateCollisionShapeSlot();
            CollisionShapeComDescriptor sd{};
            sd.type = 0u; // SHAPE_TYPE_BOX
            sd.feature = glm::vec4(half, 0.0f);
            sd.local_position = glm::vec4(0.0f);
            sd.local_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            sd.bound_rigid_body = body;
            scene.SubmitCollisionShape(shape, sd);
            ++shape_slots;
            return body;
        }

        void SetBodyVelocity(uint32_t body, glm::vec3 velocity, glm::vec3 angular = glm::vec3(0.0f)) {
            bodies[body].linear_velocity = glm::vec4(velocity, 0.0f);
            bodies[body].angular_velocity = glm::vec4(angular, 0.0f);
            scene.SubmitRigidBody(body, bodies[body]);
        }

        void Upload() {
            std::vector<uint32_t> filters(
                static_cast<size_t>(shape_slots) * PhysicsScene::MAX_FILTER_ENTRIES, PhysicsScene::INVALID_INDEX
            );
            if (shape_slots > 0u) {
                scene.SetShapeFilters(filters, shape_slots);
            }
            scene.SyncGpuBuffers(rsys.GetDeviceContext(), *submission);
            rsys.WaitForIdle();
            uploaded = true;

            // Populate the readback staging with the committed initial state, so
            // the first measurement is a real displacement rather than whatever
            // the freshly allocated staging buffer held.
            const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
            auto cb = rsys.GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            cb.begin(vk::CommandBufferBeginInfo{});
            RecordReadback(cb);
            cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
            rsys.WaitForIdle();
            AfterStep();
        }

        void RecordReadback(vk::CommandBuffer cb) {
            const auto gpu = scene.GetGpuBuffers();
            if (gpu.rigid_body_slot_count == 0u || gpu.rigid_body_center_world_position == nullptr) {
                return;
            }
            const size_t bytes = static_cast<size_t>(gpu.rigid_body_slot_count) * sizeof(glm::vec4);
            const vk::BufferCopy copy{0u, 0u, bytes};
            cb.copyBuffer(gpu.rigid_body_center_world_position->GetBuffer(), pos_staging->GetBuffer(), copy);
            cb.copyBuffer(gpu.rigid_body_linear_velocity->GetBuffer(), linvel_staging->GetBuffer(), copy);
            cb.copyBuffer(gpu.rigid_body_angular_velocity->GetBuffer(), angvel_staging->GetBuffer(), copy);
        }

        void AfterStep() {
            pos_staging->Invalidate();
            linvel_staging->Invalidate();
            angvel_staging->Invalidate();
            stepped_body_count = static_cast<uint32_t>(bodies.size());
        }

        void Step(uint32_t steps) {
            for (uint32_t i = 0; i < steps; ++i) {
                const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
                auto cb = rsys.GetDevice().allocateCommandBuffers(
                    vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
                )[0];
                cb.begin(vk::CommandBufferBeginInfo{});
                physics.GPUStep(cb);
                RecordReadback(cb);
                cb.end();
                queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
                rsys.WaitForIdle();
                AfterStep();
            }
        }

        glm::vec3 Position(uint32_t body) const {
            const auto *p = reinterpret_cast<const glm::vec4 *>(pos_staging->GetVMAddress());
            return glm::vec3(p[body]);
        }

        glm::vec3 LinearVelocity(uint32_t body) const {
            const auto *v = reinterpret_cast<const glm::vec4 *>(linvel_staging->GetVMAddress());
            return glm::vec3(v[body]);
        }

        glm::vec3 AngularVelocity(uint32_t body) const {
            const auto *w = reinterpret_cast<const glm::vec4 *>(angvel_staging->GetVMAddress());
            return glm::vec3(w[body]);
        }
    };

    XpbdConfig BaseConfig() {
        XpbdConfig config{};
        config.gravity = glm::vec3(0.0f, 0.0f, -kGravity);
        config.time_step = kTimeStep;
        config.num_substep_perstep = kSubsteps;
        config.num_iter_persubstep = 50u;
        config.num_velocity_iters = 10u;
        config.max_contact_points = 50000u;
        config.contact_margin = 0.001f;
        return config;
    }

    /// A kinematic plane whose top surface is the world z = 0 plane.
    uint32_t AddGroundPlane(Fixture &f) {
        return f.AddBox(
            {0.0f, 0.0f, -0.25f},
            {0.0f, 0.0f, 0.0f, 1.0f},
            {6.0f, 6.0f, 0.25f},
            0.0f,
            true,
            0.5f,
            0.5f
        );
    }

    /// A plane tilted by `angle` about Y, whose top surface contains the origin,
    /// plus a box resting flat on it.  Returns the box's body index.
    uint32_t AddSlopeAndBox(Fixture &f, float angle, float half_extent, float static_friction, float dynamic_friction) {
        const glm::vec4 q = RotationAboutY(angle);
        f.AddBox(Rotate(q, {0.0f, 0.0f, -0.25f}), q, {6.0f, 6.0f, 0.25f}, 0.0f, true, static_friction, dynamic_friction);
        // 1 mm of initial penetration keeps the pair in contact on substep one.
        return f.AddBox(
            Rotate(q, {0.0f, 0.0f, half_extent - 0.001f}),
            q,
            {half_extent, half_extent, half_extent},
            1.0f,
            false,
            static_friction,
            dynamic_friction
        );
    }

} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 640, .resol_y = 480, .headless = true, .title = "XPBD Contact Friction Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    constexpr float kMu = 0.5f;
    constexpr float kAnalytic = kMu * kGravity * kDt; // mu * g * dt

    // ── Scenario A: the flat-plane per-substep velocity reduction ──────────
    // A sliding box whose kinetic bound binds loses exactly `mu * g * dt` of
    // tangential speed per substep: the Coulomb bound is `mu * |lambda_n| / dt`
    // and for a supported box `|lambda_n| / dt` is the weight impulse.
    {
        PhysicsSystem physics;
        Fixture f(*rsys, physics, 1u, BaseConfig());
        AddGroundPlane(f);
        const uint32_t box = f.AddBox(
            {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 1.0f, false, kMu, kMu
        );
        f.SetBodyVelocity(box, {2.0f, 0.0f, 0.0f});
        f.Upload();

        float previous = 0.0f;
        float sum_reduction = 0.0f;
        float max_reduction = 0.0f;
        int samples = 0;
        for (uint32_t step = 0u; step < 12u; ++step) {
            f.Step(1u);
            const float vx = f.LinearVelocity(box).x;
            if (step >= 2u) {
                const float reduction = previous - vx;
                sum_reduction += reduction;
                max_reduction = std::max(max_reduction, reduction);
                ++samples;
            }
            previous = vx;
        }

        const float mean_reduction = sum_reduction / static_cast<float>(samples);
        std::cout << "flat plane: mean per-substep reduction " << mean_reduction << " (analytic " << kAnalytic
                  << "), max " << max_reduction << std::endl;

        // Analytic: mu * g * dt within 5%.
        Check(
            std::fabs(mean_reduction - kAnalytic) <= 0.05f * kAnalytic,
            "the per-substep tangential velocity reduction equals mu * g * dt within 5%"
        );
        // One-sided bound: never more than 1.1 * mu * g * dt.  This is the
        // assertion that catches per-iteration re-granting of the bound.
        Check(
            max_reduction <= 1.1f * kAnalytic,
            "the per-substep tangential velocity reduction never exceeds 1.1 * mu * g * dt"
        );
    }

    // ── Scenario B: friction strength does not depend on mass ─────────────
    {
        PhysicsSystem physics;
        Fixture light(*rsys, physics, 1u, BaseConfig());
        Fixture heavy(*rsys, physics, 2u, BaseConfig());

        const uint32_t light_box = ([&] {
            AddGroundPlane(light);
            return light.AddBox(
                {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 1.0f, false, kMu, kMu
            );
        })();
        const uint32_t heavy_box = ([&] {
            AddGroundPlane(heavy);
            return heavy.AddBox(
                {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 100.0f, false, kMu, kMu
            );
        })();
        light.SetBodyVelocity(light_box, {2.0f, 0.0f, 0.0f});
        heavy.SetBodyVelocity(heavy_box, {2.0f, 0.0f, 0.0f});
        light.Upload();
        heavy.Upload();

        for (uint32_t step = 0u; step < 15u; ++step) {
            const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
            auto cb = rsys->GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            cb.begin(vk::CommandBufferBeginInfo{});
            physics.GPUStep(cb);
            light.RecordReadback(cb);
            heavy.RecordReadback(cb);
            cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
            rsys->WaitForIdle();
            light.AfterStep();
            heavy.AfterStep();
        }

        const float light_x = light.Position(light_box).x;
        const float heavy_x = heavy.Position(heavy_box).x;
        std::cout << "mass invariance: distance m=1 -> " << light_x << ", m=100 -> " << heavy_x << std::endl;
        Check(
            std::fabs(light_x - heavy_x) <= 0.02f * std::max(std::fabs(light_x), 1e-3f),
            "mass m and 100 * m slide the same distance within 2%"
        );
    }

    // ── Scenario C: friction strength does not depend on the iteration count ─
    {
        PhysicsSystem physics;
        XpbdConfig few_config = BaseConfig();
        few_config.num_velocity_iters = 4u;
        Fixture few(*rsys, physics, 1u, few_config);
        Fixture many(*rsys, physics, 2u, BaseConfig());

        const uint32_t few_box = ([&] {
            AddGroundPlane(few);
            return few.AddBox(
                {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 1.0f, false, kMu, kMu
            );
        })();
        const uint32_t many_box = ([&] {
            AddGroundPlane(many);
            return many.AddBox(
                {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 1.0f, false, kMu, kMu
            );
        })();
        few.SetBodyVelocity(few_box, {2.0f, 0.0f, 0.0f});
        many.SetBodyVelocity(many_box, {2.0f, 0.0f, 0.0f});
        few.Upload();
        many.Upload();

        for (uint32_t step = 0u; step < 15u; ++step) {
            const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
            auto cb = rsys->GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            cb.begin(vk::CommandBufferBeginInfo{});
            physics.GPUStep(cb);
            few.RecordReadback(cb);
            many.RecordReadback(cb);
            cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
            rsys->WaitForIdle();
            few.AfterStep();
            many.AfterStep();
        }

        const float few_x = few.Position(few_box).x;
        const float many_x = many.Position(many_box).x;
        std::cout << "iteration invariance: 4 iters -> " << few_x << ", 10 iters -> " << many_x << std::endl;
        Check(
            std::fabs(few_x - many_x) <= 0.03f * std::max(std::fabs(many_x), 1e-3f),
            "num_velocity_iters = 4 and 10 slide the same distance within 3%"
        );
    }

    // ── Scenario D: friction strength does not depend on the sliding direction ─
    // A box that is long in x: sliding along x and along y give different contact
    // lever arms, so a blended-direction effective mass would decelerate the two
    // differently.
    {
        PhysicsSystem physics;
        Fixture slide_x(*rsys, physics, 1u, BaseConfig());
        Fixture slide_y(*rsys, physics, 2u, BaseConfig());

        const glm::vec3 half{1.0f, 0.5f, 0.5f};
        const uint32_t box_x = ([&] {
            AddGroundPlane(slide_x);
            return slide_x.AddBox(
                {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, half, 1.0f, false, kMu, kMu
            );
        })();
        const uint32_t box_y = ([&] {
            AddGroundPlane(slide_y);
            return slide_y.AddBox(
                {0.0f, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, half, 1.0f, false, kMu, kMu
            );
        })();
        slide_x.SetBodyVelocity(box_x, {2.0f, 0.0f, 0.0f});
        slide_y.SetBodyVelocity(box_y, {0.0f, 2.0f, 0.0f});
        slide_x.Upload();
        slide_y.Upload();

        float reduction_x = 0.0f;
        float reduction_y = 0.0f;
        for (uint32_t step = 0u; step < 10u; ++step) {
            const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
            auto cb = rsys->GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            cb.begin(vk::CommandBufferBeginInfo{});
            physics.GPUStep(cb);
            slide_x.RecordReadback(cb);
            slide_y.RecordReadback(cb);
            cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
            rsys->WaitForIdle();
            slide_x.AfterStep();
            slide_y.AfterStep();
        }
        reduction_x = 2.0f - slide_x.LinearVelocity(box_x).x;
        reduction_y = 2.0f - slide_y.LinearVelocity(box_y).y;
        std::cout << "direction invariance: +x reduction " << reduction_x << ", +y reduction " << reduction_y
                  << std::endl;
        Check(
            std::fabs(reduction_x - reduction_y) <= 0.02f * std::max(std::fabs(reduction_x), 1e-3f),
            "two sliding directions with different contact lever arms decelerate the same within 2%"
        );
    }

    // ── Scenario E: the stick / slip threshold and no-creep ────────────────
    // Sweep the slope angle and find where a resting box stops holding.  The
    // static friction budget is `mu_s * lambda_n`, so the analytic threshold is
    // tan(theta) = mu_s.
    {
        constexpr float kMuStatic = 0.5f;
        constexpr float kMuDynamic = 0.5f;
        constexpr float kHalf = 0.5f;
        constexpr uint32_t kSettleSteps = 60u;
        constexpr uint32_t kMeasureSteps = 600u;
        constexpr float kCreepLimit = 1e-3f;

        // How far a box resting on a `degrees` slope travels down-slope over
        // `kMeasureSteps` substeps, once its initial pose has settled.
        auto slope_travel = [&](float degrees) {
            PhysicsSystem physics;
            Fixture f(*rsys, physics, 1u, BaseConfig());
            const float angle = degrees * 3.14159265f / 180.0f;
            const uint32_t box = AddSlopeAndBox(f, angle, kHalf, kMuStatic, kMuDynamic);
            f.Upload();
            f.Step(kSettleSteps);
            const glm::vec3 start = f.Position(box);
            f.Step(kMeasureSteps);
            const glm::vec3 end = f.Position(box);
            const glm::vec3 down_slope = Rotate(RotationAboutY(angle), {1.0f, 0.0f, 0.0f});
            return std::fabs(glm::dot(end - start, down_slope));
        };

        const float analytic = std::atan(kMuStatic) * 180.0f / 3.14159265f;

        // Coarse scan (2 degrees) to bracket the transition, then a 0.1 degree
        // scan inside that bracket so the transition angle is pinned to within
        // one grid step.
        float last_hold = 0.0f;
        float first_slide = 0.0f;
        for (float degrees = analytic - 8.0f; degrees <= analytic + 8.0f; degrees += 2.0f) {
            const float travelled = slope_travel(degrees);
            const bool holds = travelled < kCreepLimit;
            std::cout << "slope " << degrees << " deg (coarse): travelled " << travelled
                      << (holds ? " (holds)" : " (slides)") << std::endl;
            if (holds) {
                last_hold = degrees;
            } else if (first_slide == 0.0f) {
                first_slide = degrees;
                break;
            }
        }
        Check(first_slide > 0.0f, "some slope angle makes the box slide");
        Check(last_hold > 0.0f, "some slope angle holds the box");

        if (first_slide > 0.0f && last_hold > 0.0f) {
            for (float degrees = last_hold + 0.1f; degrees <= first_slide; degrees += 0.1f) {
                const float travelled = slope_travel(degrees);
                if (travelled < kCreepLimit) {
                    last_hold = degrees;
                } else {
                    first_slide = degrees;
                    break;
                }
            }
        }

        const float transition = 0.5f * (last_hold + first_slide);
        std::cout << "slope threshold: holds at " << last_hold << " deg, slides at " << first_slide
                  << " deg; analytic tan(theta) = mu_s is " << analytic << " deg" << std::endl;

        // The transition lies in [last_hold, first_slide], one 0.1 degree grid
        // step wide, so its midpoint is the resolution-limited estimate of the
        // angle at which the box flips from holding to sliding.  The analytic
        // limit is tan(theta) = mu_s.
        Check(
            std::fabs(transition - analytic) <= 2.0f,
            "the box flips between holding and sliding within 2 degrees of tan(theta) = mu_s"
        );

        // No-creep: below the static limit the box must not drift.  The initial
        // 1 mm of penetration settles during the first few substeps, so the
        // measurement starts once the box is at rest.
        PhysicsSystem physics;
        Fixture f(*rsys, physics, 1u, BaseConfig());
        const float angle = analytic * 3.14159265f / 180.0f * 0.8f; // well inside the static cone
        const uint32_t box = AddSlopeAndBox(f, angle, kHalf, kMuStatic, kMuDynamic);
        f.Upload();
        f.Step(120u); // settle
        const glm::vec3 start = f.Position(box);
        f.Step(600u);
        const glm::vec3 end = f.Position(box);
        const glm::vec3 down_slope = Rotate(RotationAboutY(angle), {1.0f, 0.0f, 0.0f});
        const float creep = glm::length(end - start);
        const float creep_down_slope = glm::dot(end - start, down_slope);
        std::cout << "no-creep at " << (angle * 180.0f / 3.14159265f) << " deg: moved " << creep
                  << " m (down-slope " << creep_down_slope << " m) over 600 substeps" << std::endl;
        Check(creep < 1e-3f, "a body at rest on a slope below the static limit moves less than 1 mm over 600 substeps");
    }

    // ── Scenario F: the published count never exceeds the contact capacity ──
    // A scene whose candidate pairs want far more contact slots than the
    // capacity admits.  The detector is driven directly rather than through the
    // solver, because the invariant under test is the detector's:
    // `collision_count` is both a data extent and a validity bound for every
    // later stage, so it must never name a slot the detection dispatch did not
    // write.
    //
    // The capacity is calibrated from the scene rather than hard-coded: the
    // scene's natural contact-point count is measured with no budget pressure,
    // and the case under test then uses half of it.  A hard-coded capacity would
    // stop exercising the bound, silently, the moment the manifold generation
    // produced fewer points than expected.
    {
        constexpr uint32_t kOverlapBoxes = 10u;

        PhysicsSystem physics;
        XpbdConfig config = BaseConfig();
        Fixture f(*rsys, physics, 1u, config);

        f.AddBox({0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, 0.0f, 1.0f}, {6.0f, 6.0f, 0.25f}, 0.0f, true, 0.5f, 0.5f);
        // Boxes 5 cm apart with 1 m extents: every box overlaps every other, and
        // no two share a pose, so the manifold generation is not degenerate.
        for (uint32_t i = 0u; i < kOverlapBoxes; ++i) {
            const float x = 0.05f * static_cast<float>(i);
            f.AddBox({x, 0.0f, 0.5f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 1.0f, false, 0.5f, 0.5f);
        }
        f.Upload();

        const uint32_t all_pairs = f.shape_slots * (f.shape_slots - 1u) / 2u;
        // Every pair producing its maximum of 5 points: no budget pressure.
        const uint32_t ample_capacity = std::max(1u, all_pairs * 5u);

        GridConfig grid{};
        grid.world_min = config.grid_world_min;
        grid.world_max = config.grid_world_max;
        grid.cell_size = config.grid_cell_size;
        grid.max_cells_per_shape = config.max_cells_per_shape;

        SpatialHashBroadDetector broad(rsys->GetDeviceContext());
        broad.BindToScene(f.scene, grid, config.fallback_all_pairs_threshold, config.max_global_shape_count);
        ConvexCollisionDetector narrow(rsys->GetDeviceContext());

        auto count_staging = Rhi::DeviceBuffer::CreateUnique(
            rsys->GetAllocatorState(),
            Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice},
            sizeof(uint32_t),
            "XPBD friction collision count"
        );

        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();

        // Record one detection pass at `capacity` and return the count it
        // published.  Re-binding the same detector leaves its result buffers at
        // their grown size, so the budget is what bounds the writes rather than
        // the allocation.
        auto published_count = [&](uint32_t capacity) -> uint32_t {
            narrow.BindToScene(f.scene, broad, capacity, config.contact_margin);

            auto detect_cb = rsys->GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            detect_cb.begin(vk::CommandBufferBeginInfo{});
            broad.Record(detect_cb);
            narrow.Record(detect_cb);
            detect_cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {detect_cb}, {}});
            rsys->WaitForIdle();

            // A second submission, recorded only once the queue drained, so the
            // readback observes the detection dispatch's writes without needing a
            // compute-to-transfer barrier.
            const Rhi::ComputeBuffer *count_buffer = narrow.GetResultBuffers().collision_count;
            if (count_buffer == nullptr) {
                return 0u;
            }
            auto copy_cb = rsys->GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            copy_cb.begin(vk::CommandBufferBeginInfo{});
            copy_cb.copyBuffer(
                count_buffer->GetBuffer(), count_staging->GetBuffer(), vk::BufferCopy{0u, 0u, sizeof(uint32_t)}
            );
            copy_cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {copy_cb}, {}});
            rsys->WaitForIdle();
            count_staging->Invalidate();
            return *reinterpret_cast<const uint32_t *>(count_staging->GetVMAddress());
        };

        const uint32_t natural = published_count(ample_capacity);
        const uint32_t capacity = std::max(1u, natural / 2u);
        const uint32_t published = published_count(capacity);

        std::cout << "over-budget reservation: " << all_pairs << " candidate pairs produce " << natural
                  << " contact points; capacity " << capacity << " publishes " << published << std::endl;

        Check(natural > 1u, "the scene produces enough contact points for the bound to be exercised");
        Check(published <= capacity, "collision_count never exceeds the contact capacity");
        Check(published == capacity, "the contact budget binds here, so the bound is actually exercised");
    }

    // ── Scenario G: a spinning box turns its contacts' drift directions ─────
    // A box resting on the ground plane with a spin about the up axis.  Its four
    // contact points' material drift runs circumferentially, so the drift
    // directions differ from one another and each one turns as the pose turns.
    // That is the configuration in which a scalar tangential accumulator and a
    // vector one can diverge: the vector sum of the corrections is shorter than
    // the sum of their magnitudes whenever the direction turns, so the scalar
    // form spends its budget faster.  The numbers are printed because this is
    // the pre/post comparator for task 9.8 — the only scene in this file that
    // has a chance of telling the two accountings apart.
    {
        PhysicsSystem physics;
        Fixture f(*rsys, physics, 1u, BaseConfig());
        AddGroundPlane(f);
        const uint32_t box = f.AddBox(
            {0.0f, 0.0f, 0.25f - 0.002f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.25f}, 1.0f, false, kMu, kMu
        );
        f.SetBodyVelocity(box, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f});
        f.Upload();

        const float initial_spin = f.AngularVelocity(box).z;
        float previous = initial_spin;
        float first_reduction = 0.0f;
        float sum_reduction = 0.0f;
        int samples = 0;
        for (uint32_t step = 0u; step < 30u; ++step) {
            f.Step(1u);
            const float spin = f.AngularVelocity(box).z;
            if (step == 0u) {
                first_reduction = previous - spin;
            }
            if (step >= 2u) {
                sum_reduction += previous - spin;
                ++samples;
            }
            previous = spin;
        }

        const float mean_reduction = sum_reduction / static_cast<float>(samples);
        std::cout << "spinning box: initial spin " << initial_spin << ", first-substep reduction " << first_reduction
                  << ", mean per-substep reduction " << mean_reduction << ", final spin " << previous << std::endl;

        Check(std::fabs(initial_spin) > 1e-3f, "the spinning-box scenario starts with a spin");
        Check(std::fabs(previous) < 0.5f * std::fabs(initial_spin), "the contacts arrest the box's spin");
        Check(
            first_reduction > 0.0f && first_reduction <= 1.1f * std::fabs(initial_spin),
            "one substep of friction never reverses the spin"
        );
    }

    rsys->WaitForIdle();

    if (g_failures == 0) {
        std::cout << "xpbd_contact_friction_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "xpbd_contact_friction_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
