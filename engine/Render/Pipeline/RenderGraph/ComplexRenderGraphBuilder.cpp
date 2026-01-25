#include "ComplexRenderGraphBuilder.h"
#include "RenderGraph.h"
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>

#include <vulkan/vulkan.hpp>

namespace Engine {
    ComplexRenderGraphBuilder::ComplexRenderGraphBuilder(RenderSystem &system) : RenderGraphBuilder(system) {
    }

    std::unique_ptr<RenderGraph> ComplexRenderGraphBuilder::BuildDefaultRenderGraph(
        const RenderTargetTexture &color_target, const RenderTargetTexture &depth_target
    ) {
        // XXX: Hardcoded light direction
        m_system.GetSceneDataManager().SetLightDirectional(0, glm::vec3{1.0f, 1.0f, -1.0f}, glm::vec3{1.0f, 1.0f, 1.0f});
        m_system.GetSceneDataManager().SetLightCount(1);

        RenderTargetTexture::RenderTargetTextureDesc desc{
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
        Texture::SamplerDesc sampler_desc{
            .u_address = Texture::SamplerDesc::AddressMode::ClampToEdge,
            .v_address = Texture::SamplerDesc::AddressMode::ClampToEdge,
            .w_address = Texture::SamplerDesc::AddressMode::ClampToEdge
        };
        m_shadow_target =
            Engine::RenderTargetTexture::CreateUnique(m_system, desc, sampler_desc, "Shadowmap Light 0");
        m_system.GetSceneDataManager().SetLightShadowMap(0, m_shadow_target);
        auto &shadow_target = *m_shadow_target;

        this->RegisterImageAccess(color_target);
        this->RegisterImageAccess(depth_target);
        this->RegisterImageAccess(shadow_target);

        auto &system = m_system;
        auto &world = *MainClass::GetInstance()->GetWorldSystem();
        using IAT = AccessHelper::ImageAccessType;
        this->UseImage(*m_shadow_target, IAT::DepthAttachmentWrite);
        this->RecordRasterizerPassWithoutRT([&system, &shadow_target](GraphicsCommandBuffer &gcb) {
            vk::Extent2D shadow_map_extent{
                shadow_target.GetTextureDescription().width, shadow_target.GetTextureDescription().height
            };
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            gcb.BeginRendering(
                {nullptr},
                {&shadow_target,
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

        this->UseImage(shadow_target, IAT::ShaderRead);
        this->UseImage(color_target, IAT::ColorAttachmentWrite);
        this->UseImage(depth_target, IAT::DepthAttachmentWrite);
        this->RecordRasterizerPass(
            {&color_target, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
            {&depth_target,
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            [&system, &world](GraphicsCommandBuffer &gcb) {
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
            "Main pass"
        );

        return this->BuildRenderGraph();
    }
} // namespace Engine
