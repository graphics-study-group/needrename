#include "PhysicsApp.h"

#include <Core/FileSystem/DataRoot.h>

#include "CameraControllerComponent.h"
#include "SceneBuilder.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Core/Functional/OptionHandler.h>
#include <Core/Functional/Time.h>
#include <Core/Math/Transform.h>
#include <Framework/Bridge/PhysicsAdaptor.h>
#include <Framework/Component/RenderComponent/CameraComponent.h>
#include <Framework/Component/RenderComponent/LightComponent.h>
#include <Framework/Import/UrdfLoader.h>
#include <Framework/Input/Input.h>
#include <Framework/MainClass.h>
#include <Framework/Object/GameObject.h>
#include <Framework/Tools/ComplexRenderGraphBuilder.h>
#include <Framework/World/Scene.h>
#include <Framework/World/WorldSystem.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSystem.h>
#include <Physics/Solver/XPBDGpuSolver.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/Renderer/Camera.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/IPresentProvider.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Resource/RenderTargetTexture.h>
#include <Render/UserInterface/GUISystem.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Command/CommandBuffer.h>
#include <Rhi/Command/CommandEncoder.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Device/Fence.h>
#include <Rhi/Device/MemoryAccessTypes.h>
#include <Rhi/Device/MemoryTypes.h>
#include <Rhi/Submission/EpochTracker.h>
#include <Rhi/Texture/ImageUtils.h>
#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <imgui.h>

