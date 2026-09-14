// xpbd_reduce_capacity_test.cpp — Capacity-change coverage for the XPBD GPU
// solver's entry-count-driven reduction.
//
// `PhysicsApp` freezes its scene at `CommitScene`, so the one path with no
// coverage is a scene whose shape/joint count changes between steps — which,
// now that `SumByKey` and `RadixSort` take their geometry per call, is the normal
// path rather than an edge case.  This fixture drives a `PhysicsScene` with
// `XpbdGpuSolver` directly (no `PhysicsApp`), so shapes can be allocated,
// submitted and unregistered between steps.

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

    constexpr uint32_t kMaxBodies = 32u;
    // Boxes are stacked with a small overlap so every neighbouring pair starts
    // out penetrating, which is what produces contacts on the first substep.
    constexpr float kBoxHalf = 0.5f;
    constexpr float kStackStep = 0.9f; // < 2 * kBoxHalf, so neighbours overlap

    bool Finite(const glm::vec4 &v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    /**
     * @brief Headless `PhysicsScene` + `XpbdGpuSolver` harness.
     *
     * Owns its scene's submission helper and a host-visible readback staging
     * buffer, and mirrors the app's per-step order: sync the scene to the GPU,
     * record the solver into one command buffer (reading the resulting body
     * poses back on the same submission), then submit and wait.
     */
    struct Fixture {
        RenderSystem &rsys;
        PhysicsSystem &physics;
        uint32_t scene_id;
        PhysicsScene &scene;
        std::unique_ptr<Rhi::SubmissionHelper> submission;
        std::unique_ptr<Rhi::DeviceBuffer> pos_staging;
        std::unique_ptr<Rhi::DeviceBuffer> rot_staging;
        std::unique_ptr<Rhi::DeviceBuffer> linvel_staging;
        std::unique_ptr<Rhi::DeviceBuffer> angvel_staging;

        // Host mirror of every body slot.  The scene's host columns are only an
        // *upload source*, so re-uploading a structurally changed scene would reset
        // the simulation; `FoldGpuState` writes the GPU's current state back into
        // this mirror first.
        std::vector<RigidBodyComDescriptor> bodies;
        uint32_t shape_slots = 0u;
        uint32_t stepped_body_count = 0u;
        bool ever_stepped = false;

        Fixture(RenderSystem &r, PhysicsSystem &p, uint32_t id) :
            rsys(r), physics(p), scene_id(id), scene(p.CreateScene(id)) {
            scene.SetSimulationEnabled(true);
            submission = std::make_unique<Rhi::SubmissionHelper>(
                rsys.GetDeviceContext().GetDeviceInterface(), rsys.GetDeviceContext().GetAllocatorState()
            );
            auto make_staging = [this](const char *name) {
                return Rhi::DeviceBuffer::CreateUnique(
                    rsys.GetAllocatorState(),
                    Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice},
                    static_cast<size_t>(kMaxBodies) * sizeof(glm::vec4),
                    name
                );
            };
            pos_staging = make_staging("XPBD fixture pos");
            rot_staging = make_staging("XPBD fixture rot");
            linvel_staging = make_staging("XPBD fixture linvel");
            angvel_staging = make_staging("XPBD fixture angvel");
            physics.RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(rsys.GetDeviceContext()));
        }

        /// Allocate a box rigid body and (optionally) its collision shape.
        uint32_t AddBox(glm::vec3 center, glm::vec3 half, float mass, bool kinematic, bool with_shape = true) {
            const uint32_t body = scene.AllocateRigidBodySlot();
            // The body slot index is the mirror index, so an out-of-order
            // allocation would invalidate it.
            assert(body == bodies.size());

            RigidBodyComDescriptor rb{};
            rb.mass = mass;
            rb.is_kinematic = kinematic;
            rb.center_world_position = glm::vec4(center, 1.0f);
            rb.center_world_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            // Solid box: I_axis = m/12 * (b^2 + c^2) over the full extents.
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
            assert(shape == shape_slots);
            ++shape_slots;
            return body;
        }

        /// Force one body's pose and velocity (used to align two scenes).
        void SetBodyPose(uint32_t body, glm::vec3 position, glm::vec4 linear_velocity, glm::vec4 angular_velocity) {
            bodies[body].center_world_position = glm::vec4(position, 1.0f);
            bodies[body].center_world_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            bodies[body].linear_velocity = linear_velocity;
            bodies[body].angular_velocity = angular_velocity;
            scene.SubmitRigidBody(body, bodies[body]);
        }

        void UnregisterShape(uint32_t shape) {
            scene.UnregisterCollisionShape(shape);
        }

        uint32_t ShapeSlotCount() const {
            return shape_slots;
        }

        /// Fold the GPU's body state back into the host columns, so re-uploading
        /// the scene after a structural change preserves the simulation.
        void FoldGpuState() {
            if (!ever_stepped) {
                return;
            }
            const auto *p = reinterpret_cast<const glm::vec4 *>(pos_staging->GetVMAddress());
            const auto *r = reinterpret_cast<const glm::vec4 *>(rot_staging->GetVMAddress());
            const auto *lv = reinterpret_cast<const glm::vec4 *>(linvel_staging->GetVMAddress());
            const auto *av = reinterpret_cast<const glm::vec4 *>(angvel_staging->GetVMAddress());
            for (uint32_t i = 0; i < stepped_body_count; ++i) {
                bodies[i].center_world_position = p[i];
                bodies[i].center_world_rotation = r[i];
                bodies[i].linear_velocity = lv[i];
                bodies[i].angular_velocity = av[i];
                scene.SubmitRigidBody(i, bodies[i]);
            }
        }

        /// Upload the scene after a structural change (adds/removals), preserving
        /// the simulated state.
        void UploadPreservingState() {
            FoldGpuState();
            UploadHostState();
        }

        /// Upload the host columns verbatim; the host state is authoritative.
        void UploadHostState() {
            std::vector<uint32_t> filters(
                static_cast<size_t>(shape_slots) * PhysicsScene::MAX_FILTER_ENTRIES, PhysicsScene::INVALID_INDEX
            );
            if (shape_slots > 0u) {
                scene.SetShapeFilters(filters, shape_slots);
            }
            scene.SyncGpuBuffers(rsys.GetDeviceContext(), *submission);
        }

        /// Record this scene's body-state readback into an already recording buffer.
        void RecordReadback(vk::CommandBuffer cb) {
            const auto gpu = scene.GetGpuBuffers();
            if (gpu.rigid_body_slot_count == 0u || gpu.rigid_body_center_world_position == nullptr) {
                return;
            }
            const size_t bytes = static_cast<size_t>(gpu.rigid_body_slot_count) * sizeof(glm::vec4);
            const vk::BufferCopy copy{0u, 0u, bytes};
            cb.copyBuffer(gpu.rigid_body_center_world_position->GetBuffer(), pos_staging->GetBuffer(), copy);
            cb.copyBuffer(gpu.rigid_body_center_world_rotation->GetBuffer(), rot_staging->GetBuffer(), copy);
            cb.copyBuffer(gpu.rigid_body_linear_velocity->GetBuffer(), linvel_staging->GetBuffer(), copy);
            cb.copyBuffer(gpu.rigid_body_angular_velocity->GetBuffer(), angvel_staging->GetBuffer(), copy);
        }

        /// Make the readback visible to the host after the submission completes.
        void AfterStep() {
            pos_staging->Invalidate();
            rot_staging->Invalidate();
            linvel_staging->Invalidate();
            angvel_staging->Invalidate();
            stepped_body_count = static_cast<uint32_t>(bodies.size());
            ever_stepped = true;
        }

        /// Run `steps` full steps without re-uploading the scene.
        void Step(uint32_t steps) {
            for (uint32_t i = 0; i < steps; ++i) {
                physics.PreGPUStep();

                const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
                auto cb = rsys.GetDevice().allocateCommandBuffers(
                    vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
                )[0];
                cb.begin(vk::CommandBufferBeginInfo{});
                physics.GPUStep(cb);
                RecordReadback(cb);
                cb.end();
                queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
                queues.graphicsQueue.waitIdle();

                physics.PostGPUStep();
                AfterStep();
            }
        }

        glm::vec4 Position(uint32_t body) const {
            const auto *p = reinterpret_cast<const glm::vec4 *>(pos_staging->GetVMAddress());
            return p[body];
        }
    };

    // A stack of `count` dynamic boxes on a kinematic ground.  Returns the body
    // indices in stack order; the ground is body 0.
    std::vector<uint32_t> BuildStack(Fixture &f, uint32_t count) {
        f.AddBox({0.0f, 0.0f, -0.5f}, {5.0f, 5.0f, 0.5f}, 0.0f, true);
        std::vector<uint32_t> bodies;
        for (uint32_t i = 0; i < count; ++i) {
            bodies.push_back(
                f.AddBox(
                    {0.0f, 0.0f, 0.4f + kStackStep * static_cast<float>(i)},
                    {kBoxHalf, kBoxHalf, kBoxHalf},
                    1.0f,
                    false
                )
            );
        }
        return bodies;
    }

    /// Step every fixture's scene in lockstep.  `PhysicsSystem::PreGPUStep` /
    /// `GPUStep` advance every scene in the system, so two scenes that must stay
    /// comparable have to be driven from one submission each step.
    void StepAll(RenderSystem &rsys, PhysicsSystem &physics, const std::vector<Fixture *> &fixtures, uint32_t steps) {
        for (uint32_t s = 0; s < steps; ++s) {
            physics.PreGPUStep();
            const auto &queues = rsys.GetDeviceInterface().GetQueueInfo();
            auto cb = rsys.GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            cb.begin(vk::CommandBufferBeginInfo{});
            physics.GPUStep(cb);
            for (Fixture *f : fixtures) {
                f->RecordReadback(cb);
            }
            cb.end();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
            queues.graphicsQueue.waitIdle();
            physics.PostGPUStep();
            for (Fixture *f : fixtures) {
                f->AfterStep();
            }
        }
    }

    /// True when every body sits above the ground plane and carries finite state.
    bool Sane(const Fixture &f, const std::vector<uint32_t> &bodies, const char *what) {
        for (uint32_t b : bodies) {
            const glm::vec4 p = f.Position(b);
            if (!Finite(p) || p.z < -1.0f || std::fabs(p.z) > 1000.0f) {
                std::cerr << "FAIL: " << what << ": body " << b << " at (" << p.x << ", " << p.y << ", " << p.z << ")"
                          << std::endl;
                g_failures++;
                return false;
            }
        }
        return true;
    }

} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 640, .resol_y = 480, .headless = true, .title = "XPBD Reduce Capacity Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    auto rsys = cmc->GetRenderSystem();

    // ── Scenario A: the shape count changes between steps ──────────────────
    // Growing the slot count re-sizes the solver's buffers and changes the reduce
    // capacity; shrinking the *live* shape count leaves that capacity in place but
    // drops the entry count.  Neither may throw or leave a stale delta behind.
    {
        PhysicsSystem physics;
        Fixture f(*rsys, physics, 1u);
        std::vector<uint32_t> stack = BuildStack(f, 3u);
        f.UploadHostState();

        f.Step(6u);
        Sane(f, stack, "baseline stack");
        // The stack must be *supported*: an unsupported box would have free-fallen
        // well below its start (0.4) in six steps, whereas contacts push it up.
        Check(f.Position(stack[0]).z > 0.42f, "the ground contact supports the bottom box");

        // Grow: two more boxes on top of the stack (slots 5 and 6).  The scene is
        // re-uploaded with the simulated state folded back in, which is what a
        // capacity change does at the scene level.
        for (uint32_t i = 3u; i < 5u; ++i) {
            stack.push_back(
                f.AddBox(
                    {0.0f, 0.0f, 0.4f + kStackStep * static_cast<float>(i)},
                    {kBoxHalf, kBoxHalf, kBoxHalf},
                    1.0f,
                    false
                )
            );
        }
        f.UploadPreservingState();
        f.Step(6u);
        Sane(f, stack, "grown stack");
        Check(f.ShapeSlotCount() == 6u, "growing the scene grows the shape slot count");
        // Shrink the live shape count back down while the slots stay allocated.
        const glm::vec4 before = f.Position(stack[2]);
        f.UnregisterShape(3u);
        f.UnregisterShape(4u);
        f.UploadPreservingState();
        f.Step(6u);
        Sane(f, stack, "shrunk scene");
        const glm::vec4 after = f.Position(stack[2]);
        Check(
            std::fabs(after.z - before.z) < 1.0f,
            "removing the shapes above a body does not teleport it"
        );
        Check(f.ShapeSlotCount() == 6u, "unregistering a shape keeps its slot allocated (the capacity stays)");
    }

    // ── Scenario B: a shrunk contact set leaves no stale-values artefact ───
    // Two scenes with the same shape slot count (so the same reduce capacity) and
    // the same solver configuration.  One runs with an extra live shape for a
    // while and then loses it; the other never had it live.  After both are reset
    // to identical body states, one further step must produce identical poses: the
    // first scene's leftovers (records, value buffer) may not reach the output.
    {
        PhysicsSystem physics;
        Fixture varying(*rsys, physics, 1u);
        Fixture control(*rsys, physics, 2u);

        std::vector<uint32_t> varying_stack = BuildStack(varying, 3u);
        std::vector<uint32_t> control_stack = BuildStack(control, 3u);
        Check(varying_stack == control_stack, "both scenes allocate the same body indices");

        // The extra body: live in `varying`, dead from the start in `control`.
        varying.AddBox({0.0f, 0.0f, 0.4f + kStackStep * 3.0f}, {kBoxHalf, kBoxHalf, kBoxHalf}, 1.0f, false);
        control.AddBox({0.0f, 0.0f, 0.4f + kStackStep * 3.0f}, {kBoxHalf, kBoxHalf, kBoxHalf}, 1.0f, false);
        control.UnregisterShape(4u);

        varying.UploadHostState();
        control.UploadHostState();

        StepAll(*rsys, physics, {&varying, &control}, 8u);
        Sane(varying, varying_stack, "varying scene before the shrink");
        Sane(control, control_stack, "control scene");
        // The extra live shape must actually have changed the contact set, or the
        // comparison below would be vacuous.
        Check(
            std::fabs(varying.Position(varying_stack[2]).z - control.Position(control_stack[2]).z) > 1e-3f,
            "the extra live shape changes the contact-driven trajectory"
        );

        // Shrink: the varying scene's live shape set now matches the control's.
        varying.UnregisterShape(4u);

        // Align both scenes to identical body states, so any later difference can
        // only come from GPU data left over by the earlier, larger contact set.
        auto reset = [](Fixture &f) {
            for (uint32_t i = 0; i < 5u; ++i) {
                f.SetBodyPose(
                    i,
                    {0.0f, 0.0f, -0.5f + static_cast<float>(i)},
                    glm::vec4(0.0f),
                    glm::vec4(0.0f)
                );
            }
        };
        reset(varying);
        reset(control);
        varying.UploadHostState();
        control.UploadHostState();

        StepAll(*rsys, physics, {&varying, &control}, 1u);

        for (uint32_t i = 0; i < 5u; ++i) {
            const glm::vec4 a = varying.Position(i);
            const glm::vec4 b = control.Position(i);
            const float dz = std::fabs(a.z - b.z);
            if (!(dz <= 1e-4f)) {
                std::cerr << "FAIL: body " << i << " differs after a shrunk contact set: " << a.z << " vs " << b.z
                          << std::endl;
                g_failures++;
            }
        }
    }

    // ── Scenario C: joints but no contacts ────────────────────────────────
    // A kinematic post with a dynamic box hinged out to the side, plus a free
    // body far away from everything: the substep has a hinge joint and **zero**
    // contacts (so the contact entry count is zero and the counted clear writes
    // nothing).  The hinged box must stay at its anchor radius, and the free body
    // must fall exactly as gravity dictates — any phantom contact contribution
    // would move one of them.
    {
        PhysicsSystem physics;
        Fixture f(*rsys, physics, 1u);
        const uint32_t post =
            f.AddBox({0.0f, 0.0f, 2.0f}, {0.25f, 0.25f, 0.25f}, 0.0f, true, /*with_shape=*/false);
        const uint32_t arm = f.AddBox({1.1f, 0.0f, 2.0f}, {0.25f, 0.25f, 0.25f}, 1.0f, false);
        const uint32_t joint = f.scene.AllocateHingeJoint();
        HingeJointComDescriptor hj{};
        hj.obj1_index = post;
        hj.obj2_index = arm;
        hj.compliance = 0.0f;
        hj.hinge_axis_obj1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        hj.hinge_anchor_obj1 = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        hj.initial_rel_pos_local = glm::vec4(1.1f, 0.0f, 0.0f, 0.0f);
        hj.initial_rel_rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        f.scene.SubmitHingeJoint(joint, hj);

        constexpr float kFreeZ = 50.0f;
        const uint32_t free_body = f.AddBox({kFreeZ, 0.0f, kFreeZ}, {0.25f, 0.25f, 0.25f}, 1.0f, false);

        f.UploadHostState();

        constexpr uint32_t kSteps = 40u;
        f.Step(kSteps);

        const glm::vec4 p_post = f.Position(post);
        const glm::vec4 p_arm = f.Position(arm);
        Check(Finite(p_post) && Finite(p_arm), "a joints-only scene keeps finite body state");
        Check(std::fabs(p_post.z - 2.0f) < 1e-3f, "the kinematic post does not move");
        Check(p_arm.z < 1.5f, "gravity swings the hinged box down through zero-contact substeps");
        const float separation = glm::length(glm::vec3(p_arm - p_post));
        Check(
            std::fabs(separation - 1.1f) < 0.05f,
            "the hinge holds the box at its anchor distance through substeps with zero contacts"
        );

        // Semi-implicit Euler over `kSteps` steps of `dt = time_step / substeps`.
        constexpr float kTimeStep = 1.0f / 60.0f;
        constexpr uint32_t kSubsteps = 2u;
        const float dt = kTimeStep / static_cast<float>(kSubsteps);
        const uint32_t n = kSteps * kSubsteps;
        const float expected_drop = 9.81f * dt * dt * static_cast<float>(n) * static_cast<float>(n + 1u) * 0.5f;
        const float free_z = f.Position(free_body).z;
        Check(
            std::fabs(free_z - (kFreeZ - expected_drop)) < 0.1f,
            "a body with no contacts falls exactly as gravity dictates"
        );
    }

    rsys->WaitForIdle();

    if (g_failures == 0) {
        std::cout << "xpbd_reduce_capacity_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "xpbd_reduce_capacity_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
