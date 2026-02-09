#include "EditorRenderGraphBuilder.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetRef.h>
#include <Asset/Shader/ShaderAsset.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Pipeline/CommandBuffer/ComputeCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <UserInterface/GUISystem.h>

#include <vulkan/vulkan.hpp>

namespace Editor {
    using namespace Engine;

    EditorRenderGraphBuilder::EditorRenderGraphBuilder(RenderSystem &system) : RenderGraphBuilder(system) {
        // XXX: Hardcoded bloom shader. Should use AssetManager to load shader when we have pipeline asset.
        auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        m_bloom_shader = adb.GetNewAssetRef(AssetPath{adb, "~/shaders/bloom.comp.asset"});
    }

    std::unique_ptr<RenderGraph> EditorRenderGraphBuilder::BuildEditorRenderGraph(
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
        scene_widget_color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        game_widget_color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        final_color_target_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});

        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat;
        auto hdr_color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        auto scene_bloom_temp_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        auto game_bloom_temp_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});

        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
        auto depth_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});

        m_game_bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        m_game_bloom_compute_stage->Instantiate(*m_bloom_shader.cas<ShaderAsset>());
        m_scene_bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        m_scene_bloom_compute_stage->Instantiate(*m_bloom_shader.cas<ShaderAsset>());
        auto &system = m_system;
        auto &scene_bloom = *m_scene_bloom_compute_stage;
        auto &game_bloom = *m_game_bloom_compute_stage;

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

        /**
         *  Shadowmap pass
         */
        using IAT = MemoryAccessTypeImageBits;
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            this->UseImage(shadow_ids[i], IAT::DepthStencilAttachmentWrite);
        }
        this->RecordRasterizerPassWithoutRT([&system, shadow_ids](GraphicsCommandBuffer &gcb, const RenderGraph &rg) {
            vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            for (size_t i = 0; i < system.GetSceneDataManager().GetNumShadowCastingLights(); i++) {
                auto shadow_map_target = rg.GetInternalTextureResource(shadow_ids[i]);
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

        /**
         * scene widget pass
         */
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
            [&system,
             get_scene_widget_viewport_func,
             get_scene_camera_index_func](GraphicsCommandBuffer &gcb, const RenderGraph &) {
                vk::Extent2D extent{get_scene_widget_viewport_func()};
                vk::Rect2D scissor{{0, 0}, extent};
                gcb.SetupViewport(extent.width, extent.height, scissor);
                system.GetCameraManager().SetActiveCameraIndex(get_scene_camera_index_func());
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
        this->UseImage(scene_bloom_temp_id, IAT::ShaderRandomDefault);
        this->UseImage(scene_widget_color_id, IAT::ShaderRandomWrite);
        this->RecordComputePass(
            [&scene_bloom, texture_width, texture_height](ComputeCommandBuffer &ccb, const RenderGraph &) {
                ccb.BindComputeStage(scene_bloom);
                ccb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
            },
            "Bloom FX pass"
        );

        /**
         * game widget pass
         */
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
            [&system,
             get_game_widget_viewport_func,
             get_game_camera_index_func](GraphicsCommandBuffer &gcb, const RenderGraph &) {
                vk::Extent2D extent{get_game_widget_viewport_func()};
                vk::Rect2D scissor{{0, 0}, extent};
                gcb.SetupViewport(extent.width, extent.height, scissor);
                system.GetCameraManager().SetActiveCameraIndex(get_game_camera_index_func());
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
        this->UseImage(game_bloom_temp_id, IAT::ShaderRandomDefault);
        this->UseImage(game_widget_color_id, IAT::ShaderRandomWrite);
        this->RecordComputePass(
            [&game_bloom, texture_width, texture_height](ComputeCommandBuffer &ccb, const RenderGraph &) {
                ccb.BindComputeStage(game_bloom);
                ccb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
            },
            "Bloom FX pass"
        );

        this->UseImage(final_color_target_id, IAT::ColorAttachmentWrite);
        this->UseImage(scene_widget_color_id, IAT::ShaderSampledRead);
        this->UseImage(game_widget_color_id, IAT::ShaderSampledRead);
        this->RecordRasterizerPass(
            {final_color_target_id, AttachmentUtils::LoadOperation::Load, AttachmentUtils::StoreOperation::Store},
            [gui_system](GraphicsCommandBuffer &gcb, const RenderGraph &) {
                gui_system->DrawGUI(gcb.GetCommandBuffer());
            }
        );

        auto rg{this->BuildRenderGraph()};
        scene_bloom.AssignTexture("inputImage", *rg->GetInternalTextureResource(hdr_color_id));
        scene_bloom.AssignTexture("bloomTemp", *rg->GetInternalTextureResource(scene_bloom_temp_id));
        scene_bloom.AssignTexture("outputImage", *rg->GetInternalTextureResource(scene_widget_color_id));
        game_bloom.AssignTexture("inputImage", *rg->GetInternalTextureResource(hdr_color_id));
        game_bloom.AssignTexture("bloomTemp", *rg->GetInternalTextureResource(game_bloom_temp_id));
        game_bloom.AssignTexture("outputImage", *rg->GetInternalTextureResource(game_widget_color_id));

        for (size_t i = 0; i < shadow_ids.size(); i++) {
            auto shadow_map_target = rg->GetInternalTextureResource(shadow_ids[i]);
            system.GetSceneDataManager().SetLightShadowMap(i, *shadow_map_target);
        }

        return rg;
    }
} // namespace Editor