#include <cstring>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace AppPhysics {

    using namespace Engine;

    namespace {
        /// @brief Resolve an input key name to its SDL scancode.
        ///
        /// Names come from `Engine::Input::k_name_code_map` ("w", "space",
        /// "left", ...). Matching is case-insensitive so a caller can write
        /// "W" or "Space" without consulting the map's spelling.
        ///
        /// @return The scancode, or -1 when the name is not in the map.
        int ScancodeForName(const std::string &key) {
            std::string lowered;
            lowered.reserve(key.size());
            for (const char c : key) {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            const auto found = Input::k_name_code_map.find(lowered);
            return found == Input::k_name_code_map.end() ? -1 : static_cast<int>(found->second);
        }

        // Rotation that maps `from` onto `to` (both normalized at call sites).
        glm::quat QuatFromVecToVec(const glm::vec3 &from, const glm::vec3 &to) {
            float dot = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
            if (dot > 0.9999f) {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            if (dot < -0.9999f) {
                glm::vec3 axis = glm::cross(from, glm::vec3(1.0f, 0.0f, 0.0f));
                if (glm::dot(axis, axis) < 1e-6f) {
                    axis = glm::cross(from, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                axis = glm::normalize(axis);
                return glm::angleAxis(glm::pi<float>(), axis);
            }
            glm::vec3 axis = glm::normalize(glm::cross(from, to));
            return glm::angleAxis(std::acos(dot), axis);
        }

        /// @brief Draw the one-line camera control hint in the window's top-left
        /// corner.
        ///
        /// The window is input-transparent (`ImGuiWindowFlags_NoInputs`), so the
        /// overlay never captures the mouse and the camera keeps the window's
        /// full input area.
        void DrawControlHint() {
            constexpr ImGuiWindowFlags k_hint_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                                      | ImGuiWindowFlags_NoSavedSettings
                                                      | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
                                                      | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize;
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);
            if (ImGui::Begin("##physics_app_control_hint", nullptr, k_hint_flags)) {
                ImGui::TextUnformatted(
                    "Camera: hold Alt + W/A/S/D/Q/E to move, Alt + right mouse drag to look  |  "
                    "Space: pause/resume"
                );
            }
            ImGui::End();
        }
    } // namespace

    struct PhysicsApp::Impl {
        enum class Phase {
            Building,
            Committed
        };

        // Declared first so it is destroyed last: engine subsystems (VMA allocator, asset manager, render system) must outlive the
        // app-owned resources declared below (render graph textures, compute stages, asset refs).
        std::shared_ptr<MainClass> main_class{};

        Phase phase{Phase::Building};

        AppMode mode{AppMode::Windowed};

        // Raw pointers to subsystems owned by MainClass.
        WorldSystem *world{};
        RenderSystem *renderer{};
        PhysicsSystem *physics{};
        Input *input{};
        TimeSystem *time{};
        Scene *scene{};
        /// @brief ImGui overlay host; null in headless/offscreen modes.
        GUISystem *gui{};

        std::unique_ptr<SceneBuilder> builder{};
        GameObject *root{};
        GameObject *camera_object{};
        CameraComponent *camera_component{};
        CameraControllerComponent *camera_controller{};

        std::unique_ptr<ComplexRenderGraphBuilder> rg_builder{};
        std::unique_ptr<RenderGraph> render_graph{};
        RGTextureHandle final_color_id{0};

        uint32_t resol_x{1280};
        uint32_t resol_y{720};
        glm::vec3 background_color{0.0f, 0.0f, 0.0f};

        // FOV baseline read from the camera's vertical FOV at creation; used to
        // stabilize the view when the aspect ratio crosses 1.0 (see design D4).
        float camera_fov{45.0f};
        uint32_t last_present_width{0};
        uint32_t last_present_height{0};

        bool should_quit{false};
        bool paused{true};
        bool toggle_was_pressed{false};

        // ── Keyboard state, refreshed by the window event drain ──────────
        // Scancodes rather than keycodes: the engine's input map is scancode
        // based, so the two agree on a physical key regardless of layout.
        std::unordered_set<uint32_t> keys_down{};     ///< Held right now.
        std::unordered_set<uint32_t> keys_pressed{};  ///< Went down since the previous sample.
        std::unordered_set<uint32_t> keys_released{}; ///< Went up since the previous sample.

        // ── Physics state staging (readback and writes share one buffer) ──
        bool physics_readback_built{false};
        uint32_t slot_count{0};
        std::vector<uint32_t> body_to_slot{}; // BodyId -> rigid-body slot index.
        std::vector<glm::vec3> com_offsets{}; // Per-slot COM offset (GO-local).

        /// @brief GPU physics buffer backing each `BodyField` slot.
        std::array<const Rhi::ComputeBuffer *, 6> field_src{}; // Slot-indexed GPU buffer per BodyField.

        /// @brief Host staging for all six body fields (one buffer per field).
        ///
        /// Each buffer is the transfer source of the per-step upload and the target
        /// of the host-side baseline refresh described on `readback_staging`. Together
        /// the six hold "current simulated state + pending round writes": a
        /// `SetBodyValue`/`AddJointTorque` slot write overlays the refreshed baseline,
        /// and the whole-field upload applies the round's complete picture.
        ///
        /// The baseline refresh is what gives every field the one-round write
        /// lifetime: state fields get the integrated result (so their effect
        /// persists through the simulation state), while the force/torque fields get
        /// the solver-cleared values (so a force or torque never acts twice).
        /// Because untouched slots always carry their current values, a whole-field
        /// upload cannot clobber bodies the caller never wrote.
        std::unique_ptr<Rhi::DeviceBuffer> pos_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> rot_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> linvel_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> angvel_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> force_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> torque_staging{};

        /// @brief Field-indexed view of the host staging; owns nothing.
        std::array<Rhi::DeviceBuffer *, 6> field_staging{};

        /// @brief GPU -> CPU transport for the six fields, one buffer per field.
        ///
        /// These are the transfer destinations of the per-step readback copy. They are
        /// separate from `field_staging` because a backend can refuse to have one
        /// buffer be both: on WebGPU a read-back target is mapped read-only, while a
        /// host-written staging buffer is a host copy published with a queue write, and
        /// the standard interface makes those two usage sets mutually exclusive. So the
        /// device writes here, and `ConsumeReadback` folds the result into the
        /// authoritative host staging on the CPU - which is the same "refresh the
        /// baseline" step, just done by the host instead of by the copy engine.
        std::array<std::unique_ptr<Rhi::DeviceBuffer>, 6> readback_staging{};

        /// @brief Whether a readback copy has been recorded but not yet consumed.
        ///
        /// Mutable because every state query is `const` but still has to observe the
        /// last step's result.
        mutable bool readback_pending{false};

        /// @brief Whole-field dirty flags gating the upload copies in `Step`.
        std::array<bool, 6> any_dirty{false, false, false, false, false, false};

        /// @brief Commit-time body state, restored by `ResetToInitial`.
        ///
        /// One entry per rigid-body slot (four vec4 streams) plus one optional
        /// target angle per `JointId` as of `CommitScene` (empty = the joint had no
        /// actuator then).
        struct InitialSnapshot {
            std::vector<glm::vec4> positions{};
            std::vector<glm::vec4> rotations{};
            std::vector<glm::vec4> linear_velocities{};
            std::vector<glm::vec4> angular_velocities{};
            std::vector<std::optional<float>> actuator_targets{};
        };
        std::optional<InitialSnapshot> initial_snapshot{};

        /// @brief Size in bytes of one whole field's staging.
        size_t FieldBytes() const {
            return static_cast<size_t>(slot_count) * sizeof(glm::vec4);
        }

        /// @brief Map a BodyField to the write-path slot index (declaration order).
        static size_t BodyFieldIndex(BodyField field) {
            return static_cast<size_t>(field);
        }

        /// @brief Record the physics SoA -> readback staging copies for every field.
        ///
        /// This is the per-step readback: after the solve, the transport buffers hold
        /// the authoritative post-step values, so no write from an earlier round can
        /// resurface once `ConsumeReadback` folds them into the host staging.
        ///
        /// A read-back buffer that a previous read left mapped is given back to the
        /// device first: a mapped buffer cannot be a copy destination, and the failure
        /// would land frames away from the read that caused it.
        void RecordBodyStateCopy(Rhi::CommandEncoder &encoder) {
            if (slot_count == 0) {
                return;
            }
            const size_t bytes = FieldBytes();
            for (size_t i = 0; i < field_src.size(); ++i) {
                Rhi::DeviceBuffer *destination = readback_staging[i].get();
                if (!field_src[i] || destination == nullptr) {
                    continue;
                }
                destination->ReleaseReadback();
                // `CopyBuffer` is the recording layer's own transfer: it states the
                // ordering the step's compute writes need before this read, which is
                // why no `Barrier` is paired with it here.
                encoder.CopyBuffer(field_src[i]->GetBuffer(), destination->GetBuffer(), bytes);
            }
            // The copies are recorded, so the next read (or the next step) owes the host
            // staging a refresh. Set after recording rather than after submit so a read
            // that happens between the two still waits for the copy it was told about.
            readback_pending = true;
        }

        /// @brief Fold the recorded readback into the authoritative host staging.
        ///
        /// This is the baseline refresh. It runs lazily - on the first state query or
        /// the next `Step` - so a page, which cannot block on the device, pays the wait
        /// at the point it actually needs the data instead of inside `Step`.
        ///
        /// Asking a read-back buffer for its address is what waits for the copy that
        /// filled it, which is the only synchronization this path needs.
        void ConsumeReadback() const {
            if (!readback_pending) {
                return;
            }
            readback_pending = false;
            const size_t bytes = FieldBytes();
            if (bytes == 0u) {
                return;
            }
            for (size_t i = 0; i < readback_staging.size(); ++i) {
                Rhi::DeviceBuffer *source = readback_staging[i].get();
                Rhi::DeviceBuffer *destination = field_staging[i];
                if (source == nullptr || destination == nullptr) {
                    continue;
                }
                source->PrepareReadback();
                source->Invalidate(0, bytes);
                const std::byte *from = source->GetVMAddress();
                if (from == nullptr) {
                    continue;
                }
                std::memcpy(destination->GetVMAddress(), from, bytes);
            }
        }

        /// @brief Make every dirty field's pending host writes device-visible.
        ///
        /// One flush per dirty field (a no-op on coherent memory), replacing the
        /// per-call flush that `SetBodyValue` used to do.
        void FlushDirtyFieldStaging() {
            const size_t bytes = FieldBytes();
            for (size_t i = 0; i < field_staging.size(); ++i) {
                if (any_dirty[i] && field_staging[i]) {
                    field_staging[i]->Flush(0, bytes);
                }
            }
        }

        /// @brief Record the dirty host staging -> GPU buffer copies into a command buffer.
        /// Called before GPUStep so the solver reads the overridden values.
        void RecordBodyStateUpload(Rhi::CommandEncoder &encoder) {
            if (slot_count == 0) {
                return;
            }
            const size_t bytes = FieldBytes();
            for (size_t i = 0; i < field_src.size(); ++i) {
                if (!any_dirty[i] || !field_src[i] || !field_staging[i]) {
                    continue;
                }
                encoder.CopyBuffer(field_staging[i]->GetBuffer(), field_src[i]->GetBuffer(), bytes);
            }
        }

        // ── Joint registry & actuators ──────────────────────────────────
        /// @brief Per-joint metadata needed to compute joint angle/torque CPU-side.
        ///
        /// The angle is measured in the parent-link GO frame (positive rotation
        /// of the child about the positive axis relative to the parent). `axis`
        /// holds the hinge axis in that parent frame; `axis_in_child_frame`
        /// records that the raw axis was provided in the child frame (URDF
        /// joints) so it is rotated into the parent frame at CommitScene.
        struct JointRecord {
            std::string name{};
            BodyId parent{INVALID_BODY_ID};
            BodyId child{INVALID_BODY_ID};
            glm::vec3 axis{1.0f, 0.0f, 0.0f}; ///< Hinge axis in parent GO frame (normalized).
            bool axis_in_child_frame{false};
            glm::quat initial_rel_rotation{1.0f, 0.0f, 0.0f, 0.0f};
            std::optional<JointLimits> limits{};
        };
        std::vector<JointRecord> m_joints{};                  // JointId -> record (index).
        std::vector<std::shared_ptr<Actuator>> m_actuators{}; // JointId -> actuator (null = none).

        // ── Render readback ─────────────────────────────────────────────
        bool render_readback_enabled{false};
        bool has_render_capture{false};
        uint64_t render_frame_id{0};
        uint32_t render_staging_w{0};
        uint32_t render_staging_h{0};
        std::unique_ptr<Rhi::DeviceBuffer> render_staging{};

        /// @brief Finalize per-joint initial relative rotation and parent-frame axis.
        /// Called at CommitScene after FlushCmdQueue (world transforms are final),
        /// using the same values `PhysicsConstraintComponent::Init` read.
        void ComputeJointTransforms() {
            for (auto &rec : m_joints) {
                glm::quat q_parent = builder->GetBodyGameObject(rec.parent).GetWorldTransform().GetRotation();
                glm::quat q_child = builder->GetBodyGameObject(rec.child).GetWorldTransform().GetRotation();
                const glm::quat r0 = glm::inverse(q_parent) * q_child;
                if (rec.axis_in_child_frame) {
                    rec.axis = r0 * rec.axis; // child frame -> parent frame
                }
                if (glm::length(rec.axis) < 1e-6f) {
                    rec.axis = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    rec.axis = glm::normalize(rec.axis);
                }
                rec.initial_rel_rotation = r0;
            }
        }

        /// @brief Run every registered actuator: measure joint state from the CPU
        /// staging (the shared "simulated state + pending writes" view, so it sees
        /// pose writes made since the last step), compute torque, and add it to
        /// the round's ExternalTorque staging so the upload applies it. Called
        /// each Step before the write upload.
        ///
        /// Contributions ACCUMULATE into the round's values, never assign: a serial
        /// joint chain (an A1 leg is hip -> thigh -> calf) makes one body the child
        /// of one actuated hinge and the parent of the next, so the two joints both
        /// push on it with equal-and-opposite pairs and the body must receive the
        /// sum. Assigning would let whichever joint comes later in registry order
        /// erase the earlier one's torque, which makes a chained leg physically
        /// unable to hold or move its joints.
        void RunActuators() {
            // The measurement is of the current known state, so the last step's
            // readback has to be in the host staging before it is taken.
            ConsumeReadback();
            const auto *rot = reinterpret_cast<const glm::vec4 *>(rot_staging->GetVMAddress());
            const auto *avel = reinterpret_cast<const glm::vec4 *>(angvel_staging->GetVMAddress());
            auto *torque = reinterpret_cast<glm::vec4 *>(torque_staging->GetVMAddress());

            bool wrote_torque = false;
            for (size_t i = 0; i < m_actuators.size(); ++i) {
                if (!m_actuators[i]) {
                    continue;
                }
                const auto &rec = m_joints[i];
                const uint32_t ps = body_to_slot[rec.parent];
                const uint32_t cs = body_to_slot[rec.child];
                const glm::quat rp(rot[ps].w, rot[ps].x, rot[ps].y, rot[ps].z);
                const glm::quat rc(rot[cs].w, rot[cs].x, rot[cs].y, rot[cs].z);
                const glm::quat q_rel = glm::inverse(rp) * rc * glm::inverse(rec.initial_rel_rotation);
                const glm::vec3 axis_world = rp * rec.axis;
                const float angle =
                    2.0f * std::atan2(glm::dot(rec.axis, glm::vec3(q_rel.x, q_rel.y, q_rel.z)), q_rel.w);
                const glm::vec3 omega_child(avel[cs]);
                const glm::vec3 omega_parent(avel[ps]);
                const float omega = glm::dot(omega_child - omega_parent, axis_world);

                const float tau = m_actuators[i]->ComputeTorque(angle, omega);
                const glm::vec3 child_torque = tau * axis_world;
                // Actuator contributions land last within the step and accumulate
                // with whatever the caller wrote into this round.
                torque[cs] += glm::vec4(child_torque, 0.0f);
                torque[ps] += glm::vec4(-child_torque, 0.0f);
                wrote_torque = true;
            }
            if (wrote_torque) {
                any_dirty[BodyFieldIndex(BodyField::ExternalTorque)] = true;
            }
        }

        /// @brief Add a world-space torque to a body's round external-torque value.
        ///
        /// Accumulation is the composition rule for joint torques: two joints that
        /// share a body each add their equal-and-opposite contribution, so a serial
        /// chain keeps both. The value lives only in the current round (the step
        /// consumes it and the readback refreshes the baseline).
        ///
        /// @param body   Body owns a rigid-body slot.
        /// @param torque World-space torque to add.
        void AccumulateBodyTorque(BodyId body, const glm::vec3 &torque) {
            auto *dst = reinterpret_cast<glm::vec4 *>(torque_staging->GetVMAddress());
            dst[body_to_slot[body]] += glm::vec4(torque, 0.0f);
            any_dirty[BodyFieldIndex(BodyField::ExternalTorque)] = true;
        }

        /// @brief Record the state every body and actuator has at `CommitScene`.
        ///
        /// Taken from the freshly seeded staging (see `BuildPhysicsReadback`), so
        /// it is exactly what a read returns before the first step. Only the commit
        /// state is kept; later steps do not touch it.
        void RecordInitialSnapshot() {
            ConsumeReadback();
            InitialSnapshot snap;
            const size_t count = slot_count;
            if (count > 0) {
                const auto *pos = reinterpret_cast<const glm::vec4 *>(pos_staging->GetVMAddress());
                const auto *rot = reinterpret_cast<const glm::vec4 *>(rot_staging->GetVMAddress());
                const auto *lvel = reinterpret_cast<const glm::vec4 *>(linvel_staging->GetVMAddress());
                const auto *avel = reinterpret_cast<const glm::vec4 *>(angvel_staging->GetVMAddress());
                snap.positions.assign(pos, pos + count);
                snap.rotations.assign(rot, rot + count);
                snap.linear_velocities.assign(lvel, lvel + count);
                snap.angular_velocities.assign(avel, avel + count);
            }
            snap.actuator_targets.resize(m_actuators.size());
            for (size_t i = 0; i < m_actuators.size(); ++i) {
                if (m_actuators[i]) {
                    snap.actuator_targets[i] = m_actuators[i]->GetTargetAngle();
                }
            }
            initial_snapshot = std::move(snap);
        }

        /// @brief Build the BodyId->slot mapping, allocate the host staging, and
        /// seed it from the GPU physics buffers.
        void BuildPhysicsReadback() {
            auto *phys_scene = scene->GetPhysicsScene();
            if (!phys_scene) {
                throw std::logic_error("PhysicsApp: no physics scene available for readback");
            }
            auto gpu = phys_scene->GetGpuBuffers();
            slot_count = gpu.rigid_body_slot_count;
            field_src[BodyFieldIndex(BodyField::Position)] = gpu.rigid_body_center_world_position;
            field_src[BodyFieldIndex(BodyField::Rotation)] = gpu.rigid_body_center_world_rotation;
            field_src[BodyFieldIndex(BodyField::LinearVelocity)] = gpu.rigid_body_linear_velocity;
            field_src[BodyFieldIndex(BodyField::AngularVelocity)] = gpu.rigid_body_angular_velocity;
            field_src[BodyFieldIndex(BodyField::ExternalForce)] = gpu.rigid_body_external_force;
            field_src[BodyFieldIndex(BodyField::ExternalTorque)] = gpu.rigid_body_external_torque;

            auto &adaptor = scene->GetPhysicsAdaptor();
            const uint32_t n = builder->GetBodyCount();
            body_to_slot.assign(n, ~0u);
            for (BodyId id = 0; id < n; ++id) {
                const uint32_t slot = adaptor.FindRigidBodyByObjectHandle(builder->GetBodyHandle(id));
                if (slot == PhysicsScene::INVALID_INDEX) {
                    throw std::logic_error("PhysicsApp: body has no rigid-body slot at CommitScene");
                }
                body_to_slot[id] = slot;
            }

            // Mapping uniqueness sanity check.
            std::vector<bool> seen(slot_count, false);
            for (const uint32_t s : body_to_slot) {
                if (s >= slot_count || seen[s]) {
                    throw std::logic_error("PhysicsApp: non-unique or out-of-range rigid-body slot mapping");
                }
                seen[s] = true;
            }

            com_offsets.assign(slot_count, glm::vec3(0.0f));
            for (BodyId id = 0; id < n; ++id) {
                const uint32_t slot = body_to_slot[id];
                com_offsets[slot] = adaptor.GetComOffsetLocal(slot);
            }

            // Allocate the six authoritative staging buffers (fixed size, frozen),
            // one per BodyField. They are the transfer source of the upload and the
            // host-side target of the baseline refresh, with mapped host random
            // access: uniform usage is what lets the round's write lifetime be one
            // rule for every field.
            const size_t bytes = FieldBytes();
            const Rhi::BufferType shared_type{Rhi::BufferTypeBits::StagingToDevice};
            const Rhi::BufferType readback_type{Rhi::BufferTypeBits::ReadbackFromDevice};
            const auto &alloc = renderer->GetAllocatorState();
            pos_staging = Rhi::DeviceBuffer::CreateUnique(alloc, shared_type, bytes, "Physics pos staging");
            rot_staging = Rhi::DeviceBuffer::CreateUnique(alloc, shared_type, bytes, "Physics rot staging");
            linvel_staging = Rhi::DeviceBuffer::CreateUnique(alloc, shared_type, bytes, "Physics linvel staging");
            angvel_staging = Rhi::DeviceBuffer::CreateUnique(alloc, shared_type, bytes, "Physics angvel staging");
            force_staging = Rhi::DeviceBuffer::CreateUnique(alloc, shared_type, bytes, "Physics extforce staging");
            torque_staging = Rhi::DeviceBuffer::CreateUnique(alloc, shared_type, bytes, "Physics exttorque staging");
            field_staging[BodyFieldIndex(BodyField::Position)] = pos_staging.get();
            field_staging[BodyFieldIndex(BodyField::Rotation)] = rot_staging.get();
            field_staging[BodyFieldIndex(BodyField::LinearVelocity)] = linvel_staging.get();
            field_staging[BodyFieldIndex(BodyField::AngularVelocity)] = angvel_staging.get();
            field_staging[BodyFieldIndex(BodyField::ExternalForce)] = force_staging.get();
            field_staging[BodyFieldIndex(BodyField::ExternalTorque)] = torque_staging.get();

            readback_staging[BodyFieldIndex(BodyField::Position)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Physics pos readback");
            readback_staging[BodyFieldIndex(BodyField::Rotation)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Physics rot readback");
            readback_staging[BodyFieldIndex(BodyField::LinearVelocity)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Physics linvel readback");
            readback_staging[BodyFieldIndex(BodyField::AngularVelocity)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Physics angvel readback");
            readback_staging[BodyFieldIndex(BodyField::ExternalForce)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Physics extforce readback");
            readback_staging[BodyFieldIndex(BodyField::ExternalTorque)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, readback_type, bytes, "Physics exttorque readback");

            // Zero the host staging so a field that is uploaded before its first
            // readback (a caller's very first write) starts from a defined baseline
            // instead of uninitialized bytes. The seed copy below replaces it.
            for (auto *buffer : field_staging) {
                if (buffer != nullptr) {
                    std::memset(buffer->GetVMAddress(), 0, bytes);
                    buffer->Flush(0, bytes);
                }
            }

            // One-time seed copy so state reads are legal before the first Step.
            auto &dc = renderer->GetDeviceContext();
            const auto &dev_iface = dc.GetDeviceInterface();
            {
                auto buffers = Rhi::CommandBuffer::Create(dc, Rhi::CommandBufferPool::GraphicsOneTime, 1);
                Rhi::CommandBuffer &cb = buffers[0];
                cb.Begin();
                Rhi::CommandEncoder encoder{cb.GetHandle()};
                RecordBodyStateCopy(encoder);
                cb.End();

                const Rhi::Fence fence = Rhi::Fence::Create(dev_iface, false);
                Rhi::Submission submission{};
                submission.command_buffers.push_back(cb.GetHandle());
                dc.Submit(submission, fence.GetHandle());
                if (!dc.WaitForFence(fence.GetHandle(), std::numeric_limits<uint64_t>::max())) {
                    throw std::runtime_error("PhysicsApp: physics readback seed copy wait failed");
                }
            }

            // The seed copy was device work: fold it into the host staging so reads
            // before the first Step return the loaded pose.
            ConsumeReadback();

            physics_readback_built = true;
        }

        /// @brief Ensure the render readback staging matches the current extent.
        void EnsureRenderStaging(const RenderTargetTexture &final_tex) {
            const auto &desc = final_tex.GetTextureDescription();
            const uint32_t w = desc.width, h = desc.height;
            if (render_staging && render_staging_w == w && render_staging_h == h) {
                return;
            }
            const Rhi::BufferType rt{Rhi::BufferTypeBits::ReadbackFromDevice};
            render_staging = Rhi::DeviceBuffer::CreateUnique(
                renderer->GetAllocatorState(), rt, size_t(w) * h * 4u, "Render readback"
            );
            render_staging_w = w;
            render_staging_h = h;
        }
    };

    std::unique_ptr<PhysicsApp> PhysicsApp::Create(const CreateInfo &info) {
        std::unique_ptr<PhysicsApp> app{new PhysicsApp()};
        auto &impl = *app->m_impl;
        impl.resol_x = info.resol_x;
        impl.resol_y = info.resol_y;
        impl.mode = info.mode;
        impl.background_color = info.background_color;

        if (impl.mode != AppMode::Windowed && impl.mode != AppMode::Offscreen && impl.mode != AppMode::PhysicsOnly) {
            throw std::invalid_argument("PhysicsApp: unknown AppMode");
        }

        auto cmc = MainClass::GetInstance();
        StartupOptions opt{};
        opt.resol_x = static_cast<int>(info.resol_x);
        opt.resol_y = static_cast<int>(info.resol_y);
        opt.title = info.window_title;
        opt.headless = (impl.mode != AppMode::Windowed);

        // Physics timing knobs are creation-time solver configuration: start from
        // the engine's default XPBD config, apply the caller's overrides, and hand
        // the result to the solver creation path. They never enter the engine's
        // startup options or main-class state.
        XpbdConfig solver_config = MakeDefaultSolverConfig();
        if (info.physics_time_step > 0.0f) {
            solver_config.time_step = info.physics_time_step;
        }
        if (info.physics_substeps > 0u) {
            solver_config.num_substep_perstep = info.physics_substeps;
        }
        if (info.physics_iterations > 0u) {
            solver_config.num_iter_persubstep = info.physics_iterations;
        }

        // Keep SIGINT with the host process. SDL installs its own SIGINT/SIGTERM
        // handler at init and converts the signal into an SDL_EVENT_QUIT, which
        // is only ever observed by a loop that drains the window event queue.
        // An embedded host (Python, a test harness) usually does not, so Ctrl+C
        // would become a no-op event nobody reads; leaving the signal alone lets
        // the host raise its own interrupt instead. Must precede SDL_Init.
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        // SDL mutes the console keyboard by default ("so keystrokes don't show up
        // on the console"), which on Windows takes the console out of processed
        // mode: Ctrl+C stops being a console signal and becomes a keystroke SDL
        // swallows, so a terminal cannot interrupt the app at all. Pass console
        // input through instead; the window still receives its own key events.
        SDL_SetHint(SDL_HINT_MUTE_CONSOLE_KEYBOARD, "0");

        cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
        cmc->LoadBuiltinAssets(BuiltinAssetsDir());
        cmc->LoadProject(ProjectsDir() / "empty_with_sky", &solver_config);

        impl.main_class = cmc;
        impl.world = cmc->GetWorldSystem().get();
        impl.renderer = cmc->GetRenderSystem().get();
        impl.physics = cmc->GetPhysicsSystem().get();
        impl.input = cmc->GetInputSystem().get();
        impl.time = cmc->GetTimeSystem().get();
        impl.scene = &impl.world->GetMainSceneRef();

        // The control hint is an ImGui overlay on the presented frame; it exists
        // only for a windowed run (headless initialization creates no GUI
        // system). The backend is created against the render graph's final
        // color format, which `BuildDefaultRenderGraph` fixes at R8G8B8A8UNorm.
        impl.gui = cmc->GetGUISystem().get();
        if (impl.mode == AppMode::Windowed && impl.gui != nullptr) {
            impl.gui->CreateBackend(*impl.renderer, Rhi::ImageFormat::R8G8B8A8UNorm);
        } else {
            impl.gui = nullptr;
        }

        // Register input axes used by the built-in camera controls and the
        // SPACE pause toggle (only when a window/input exists).
        auto input = impl.input;
        if (input) {
            input->AddAxis(Input::ButtonAxis("move forward", Input::AxisType::TypeKey, "w", "s"));
            input->AddAxis(Input::ButtonAxis("move right", Input::AxisType::TypeKey, "d", "a"));
            input->AddAxis(Input::ButtonAxis("move up", Input::AxisType::TypeKey, "e", "q"));
            input->AddAxis(
                Input::MotionAxis(
                    "look x", Input::AxisType::TypeMouseMotion, "x", 0.3f, 3.0f, 0.001f, 3.0f, false, true
                )
            );
            input->AddAxis(
                Input::MotionAxis(
                    "look y", Input::AxisType::TypeMouseMotion, "y", 0.3f, 3.0f, 0.001f, 3.0f, false, true
                )
            );
            input->AddAxis(Input::ButtonAxis("mouse right", Input::AxisType::TypeMouseButton, "mouse right", ""));
            input->AddAxis(Input::ButtonAxis("toggle simulation", Input::AxisType::TypeKey, "space", ""));
            // Camera movement arming keys: the fly controls translate only while
            // either Alt is held (see CameraControllerComponent::Tick).
            input->AddAxis(Input::ButtonAxis("camera alt left", Input::AxisType::TypeKey, "left alt", ""));
            input->AddAxis(Input::ButtonAxis("camera alt right", Input::AxisType::TypeKey, "right alt", ""));
        }

        // Root GameObject parenting all app-created scene objects.
        auto &root = impl.scene->CreateGameObject();
        {
            Transform t;
            t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
            root.SetTransform(t);
        }
        impl.root = &root;

        auto *adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase()).get();
        const bool with_visuals = (impl.mode != AppMode::PhysicsOnly);
        impl.builder = std::make_unique<SceneBuilder>(*impl.scene, *adb, root, with_visuals);

        // Built-in camera with fly controls (skipped in headless mode).
        if (impl.mode != AppMode::PhysicsOnly) {
            auto &camera_object = impl.scene->CreateGameObject();
            camera_object.SetParent(root.GetHandle());
            impl.camera_object = &camera_object;
            auto &camera_comp = camera_object.AddComponent<CameraComponent>();
            camera_comp.m_camera->set_aspect_ratio(
                1.0f * static_cast<float>(info.resol_x) / static_cast<float>(info.resol_y)
            );
            impl.camera_component = &camera_comp;
            // The fly-controls controller needs window input; add it only in
            // windowed mode. Offscreen renders with a static camera.
            if (impl.mode == AppMode::Windowed) {
                impl.camera_controller = &camera_object.AddComponent<CameraControllerComponent>();
            }
            impl.camera_fov = camera_comp.m_camera->m_fov_vertical;
            impl.world->SetActiveCamera(camera_comp.GetHandle(), &impl.renderer->GetCameraManager());

            // Default camera pose looking at the origin.
            app->SetCameraPose(glm::vec3(6.0f, -5.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        }

        return app;
    }

    PhysicsApp::PhysicsApp() : m_impl(std::make_unique<Impl>()) {
    }

    PhysicsApp::~PhysicsApp() {
        // Drain the GPU before destroying the render graph / resizable RTT...
        if (m_impl && m_impl->renderer) {
            m_impl->renderer->WaitForIdle();
        }
    }

    // ── Building phase ──────────────────────────────────────────────────

    BodyId PhysicsApp::AddBox(const BoxDesc &desc) {
        if (m_impl->phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        return m_impl->builder->AddBox(desc);
    }

    BodyId PhysicsApp::AddSphere(const SphereDesc &desc) {
        if (m_impl->phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        return m_impl->builder->AddSphere(desc);
    }

    BodyId PhysicsApp::AddCylinder(const CylinderDesc &desc) {
        if (m_impl->phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        return m_impl->builder->AddCylinder(desc);
    }

    void PhysicsApp::AddFixedJoint(BodyId obj1, BodyId obj2, const FixedJointParams &params) {
        if (m_impl->phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        m_impl->builder->AddFixedJoint(obj1, obj2, params);
    }

    JointId PhysicsApp::AddHingeJoint(BodyId obj1, BodyId obj2, const HingeJointParams &params) {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        impl.builder->AddHingeJoint(obj1, obj2, params);

        Impl::JointRecord record;
        record.parent = obj1;
        record.child = obj2;
        record.axis = params.axis_obj1;
        if (glm::length(record.axis) < 1e-6f) {
            record.axis = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            record.axis = glm::normalize(record.axis);
        }
        // Manual joints: the constraint component is on obj1 (parent), so the
        // axis is already in the parent frame.
        record.axis_in_child_frame = false;

        impl.m_joints.push_back(std::move(record));
        impl.m_actuators.emplace_back();
        return static_cast<JointId>(impl.m_joints.size() - 1);
    }

    UrdfImportResult PhysicsApp::LoadUrdf(const UrdfImportConfig &config) {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }

        if (!std::filesystem::exists(config.urdf_path)) {
            throw std::runtime_error("PhysicsApp: URDF file does not exist: " + config.urdf_path.string());
        }

        Engine::UrdfLoader loader;
        Engine::UrdfRobot robot = loader.ParseUrdf(config.urdf_path);
        if (robot.links.empty()) {
            throw std::runtime_error(
                "PhysicsApp: failed to parse URDF or robot has no links: " + config.urdf_path.string()
            );
        }

        Engine::UrdfBuildOptions opts;
        opts.position = config.position;
        opts.rotation = config.rotation;
        opts.static_friction = config.static_friction;
        opts.dynamic_friction = config.dynamic_friction;
        opts.restitution = config.restitution;
        opts.with_visuals = impl.mode != AppMode::PhysicsOnly;

        Engine::UrdfBuiltRobot built = loader.BuildRobotScene(robot, *impl.scene, impl.root, opts);

        std::unordered_map<std::string, const Engine::UrdfJoint *> joint_by_name;
        for (const auto &j : robot.joints) {
            joint_by_name.emplace(j.name, &j);
        }

        std::unordered_map<Engine::ObjectHandle, BodyId> handle_to_body;
        UrdfImportResult result;
        for (const auto &[name, handle] : built.link_objects) {
            BodyId id = impl.builder->RegisterExistingBody(handle);
            result.link_bodies[name] = id;
            handle_to_body[handle] = id;
        }
        for (const auto &[name, pair] : built.joint_objects) {
            const BodyId parent_id = handle_to_body.at(pair.parent);
            const BodyId child_id = handle_to_body.at(pair.child);
            const JointId id = static_cast<JointId>(impl.m_joints.size());

            Impl::JointRecord record;
            record.name = name;
            record.parent = parent_id;
            record.child = child_id;
            record.axis = pair.axis;
            if (glm::length(record.axis) < 1e-6f) {
                record.axis = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                record.axis = glm::normalize(record.axis);
            }
            // URDF joints put the constraint on the child link, so the axis the
            // engine provides is in the child frame; it is rotated to the parent
            // frame at CommitScene.
            record.axis_in_child_frame = true;
            if (auto it = joint_by_name.find(name);
                it != joint_by_name.end() && it->second->type == Engine::UrdfJointType::Revolute) {
                JointLimits lim;
                lim.lower = it->second->limit_lower;
                lim.upper = it->second->limit_upper;
                lim.effort = it->second->limit_effort;
                lim.velocity = it->second->limit_velocity;
                record.limits = lim;
            }

            impl.m_joints.push_back(std::move(record));
            impl.m_actuators.emplace_back();
            result.joint_bodies[name] = {parent_id, child_id, id};
        }

        return result;
    }

    void PhysicsApp::AddActuator(JointId joint, std::shared_ptr<Actuator> actuator) {
        auto &impl = *m_impl;
        if (joint >= impl.m_actuators.size()) {
            throw std::out_of_range("PhysicsApp: AddActuator invalid JointId");
        }
        if (!actuator) {
            throw std::invalid_argument("PhysicsApp: AddActuator null actuator");
        }
        if (impl.m_actuators[joint]) {
            throw std::invalid_argument("PhysicsApp: AddActuator joint already has an actuator");
        }
        impl.m_actuators[joint] = std::move(actuator);
    }

    std::shared_ptr<Actuator> PhysicsApp::GetActuator(JointId joint) const {
        const auto &impl = *m_impl;
        if (joint >= impl.m_actuators.size()) {
            throw std::out_of_range("PhysicsApp: GetActuator invalid JointId");
        }
        return impl.m_actuators[joint];
    }

    void PhysicsApp::RemoveActuator(JointId joint) {
        auto &impl = *m_impl;
        if (joint >= impl.m_actuators.size()) {
            throw std::out_of_range("PhysicsApp: RemoveActuator invalid JointId");
        }
        if (!impl.m_actuators[joint]) {
            throw std::logic_error("PhysicsApp: RemoveActuator joint has no actuator");
        }
        impl.m_actuators[joint].reset();

        // Nothing else to clean up: actuator torques live only in the round that
        // produced them (see RunActuators and the round write lifetime), so a
        // detached actuator leaves no torque behind and the joint is immediately
        // available to `AddJointTorque`.
    }

    void PhysicsApp::SetTargetAngle(JointId joint, float target) {
        auto &impl = *m_impl;
        if (joint >= impl.m_actuators.size()) {
            throw std::out_of_range("PhysicsApp: SetTargetAngle invalid JointId");
        }
        if (!impl.m_actuators[joint]) {
            throw std::logic_error("PhysicsApp: SetTargetAngle joint has no actuator");
        }
        impl.m_actuators[joint]->SetTargetAngle(target);
    }

    void PhysicsApp::AddDirectionalLight(const DirectionalLightParams &params) {
        if (m_impl->mode == AppMode::PhysicsOnly) {
            throw std::logic_error("PhysicsApp: AddDirectionalLight not available in headless mode");
        }
        if (m_impl->phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        auto &impl = *m_impl;
        auto &light_obj = impl.scene->CreateGameObject();
        light_obj.SetParent(impl.root->GetHandle());
        auto &light_comp = light_obj.AddComponent<LightComponent>();
        light_comp.m_type = LightType::Directional;
        light_comp.m_color = params.color;
        light_comp.m_intensity = params.intensity;
        light_comp.m_cast_shadow = params.cast_shadow;

        // Directional lights point along local +Y in the engine; rotate +Y to
        // face the requested world-space direction.
        glm::vec3 dir = params.direction;
        float len = glm::length(dir);
        if (len < 1e-6f) {
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            dir /= len;
        }
        glm::quat rot = QuatFromVecToVec(glm::vec3(0.0f, 1.0f, 0.0f), dir);
        Transform t;
        t.SetRotation(rot);
        light_obj.SetTransform(t);
    }

    void PhysicsApp::SetCameraPose(const glm::vec3 &position, const glm::vec3 &look_target) {
        if (m_impl->mode == AppMode::PhysicsOnly) {
            throw std::logic_error("PhysicsApp: SetCameraPose not available in headless mode");
        }
        auto &impl = *m_impl;
        if (!impl.camera_object) return;

        glm::vec3 dir = look_target - position;
        float len = glm::length(dir);
        if (len < 1e-6f) return;
        dir /= len;

        // Z-up locked orientation: yaw around world Z, pitch around world X.
        // Verified to match the CameraControllerComponent reconstruction.
        float hor = glm::length(glm::vec2(dir.x, dir.y));
        float yaw = glm::degrees(glm::atan(-dir.x, dir.y));
        float pitch = glm::degrees(glm::atan(dir.z, hor));

        glm::quat qYaw = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat qPitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        Transform t;
        t.SetPosition(position);
        t.SetRotation(qYaw * qPitch);
        impl.camera_object->SetTransform(t);
        if (impl.camera_controller) {
            impl.camera_controller->m_yaw = yaw;
            impl.camera_controller->m_pitch = pitch;
        }
    }

    void PhysicsApp::CommitScene() {
        auto &impl = *m_impl;
        if (impl.phase == Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: CommitScene called twice");
        }

        // Queue creations become live; components run Init (rigid body slots,
        // joint setup).
        impl.scene->FlushCmdQueue();
        impl.scene->AddInitEvent();
        impl.scene->ProcessEvents();

        // Finalize joint axis (parent frame) and initial relative rotation from
        // the now-final GO world transforms, matching the engine's constraint math.
        impl.ComputeJointTransforms();

        // One-time CPU -> GPU sync of physics descriptors.
        impl.scene->FlushPhysics(*impl.renderer);

        // Build the BodyId->slot readback BEFORE freezing: FlushPhysics has
        // assigned slots, and the GPU buffers exist. Seeds the initial state so
        // reads are legal before the first Step, and allocates the write staging
        // (same frozen sizing) for SetBodyValue.
        impl.BuildPhysicsReadback();

        // Episode-start snapshot: exactly the seeded state, so ResetToInitial can
        // restore it without any caller-side recording call.
        impl.RecordInitialSnapshot();

        // One-time initial model matrix production (rendering modes only), from
        // the FlushPhysics-seeded poses. Model matrices are produced only when a
        // caller asks for them, so without this the renderer would read the
        // buffer's creation-time contents while paused. Only the model matrix
        // entry point is invoked: the step pipeline does not run, and no body
        // advances.
        if (impl.mode != AppMode::PhysicsOnly) {
            auto *phys_scene = impl.scene->GetPhysicsScene();
            auto &scene_data = impl.renderer->GetSceneDataManager();
            scene_data.EnsureModelMatricesCapacity(phys_scene->GetGpuBuffers().rigid_body_slot_count);

            auto &dc = impl.renderer->GetDeviceContext();
            const auto &dev_iface = dc.GetDeviceInterface();
            auto buffers = Rhi::CommandBuffer::Create(dc, Rhi::CommandBufferPool::GraphicsOneTime, 1);
            Rhi::CommandBuffer &cb = buffers[0];
            cb.Begin();
            Rhi::CommandEncoder model_matrix_encoder{cb.GetHandle()};
            impl.physics->GPUCalcModelMatrices(*phys_scene, model_matrix_encoder, scene_data.GetModelMatricesBuffer());
            cb.End();

            const Rhi::Fence fence = Rhi::Fence::Create(dev_iface, false);
            Rhi::Submission submission{};
            submission.command_buffers.push_back(cb.GetHandle());
            dc.Submit(submission, fence.GetHandle());
            if (!dc.WaitForFence(fence.GetHandle(), std::numeric_limits<uint64_t>::max())) {
                throw std::runtime_error("PhysicsApp: physics model-matrix seed wait failed");
            }
        }

        // Build the default render graph once. The builder must outlive the graph:
        // its pass lambdas capture references into it. (Skipped headless.)
        if (impl.mode != AppMode::PhysicsOnly) {
            impl.rg_builder = std::make_unique<ComplexRenderGraphBuilder>(*impl.renderer);
            RGTextureHandle final_color_id{0};
            // Solid background clear color (linear rgb); replaces the skybox.
            const AttachmentUtils::ColorClearValue background{
                impl.background_color.r, impl.background_color.g, impl.background_color.b, 1.0f
            };
            auto rg = impl.rg_builder->BuildDefaultRenderGraph(final_color_id, background, impl.gui);
            impl.render_graph = std::move(rg);
            impl.final_color_id = final_color_id;
        }

        // Freeze the scene and start paused (mirrors the previous example UX).
        // Pause is now an app-level flag; the scene simulation is enabled exactly
        // once here and never toggled by the app afterwards.
        impl.phase = Impl::Phase::Committed;
        impl.paused = true;
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            phys_scene->SetSimulationEnabled(true);
        }
        impl.scene->GetPhysicsAdaptor().SetPhysicsActive(true);
    }

    // ── Drive phase ─────────────────────────────────────────────────────

    void PhysicsApp::Step() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: Step called before CommitScene");
        }

        // Pre-wait: render frames (3 in flight) must finish reading the physics
        // buffers before the next physics step writes them.
        impl.renderer->WaitForIdle();

        // Dedicated command buffer for physics (independent from the render main
        // command buffer). The engine's RHI provides queue + pool through the
        // DeviceInterface, replacing the SubmissionHelper in this design.
        auto &dc = impl.renderer->GetDeviceContext();
        const auto &dev_iface = dc.GetDeviceInterface();

        auto buffers = Rhi::CommandBuffer::Create(dc, Rhi::CommandBufferPool::GraphicsOneTime, 1);
        Rhi::CommandBuffer &cb = buffers[0];
        // Open the epoch this step's submission belongs to before recording
        auto &epoch_tracker = dc.GetEpochTracker();
        const Rhi::EpochWatermark step_epoch = epoch_tracker.BeginEpoch();
        cb.Begin();
        Rhi::CommandEncoder step_encoder{cb.GetHandle()};
        try {
            // Measure joint state from the last readback and write actuator torques
            // into the ExternalTorque staging before the solver reads them.
            impl.RunActuators();
            // Make pending host writes device-visible once per dirty field, then
            // upload the dirty fields so the solver reads the overridden values.
            impl.FlushDirtyFieldStaging();
            impl.RecordBodyStateUpload(step_encoder);
            impl.physics->GPUStep(step_encoder);
            // Produce model matrices on the same command buffer, after the step, so
            // that a render frame following this step observes the new poses.
            if (impl.mode != AppMode::PhysicsOnly) {
                if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
                    auto &scene_data = impl.renderer->GetSceneDataManager();
                    scene_data.EnsureModelMatricesCapacity(phys_scene->GetGpuBuffers().rigid_body_slot_count);
                    impl.physics->GPUCalcModelMatrices(*phys_scene, step_encoder, scene_data.GetModelMatricesBuffer());
                }
            }
            // Physics readback: copy the SoA into resident staging in the same CB,
            // so the existing fence wait makes the state CPU-visible on return.
            impl.RecordBodyStateCopy(step_encoder);
            cb.End();
        } catch (...) {
            // Nothing reached the queue, so the epoch produced no GPU work and
            // must not be left to stall the completed prefix.
            epoch_tracker.AbandonEpoch(step_epoch);
            throw;
        }

        const Rhi::Fence fence = Rhi::Fence::Create(dev_iface, false);
        Rhi::Submission submission{};
        submission.command_buffers.push_back(cb.GetHandle());
        dc.Submit(submission, fence.GetHandle());
#if defined(__EMSCRIPTEN__)
        // A page cannot block on the device: a work-done callback only arrives when the browser
        // turns its event loop, and this thread is holding it. The submission is queued and the next
        // readback waits for it through the map instead - which is what the browser build of
        // `WaitForFence` does when it is handed a zero budget.
        dc.WaitForFence(fence.GetHandle(), 0);
#else
        if (!dc.WaitForFence(fence.GetHandle(), std::numeric_limits<uint64_t>::max())) {
            throw std::runtime_error("PhysicsApp: physics submission wait failed");
        }
#endif
        // The completion signal of this step's own submission has been observed
        epoch_tracker.ReportComplete(step_epoch);

        // The recorded uploads have been submitted and executed; clear the dirty
        // flags so unset fields are not re-uploaded next step.
        impl.any_dirty.fill(false);

        // The readback copy was device work: on a backend that was waited on it is
        // already complete, so the baseline can be refreshed here. A page never waited,
        // so it leaves the refresh to the first state query - the map there is what
        // waits, and it happens outside this recording thread's event-loop ownership.
#if !defined(__EMSCRIPTEN__)
        impl.ConsumeReadback();
#endif

        // No wait after the fence: it already proves this step's work has run, and the next render
        // frame's submission waits on the step's own work through the queue's ordering. A second
        // device-wide wait here would be exactly the block a frame path is meant not to have.
    }

    void PhysicsApp::RenderNextFrame() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: RenderNextFrame called before CommitScene");
        }
        if (impl.mode == AppMode::PhysicsOnly) {
            throw std::logic_error("PhysicsApp: RenderNextFrame not available in headless mode");
        }

        impl.time->NextFrame();

        // Window input is processed only in windowed mode; offscreen has no
        // window/input and no SPACE toggle.
        if (impl.mode == AppMode::Windowed) {
            // Edges live for exactly one sample: clear the previous drain's
            // before collecting this one.
            impl.keys_pressed.clear();
            impl.keys_released.clear();

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                // SDL3 splits "the user closed the window" (the X button) from
                // "the application should quit" (SDL_EVENT_QUIT, which the SDL
                // signal handler and session shutdown produce). Both mean the
                // host loop has to stop, so both latch the same flag.
                if (event.type == SDL_EVENT_QUIT) {
                    impl.should_quit = true;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PhysicsApp: quit event received");
                } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    impl.should_quit = true;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PhysicsApp: window close requested");
                }
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    impl.keys_down.insert(event.key.scancode);
                    impl.keys_pressed.insert(event.key.scancode);
                } else if (event.type == SDL_EVENT_KEY_UP) {
                    impl.keys_down.erase(event.key.scancode);
                    impl.keys_released.insert(event.key.scancode);
                }
                if (impl.gui) {
                    impl.gui->ProcessEvent(&event);
                }
                if (impl.input) {
                    impl.input->ProcessEvent(&event);
                }
            }
            if (impl.input) {
                impl.input->Update(impl.time->GetDeltaTimeInSeconds());
            }

            // Built-in SPACE pause toggle.
            if (impl.input) {
                float toggle = impl.input->GetAxisRaw("toggle simulation");
                if (toggle > 0.0f && !impl.toggle_was_pressed) {
                    if (impl.paused) {
                        Resume();
                    } else {
                        Pause();
                    }
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Simulation %s", impl.paused ? "paused" : "resumed");
                }
                impl.toggle_was_pressed = (toggle > 0.0f);
            }
        }

        // Component ticks: camera controller + CameraComponent view update.
        impl.scene->AddTickEvent();
        impl.scene->ProcessEvents();

        impl.world->UpdateRendererData(*impl.renderer);

        // Keep the render-owned model matrices buffer large enough for this
        // frame's rigid body slot count before StartFrame writes the frame's
        // descriptor set, so the handle bound for the frame is final.
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            impl.renderer->GetSceneDataManager().EnsureModelMatricesCapacity(
                phys_scene->GetGpuBuffers().rigid_body_slot_count
            );
        }

        // Keep the active camera's aspect ratio aligned with the present extent,
        // and mirror the SceneWidget FOV heuristic: fixed vertical FOV in
        // landscape, fixed horizontal FOV in portrait, so the visible extent is
        // stable across a resize (see design D4). Interim shim — no camera-API
        // commitment.
        if (impl.camera_component) {
            const Rhi::Extent2D present_extent = impl.renderer->GetPresentProvider().GetExtent();
            if ((present_extent.width > 0 && present_extent.height > 0)
                && (present_extent.width != impl.last_present_width
                    || present_extent.height != impl.last_present_height)) {
                impl.last_present_width = present_extent.width;
                impl.last_present_height = present_extent.height;
                if (auto active_camera = impl.world->GetActiveCamera()) {
                    float aspect = static_cast<float>(present_extent.width) / static_cast<float>(present_extent.height);
                    active_camera->set_aspect_ratio(aspect);
                    if (aspect > 1.0f) {
                        active_camera->set_fov_vertical(impl.camera_fov);
                    } else {
                        active_camera->set_fov_horizontal(impl.camera_fov);
                    }
                }
            }
        }

        const uint32_t frame_index = impl.renderer->TryStartFrame();
        if (frame_index == Engine::k_frame_busy || frame_index == std::numeric_limits<uint32_t>::max()) {
            // The slot is still in flight, or the presentation target is out of date: skip this
            // frame and try again next call. A previous render-readback capture remains valid.
            return;
        }

        // Prepare the ImGui overlay frame before the command buffer records the
        // render graph: the graph's GUI pass consumes the draw data built here.
        // Doing this only after a successful StartFrame keeps the ImGui
        // frame/Render pairing intact when a frame is skipped.
        if (impl.gui != nullptr && impl.render_graph && impl.render_graph->GetNumPasses() > 0) {
            ImGui::SetCurrentContext(impl.gui->GetCurrentContext());
            impl.gui->PrepareGUI();
            DrawControlHint();
        }
        auto cb = impl.renderer->GetFrameManager().BeginMainCommandBuffer();
        if (impl.render_graph && impl.render_graph->GetNumPasses() > 0) {
            impl.render_graph->RecordAllPasses(cb.GetEncoder());
        }

        // Opt-in render readback: record a copy of the final RTT into the
        // resident staging buffer after all passes.
        //
        // The layout the final color target is left in depends on what the graph
        // last did with it: the ImGui overlay pass ends in a color attachment
        // (e.COLOR_ATTACHMENT_OPTIMAL), while without it the bloom compute pass
        // leaves it as a storage image (e.GENERAL). Both the readback copy and
        // the present blit derive their source layout from this value, so it
        // must match the graph's real last access.
        Rhi::MemoryAccessTypeImageBits final_last_access = impl.gui != nullptr
                                                               ? Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite
                                                               : Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite;
        if (impl.render_readback_enabled && impl.render_graph) {
            auto *final_tex = impl.render_graph->GetInternalTextureResource(impl.final_color_id);
            impl.EnsureRenderStaging(*final_tex);
            // The previous frame's capture has been read by now; a mapped buffer cannot be the
            // destination of this frame's copy.
            impl.render_staging->ReleaseReadback();
            cb.RecordCopyImageToBuffer(*final_tex, *impl.render_staging, final_last_access);
            impl.render_frame_id = impl.has_render_capture ? impl.render_frame_id + 1 : 0;
            impl.has_render_capture = true;
        }

        impl.renderer->CompleteFrame(
            *impl.render_graph->GetInternalTextureResource(impl.final_color_id), final_last_access
        );

        // While paused the caller's loop does not call Step(), so nothing drains
        // the renderer: the present queue lags the graphics queue and FrameManager
        // re-signals its frame-completed semaphore before the previous present
        // completes (VUID-vkQueueSubmit2-semaphore-03868). Drain here instead;
        // when running, Step's own WaitForIdle calls cover this.
        if (impl.paused) {
            impl.renderer->WaitForIdle();
        }
    }

    void PhysicsApp::Pause() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: Pause called before CommitScene");
        }
        impl.paused = true;
    }

    void PhysicsApp::Resume() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: Resume called before CommitScene");
        }
        impl.paused = false;
    }

    bool PhysicsApp::IsPaused() const {
        return m_impl->paused;
    }

    bool PhysicsApp::ShouldQuit() const {
        // Headless/offscreen have no window close event source.
        if (m_impl->mode != AppMode::Windowed) {
            return false;
        }
        return m_impl->should_quit;
    }

    bool PhysicsApp::IsKeyDown(const std::string &key) const {
        // Headless/offscreen never drain a window event queue.
        if (m_impl->mode != AppMode::Windowed) {
            return false;
        }
        const int scancode = ScancodeForName(key);
        return scancode >= 0 && m_impl->keys_down.contains(static_cast<uint32_t>(scancode));
    }

    bool PhysicsApp::IsKeyPressed(const std::string &key) const {
        if (m_impl->mode != AppMode::Windowed) {
            return false;
        }
        const int scancode = ScancodeForName(key);
        return scancode >= 0 && m_impl->keys_pressed.contains(static_cast<uint32_t>(scancode));
    }

    bool PhysicsApp::IsKeyReleased(const std::string &key) const {
        if (m_impl->mode != AppMode::Windowed) {
            return false;
        }
        const int scancode = ScancodeForName(key);
        return scancode >= 0 && m_impl->keys_released.contains(static_cast<uint32_t>(scancode));
    }

    std::vector<std::string> PhysicsApp::GetKeysDown() const {
        std::vector<std::string> names;
        if (m_impl->mode != AppMode::Windowed || m_impl->keys_down.empty()) {
            return names;
        }
        names.reserve(m_impl->keys_down.size());
        for (const auto &[name, code] : Input::k_name_code_map) {
            if (m_impl->keys_down.contains(code)) {
                names.push_back(name);
            }
        }
        // The map is unordered; sort so a readout is stable between samples.
        std::sort(names.begin(), names.end());
        return names;
    }

    // ── Drive-phase readback ───────────────────────────────────────────

    void PhysicsApp::SetRenderReadbackEnabled(bool enabled) {
        auto &impl = *m_impl;
        if (impl.mode == AppMode::PhysicsOnly && enabled) {
            throw std::logic_error("PhysicsApp: render readback not available in headless mode");
        }
        impl.render_readback_enabled = enabled;
    }

    RenderOutput PhysicsApp::GetRenderOutput() {
        auto &impl = *m_impl;
        if (impl.mode == AppMode::PhysicsOnly) {
            throw std::logic_error("PhysicsApp: GetRenderOutput not available in headless mode");
        }
        if (!impl.render_readback_enabled) {
            throw std::logic_error("PhysicsApp: GetRenderOutput called while readback is disabled");
        }
        if (!impl.has_render_capture) {
            throw std::logic_error("PhysicsApp: GetRenderOutput called before any frame was captured");
        }
        impl.renderer->GetFrameManager().WaitForFrameCompletion();
        return {
            impl.render_staging->GetVMAddress(), impl.render_staging_w, impl.render_staging_h, impl.render_frame_id
        };
    }

    BodyState PhysicsApp::GetBodyState(BodyId id) const {
        const auto &impl = *m_impl;
        if (!impl.physics_readback_built) {
            throw std::logic_error("PhysicsApp: GetBodyState called before CommitScene");
        }
        if (id == INVALID_BODY_ID || id >= impl.body_to_slot.size()) {
            throw std::out_of_range("PhysicsApp: invalid BodyId");
        }
        impl.ConsumeReadback();
        const auto *pos = reinterpret_cast<const glm::vec4 *>(impl.pos_staging->GetVMAddress());
        const auto *rot = reinterpret_cast<const glm::vec4 *>(impl.rot_staging->GetVMAddress());
        const auto *lvel = reinterpret_cast<const glm::vec4 *>(impl.linvel_staging->GetVMAddress());
        const auto *avel = reinterpret_cast<const glm::vec4 *>(impl.angvel_staging->GetVMAddress());
        const uint32_t slot = impl.body_to_slot[id];
        const glm::vec4 p = pos[slot];
        const glm::vec4 r = rot[slot];
        return {glm::vec3(p), glm::quat(r.w, r.x, r.y, r.z), glm::vec3(lvel[slot]), glm::vec3(avel[slot])};
    }

    JointState PhysicsApp::GetJointState(JointId joint) const {
        const auto &impl = *m_impl;
        if (!impl.physics_readback_built) {
            throw std::logic_error("PhysicsApp: GetJointState called before CommitScene");
        }
        if (joint >= impl.m_joints.size()) {
            throw std::out_of_range("PhysicsApp: GetJointState invalid JointId");
        }
        const auto &rec = impl.m_joints[joint];
        const BodyState parent = GetBodyState(rec.parent);
        const BodyState child = GetBodyState(rec.child);

        // Deviation rotation of the child relative to the parent from the initial
        // pose, expressed in the parent frame.
        const glm::quat q_rel = glm::inverse(parent.rotation) * child.rotation * glm::inverse(rec.initial_rel_rotation);
        const glm::vec3 axis_world = parent.rotation * rec.axis;
        const float angle = 2.0f * std::atan2(glm::dot(rec.axis, glm::vec3(q_rel.x, q_rel.y, q_rel.z)), q_rel.w);
        const float omega = glm::dot(child.angular_velocity - parent.angular_velocity, axis_world);
        return {angle, omega};
    }

    std::optional<JointLimits> PhysicsApp::GetJointLimits(JointId joint) const {
        const auto &impl = *m_impl;
        if (joint >= impl.m_joints.size()) {
            throw std::out_of_range("PhysicsApp: GetJointLimits invalid JointId");
        }
        return impl.m_joints[joint].limits;
    }

    BodyStatesView PhysicsApp::GetBodyStates() const {
        const auto &impl = *m_impl;
        if (!impl.physics_readback_built) {
            throw std::logic_error("PhysicsApp: GetBodyStates called before CommitScene");
        }
        const uint32_t cnt = impl.slot_count;
        impl.ConsumeReadback();
        return {
            {impl.body_to_slot.data(), impl.body_to_slot.size()},
            {impl.com_offsets.data(), impl.com_offsets.size()},
            {reinterpret_cast<const glm::vec4 *>(impl.pos_staging->GetVMAddress()), cnt},
            {reinterpret_cast<const glm::vec4 *>(impl.rot_staging->GetVMAddress()), cnt},
            {reinterpret_cast<const glm::vec4 *>(impl.linvel_staging->GetVMAddress()), cnt},
            {reinterpret_cast<const glm::vec4 *>(impl.angvel_staging->GetVMAddress()), cnt}
        };
    }

    void PhysicsApp::SetBodyValue(BodyId id, BodyField field, glm::vec4 value) {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: SetBodyValue called before CommitScene");
        }
        if (id >= impl.body_to_slot.size()) {
            throw std::out_of_range("PhysicsApp: SetBodyValue invalid BodyId");
        }

        if (field == BodyField::Rotation) {
            glm::quat q(value.w, value.x, value.y, value.z);
            q = glm::normalize(q);
            value = glm::vec4(q.x, q.y, q.z, q.w);
        }

        const size_t idx = Impl::BodyFieldIndex(field);
        const uint32_t slot = impl.body_to_slot[id];
        // Take the pending readback first: the write overlays the refreshed baseline,
        // so a write made between Step calls is not clobbered by that step's result.
        impl.ConsumeReadback();
        // Plain host store into the field's staging: that staging is the buffer state
        // reads use, so the write is observable immediately. No device call here - Step
        // flushes once per dirty field.
        auto *dst = reinterpret_cast<glm::vec4 *>(impl.field_staging[idx]->GetVMAddress());
        dst[slot] = value;
        impl.any_dirty[idx] = true;
    }

    void PhysicsApp::ResetToInitial() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed || !impl.initial_snapshot) {
            throw std::logic_error("PhysicsApp: ResetToInitial called before CommitScene");
        }
        const auto &snap = *impl.initial_snapshot;
        const size_t bytes = impl.FieldBytes();

        // The reset overwrites the whole baseline, so the last step's readback is not
        // allowed to land on top of it afterwards.
        impl.ConsumeReadback();

        if (bytes > 0) {
            // Restore pose and velocity through the shared staging: reads observe the
            // restored state immediately, and the next Step uploads it (velocity is
            // an integration seed for XPBD's pose-delta derivation). Overwriting the
            // staging also discards any uncommitted round writes of those fields.
            std::memcpy(impl.pos_staging->GetVMAddress(), snap.positions.data(), bytes);
            std::memcpy(impl.rot_staging->GetVMAddress(), snap.rotations.data(), bytes);
            std::memcpy(impl.linvel_staging->GetVMAddress(), snap.linear_velocities.data(), bytes);
            std::memcpy(impl.angvel_staging->GetVMAddress(), snap.angular_velocities.data(), bytes);

            // Force and torque are round commands, not state: zeroing both discards
            // any pending round writes so no force or torque from the finished
            // episode leaks into the next one.
            std::memset(impl.force_staging->GetVMAddress(), 0, bytes);
            std::memset(impl.torque_staging->GetVMAddress(), 0, bytes);
        }

        for (const BodyField field :
             {BodyField::Position,
              BodyField::Rotation,
              BodyField::LinearVelocity,
              BodyField::AngularVelocity,
              BodyField::ExternalForce,
              BodyField::ExternalTorque}) {
            impl.any_dirty[Impl::BodyFieldIndex(field)] = true;
        }

        // Restore commit-time targets and clear control state. A target only exists
        // for actuators registered at CommitScene; later registrations keep theirs.
        for (size_t i = 0; i < impl.m_actuators.size(); ++i) {
            if (!impl.m_actuators[i]) {
                continue;
            }
            if (i < snap.actuator_targets.size() && snap.actuator_targets[i].has_value()) {
                impl.m_actuators[i]->SetTargetAngle(*snap.actuator_targets[i]);
            }
            impl.m_actuators[i]->Reset();
        }
    }

    void PhysicsApp::AddJointTorque(JointId joint, float torque) {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: AddJointTorque called before CommitScene");
        }
        if (joint >= impl.m_joints.size()) {
            throw std::out_of_range("PhysicsApp: AddJointTorque invalid JointId");
        }
        if (impl.m_actuators[joint]) {
            throw std::logic_error("PhysicsApp: AddJointTorque joint has an actuator");
        }

        // Resolve the world-space hinge axis from the latest readback rotations:
        // the recorded axis lives in the parent's frame (finalized at CommitScene).
        impl.ConsumeReadback();
        const auto &rec = impl.m_joints[joint];
        const auto *rot = reinterpret_cast<const glm::vec4 *>(impl.rot_staging->GetVMAddress());
        const glm::vec4 rp = rot[impl.body_to_slot[rec.parent]];
        const glm::quat parent_rotation(rp.w, rp.x, rp.y, rp.z);
        const glm::vec3 child_torque = torque * (parent_rotation * rec.axis);

        // Accumulate into the round's per-body external torques: a body shared by
        // two joints receives both contributions, so a serial chain composes. A
        // body-level SetBodyValue(ExternalTorque) made earlier in the round adds in
        // call order instead of being erased.
        impl.AccumulateBodyTorque(rec.child, child_torque);
        impl.AccumulateBodyTorque(rec.parent, -child_torque);
    }
} // namespace AppPhysics
