#include "ComplexRenderGraphBuilder.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Framework/MainClass.h>
#include <Framework/Tools/ShadowCascadeResources.h>
#include <Framework/World/WorldSystem.h>
#include <Render/Asset/Shader/ShaderAsset.h>
#include <Render/Asset/Shader/ShaderKernel.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/Pipeline/Renderer/Camera.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/IPresentProvider.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Resource/RenderTargetTexture.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <vulkan/vulkan.hpp>

#include <Rhi/Buffer/ComputeBuffer.h>

namespace Engine {
    ComplexRenderGraphBuilder::ComplexRenderGraphBuilder(RenderSystem &system) : m_system(system) {
        // XXX: Hardcoded bloom shader. Should use AssetManager to load shader when we have pipeline asset.
        auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        m_bloom_shader = adb.GetNewAssetRef(AssetPath{"builtin://shaders/bloom.comp.asset"});
    }

    std::unique_ptr<RenderGraph> ComplexRenderGraphBuilder::BuildDefaultRenderGraph(
        RGTextureHandle &final_color_target_id
    ) {
        RenderGraphBuilder rgb{m_system};

        // Import the render-owned model matrices buffer.
        const RGBufferHandle mm_handle = rgb.ImportExternalResource(
            m_system.GetSceneDataManager().GetModelMatricesBuffer(),
            Rhi::MemoryAccessTypeBuffer(Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite)
        );

        // Request transient resources. Final/HDR/Depth textures are resizable
        // (scale 1.0) and follow the ResizableRTTManager reference size, which the
        // RenderSystem keeps equal to the swapchain/window size.
        RenderTargetTexture::RenderTargetTextureDesc rtt_desc{
            .dimensions = 2,
            .width = 0,
            .height = 0,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
            .multisample = 1,
            .is_cube_map = false
        };
        auto color_id =
            rgb.RequestResizableRenderTargetTexture(rtt_desc, Rhi::Texture::SamplerDesc{}, 1.0f, 1.0f, "Final Color");
        final_color_target_id = color_id;
        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat;
        auto hdr_color_id =
            rgb.RequestResizableRenderTargetTexture(rtt_desc, Rhi::Texture::SamplerDesc{}, 1.0f, 1.0f, "HDR Color");
        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
        auto depth_id =
            rgb.RequestResizableRenderTargetTexture(rtt_desc, Rhi::Texture::SamplerDesc{}, 1.0f, 1.0f, "Depth");

        // Shadow map cascade passes. The storage is render-owned and every
        // budgeted slot is imported into the graph, so lights can come and go at
        // runtime without this graph being rebuilt. Shared with the editor's
        // graph builder so both applications stay in step.
        const std::vector<RGTextureHandle> shadow_ids = AddShadowCascadePasses(rgb, m_system, mm_handle);

        // Set up the bloom kernel: one kernel exists per shader asset on the
        // device, no matter how many builders request it.
        m_bloom_kernel = &RequestComputeKernel(m_system.GetDeviceContext(), *m_bloom_shader.as<ShaderAsset>());

        auto &system = m_system;
        auto world_system = MainClass::GetInstance()->GetWorldSystem().get();
        auto &bloom_kernel = *m_bloom_kernel;
        using IAT = Rhi::MemoryAccessTypeImageBits;

        /**
         * Main Lit pass — rasterizer with color + depth attachments, WrapRenderPass().
         */
        {
            auto lit_pass_builder = RenderGraphPassBuilder{m_system}.SetName("Main Lit pass");

            // Declare shadow map reads
            for (size_t i = 0; i < shadow_ids.size(); i++) {
                lit_pass_builder.UseImage(shadow_ids[i], IAT::ShaderSampledRead);
            }

            // Declare model matrices read (after physics compute writes).
            lit_pass_builder.UseBuffer(
                mm_handle, Rhi::MemoryAccessTypeBuffer(Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead)
            );

            lit_pass_builder
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
                .SetPassFunction([&system, world_system](CommandBuffer &cb, const RenderGraph &) {
                    vk::Extent2D extent{system.GetPresentProvider().GetExtent()};
                    if (extent.width <= 0 || extent.height <= 0) return;
                    vk::Rect2D scissor{{0, 0}, extent};
                    cb.SetupViewport(extent.width, extent.height, scissor);
                    auto active_camera = world_system->GetActiveCamera();
                    if (active_camera == nullptr) {
                        // No active camera, skip rendering.
                        return;
                    }
                    system.GetCameraManager().SetActiveCameraIndex(active_camera->m_display_id);
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
            rgb.AddPass(lit_pass_builder.Get());
        }

        /**
         * Bloom FX compute pass.
         */
        rgb.AddPass(
            RenderGraphPassBuilder{m_system}
                .SetName("Bloom FX pass")
                .UseImage(hdr_color_id, IAT::ShaderRandomRead)
                .UseImage(final_color_target_id, IAT::ShaderRandomWrite)
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction([&bloom_kernel,
                                  hdr_color_id,
                                  final_color_target_id](CommandBuffer &cb, const RenderGraph &rg) {
                    const auto &hdr_desc = rg.GetInternalTextureResource(hdr_color_id)->GetTextureDescription();
                    bloom_kernel.Dispatch(
                        cb.GetCommandBuffer(),
                        {{"inputImage",
                          Rhi::ComputeKernelResource::Image(*rg.GetInternalTextureResource(hdr_color_id))},
                         {"outputImage",
                          Rhi::ComputeKernelResource::Image(*rg.GetInternalTextureResource(final_color_target_id))}},
                        hdr_desc.width / 16 + 1,
                        hdr_desc.height / 16 + 1,
                        1
                    );
                })
                .Get()
        );

        return rgb.BuildRenderGraph();
    }
} // namespace Engine
