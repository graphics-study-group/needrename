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
#include <Rhi/Device/MemoryAccessTypes.h>
#include <Rhi/Device/Structs.h>
#include <SDL3/SDL.h>
#include <cmake_config.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <vulkan/vulkan.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>

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

        bool should_quit{false};
        bool paused{true};
        bool toggle_was_pressed{false};
    };

    std::unique_ptr<PhysicsApp> PhysicsApp::Create(const CreateInfo &info) {
        std::unique_ptr<PhysicsApp> app{new PhysicsApp()};
        auto &impl = *app->m_impl;
        impl.resol_x = info.resol_x;
        impl.resol_y = info.resol_y;

        auto cmc = MainClass::GetInstance();
        StartupOptions opt{};
        opt.resol_x = static_cast<int>(info.resol_x);
        opt.resol_y = static_cast<int>(info.resol_y);
        opt.title = info.window_title;
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
        // SPACE pause toggle.
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
        impl.builder = std::make_unique<SceneBuilder>(*impl.scene, *adb, root);

        // Built-in camera with fly controls.
        auto &camera_object = impl.scene->CreateGameObject();
        camera_object.SetParent(root.GetHandle());
        impl.camera_object = &camera_object;
        auto &camera_comp = camera_object.AddComponent<CameraComponent>();
        camera_comp.m_camera->set_aspect_ratio(
            1.0f * static_cast<float>(info.resol_x) / static_cast<float>(info.resol_y)
        );
        impl.camera_component = &camera_comp;
        impl.camera_controller = &camera_object.AddComponent<CameraControllerComponent>();
        impl.world->SetActiveCamera(camera_comp.GetHandle(), &impl.renderer->GetCameraManager());

        // Default camera pose looking at the origin.
        app->SetCameraPose(glm::vec3(6.0f, -5.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f));

        return app;
    }

    PhysicsApp::PhysicsApp() : m_impl(std::make_unique<Impl>()) {
    }

    PhysicsApp::~PhysicsApp() = default;

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

    void PhysicsApp::AddHingeJoint(BodyId obj1, BodyId obj2, const HingeJointParams &params) {
        if (m_impl->phase != Impl::Phase::Building) {
            throw std::logic_error("PhysicsApp: scene is frozen after CommitScene");
        }
        m_impl->builder->AddHingeJoint(obj1, obj2, params);
    }

    void PhysicsApp::AddDirectionalLight(const DirectionalLightParams &params) {
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

        // One-time CPU -> GPU sync of physics descriptors.
        impl.scene->FlushPhysics(*impl.renderer);

        // Physics -> render bridge for model matrices.
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            impl.renderer->GetSceneDataManager().SetModelMatricesBuffer(phys_scene->GetGpuBuffers().model_matrices);
        }

        // Build the default render graph once. The builder must outlive the graph:
        // its pass lambdas capture references into it.
        impl.rg_builder = std::make_unique<ComplexRenderGraphBuilder>(*impl.renderer);
        RGTextureHandle final_color_id{0};
        auto mm_buf = impl.scene->GetPhysicsScene()->GetGpuBuffers().model_matrices;
        const auto present_extent = impl.renderer->GetPresentProvider().GetExtent();
        auto rg = impl.rg_builder->BuildDefaultRenderGraph(
            present_extent.width, present_extent.height, final_color_id, mm_buf
        );
        impl.render_graph = std::move(rg);
        impl.final_color_id = final_color_id;

        // Freeze the scene and start paused (mirrors the previous example UX).
        impl.phase = Impl::Phase::Committed;
        impl.paused = true;
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            phys_scene->SetSimulationEnabled(false);
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
        impl.physics->GPUStep(cb.get());
        cb->end();

        vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
        vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
        queue_info.graphicsQueue.submit2(sinfo, fence.get());
        auto wait_result = device.waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
        if (wait_result != vk::Result::eSuccess) {
            throw std::runtime_error("PhysicsApp: physics submission wait failed");
        }

        impl.physics->PostGPUStep();

        // Post-wait: physics writes are visible before the next render frame.
        impl.renderer->WaitForIdle();
    }

    void PhysicsApp::RenderNextFrame() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: RenderNextFrame called before CommitScene");
        }

        impl.time->NextFrame();

        // Input events are processed inside the render frame.
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

        // Component ticks: camera controller + CameraComponent view update.
        impl.scene->AddTickEvent();
        impl.scene->ProcessEvents();

        impl.world->UpdateRendererData(*impl.renderer);

        // Physics -> render bridge (buffer pointer is stable; content updated by
        // Step). Kept per-frame to mirror the main loop behavior.
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            impl.renderer->GetSceneDataManager().SetModelMatricesBuffer(phys_scene->GetGpuBuffers().model_matrices);
        }

        if (impl.renderer->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date / minimized: skip this frame and try again
            // next call.
            return;
        }
        auto cb = impl.renderer->GetFrameManager().BeginMainCommandBuffer();
        if (impl.render_graph && impl.render_graph->GetNumPasses() > 0) {
            impl.render_graph->RecordAllPasses(cb.GetCommandBuffer());
        }
        impl.renderer->CompleteFrame(
            *impl.render_graph->GetInternalTextureResource(impl.final_color_id),
            Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite
        );
    }

    void PhysicsApp::Pause() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: Pause called before CommitScene");
        }
        if (impl.paused) return;
        impl.paused = true;
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            phys_scene->SetSimulationEnabled(false);
        }
    }

    void PhysicsApp::Resume() {
        auto &impl = *m_impl;
        if (impl.phase != Impl::Phase::Committed) {
            throw std::logic_error("PhysicsApp: Resume called before CommitScene");
        }
        if (!impl.paused) return;
        impl.paused = false;
        if (auto *phys_scene = impl.scene->GetPhysicsScene()) {
            phys_scene->SetSimulationEnabled(true);
        }
    }

    bool PhysicsApp::IsPaused() const {
        return m_impl->paused;
    }

    bool PhysicsApp::ShouldQuit() const {
        return m_impl->should_quit;
    }
} // namespace AppPhysics
