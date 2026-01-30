#include "ComplexRenderGraphBuilder.h"
#include "RenderGraph.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Shader/ShaderAsset.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <UserInterface/GUISystem.h>

#include <vulkan/vulkan.hpp>

namespace Engine {
    ComplexRenderGraphBuilder::ComplexRenderGraphBuilder(RenderSystem &system) : RenderGraphBuilder(system) {
        // XXX: Hardcoded bloom shader. Should use AssetManager to load shader when we have pipeline asset.
        auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        auto &amg = *MainClass::GetInstance()->GetAssetManager();
        m_bloom_shader = adb.GetNewAssetRef(AssetPath{adb, "~/shaders/bloom.comp.asset"});
        amg.LoadAssetImmediately(m_bloom_shader);
    }

    void ComplexRenderGraphBuilder::RecordMainRender(
        uint32_t texture_width,
        uint32_t texture_height,
        std::function<vk::Extent2D()> get_viewport_func,
        std::function<uint8_t()> get_camera_index_func,
        std::shared_ptr<ComputeStage> bloom_compute_stage,
        int32_t &hdr_color_id,
        int32_t &bloom_temp_id,
        int32_t &final_color_target_id
    ) {
        RenderTargetTexture::RenderTargetTextureDesc rtt_desc{
            .dimensions = 2,
            .width = texture_width,
            .height = texture_height,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
            .multisample = 1,
            .is_cube_map = false
        };
        auto color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        final_color_target_id = color_id;
        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat;
        hdr_color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        bloom_temp_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
        auto depth_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});

        RenderTargetTexture::RenderTargetTextureDesc shadow_desc{
            .dimensions = 2,
            .width = SHADOWMAP_WIDTH,
            .height = SHADOWMAP_HEIGHT,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT,
            .multisample = 1,
            .is_cube_map = false
        };
        std::vector<int32_t> shadow_ids;
        shadow_ids.resize(RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS);
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            shadow_ids[i] = this->RequestRenderTargetTexture(shadow_desc, Texture::SamplerDesc{});
        }

        auto &system = m_system;
        using IAT = MemoryAccessTypeImageBits;
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            this->UseImage(shadow_ids[i], IAT::DepthStencilAttachmentWrite);
        }
        this->RecordRasterizerPassWithoutRT([&system, shadow_ids](GraphicsCommandBuffer &gcb, const RenderGraph &rg) {
            vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            for (size_t i = 0; i < system.GetSceneDataManager().GetNumShadowCastingLights(); i++) {
                auto shadow_map_target = rg.GetInternalTextureResource(shadow_ids[i]);
                system.GetSceneDataManager().SetLightShadowMap(i, *shadow_map_target);
                gcb.BeginRendering(
                    {nullptr},
                    {shadow_map_target,
                    nullptr,
                    AttachmentUtils::LoadOperation::Clear,
                    AttachmentUtils::StoreOperation::Store,
                    AttachmentUtils::DepthClearValue{1.0f, 0U}},
                    shadow_map_extent,
                    "Shadowmap Pass"
                );
                gcb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
                gcb.DrawRenderers(
                    "Shadowmap", system.GetRendererManager().FilterAndSortRenderers({}), 0, shadow_map_extent
                );
                gcb.EndRendering();
            }
        });

        for (size_t i = 0; i < shadow_ids.size(); i++) {
            this->UseImage(shadow_ids[i], IAT::ShaderSampledRead);
        }
        this->UseImage(hdr_color_id, IAT::ColorAttachmentWrite);
        this->UseImage(depth_id, IAT::DepthStencilAttachmentWrite);
        this->RecordRasterizerPass(
            {hdr_color_id, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
            {depth_id,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            [&system, get_viewport_func, get_camera_index_func](GraphicsCommandBuffer &gcb, const RenderGraph &) {
                vk::Extent2D extent{get_viewport_func()};
                vk::Rect2D scissor{{0, 0}, extent};
                gcb.SetupViewport(extent.width, extent.height, scissor);
                system.GetCameraManager().SetActiveCameraIndex(get_camera_index_func());
                gcb.DrawRenderers(
                    "Lit",
                    system.GetRendererManager().FilterAndSortRenderers({}),
                    system.GetCameraManager().GetActiveCameraIndex(),
                    extent
                );
                system.GetSceneDataManager().DrawSkybox(
                    system.GetFrameManager().GetRawMainCommandBuffer(),
                    system.GetFrameManager().GetFrameInFlight(),
                    system.GetCameraManager().GetPVMatForSkybox(),
                    extent
                );
            },
            "Main Lit pass"
        );

        this->UseImage(hdr_color_id, IAT::ShaderRandomRead);
        this->UseImage(bloom_temp_id, IAT::ShaderRandomDefault);
        this->UseImage(color_id, IAT::ShaderRandomWrite);
        this->RecordComputePass(
            [bloom_compute_stage, texture_width, texture_height](ComputeCommandBuffer &ccb, const RenderGraph &) {
                ccb.BindComputeStage(*bloom_compute_stage);
                ccb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
            },
            "Bloom FX pass"
        );
    }

    std::unique_ptr<RenderGraph> ComplexRenderGraphBuilder::BuildDefaultRenderGraph(
        uint32_t texture_width,
        uint32_t texture_height,
        std::function<vk::Extent2D()> get_viewport_func,
        std::function<uint8_t()> get_camera_index_func,
        int32_t &final_color_target_id
    ) {
        auto bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        bloom_compute_stage->Instantiate(*m_bloom_shader->cas<ShaderAsset>());
        int32_t hdr_color_id, bloom_temp_id;
        RecordMainRender(
            texture_width,
            texture_height,
            get_viewport_func,
            get_camera_index_func,
            bloom_compute_stage,
            hdr_color_id,
            bloom_temp_id,
            final_color_target_id
        );
        auto rg{this->BuildRenderGraph()};
        bloom_compute_stage->AssignTexture("inputImage", *rg->GetInternalTextureResource(hdr_color_id));
        bloom_compute_stage->AssignTexture("bloomTemp", *rg->GetInternalTextureResource(bloom_temp_id));
        bloom_compute_stage->AssignTexture("outputImage", *rg->GetInternalTextureResource(final_color_target_id));
        return rg;
    }

    std::unique_ptr<RenderGraph> ComplexRenderGraphBuilder::BuildEditorRenderGraph(
        uint32_t texture_width,
        uint32_t texture_height,
        std::function<vk::Extent2D()> get_scene_widget_viewport_func,
        std::function<uint8_t()> get_scene_camera_index_func,
        std::function<vk::Extent2D()> get_game_widget_viewport_func,
        std::function<uint8_t()> get_game_camera_index_func,
        GUISystem *gui_system,
        int32_t &scene_widget_color_id,
        int32_t &game_widget_color_id,
        int32_t &final_color_target_id
    ) {
        auto bloom_compute_stage_scene = std::make_shared<ComputeStage>(m_system);
        bloom_compute_stage_scene->Instantiate(*m_bloom_shader->cas<ShaderAsset>());
        auto bloom_compute_stage_game = std::make_shared<ComputeStage>(m_system);
        bloom_compute_stage_game->Instantiate(*m_bloom_shader->cas<ShaderAsset>());
        int32_t hdr_color_id1, bloom_temp_id1, color_id1;
        RecordMainRender(
            texture_width,
            texture_height,
            get_scene_widget_viewport_func,
            get_scene_camera_index_func,
            bloom_compute_stage_scene,
            hdr_color_id1,
            bloom_temp_id1,
            color_id1
        );
        int32_t hdr_color_id2, bloom_temp_id2, color_id2;
        RecordMainRender(
            texture_width,
            texture_height,
            get_game_widget_viewport_func,
            get_game_camera_index_func,
            bloom_compute_stage_game,
            hdr_color_id2,
            bloom_temp_id2,
            color_id2
        );
        scene_widget_color_id = color_id1;
        game_widget_color_id = color_id2;

        RenderTargetTexture::RenderTargetTextureDesc rtt_desc{
            .dimensions = 2,
            .width = texture_width,
            .height = texture_height,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
            .multisample = 1,
            .is_cube_map = false
        };
        final_color_target_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        this->UseImage(color_id1, MemoryAccessTypeImageBits::ShaderSampledRead);
        this->UseImage(color_id2, MemoryAccessTypeImageBits::ShaderSampledRead);
        this->UseImage(final_color_target_id, MemoryAccessTypeImageBits::ColorAttachmentWrite);
        this->RecordRasterizerPass(
            {final_color_target_id, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
            [gui_system](GraphicsCommandBuffer &gcb, const RenderGraph &) {
                gui_system->DrawGUI(gcb.GetCommandBuffer());
            }
        );

        auto rg{this->BuildRenderGraph()};
        bloom_compute_stage_scene->AssignTexture("inputImage", *rg->GetInternalTextureResource(hdr_color_id1));
        bloom_compute_stage_scene->AssignTexture("bloomTemp", *rg->GetInternalTextureResource(bloom_temp_id1));
        bloom_compute_stage_scene->AssignTexture("outputImage", *rg->GetInternalTextureResource(color_id1));
        bloom_compute_stage_game->AssignTexture("inputImage", *rg->GetInternalTextureResource(hdr_color_id2));
        bloom_compute_stage_game->AssignTexture("bloomTemp", *rg->GetInternalTextureResource(bloom_temp_id2));
        bloom_compute_stage_game->AssignTexture("outputImage", *rg->GetInternalTextureResource(color_id2));

        return rg;
    }
} // namespace Engine
