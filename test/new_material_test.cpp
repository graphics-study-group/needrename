#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/RenderSystem.h"
#include "GUI/GUISystem.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/Material/Templates/BlinnPhong.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Memory/Image2DTexture.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Asset/AssetManager/AssetManager.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;


struct TestMeshAsset : public MeshAsset {
    TestMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0] = {
            .m_indices = {0, 3, 2, 0, 2, 1},
            .m_positions = {
                {0.5f, -0.5f, 0.0f}, 
                {0.5f, 0.5f, 0.0f}, 
                {-0.5f, 0.5f, 0.0f},
                {-0.5f, -0.5f, 0.0f},
            },
            .m_attributes = {
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 0.0f}}, 
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 1.0f}}, 
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 1.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 0.0f}}
            },
        };
    }
};

std::shared_ptr<MaterialTemplateAsset> ConstructMaterialTemplate()
{
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.frag.spv.asset");
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);
    
    test_asset->name = "Blinn-Phong";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};

    ShaderVariableProperty light_source, light_color;
    light_source.frequency = light_color.frequency = ShaderVariableProperty::Frequency::PerScene;
    light_source.type = light_color.type = ShaderVariableProperty::Type::Vec4;
    light_source.binding = light_color.binding = 0;
    light_source.offset = 0;
    light_source.name = "light_source";
    light_color.offset = 16;
    light_color.name = "light_color";

    ShaderVariableProperty view, proj;
    view.frequency = proj.frequency = ShaderVariableProperty::Frequency::PerCamera;
    view.type = proj.type = ShaderVariableProperty::Type::Mat4;
    view.binding = proj.binding = 0;
    view.offset = 0;
    proj.offset = 64;
    view.name = "view";
    proj.name = "proj";

    ShaderVariableProperty base_tex, specular_color, ambient_color;
    base_tex.frequency = specular_color.frequency = ambient_color.frequency = ShaderVariableProperty::Frequency::PerMaterial;
    base_tex.type = ShaderVariableProperty::Type::Texture;
    specular_color.type = ambient_color.type = ShaderVariableProperty::Type::Vec4;
    base_tex.binding = 1;
    specular_color.binding = ambient_color.binding = 0;
    specular_color.offset = 0;
    ambient_color.offset = 16;
    base_tex.name = "base_tex";
    specular_color.name = "specular_color";
    ambient_color.name = "ambient_color";

    mtspp.shaders.uniforms = {
        light_source, light_color, view, proj, base_tex, specular_color, ambient_color
    };
    test_asset->properties.properties[0] = mtspp;

    return test_asset;
}

int main(int argc, char ** argv)
{
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();
    // rsys->EnableDepthTesting();

    RenderTargetSetup rts{rsys};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });

    // Prepare texture
    auto test_texture_asset = std::make_shared<Image2DTextureAsset>();
    test_texture_asset->LoadFromFile(std::string(ENGINE_ASSETS_DIR) + "/bunny/bunny.png");
    auto allocated_image_texture = std::make_shared<AllocatedImage2DTexture>(rsys);
    allocated_image_texture->Create(*test_texture_asset);

    // Prepare material
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto test_asset = ConstructMaterialTemplate();
    auto test_asset_ref = std::make_shared<AssetRef>(test_asset);
    auto test_template = std::make_shared<Materials::BlinnPhongTemplate>(rsys, test_asset_ref);
    auto test_material_instance = std::make_shared<Materials::BlinnPhongInstance>(rsys, test_template);
    test_material_instance->SetAmbient(glm::vec4(0.0, 0.0, 0.0, 0.0));
    test_material_instance->SetSpecular(glm::vec4(1.0, 1.0, 1.0, 64.0));
    test_material_instance->SetBaseTexture(allocated_image_texture);
    test_material_instance->WriteDescriptors(0);

    // std::filesystem::path project_path(ENGINE_TESTS_DIR);
    // project_path = project_path / "new_material_test_project";
    // if (std::filesystem::exists(project_path))
    //     std::filesystem::remove_all(project_path);
    // std::filesystem::copy(std::filesystem::path(ENGINE_PROJECTS_DIR) / "empty_project", project_path, std::filesystem::copy_options::recursive);

    // Serialization::Archive archive;
    // archive.prepare_save();
    // test_asset->save_asset_to_archive(archive);
    // archive.save_to_file(project_path / "BlinnPhongTemplate.asset");

    // archive.clear();
    // archive.prepare_save();
    // test_asset->properties.properties[0].shaders.shaders[0]->as<ShaderAsset>()->save_asset_to_archive(archive);
    // archive.save_to_file(project_path / "blinn_phong.vert.spv.asset");

    // archive.clear();
    // archive.prepare_save();
    // test_asset->properties.properties[0].shaders.shaders[1]->as<ShaderAsset>()->save_asset_to_archive(archive);
    // archive.save_to_file(project_path / "blinn_phong.frag.spv.asset");

    // Prepare mesh
    auto test_mesh_asset = std::make_shared<TestMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys, test_mesh_asset_ref, 0};
    test_mesh.Prepare();

    // Submit scene data
    const auto & global_pool = rsys->GetGlobalConstantDescriptorPool();
    struct {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
    } camera_mats;
    ConstantData::PerSceneStruct scene {
        glm::vec4{-5.0f, -5.0f, -5.0f, 0.0f},
        glm::vec4{1.0, 1.0, 1.0, 0.0},
    };
    for (uint32_t i = 0; i < 3; i++) {
        auto ptr = global_pool.GetPerSceneConstantMemory(i);
        memcpy(ptr, &scene, sizeof scene);
        global_pool.FlushPerSceneConstantMemory(i); 
        std::byte * camera_ptr = global_pool.GetPerCameraConstantMemory(i);
        std::memcpy(camera_ptr, &camera_mats, sizeof camera_mats);
        global_pool.FlushPerCameraConstantMemory(i);
    }
    
    auto & tcb = rsys->GetTransferCommandBuffer();
    tcb.Begin();
    tcb.CommitVertexBuffer(test_mesh);
    tcb.CommitTextureImage(*allocated_image_texture, test_texture_asset->GetPixelData(), test_texture_asset->GetPixelDataSize());
    tcb.End();
    tcb.SubmitAndExecute();

    glm::mat4 eye4 = glm::mat4(1.0f);

    bool quited = false;
    while(max_frame_count--) {
        SDL_Event event;
        while(SDL_PollEvent(&event) != 0) {
            switch(event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
        }

        auto index = rsys->StartFrame();
        RenderCommandBuffer & cb = rsys->GetCurrentCommandBuffer();

        assert(index < 3);
    
        cb.Begin();

        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        vk::Rect2D scissor{{0, 0}, extent};
        cb.BeginRendering(rts, extent, index);

        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.BindMaterial(*test_material_instance, 0);
        // Push model matrix...
        vk::CommandBuffer rcb = cb.get();
        rcb.pushConstants(
            test_template->GetPipelineLayout(0), 
            vk::ShaderStageFlagBits::eVertex, 
            0, 
            ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
            reinterpret_cast<const void *>(&eye4)
        );
        cb.DrawMesh(test_mesh);

        cb.EndRendering();

        cb.End();
        cb.Submit();

        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
