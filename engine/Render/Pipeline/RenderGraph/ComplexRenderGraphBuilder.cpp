#include "ComplexRenderGraphBuilder.h"
#include "RenderGraph.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Shader/ShaderAsset.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>

#include <vulkan/vulkan.hpp>

namespace Engine {
    ComplexRenderGraphBuilder::ComplexRenderGraphBuilder(RenderSystem &system) : RenderGraphBuilder(system) {
    }

    std::unique_ptr<RenderGraph> ComplexRenderGraphBuilder::BuildDefaultRenderGraph(uint32_t width, uint32_t height) {
        RenderTargetTexture::RenderTargetTextureDesc rtt_desc{
            .dimensions = 2,
            .width = 1920,
            .height = 1080,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
            .multisample = 1,
            .is_cube_map = false
        };
        auto color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        m_final_color_attachment_id = color_id;
        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat;
        auto hdr_color_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
        auto bloom_temp_id = this->RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{});
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
        auto shadow_id = this->RequestRenderTargetTexture(shadow_desc, Texture::SamplerDesc{});

        // XXX: Hardcoded bloom shader. Should use AssetManager to load shader when we have pipeline asset.
        auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        auto &amg = *MainClass::GetInstance()->GetAssetManager();
        m_bloom_shader = adb.GetNewAssetRef(AssetPath{adb, "~/shaders/bloom.comp.asset"});
        amg.LoadAssetImmediately(m_bloom_shader);
        auto bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        bloom_compute_stage->Instantiate(*m_bloom_shader->cas<ShaderAsset>());

        auto &system = m_system;
        auto &world = *MainClass::GetInstance()->GetWorldSystem();
        using IAT = MemoryAccessTypeImageBits;
        this->UseImage(shadow_id, IAT::DepthStencilAttachmentWrite);
        this->RecordRasterizerPassWithoutRT([&system, &shadow_id](GraphicsCommandBuffer &gcb, const RenderGraph &rg) {
            vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            auto shadow_map_target = rg.GetInternalTextureResource(shadow_id);
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
        });

        this->UseImage(shadow_id, IAT::ShaderSampledRead);
        this->UseImage(hdr_color_id, IAT::ColorAttachmentWrite);
        this->UseImage(depth_id, IAT::DepthStencilAttachmentWrite);
        this->RecordRasterizerPass(
            {hdr_color_id, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
            {depth_id,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            [&system, &world](GraphicsCommandBuffer &gcb, const RenderGraph &) {
                vk::Extent2D extent{system.GetSwapchain().GetExtent()};
                vk::Rect2D scissor{{0, 0}, extent};
                gcb.SetupViewport(extent.width, extent.height, scissor);
                gcb.DrawRenderers("Lit", system.GetRendererManager().FilterAndSortRenderers({}));
                system.GetSceneDataManager().DrawSkybox(
                    system.GetFrameManager().GetRawMainCommandBuffer(),
                    system.GetFrameManager().GetFrameInFlight(),
                    world.GetActiveCamera()->GetViewMatrix(),
                    world.GetActiveCamera()->GetProjectionMatrix()
                );
            },
            "Main Lit pass"
        );

        this->UseImage(hdr_color_id, IAT::ShaderRandomRead);
        this->UseImage(bloom_temp_id, IAT::ShaderRandomDefault);
        this->UseImage(color_id, IAT::ShaderRandomWrite);
        this->RecordComputePass(
            [bloom_compute_stage, width, height](ComputeCommandBuffer &ccb, const RenderGraph &) {
                ccb.BindComputeStage(*bloom_compute_stage);
                ccb.DispatchCompute(width / 16 + 1, height / 16 + 1, 1);
            },
            "Bloom FX pass"
        );

        auto rg{this->BuildRenderGraph()};

        bloom_compute_stage->AssignTexture("inputImage", *rg->GetInternalTextureResource(hdr_color_id));
        bloom_compute_stage->AssignTexture("bloomTemp", *rg->GetInternalTextureResource(bloom_temp_id));
        bloom_compute_stage->AssignTexture("outputImage", *rg->GetInternalTextureResource(color_id));
        return rg;
    }

    MemoryAccessTypeImageBits ComplexRenderGraphBuilder::GetColorAttachmentAccessType() const {
        return MemoryAccessTypeImageBits::ShaderRandomWrite;
    }
    uint32_t ComplexRenderGraphBuilder::GetFinalColorAttachmentID() const {
        return m_final_color_attachment_id;
    }
} // namespace Engine
