#include "EditorRenderGraphBuilder.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetRef.h>
#include <Asset/Shader/ShaderAsset.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Renderer/Camera.h>
#include <UserInterface/GUISystem.h>

#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/SceneWidget.h>

#include <vulkan/vulkan.hpp>

namespace Editor {
    using namespace Engine;

    EditorRenderGraphBuilder::EditorRenderGraphBuilder(RenderSystem &system) : m_system(system) {
        // XXX: Hardcoded bloom shader. Should use AssetManager to load shader when we have pipeline asset.
        auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        m_bloom_shader = adb.GetNewAssetRef(AssetPath{adb, "~/shaders/bloom.comp.asset"});
    }

    std::unique_ptr<RenderGraph> EditorRenderGraphBuilder::BuildEditorRenderGraph(
        uint32_t texture_width,
        uint32_t texture_height,
        SceneWidget *scene_widget,
        GameWidget *game_widget,
        RGTextureHandle &scene_widget_color_id,
        RGTextureHandle &game_widget_color_id,
        RGTextureHandle &final_color_target_id
    ) {
        RenderGraphBuilder rgb{m_system};

        // Request transient resources
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
        scene_widget_color_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "Scene Widget Color");
        game_widget_color_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "Game Widget Color");
        final_color_target_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "Final Color");

        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat;
        auto hdr_color_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "HDR Color");

        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
        auto depth_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "Depth");

        // Shadow map targets
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
        std::vector<RGTextureHandle> shadow_ids;
        shadow_ids.resize(RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS);
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            shadow_ids[i] =
                rgb.RequestRenderTargetTexture(shadow_desc, Texture::SamplerDesc{}, "Shadow Map " + std::to_string(i));
        }

        // Init compute stages
        m_game_bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        m_game_bloom_compute_stage->Instantiate(*m_bloom_shader.as<ShaderAsset>());
        m_scene_bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        m_scene_bloom_compute_stage->Instantiate(*m_bloom_shader.as<ShaderAsset>());

        auto &system = m_system;
        auto world_system = MainClass::GetInstance()->GetWorldSystem().get();
        auto gui_system = MainClass::GetInstance()->GetGUISystem().get();
        auto &scene_bloom = *m_scene_bloom_compute_stage;
        auto &game_bloom = *m_game_bloom_compute_stage;

        using IAT = MemoryAccessTypeImageBits;

        /**
         * Shadowmap passes — one per shadow-casting light.
         */
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            rgb.AddPass(
                RenderGraphPassBuilder{m_system}
                    .SetName("Shadowmap Pass " + std::to_string(i))
                    .SetDepthStencilAttachment(
                        {shadow_ids[i],
                         {},
                         AttachmentUtils::LoadOperation::Clear,
                         AttachmentUtils::StoreOperation::Store,
                         AttachmentUtils::DepthClearValue{1.0f, 0U}}
                    )
                    .SetPassFunction([&system, shadow_ids, i](CommandBuffer &cb, const RenderGraph &rg) {
                        vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
                        vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
                        if (i < system.GetSceneDataManager().GetNumShadowCastingLights()) {
                            auto shadow_map_target = rg.GetInternalTextureResource(shadow_ids[i]);
                            cb.BeginRendering(
                                {nullptr},
                                {shadow_map_target,
                                 TextureSubresourceRange::GetSingleRange(),
                                 AttachmentUtils::LoadOperation::Clear,
                                 AttachmentUtils::StoreOperation::Store,
                                 AttachmentUtils::DepthClearValue{1.0f, 0U}},
                                shadow_map_extent,
                                "Shadowmap Pass"
                            );
                            cb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
                            cb.DrawRenderers(
                                "Shadowmap",
                                system.GetRendererManager().FilterAndSortRenderers({}),
                                0,
                                shadow_map_extent
                            );
                            cb.EndRendering();
                        }
                    })
                    .Get()
                // No WrapRenderPass — manual rendering
            );
        }

        /**
         * Scene widget lit pass.
         */
        {
            auto builder = RenderGraphPassBuilder{m_system}.SetName("Scene Lit pass");
            for (size_t i = 0; i < shadow_ids.size(); i++) {
                builder.UseImage(shadow_ids[i], IAT::ShaderSampledRead);
            }
            builder
                .AppendColorAttachment(
                    {hdr_color_id, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
                )
                .SetDepthStencilAttachment(
                    {depth_id,
                     {},
                     AttachmentUtils::LoadOperation::Clear,
                     AttachmentUtils::StoreOperation::DontCare,
                     AttachmentUtils::DepthClearValue{1.0f, 0U}}
                )
                .SetPassFunction([&system, scene_widget](CommandBuffer &cb, const RenderGraph &) {
                    vk::Extent2D extent{
                        static_cast<uint32_t>(scene_widget->m_viewport_size.x),
                        static_cast<uint32_t>(scene_widget->m_viewport_size.y)
                    };
                    vk::Rect2D scissor{{0, 0}, extent};
                    cb.SetupViewport(extent.width, extent.height, scissor);
                    system.GetCameraManager().SetActiveCameraIndex(scene_widget->GetCameraIndex());
                    cb.DrawRenderers(
                        "Lit",
                        system.GetRendererManager().FilterAndSortRenderers({}),
                        system.GetCameraManager().GetActiveCameraIndex(),
                        extent
                    );
                    system.GetSceneDataManager().DrawSkybox(
                        cb,
                        system.GetFrameManager().GetFrameInFlight(),
                        system.GetCameraManager().GetPVMatForSkybox(),
                        extent
                    );
                })
                .WrapRenderPass();
            rgb.AddPass(builder.Get());
        }

        /**
         * Scene widget bloom compute pass.
         */
        auto &scene_bloom_binding = scene_bloom.AllocateResourceBinding();
        rgb.AddPass(
            RenderGraphPassBuilder{m_system}
                .SetName("Scene Bloom FX pass")
                .UseImage(hdr_color_id, IAT::ShaderRandomRead)
                .UseImage(scene_widget_color_id, IAT::ShaderRandomWrite)
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction([&scene_bloom,
                                  texture_width,
                                  texture_height,
                                  &scene_bloom_binding,
                                  hdr_color_id,
                                  scene_widget_color_id](CommandBuffer &cb, const RenderGraph &rg) {
                    scene_bloom_binding.GetShaderResourceBinding().BindTexture(
                        "inputImage", *rg.GetInternalTextureResource(hdr_color_id)
                    );
                    scene_bloom_binding.GetShaderResourceBinding().BindTexture(
                        "outputImage", *rg.GetInternalTextureResource(scene_widget_color_id)
                    );
                    cb.BindComputeStage(scene_bloom);
                    cb.BindComputeResource(scene_bloom_binding);
                    cb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
                })
                .Get()
        );

        /**
         * Game widget lit pass.
         */
        {
            auto builder = RenderGraphPassBuilder{m_system}.SetName("Game Lit pass");
            for (size_t i = 0; i < shadow_ids.size(); i++) {
                builder.UseImage(shadow_ids[i], IAT::ShaderSampledRead);
            }
            builder
                .AppendColorAttachment(
                    {hdr_color_id, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
                )
                .SetDepthStencilAttachment(
                    {depth_id,
                     {},
                     AttachmentUtils::LoadOperation::Clear,
                     AttachmentUtils::StoreOperation::DontCare,
                     AttachmentUtils::DepthClearValue{1.0f, 0U}}
                )
                .SetPassFunction([&system, game_widget, world_system](CommandBuffer &cb, const RenderGraph &) {
                    vk::Extent2D extent{
                        static_cast<uint32_t>(game_widget->m_viewport_size.x),
                        static_cast<uint32_t>(game_widget->m_viewport_size.y)
                    };
                    vk::Rect2D scissor{{0, 0}, extent};
                    cb.SetupViewport(extent.width, extent.height, scissor);
                    auto camera = world_system->GetActiveCamera();
                    if (camera == nullptr) {
                        return;
                    }
                    system.GetCameraManager().SetActiveCameraIndex(camera->m_display_id);
                    cb.DrawRenderers(
                        "Lit",
                        system.GetRendererManager().FilterAndSortRenderers({}),
                        system.GetCameraManager().GetActiveCameraIndex(),
                        extent
                    );
                    system.GetSceneDataManager().DrawSkybox(
                        cb,
                        system.GetFrameManager().GetFrameInFlight(),
                        system.GetCameraManager().GetPVMatForSkybox(),
                        extent
                    );
                })
                .WrapRenderPass();
            rgb.AddPass(builder.Get());
        }

        /**
         * Game widget bloom compute pass.
         */
        auto &game_bloom_binding = game_bloom.AllocateResourceBinding();
        rgb.AddPass(
            RenderGraphPassBuilder{m_system}
                .SetName("Game Bloom FX pass")
                .UseImage(hdr_color_id, IAT::ShaderRandomRead)
                .UseImage(game_widget_color_id, IAT::ShaderRandomWrite)
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction([&game_bloom,
                                  texture_width,
                                  texture_height,
                                  &game_bloom_binding,
                                  hdr_color_id,
                                  game_widget_color_id](CommandBuffer &cb, const RenderGraph &rg) {
                    game_bloom_binding.GetShaderResourceBinding().BindTexture(
                        "inputImage", *rg.GetInternalTextureResource(hdr_color_id)
                    );
                    game_bloom_binding.GetShaderResourceBinding().BindTexture(
                        "outputImage", *rg.GetInternalTextureResource(game_widget_color_id)
                    );
                    cb.BindComputeStage(game_bloom);
                    cb.BindComputeResource(game_bloom_binding);
                    cb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
                })
                .Get()
        );

        /**
         * GUI compose pass — reads scene and game widget colors, writes to final output.
         */
        rgb.AddPass(
            RenderGraphPassBuilder{m_system}
                .SetName("GUI Compose pass")
                .UseImage(scene_widget_color_id, IAT::ShaderSampledRead)
                .UseImage(game_widget_color_id, IAT::ShaderSampledRead)
                .AppendColorAttachment(
                    {final_color_target_id,
                     {},
                     AttachmentUtils::LoadOperation::Load,
                     AttachmentUtils::StoreOperation::Store}
                )
                .SetPassFunction([gui_system](CommandBuffer &cb, const RenderGraph &) {
                    gui_system->DrawGUI(cb.GetCommandBuffer());
                })
                .WrapRenderPass()
                .Get()
        );

        auto rg = rgb.BuildRenderGraph();

        // Post-build: register shadow maps with scene data manager
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            auto shadow_map_target = rg->GetInternalTextureResource(shadow_ids[i]);
            system.GetSceneDataManager().SetLightShadowMap(i, *shadow_map_target);
        }

        return rg;
    }
} // namespace Editor
