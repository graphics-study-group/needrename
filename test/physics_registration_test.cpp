#include "Core/Functional/SDLWindow.h"
#include "Framework/component/physics/CollisionShapeComponent.h"
#include "Framework/component/physics/RigidBodyComponent.h"
#include "Framework/object/GameObject.h"
#include "Framework/world/Scene.h"
#include "Framework/world/WorldSystem.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph/ComplexRenderGraphBuilder.h"
#include "cmake_config.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace Engine;

namespace {
    bool Near(float lhs, float rhs, float eps = 1e-4f) {
        return std::fabs(lhs - rhs) <= eps;
    }
} // namespace

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
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
    auto rgb = std::make_unique<ComplexRenderGraphBuilder>(*cmc->GetRenderSystem());
    auto [w, h] = cmc->GetWindow()->GetSize();
    int32_t final_color_id;
    auto rg = rgb->BuildDefaultRenderGraph(w, h, final_color_id);
    cmc->SetRenderGraph(rg, final_color_id);
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
    assert(physics_scene != nullptr);

    const uint32_t root_rigidbody_index = root_rigidbody.GetPhysicsRigidBodyIndex();
    const uint32_t nested_rigidbody_index = nested_rigidbody.GetPhysicsRigidBodyIndex();

    assert(root_rigidbody_index != PhysicsScene::INVALID_INDEX);
    assert(nested_rigidbody_index != PhysicsScene::INVALID_INDEX);
    assert(root_rigidbody_index != nested_rigidbody_index);

    physics_scene->DebugPrint();

    return 0;
}
