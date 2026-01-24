#include "DefaultExcutor.h"
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    DefaultExcutor::DefaultExcutor(RenderSystem &system, uint32_t target_width, uint32_t target_height) :
        RenderExcutor(system) {
        // XXX: hardcoded light setup for testing
        m_system.GetSceneDataManager().SetLightDirectional(0, glm::vec3{1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, 1.0f});
        m_system.GetSceneDataManager().SetLightCount(1);

        RenderTargetTexture::RenderTargetTextureDesc desc{
            .dimensions = 2,
            .width = target_width,
            .height = target_height,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
            .multisample = 1,
            .is_cube_map = false
        };
        m_color_target =
            Engine::RenderTargetTexture::CreateUnique(m_system, desc, Texture::SamplerDesc{}, "Color attachment");
        desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
        m_depth_target =
            Engine::RenderTargetTexture::CreateUnique(m_system, desc, Texture::SamplerDesc{}, "Depth attachment");

        desc.width = SHADOWMAP_WIDTH;
        desc.height = SHADOWMAP_HEIGHT;
        std::shared_ptr<RenderTargetTexture> shadow =
            Engine::RenderTargetTexture::CreateUnique(m_system, desc, Texture::SamplerDesc{}, "Shadowmap Light 0");
        m_system.GetSceneDataManager().SetLightShadowMap(0, shadow);

        RenderGraphBuilder rgb{m_system};
        rgb.RegisterImageAccess(*m_color_target);
        rgb.RegisterImageAccess(*m_depth_target);
        rgb.RegisterImageAccess(*shadow);

        using IAT = AccessHelper::ImageAccessType;
        rgb.UseImage(*shadow, IAT::DepthAttachmentWrite);
        vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
        rgb.RecordRasterizerPassWithoutRT([&system, shadow, shadow_map_extent](GraphicsCommandBuffer &gcb) {
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            gcb.BeginRendering(
                {nullptr},
                {shadow.get(),
                 nullptr,
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}},
                shadow_map_extent,
                "Shadowmap Pass"
            );
            gcb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
            gcb.DrawRenderers(
                "Shadowmap",
                system.GetRendererManager().FilterAndSortRenderers({}),
                0,
                vk::Extent2D{shadow->GetTextureDescription().width, shadow->GetTextureDescription().height}
            );
            gcb.EndRendering();
        });

        rgb.UseImage(*shadow, IAT::ShaderRead);
        rgb.UseImage(*m_color_target, IAT::ColorAttachmentWrite);
        rgb.UseImage(*m_depth_target, IAT::DepthAttachmentWrite);
        rgb.RecordRasterizerPass(
            {m_color_target.get(),
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::Store},
            {m_depth_target.get(),
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            [&system](GraphicsCommandBuffer &gcb) {
                vk::Extent2D extent{system.GetSwapchain().GetExtent()};
                vk::Rect2D scissor{{0, 0}, extent};
                gcb.SetupViewport(extent.width, extent.height, scissor);
                gcb.DrawRenderers("Lit", system.GetRendererManager().FilterAndSortRenderers({}));
            },
            "Lit pass"
        );

        m_render_graph = std::make_shared<RenderGraph>(rgb.BuildRenderGraph());
    }

    void DefaultExcutor::Excute() {
        m_render_graph->Execute();
    }

    std::shared_ptr<RenderTargetTexture> DefaultExcutor::GetColorRenderTarget() {
        return m_color_target;
    }
} // namespace Engine
