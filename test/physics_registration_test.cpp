#include "Core/Functional/SDLWindow.h"
#include "Framework/component/physics/CollisionShapeComponent.h"
#include "Framework/component/physics/RigidBodyComponent.h"
#include "Framework/object/GameObject.h"
#include "Framework/world/Scene.h"
#include "Framework/world/WorldSystem.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/XPBDGpuSolver.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph2/RenderGraph2.h"
#include "Render/Pipeline/RenderGraph2/RenderGraphBuilder2.h"
#include "cmake_config.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace Engine;

int main(int argc, char **argv) {
    int64_t max_frame_count = 5;
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Physics Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);
    auto &world_system = *cmc->GetWorldSystem();
    Scene &scene = world_system.GetMainSceneRef();

    GameObject &root = scene.CreateGameObject();
    GameObject &child = scene.CreateGameObject();
    GameObject &child_rigidbody_root = scene.CreateGameObject();
    GameObject &child_rigidbody_grandchild = scene.CreateGameObject();
    GameObject &loose = scene.CreateGameObject();

    child.SetParent(root.GetHandle());
    child_rigidbody_root.SetParent(root.GetHandle());
    child_rigidbody_grandchild.SetParent(child_rigidbody_root.GetHandle());

    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(2.0f, 0.0f, 0.0f));
        child.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(4.0f, 0.0f, 0.0f));
        child_rigidbody_root.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(5.0f, 0.0f, 0.0f));
        child_rigidbody_grandchild.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        loose.SetTransform(t);
    }

    auto &root_rigidbody = root.AddComponent<RigidBodyComponent>();
    auto &root_shape = root.AddComponent<CollisionShapeComponent>();
    root_shape.m_box_size = glm::vec3(2.0f, 2.0f, 2.0f);

    auto &child_shape = child.AddComponent<CollisionShapeComponent>();
    child_shape.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &nested_rigidbody = child_rigidbody_root.AddComponent<RigidBodyComponent>();
    auto &nested_shape_root = child_rigidbody_root.AddComponent<CollisionShapeComponent>();
    nested_shape_root.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &nested_shape_grandchild = child_rigidbody_grandchild.AddComponent<CollisionShapeComponent>();
    nested_shape_grandchild.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &loose_shape = loose.AddComponent<CollisionShapeComponent>();
    loose_shape.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);
    loose_shape.m_box_center = glm::vec3(1.0f, 0.0f, 0.0f);

    scene.FlushCmdQueue();

    PhysicsScene *physics_scene = scene.GetPhysicsScene();
    if (physics_scene == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene is null.");
        return -1;
    }

    physics_scene->InitializePendingRigidBodies(*cmc->GetRenderSystem());

    physics_scene->DebugPrint();

    // --- Build the XPBD physics render graph ---
    XPBDGpuSolver xpbd_solver{*cmc->GetRenderSystem()};

    RenderGraphBuilder2 rgb{*cmc->GetRenderSystem()};
    xpbd_solver.Step(rgb, *physics_scene);
    auto rg = rgb.BuildRenderGraph();

    for (int64_t frame = 0; frame < max_frame_count; frame++) {
        const auto &queues = cmc->GetRenderSystem()->GetDeviceInterface().GetQueueInfo();
        auto cbai = vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1};
        auto cb = cmc->GetRenderSystem()->GetDevice().allocateCommandBuffers(cbai);
        rg.RecordAllPasses(cb[0]);
        auto si = vk::SubmitInfo{{}, {}, {cb}, {}};
        queues.graphicsQueue.submit(si);
        queues.graphicsQueue.waitIdle();
    }

    return 0;
}
