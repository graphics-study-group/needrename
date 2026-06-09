#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "Framework/component/TransformComponent/TransformComponent.h"
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
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"
#include "cmake_config.h"

#include <SDL3/SDL.h>
#include <cassert>

using namespace Engine;

int main(int argc, char **argv) {
    int64_t max_frame_count = 300;
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

    // Physics components
    root.AddComponent<RigidBodyComponent>();
    auto &root_shape = root.AddComponent<CollisionShapeComponent>();
    root_shape.m_box_size = glm::vec3(2.0f, 2.0f, 2.0f);

    auto &child_shape = child.AddComponent<CollisionShapeComponent>();
    child_shape.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    child_rigidbody_root.AddComponent<RigidBodyComponent>();
    auto &nested_shape_root = child_rigidbody_root.AddComponent<CollisionShapeComponent>();
    nested_shape_root.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &nested_shape_grandchild = child_rigidbody_grandchild.AddComponent<CollisionShapeComponent>();
    nested_shape_grandchild.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &loose_shape = loose.AddComponent<CollisionShapeComponent>();
    loose_shape.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);
    loose_shape.m_box_center = glm::vec3(1.0f, 0.0f, 0.0f);

    // Add mesh components to all physics objects for visualization.
    auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    AssetRef sphere_mesh = adb.GetNewAssetRef(AssetPath{adb, "/Sphere.asset"});
    AssetRef pbr_material = adb.GetNewAssetRef(AssetPath{adb, "/red_brick_sphere_512_default_pbr.asset"});

    auto add_mesh = [&](GameObject &obj) -> StaticMeshComponent & {
        auto &mc = obj.AddComponent<StaticMeshComponent>();
        mc.m_mesh_asset = sphere_mesh;
        mc.m_material_assets.push_back(pbr_material);
        mc.m_is_eagerly_loaded = true;
        obj.GetTransformRef().SetScale(glm::vec3(0.3f));
        return mc;
    };

    auto &root_mesh = add_mesh(root);
    auto &child_mesh = add_mesh(child);
    auto &nested_root_mesh = add_mesh(child_rigidbody_root);
    auto &nested_grandchild_mesh = add_mesh(child_rigidbody_grandchild);
    auto &loose_mesh = add_mesh(loose);

    scene.FlushCmdQueue();

    PhysicsScene *physics_scene = scene.GetPhysicsScene();
    if (physics_scene == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene is null.");
        return -1;
    }

    physics_scene->InitializePendingRigidBodies(*cmc->GetRenderSystem());
    physics_scene->DebugPrint();

    // Awake mesh components → registers renderers with RendererManager.
    scene.AddInitEvent();
    scene.ProcessEvents();

    // Set model_mat_index for physics-driven renderers.
    root_mesh.PreRenderUpdate();
    child_mesh.PreRenderUpdate();
    nested_root_mesh.PreRenderUpdate();
    nested_grandchild_mesh.PreRenderUpdate();
    loose_mesh.PreRenderUpdate();

    // Set up active camera for rendering.
    {
        auto active_camera = world_system.GetActiveCamera();
        if (active_camera) {
            cmc->GetRenderSystem()->GetCameraManager().SetActiveCameraIndex(active_camera->m_display_id);
        }
    }

    // --- Build the combined physics + rendering render graph ---
    auto system = cmc->GetRenderSystem();
    XPBDGpuSolver xpbd_solver(*system);

    RenderGraphBuilder rgb(*system);

    // 1) Import the model matrices buffer ONCE so both the XPBD compute passes
    //    and the rendering passes share the same handle.  The render graph uses
    //    this to insert correct barriers between compute-write and vertex-read.
    auto gpu = physics_scene->GetGpuBuffers();
    assert(gpu.model_matrices != nullptr);
    auto mm_handle =
        rgb.ImportExternalResource(*gpu.model_matrices, MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::None));

    // 2) Add XPBD physics passes, passing the pre-imported handle.
    xpbd_solver.Step(rgb, *physics_scene, mm_handle);

    // 3) Request transient render targets (仿照 ComplexRenderGraphBuilder).
    RenderTargetTexture::RenderTargetTextureDesc color_desc{
        .dimensions = 2,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    auto color_id = rgb.RequestRenderTargetTexture(color_desc, Texture::SamplerDesc{}, "Final Color");

    RenderTargetTexture::RenderTargetTextureDesc depth_desc{
        .dimensions = 2,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT,
        .multisample = 1,
        .is_cube_map = false
    };
    auto depth_id = rgb.RequestRenderTargetTexture(depth_desc, Texture::SamplerDesc{}, "Depth");

    // 4) Add the lit pass — same pattern as ComplexRenderGraphBuilder's lit pass
    //    but simplified (no shadow map reads, no bloom).
    rgb.AddPass(
        RenderGraphPassBuilder(*system)
            .SetName("Main Lit pass")
            .AppendColorAttachment(
                RGAttachmentDesc2{
                    color_id,
                    {},
                    AttachmentUtils::LoadOperation::Clear,
                    AttachmentUtils::StoreOperation::Store,
                    AttachmentUtils::ColorClearValue{0.1f, 0.1f, 0.15f, 1.0f}
                }
            )
            .SetDepthStencilAttachment(
                RGAttachmentDesc2{
                    depth_id,
                    {},
                    AttachmentUtils::LoadOperation::Clear,
                    AttachmentUtils::StoreOperation::DontCare,
                    AttachmentUtils::DepthClearValue{1.0f, 0U}
                }
            )
            // Declare model matrix buffer read so the render graph inserts the
            // compute-write → vertex-read barrier between XPBD and Lit passes.
            .UseBuffer(mm_handle, MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::ShaderRandomRead))
            .SetPassFunction([system](CommandBuffer &cb, const RenderGraph &) {
                vk::Extent2D extent = system->GetSwapchain().GetExtent();
                vk::Rect2D scissor{{0, 0}, extent};
                cb.SetupViewport(static_cast<float>(extent.width), static_cast<float>(extent.height), scissor);

                auto active_camera = system->GetCameraManager().GetActiveCameraIndex();
                cb.DrawRenderers("Lit", system->GetRendererManager().FilterAndSortRenderers({}), active_camera, extent);
            })
            .WrapRenderPass()
            .Get()
    );

    auto rg = rgb.BuildRenderGraph();

    // --- Main loop (like project_loading_test) ---
    cmc->SetRenderGraph(std::move(rg), color_id);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop (%lld frames)", (long long)max_frame_count);
    cmc->LoopFinite(max_frame_count);

    return 0;
}
