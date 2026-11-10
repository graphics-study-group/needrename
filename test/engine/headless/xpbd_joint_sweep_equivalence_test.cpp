// xpbd_joint_sweep_equivalence_test.cpp — The two joint dispatch modes must
// produce the same simulation.
//
// The solver records the joint solve either as one dispatch whose only
// workgroup walks every color (`joint_color_sweep.comp`), or as one dispatch
// per color (`accumulate_{hinge,fixed}_position.comp`). The mode is selected by
// `XpbdConfig::joint_sweep_max_color_width`.
//
// This fixture runs the same hinged chain in two scenes of one PhysicsSystem,
// from identical initial state, with the sweep enabled in one and disabled in
// the other, and requires the body poses to stay identical for every step. A
// difference means the two modes diverged in the constraint math or in the
// color handoff.

#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"

#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSystem.h>
#include <Physics/Solver/XPBDGpuSolver.h>

#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Submission/SubmissionHelper.h>

#include <SDL3/SDL.h>
#include <cassert>
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

    constexpr uint32_t kMaxBodies = 8u;
    constexpr glm::vec4 kAxis{0.0f, 0.0f, 1.0f, 0.0f};

    struct Fixture {
        RenderSystem &rsys;
        PhysicsSystem &physics;
        uint32_t scene_id;
        PhysicsScene &scene;
        std::unique_ptr<Rhi::SubmissionHelper> submission;
        std::unique_ptr<Rhi::DeviceBuffer> pos_staging;
        std::unique_ptr<Rhi::DeviceBuffer> rot_staging;

        std::vector<RigidBodyComDescriptor> bodies;

        Fixture(RenderSystem &r, PhysicsSystem &p, uint32_t id, uint32_t sweep_width) :
            rsys(r), physics(p), scene_id(id), scene(p.CreateScene(id)) {
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
            pos_staging = make_staging("XPBD sweep pos");
            rot_staging = make_staging("XPBD sweep rot");

            auto solver = std::make_unique<XpbdGpuSolver>(rsys.GetDeviceContext());
            XpbdConfig config{};
            config.max_contact_points = 64u;
            config.num_iter_persubstep = 8u;
            config.num_velocity_iters = 4u;
            config.joint_sweep_max_color_width = sweep_width;
            solver->SetConfig(config);
            physics.RegisterSolver(scene_id, std::move(solver));
        }

        uint32_t AddBody(glm::vec3 center, float mass, bool kinematic, glm::vec3 half) {
            const uint32_t body = scene.AllocateRigidBodySlot();
            assert(body == bodies.size());

            RigidBodyComDescriptor rb{};
            rb.mass = kinematic ? 0.0f : mass;
            rb.is_kinematic = kinematic;
            rb.center_world_position = glm::vec4(center, 1.0f);
            rb.center_world_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

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
            const glm::vec3 inv(
                ixx > 1e-12f ? 1.0f / ixx : 0.0f, iyy > 1e-12f ? 1.0f / iyy : 0.0f, izz > 1e-12f ? 1.0f / izz : 0.0f
            );
            rb.inverse_inertia = glm::mat4(
                glm::vec4(inv.x, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, inv.y, 0.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, inv.z, 0.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
            );
            bodies.push_back(rb);
            scene.SubmitRigidBody(body, rb);

            const uint32_t shape = scene.AllocateCollisionShapeSlot();
            CollisionShapeComDescriptor sd{};
            sd.type = 0u;
            sd.feature = glm::vec4(half, 0.0f);
            sd.local_position = glm::vec4(0.0f);
            sd.local_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            sd.bound_rigid_body = body;
            scene.SubmitCollisionShape(shape, sd);
            return body;
        }

        /// obj1 is the parent, obj2 the child. The hinge sits on obj1's COM, so
        /// the child hangs from it at the initial offset.
        uint32_t AddHinge(uint32_t obj1, uint32_t obj2) {
            const uint32_t slot = scene.AllocateHingeJoint();
            HingeJointComDescriptor joint{};
            joint.obj1_index = obj1;
            joint.obj2_index = obj2;
            joint.compliance = 0.0f;
            joint.hinge_axis_obj1 = kAxis;
            joint.hinge_anchor_obj1 = glm::vec4(0.0f);
            const glm::vec3 rel =
                glm::vec3(bodies[obj2].center_world_position) - glm::vec3(bodies[obj1].center_world_position);
            joint.initial_rel_pos_local = glm::vec4(rel, 0.0f);
            joint.initial_rel_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            scene.SubmitHingeJoint(slot, joint);
            return slot;
        }

        void Upload() {
            std::vector<uint32_t> filters(
                static_cast<size_t>(bodies.size()) * PhysicsScene::MAX_FILTER_ENTRIES, PhysicsScene::INVALID_INDEX
            );
            scene.SetShapeFilters(filters, static_cast<uint32_t>(bodies.size()));
            scene.SyncGpuBuffers(rsys.GetDeviceContext(), *submission);
        }

        void RecordReadback(vk::CommandBuffer cb) {
            const auto gpu = scene.GetGpuBuffers();
            if (gpu.rigid_body_slot_count == 0u) return;
            const size_t bytes = static_cast<size_t>(gpu.rigid_body_slot_count) * sizeof(glm::vec4);
            const vk::BufferCopy copy{0u, 0u, bytes};
            cb.copyBuffer(gpu.rigid_body_center_world_position->GetBuffer(), pos_staging->GetBuffer(), copy);
            cb.copyBuffer(gpu.rigid_body_center_world_rotation->GetBuffer(), rot_staging->GetBuffer(), copy);
        }

        void AfterStep() {
            pos_staging->Invalidate();
            rot_staging->Invalidate();
        }

        glm::quat Rotation(uint32_t body) const {
            const auto *r = reinterpret_cast<const glm::vec4 *>(rot_staging->GetVMAddress());
            return glm::quat(r[body].w, r[body].x, r[body].y, r[body].z);
        }

        glm::vec4 Position(uint32_t body) const {
            const auto *p = reinterpret_cast<const glm::vec4 *>(pos_staging->GetVMAddress());
            return p[body];
        }
    };

    // A kinematic anchor at the origin with a three-link chain hanging off it.
    // Two hinge colors are enough here, which is below the sweep width, so the
    // sweep-enabled fixture really records the sweep and the disabled one really
    // records one dispatch per color.
    std::vector<uint32_t> BuildChain(Fixture &f) {
        std::vector<uint32_t> chain;
        chain.push_back(f.AddBody({0.0f, 0.0f, 0.0f}, 0.0f, true, {0.2f, 0.2f, 0.2f}));
        for (uint32_t i = 1u; i <= 3u; ++i) {
            chain.push_back(f.AddBody({static_cast<float>(i), 0.0f, 0.0f}, 1.0f, false, {0.2f, 0.2f, 0.2f}));
        }
        for (uint32_t i = 0u; i + 1u < chain.size(); ++i) {
            f.AddHinge(chain[i], chain[i + 1u]);
        }
        return chain;
    }
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 640, .resol_y = 480, .headless = true, .title = "XPBD Joint Sweep Equivalence"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    PhysicsSystem physics;
    Fixture sweep(*rsys, physics, 1u, 64u);
    Fixture per_color(*rsys, physics, 2u, 0u);

    const std::vector<uint32_t> sweep_chain = BuildChain(sweep);
    const std::vector<uint32_t> per_color_chain = BuildChain(per_color);
    Check(sweep_chain == per_color_chain, "both scenes allocate the same body indices");

    sweep.Upload();
    per_color.Upload();

    constexpr uint32_t kSteps = 30u;
    for (uint32_t step = 0u; step < kSteps; ++step) {
        const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
        auto cb = rsys->GetDevice().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
        )[0];
        cb.begin(vk::CommandBufferBeginInfo{});
        physics.GPUStep(cb);
        sweep.RecordReadback(cb);
        per_color.RecordReadback(cb);
        cb.end();
        queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
        rsys->WaitForIdle();
        sweep.AfterStep();
        per_color.AfterStep();

        for (uint32_t body : sweep_chain) {
            const glm::vec4 a = sweep.Position(body);
            const glm::vec4 b = per_color.Position(body);
            if (glm::length(a - b) > 1e-6f) {
                std::cerr << "FAIL: sweep and per-color positions diverged at step " << step << ", body " << body
                          << ": (" << a.x << ", " << a.y << ", " << a.z << ") vs (" << b.x << ", " << b.y << ", "
                          << b.z << ")" << std::endl;
                g_failures++;
                break;
            }
            const glm::quat qa = sweep.Rotation(body);
            const glm::quat qb = per_color.Rotation(body);
            if (std::fabs(glm::dot(qa, qb)) < 1.0f - 1e-6f) {
                std::cerr << "FAIL: sweep and per-color rotations diverged at step " << step << ", body " << body
                          << std::endl;
                g_failures++;
                break;
            }
        }
        if (g_failures > 0) break;
    }

    // The chain must have actually moved, or the comparison above is vacuous.
    Check(
        std::fabs(sweep.Position(sweep_chain.back()).z) > 1e-3f,
        "the chain swings under gravity, so the comparison exercises the joint solve"
    );

    if (g_failures > 0) {
        std::cerr << "joint sweep equivalence failed" << std::endl;
        return 1;
    }
    std::cout << "joint sweep equivalence passed" << std::endl;
    return 0;
}
