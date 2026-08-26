#include "PhysicsApp.h"

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
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/Renderer/Camera.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/IPresentProvider.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Resource/RenderTargetTexture.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/MemoryAccessTypes.h>
#include <Rhi/Device/MemoryTypes.h>
#include <Rhi/Device/Structs.h>
#include <SDL3/SDL.h>
#include <cmake_config.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <vulkan/vulkan.hpp>

#include <cstring>

#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace AppPhysics {

    using namespace Engine;

    namespace {
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

        // FOV baseline read from the camera's vertical FOV at creation; used to
        // stabilize the view when the aspect ratio crosses 1.0 (see design D4).
        float camera_fov{45.0f};
        uint32_t last_present_width{0};
        uint32_t last_present_height{0};

        bool should_quit{false};
        bool paused{true};
        bool toggle_was_pressed{false};

        // ── Physics state readback ──────────────────────────────────────
        bool physics_readback_built{false};
        uint32_t slot_count{0};
        std::vector<uint32_t> body_to_slot{}; // BodyId -> rigid-body slot index.
        std::vector<glm::vec3> com_offsets{}; // Per-slot COM offset (GO-local).

        const Rhi::ComputeBuffer *pos_src{};
        const Rhi::ComputeBuffer *rot_src{};
        const Rhi::ComputeBuffer *linvel_src{};
        const Rhi::ComputeBuffer *angvel_src{};

        std::unique_ptr<Rhi::DeviceBuffer> pos_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> rot_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> linvel_staging{};
        std::unique_ptr<Rhi::DeviceBuffer> angvel_staging{};

        // ── Physics state write (SetBodyValue) ──────────────────────────
        // One persistent upload staging buffer per writable field, mirroring the
        // readback design (fixed size, frozen after CommitScene). Whole-field
        // dirty flags gate the upload copies in Step.
        std::array<const Rhi::ComputeBuffer *, 6> write_src{}; // Slot-indexed GPU buffer per BodyField.
        std::array<std::unique_ptr<Rhi::DeviceBuffer>, 6> write_staging{};
        std::array<bool, 6> any_dirty{false, false, false, false, false, false};

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
        std::vector<std::unique_ptr<Actuator>> m_actuators{}; // JointId -> actuator (null = none).

        // ── Render readback ─────────────────────────────────────────────
        bool render_readback_enabled{false};
        bool has_render_capture{false};
        uint64_t render_frame_id{0};
        uint32_t render_staging_w{0};
        uint32_t render_staging_h{0};
        std::unique_ptr<Rhi::DeviceBuffer> render_staging{};

        /// @brief Record the physics SoA -> staging copies into a command buffer.
        void RecordBodyStateCopy(vk::CommandBuffer cb) {
            if (slot_count == 0) {
                return;
            }
            const size_t bytes = static_cast<size_t>(slot_count) * sizeof(glm::vec4);
            cb.copyBuffer(pos_src->GetBuffer(), pos_staging->GetBuffer(), vk::BufferCopy{0, 0, bytes});
            cb.copyBuffer(rot_src->GetBuffer(), rot_staging->GetBuffer(), vk::BufferCopy{0, 0, bytes});
            cb.copyBuffer(linvel_src->GetBuffer(), linvel_staging->GetBuffer(), vk::BufferCopy{0, 0, bytes});
            cb.copyBuffer(angvel_src->GetBuffer(), angvel_staging->GetBuffer(), vk::BufferCopy{0, 0, bytes});
        }

        /// @brief Map a BodyField to the write-path slot index (declaration order).
        static size_t BodyFieldIndex(BodyField field) {
            return static_cast<size_t>(field);
        }

        /// @brief Record the dirty write staging -> GPU buffer copies into a command buffer.
        /// Called before GPUStep so the solver reads the overridden values.
        void RecordBodyStateUpload(vk::CommandBuffer cb) {
            if (slot_count == 0) {
                return;
            }
            const size_t bytes = static_cast<size_t>(slot_count) * sizeof(glm::vec4);
            for (size_t i = 0; i < write_src.size(); ++i) {
                if (!any_dirty[i] || !write_src[i] || !write_staging[i]) {
                    continue;
                }
                cb.copyBuffer(write_staging[i]->GetBuffer(), write_src[i]->GetBuffer(), vk::BufferCopy{0, 0, bytes});
            }
        }

        /// @brief Allocate the six write staging buffers and capture the GPU sources.
        void BuildPhysicsWrite() {
            auto *phys_scene = scene->GetPhysicsScene();
            if (!phys_scene) {
                throw std::logic_error("PhysicsApp: no physics scene available for writes");
            }
            auto gpu = phys_scene->GetGpuBuffers();
            write_src[BodyFieldIndex(BodyField::Position)] = gpu.rigid_body_center_world_position;
            write_src[BodyFieldIndex(BodyField::Rotation)] = gpu.rigid_body_center_world_rotation;
            write_src[BodyFieldIndex(BodyField::LinearVelocity)] = gpu.rigid_body_linear_velocity;
            write_src[BodyFieldIndex(BodyField::AngularVelocity)] = gpu.rigid_body_angular_velocity;
            write_src[BodyFieldIndex(BodyField::ExternalForce)] = gpu.rigid_body_external_force;
            write_src[BodyFieldIndex(BodyField::ExternalTorque)] = gpu.rigid_body_external_torque;

            const size_t bytes = static_cast<size_t>(slot_count) * sizeof(glm::vec4);
            const Rhi::BufferType ut{Rhi::BufferTypeBits::StagingToDevice};
            const auto &alloc = renderer->GetAllocatorState();
            write_staging[BodyFieldIndex(BodyField::Position)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, ut, bytes, "Physics pos write staging");
            write_staging[BodyFieldIndex(BodyField::Rotation)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, ut, bytes, "Physics rot write staging");
            write_staging[BodyFieldIndex(BodyField::LinearVelocity)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, ut, bytes, "Physics linvel write staging");
            write_staging[BodyFieldIndex(BodyField::AngularVelocity)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, ut, bytes, "Physics angvel write staging");
            write_staging[BodyFieldIndex(BodyField::ExternalForce)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, ut, bytes, "Physics extforce write staging");
            write_staging[BodyFieldIndex(BodyField::ExternalTorque)] =
                Rhi::DeviceBuffer::CreateUnique(alloc, ut, bytes, "Physics exttorque write staging");

            // Zero the staging so untouched slots are clean. The whole field is
            // uploaded whenever any slot is dirty (and actuators mark ExternalTorque
            // dirty every step), so uninitialized slots would otherwise upload
            // garbage for bodies the caller never wrote.
            for (auto &sb : write_staging) {
                std::memset(sb->GetVMAddress(), 0, bytes);
                sb->Flush(0, bytes);
            }
        }

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
        /// readback staging, compute torque, and write it to the ExternalTorque
        /// write staging so the next upload applies it. Called each Step before
        /// the write upload.
        void RunActuators() {
            bool any_actuator = false;
            for (const auto &a : m_actuators) {
                any_actuator = any_actuator || (a != nullptr);
            }
            if (!any_actuator) {
                return;
            }

            const auto *rot = reinterpret_cast<const glm::vec4 *>(rot_staging->GetVMAddress());
            const auto *avel = reinterpret_cast<const glm::vec4 *>(angvel_staging->GetVMAddress());

            const size_t torque_idx = BodyFieldIndex(BodyField::ExternalTorque);
            auto *torque = reinterpret_cast<glm::vec4 *>(write_staging[torque_idx]->GetVMAddress());

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
                torque[cs] = glm::vec4(child_torque, 0.0f);
                torque[ps] = glm::vec4(-child_torque, 0.0f);
            }

            write_staging[torque_idx]->Flush(0, static_cast<size_t>(slot_count) * sizeof(glm::vec4));
            any_dirty[torque_idx] = true;
        }

        /// @brief Build the BodyId->slot mapping, allocate staging, and seed it.
        void BuildPhysicsReadback() {
            auto *phys_scene = scene->GetPhysicsScene();
            if (!phys_scene) {
                throw std::logic_error("PhysicsApp: no physics scene available for readback");
            }
            auto gpu = phys_scene->GetGpuBuffers();
            slot_count = gpu.rigid_body_slot_count;
            pos_src = gpu.rigid_body_center_world_position;
            rot_src = gpu.rigid_body_center_world_rotation;
            linvel_src = gpu.rigid_body_linear_velocity;
            angvel_src = gpu.rigid_body_angular_velocity;

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

            // Allocate the four resident readback buffers (fixed size, frozen).
            const size_t bytes = static_cast<size_t>(slot_count) * sizeof(glm::vec4);
            const Rhi::BufferType rt{Rhi::BufferTypeBits::ReadbackFromDevice};
            const auto &alloc = renderer->GetAllocatorState();
            pos_staging = Rhi::DeviceBuffer::CreateUnique(alloc, rt, bytes, "Physics pos readback");
            rot_staging = Rhi::DeviceBuffer::CreateUnique(alloc, rt, bytes, "Physics rot readback");
            linvel_staging = Rhi::DeviceBuffer::CreateUnique(alloc, rt, bytes, "Physics linvel readback");
            angvel_staging = Rhi::DeviceBuffer::CreateUnique(alloc, rt, bytes, "Physics angvel readback");

            // One-time seed copy so state reads are legal before the first Step.
            auto &dc = renderer->GetDeviceContext();
            const auto &dev_iface = dc.GetDeviceInterface();
            auto device = dev_iface.GetDevice();
            auto cbs = device.allocateCommandBuffersUnique(
                vk::CommandBufferAllocateInfo{
                    dev_iface.GetQueueInfo().graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
                }
            );
            auto cb = std::move(cbs[0]);
            cb->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
            RecordBodyStateCopy(cb.get());
            cb->end();

            vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
            vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
            vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
            dev_iface.GetQueueInfo().graphicsQueue.submit2(sinfo, fence.get());
            auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
            if (wait_result != vk::Result::eSuccess) {
                throw std::runtime_error("PhysicsApp: physics readback seed copy wait failed");
            }

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

        if (impl.mode != AppMode::Windowed && impl.mode != AppMode::Offscreen && impl.mode != AppMode::PhysicsOnly) {
            throw std::invalid_argument("PhysicsApp: unknown AppMode");
        }

        auto cmc = MainClass::GetInstance();
        StartupOptions opt{};
        opt.resol_x = static_cast<int>(info.resol_x);
        opt.resol_y = static_cast<int>(info.resol_y);
        opt.title = info.window_title;
        opt.headless = (impl.mode != AppMode::Windowed);
        cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
        cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
        cmc->LoadProject(std::filesystem::path(ENGINE_PROJECTS_DIR) / "empty_with_sky");

        impl.main_class = cmc;
        impl.world = cmc->GetWorldSystem().get();
        impl.renderer = cmc->GetRenderSystem().get();
        impl.physics = cmc->GetPhysicsSystem().get();
        impl.input = cmc->GetInputSystem().get();
        impl.time = cmc->GetTimeSystem().get();
        impl.scene = &impl.world->GetMainSceneRef();

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

    void PhysicsApp::AddActuator(JointId joint, std::unique_ptr<Actuator> actuator) {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: AddActuator called after CommitScene");
        }
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
        // reads are legal before the first Step.
        impl.BuildPhysicsReadback();

        // Build the write staging for SetBodyValue (same frozen sizing as readback).
        impl.BuildPhysicsWrite();

        // One-time seed "step" (rendering modes only): run PreGPUStep + GPUStep
        // while scene simulation is still disabled (the enable call comes at the
        // end of CommitScene), so the solver only executes its model-matrix dispatch
        // and writes initial model matrices from the FlushPhysics-seeded poses
        if (impl.mode != AppMode::PhysicsOnly) {
            impl.physics->PreGPUStep();
            auto &dc = impl.renderer->GetDeviceContext();
            const auto &dev_iface = dc.GetDeviceInterface();
            auto device = dev_iface.GetDevice();
            auto cbs = device.allocateCommandBuffersUnique(
                vk::CommandBufferAllocateInfo{
                    dev_iface.GetQueueInfo().graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
                }
            );
            auto cb = std::move(cbs[0]);
            cb->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
            impl.physics->GPUStep(cb.get());
            cb->end();

            vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
            vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
            vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
            dev_iface.GetQueueInfo().graphicsQueue.submit2(sinfo, fence.get());
            auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
            if (wait_result != vk::Result::eSuccess) {
                throw std::runtime_error("PhysicsApp: physics model-matrix seed wait failed");
            }
            impl.physics->PostGPUStep();
        }

        // Physics -> render bridge for model matrices (skipped headless).
        if (impl.mode != AppMode::PhysicsOnly) {
            if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
                impl.renderer->GetSceneDataManager().SetModelMatricesBuffer(phys_scene->GetGpuBuffers().model_matrices);
            }
        }

        // Build the default render graph once. The builder must outlive the graph:
        // its pass lambdas capture references into it. (Skipped headless.)
        if (impl.mode != AppMode::PhysicsOnly) {
            impl.rg_builder = std::make_unique<ComplexRenderGraphBuilder>(*impl.renderer);
            RGTextureHandle final_color_id{0};
            auto mm_buf = impl.scene->GetPhysicsScene()->GetGpuBuffers().model_matrices;
            auto rg = impl.rg_builder->BuildDefaultRenderGraph(final_color_id, mm_buf);
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
        impl.physics->PreGPUStep();

        // Dedicated command buffer for physics (independent from the render main
        // command buffer). The engine's RHI provides queue + pool through the
        // DeviceInterface, replacing the SubmissionHelper in this design.
        auto &dc = impl.renderer->GetDeviceContext();
        const auto &dev_iface = dc.GetDeviceInterface();
        auto device = dev_iface.GetDevice();
        const auto &queue_info = dev_iface.GetQueueInfo();

        vk::CommandBufferAllocateInfo cbainfo{queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1};
        auto cbs = device.allocateCommandBuffersUnique(cbainfo);
        auto cb = std::move(cbs[0]);
        vk::CommandBufferBeginInfo cbbinfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        cb->begin(cbbinfo);
        // Measure joint state from the last readback and write actuator torques
        // into the ExternalTorque write staging before the solver reads them.
        impl.RunActuators();
        // Write staging -> GPU uploads first so the solver reads overridden values.
        impl.RecordBodyStateUpload(cb.get());
        impl.physics->GPUStep(cb.get());
        // Physics readback: copy the SoA into resident staging in the same CB,
        // so the existing fence wait makes the state CPU-visible on return.
        impl.RecordBodyStateCopy(cb.get());
        cb->end();

        vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
        vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
        queue_info.graphicsQueue.submit2(sinfo, fence.get());
        auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
        if (wait_result != vk::Result::eSuccess) {
            throw std::runtime_error("PhysicsApp: physics submission wait failed");
        }

        // The recorded uploads have been submitted and executed; clear the dirty
        // flags so unset fields are not re-uploaded next step.
        impl.any_dirty.fill(false);

        impl.physics->PostGPUStep();

        // Post-wait: physics writes are visible before the next render frame.
        impl.renderer->WaitForIdle();
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
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    impl.should_quit = true;
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

        // Physics -> render bridge (buffer pointer is stable; content updated by
        // Step). Kept per-frame to mirror the main loop behavior.
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            impl.renderer->GetSceneDataManager().SetModelMatricesBuffer(phys_scene->GetGpuBuffers().model_matrices);
        }

        // Keep the active camera's aspect ratio aligned with the present extent,
        // and mirror the SceneWidget FOV heuristic: fixed vertical FOV in
        // landscape, fixed horizontal FOV in portrait, so the visible extent is
        // stable across a resize (see design D4). Interim shim — no camera-API
        // commitment.
        if (impl.camera_component) {
            const vk::Extent2D present_extent = impl.renderer->GetPresentProvider().GetExtent();
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

        if (impl.renderer->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date / minimized: skip this frame and try again
            // next call. A previous render-readback capture remains valid.
            return;
        }
        auto cb = impl.renderer->GetFrameManager().BeginMainCommandBuffer();
        if (impl.render_graph && impl.render_graph->GetNumPasses() > 0) {
            impl.render_graph->RecordAllPasses(cb.GetCommandBuffer());
        }

        // Opt-in render readback: record a copy of the final RTT into the
        // resident staging buffer after all passes.
        Rhi::MemoryAccessTypeImageBits final_last_access = Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite;
        if (impl.render_readback_enabled && impl.render_graph) {
            auto *final_tex = impl.render_graph->GetInternalTextureResource(impl.final_color_id);
            impl.EnsureRenderStaging(*final_tex);
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
        auto *dst = reinterpret_cast<glm::vec4 *>(impl.write_staging[idx]->GetVMAddress());
        dst[slot] = value;
        impl.write_staging[idx]->Flush(slot * sizeof(glm::vec4), sizeof(glm::vec4));
        impl.any_dirty[idx] = true;
    }
} // namespace AppPhysics
