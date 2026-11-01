#include "CommandBuffer.h"

#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include "Render/Backend/Vulkan/AttachmentUtilsFunc.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/Pipeline/Renderer/Camera.h"
#include "Render/Pipeline/Renderer/IVertexBasedRenderer.h"
#include "Render/Pipeline/Renderer/VertexAttribute.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/Resource/MaterialInstanceManager.h"
#include "Rhi/Backend/Vulkan/MemoryAccessHelper.hpp"
#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Texture/Texture.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {

    CommandBuffer::CommandBuffer(RenderSystem &system, Rhi::CommandEncoder &encoder, uint32_t frame_in_flight) :
        m_system(system),
        cb(Rhi::ToVulkanCommandBuffer(encoder.GetHandle())),
        m_encoder(&encoder),
        m_inflight_frame_index(frame_in_flight) {
    }

    // ── Transfer ────────────────────────────────────────────────────────────

    void CommandBuffer::BlitColorImage(const Rhi::Texture &src, const Rhi::Texture &dst) {
        const auto &src_desc{src.GetTextureDescription()}, &dst_desc{dst.GetTextureDescription()};
        BlitColorImage(
            src,
            dst,
            TextureArea{
                .mip_level = 0,
                .array_layer_base = 0,
                .array_layer_count = src_desc.array_layers,
                .x0 = 0,
                .y0 = 0,
                .z0 = 0,
                .x1 = static_cast<int32_t>(src_desc.width),
                .y1 = static_cast<int32_t>(src_desc.height),
                .z1 = static_cast<int32_t>(src_desc.depth)
            },
            TextureArea{
                .mip_level = 0,
                .array_layer_base = 0,
                .array_layer_count = dst_desc.array_layers,
                .x0 = 0,
                .y0 = 0,
                .z0 = 0,
                .x1 = static_cast<int32_t>(dst_desc.width),
                .y1 = static_cast<int32_t>(dst_desc.height),
                .z1 = static_cast<int32_t>(dst_desc.depth)
            }
        );
    }

    void CommandBuffer::BlitColorImage(
        const Rhi::Texture &src, const Rhi::Texture &dst, TextureArea src_area, TextureArea dst_area
    ) {
        assert(0 <= src_area.x0 && 0 <= src_area.y0 && 0 <= src_area.z0);
        assert(0 <= dst_area.x0 && 0 <= dst_area.y0 && 0 <= dst_area.z0);
        assert(src_area.x0 < src_area.x1 && src_area.y0 < src_area.y1 && src_area.z0 < src_area.z1);
        assert(dst_area.x0 < dst_area.x1 && dst_area.y0 < dst_area.y1 && dst_area.z0 < src_area.z1);

        Rhi::ImageBlitDescription blit{};
        blit.source = src.GetImage();
        blit.destination = dst.GetImage();
        blit.aspect = Rhi::ImageAspect::Color;
        blit.source_mip = src_area.mip_level;
        blit.source_layer = src_area.array_layer_base;
        blit.source_layer_count = src_area.array_layer_count;
        blit.destination_mip = dst_area.mip_level;
        blit.destination_layer = dst_area.array_layer_base;
        blit.destination_layer_count = dst_area.array_layer_count;
        blit.source_min = Rhi::Offset3D{src_area.x0, src_area.y0, src_area.z0};
        blit.source_max = Rhi::Offset3D{src_area.x1, src_area.y1, src_area.z1};
        blit.destination_min = Rhi::Offset3D{dst_area.x0, dst_area.y0, dst_area.z0};
        blit.destination_max = Rhi::Offset3D{dst_area.x1, dst_area.y1, dst_area.z1};
        m_encoder->BlitImage(blit, true);
    }

    void CommandBuffer::RecordCopyImageToBuffer(
        const RenderTargetTexture &image, const Rhi::DeviceBuffer &dst, Rhi::MemoryAccessTypeImageBits last_access
    ) {
        const auto &desc = image.GetTextureDescription();

        // Pre-barrier: source image into a copyable layout. The transition is one
        // the caller knows the shape of - a single mip and layer, colour aspect,
        // read by a copy - so it is recorded through the seam.
        Rhi::ImageBarrier pre_barrier{};
        pre_barrier.image = image.GetImage();
        pre_barrier.aspect = Rhi::ImageAspect::Color;
        pre_barrier.base_mip = 0;
        pre_barrier.mip_count = 1;
        pre_barrier.base_layer = 0;
        pre_barrier.layer_count = 1;
        pre_barrier.from = {last_access};
        pre_barrier.to = {Rhi::MemoryAccessTypeImageBits::TransferRead};
        pre_barrier.from_scope = Rhi::PipelineScope::Unspecified;
        pre_barrier.to_scope = Rhi::PipelineScope::Transfer;
        m_encoder->Barrier(pre_barrier);

        vk::BufferImageCopy region{
            0, // bufferOffset
            0, // bufferRowLength (tight packing)
            0, // bufferImageHeight
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            vk::Offset3D{0, 0, 0},
            vk::Extent3D{desc.width, desc.height, desc.depth}
        };
        cb.copyImageToBuffer(Engine::Rhi::ToVulkanImage(image.GetImage()), vk::ImageLayout::eTransferSrcOptimal, Rhi::ToVulkanBuffer(dst.GetBuffer()), region);

        // Post-barrier: restore the source image to its nominal layout so the
        // frame-completion blit (or any later pass) still observes it.
        Rhi::ImageBarrier post_barrier = pre_barrier;
        post_barrier.from = {Rhi::MemoryAccessTypeImageBits::TransferRead};
        post_barrier.to = {};
        post_barrier.from_scope = Rhi::PipelineScope::Transfer;
        post_barrier.to_scope = Rhi::PipelineScope::Unspecified;
        m_encoder->Barrier(post_barrier);
    }

    // ── Render pass ─────────────────────────────────────────────────────────

    void CommandBuffer::BeginRendering(
        const AttachmentUtils::AttachmentDescription &color,
        const AttachmentUtils::AttachmentDescription &depth,
        Rhi::Extent2D extent,
        const std::string &name
    ) {
        Rhi::RenderPassDescription description{};
        description.area = Rhi::Rect2D{Rhi::Offset2D{0, 0}, Rhi::Extent2D{extent.width, extent.height}};

        if (color.texture) {
            Rhi::ColorAttachmentDescription attachment{};
            attachment.view = color.texture->GetImageView(color.range);
            attachment.load = AttachmentUtils::ToRhiLoadOperation(color.load_op);
            attachment.store = AttachmentUtils::ToRhiStoreOperation(color.store_op);
            attachment.clear = AttachmentUtils::ToRhiClearColor(color.clear_value);
            description.color_attachments.push_back(attachment);
            m_pripr.color_attachment_format[0] = color.texture->GetTextureDescription().format;
            m_pripr.color_attachment_format[1] = Rhi::ImageFormat::UNDEFINED;
        } else {
            m_pripr.color_attachment_format[0] = Rhi::ImageFormat::UNDEFINED;
        }

        if (depth.texture) {
            description.depth_attachment.view = depth.texture->GetImageView(depth.range);
            description.depth_attachment.load = AttachmentUtils::ToRhiLoadOperation(depth.load_op);
            description.depth_attachment.store = AttachmentUtils::ToRhiStoreOperation(depth.store_op);
            description.depth_attachment.clear_depth = AttachmentUtils::ToRhiClearDepth(depth.clear_value);
            m_pripr.depth_stencil_attachment_format = depth.texture->GetTextureDescription().format;
        } else {
            m_pripr.depth_stencil_attachment_format = Rhi::ImageFormat::UNDEFINED;
        }

        DEBUG_CMD_START_LABEL(cb, name.c_str());
        m_encoder->BeginRenderPass(description);
    }

    void CommandBuffer::BeginRendering(
        const std::vector<AttachmentUtils::AttachmentDescription> &colors,
        const AttachmentUtils::AttachmentDescription depth,
        Rhi::Extent2D extent,
        const std::string &name
    ) {
        assert(colors.size() < 8 && "At most 8 color rendering targets are supported.");

        Rhi::RenderPassDescription description{};
        description.area = Rhi::Rect2D{Rhi::Offset2D{0, 0}, Rhi::Extent2D{extent.width, extent.height}};
        description.color_attachments.reserve(colors.size());
        for (size_t i = 0; i < colors.size(); i++) {
            Rhi::ColorAttachmentDescription attachment{};
            attachment.view = colors[i].texture->GetImageView(colors[i].range);
            attachment.load = AttachmentUtils::ToRhiLoadOperation(colors[i].load_op);
            attachment.store = AttachmentUtils::ToRhiStoreOperation(colors[i].store_op);
            attachment.clear = AttachmentUtils::ToRhiClearColor(colors[i].clear_value);
            description.color_attachments.push_back(attachment);
            m_pripr.color_attachment_format[i] = colors[i].texture->GetTextureDescription().format;
        }
        if (colors.size() < 8) {
            m_pripr.color_attachment_format[colors.size()] = Rhi::ImageFormat::UNDEFINED;
        }

        if (depth.texture) {
            description.depth_attachment.view = depth.texture->GetImageView(depth.range);
            description.depth_attachment.load = AttachmentUtils::ToRhiLoadOperation(depth.load_op);
            description.depth_attachment.store = AttachmentUtils::ToRhiStoreOperation(depth.store_op);
            description.depth_attachment.clear_depth = AttachmentUtils::ToRhiClearDepth(depth.clear_value);
            m_pripr.depth_stencil_attachment_format = depth.texture->GetTextureDescription().format;
        } else {
            m_pripr.depth_stencil_attachment_format = Rhi::ImageFormat::UNDEFINED;
        }

        DEBUG_CMD_START_LABEL(cb, name.c_str());
        m_encoder->BeginRenderPass(description);
    }

    void CommandBuffer::EndRendering() {
        m_encoder->EndRenderPass();
        DEBUG_CMD_END_LABEL(cb);
        m_pripr = {};
    }

    // ── Scene / Camera / Material ───────────────────────────────────────────

    void CommandBuffer::BindSceneResources(const RenderSystemState::SceneDataManager &sdm) {
        m_encoder->BindGraphicsDescriptorSet(0, sdm.GetLightDescriptorSet(m_inflight_frame_index), sdm.GetCommonPipelineLayout());
    }

    void CommandBuffer::BindCameraResources(const RenderSystemState::CameraManager &cm) {
        m_encoder->BindGraphicsDescriptorSet(1, cm.GetDescriptorSet(m_inflight_frame_index), cm.GetCommonPipelineLayout());
    }

    void CommandBuffer::BindMaterial(MaterialInstance &material, MaterialTemplate &tpl) {
        const auto &pipeline = tpl.GetPipeline();
        const auto &pipeline_layout = tpl.GetPipelineLayout();

        bool bind_new_pipeline = false;
        if (!m_bound_material_pipeline.has_value()) {
            bind_new_pipeline = true;
        } else if (pipeline != m_bound_material_pipeline.value().first
                   || pipeline_layout != m_bound_material_pipeline.value().second) {
            bind_new_pipeline = true;
        }
        if (bind_new_pipeline) {
            m_encoder->BindGraphicsPipeline(Rhi::FromVulkan(pipeline), Rhi::FromVulkan(pipeline_layout));
            m_bound_material_pipeline = std::make_pair(pipeline, pipeline_layout);
        }

        if (!tpl.HasMaterialData()) return;

        // Refresh the set on every bind, so the epoch the arena records for it
        // stays an upper bound on the epochs whose command buffers use it.
        auto material_binding = material.UpdateGPUInfo(tpl, m_inflight_frame_index);
        if (material_binding.set.IsValid()) {
            m_encoder->BindGraphicsDescriptorSet(
                2, material_binding.set, Rhi::FromVulkan(pipeline_layout), material_binding.dynamic_offsets
            );
        }
    }

    // ── Viewport ────────────────────────────────────────────────────────────

    void CommandBuffer::SetupViewport(float vpWidth, float vpHeight, Rhi::Rect2D scissor) {
        m_encoder->SetViewport(vpWidth, vpHeight, scissor);
    }

    // ── Drawing ─────────────────────────────────────────────────────────────

    void CommandBuffer::DrawMesh(const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix) {
        this->DrawMesh(mesh, model_matrix, m_system.GetCameraManager().GetActiveCameraIndex());
    }

    void CommandBuffer::DrawMesh(const IVertexBasedRenderer &mesh) {
        this->DrawMesh(mesh, glm::mat4{1.0f});
    }

    void CommandBuffer::DrawMesh(
        const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix, int32_t camera_index, int32_t model_mat_index
    ) {
        auto bindings = mesh.GetVertexAttributeBufferBindings();
        std::vector<Rhi::BufferHandle> buffers{};
        std::vector<uint64_t> offsets{};
        buffers.reserve(bindings.size());
        offsets.reserve(bindings.size());
        for (size_t i = 0; i < bindings.size(); i++) {
            offsets.push_back(bindings[i].offset);
            buffers.push_back(bindings[i].buffer->GetBuffer());
        }
        m_encoder->BindVertexBuffers(buffers, offsets);
        auto indices = mesh.GetIndexBufferBinding();
        m_encoder->BindIndexBuffer(indices.buffer->GetBuffer(), indices.offset, true);
        struct {
            glm::mat4 m;
            int32_t i;
            int32_t mi;
        } push_constants{.m = model_matrix, .i = camera_index, .mi = model_mat_index};

        m_encoder->PushConstants(std::as_bytes(std::span{&push_constants, 1}));
        m_encoder->DrawIndexed(mesh.GetIndexCount(), 1, 0, 0, 0);
    }

    void CommandBuffer::DrawRenderers(const std::string &tag, const RendererList &renderers) {
        this->DrawRenderers(
            tag, renderers, m_system.GetCameraManager().GetActiveCameraIndex(),
            m_system.GetPresentProvider().GetExtent()
        );
    }

    void CommandBuffer::DrawRenderers(
        const std::string &tag, const RendererList &renderers, int32_t camera_index, Rhi::Extent2D extent
    ) {
        auto &renderer_manager = m_system.GetRendererManager();
        auto *material_manager = m_system.GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();

        BindSceneResources(m_system.GetSceneDataManager());
        BindCameraResources(m_system.GetCameraManager());

        Rhi::Rect2D scissor{{0, 0}, extent};
        this->SetupViewport(extent.width, extent.height, scissor);
        for (const auto &rid : renderers) {
            auto material_handle = renderer_manager.GetMaterialResourceHandle(rid);
            material_manager->EnsureReady(material_handle);
            auto *mesh = renderer_manager.GetRenderer(rid);
            auto *material_instance = material_manager->Resolve(material_handle);
            if (!mesh || !material_instance) continue;

            const glm::mat4 &model_matrix = renderer_manager.GetModelMatrix(rid);
            int32_t model_mat_index = renderer_manager.GetModelMatrixIndex(rid);

            auto tpl = material_instance->GetLibrary().FindMaterialTemplate(
                tag, {{mesh->GetVertexAttributeFormat()}, m_pripr}
            );
            if (!tpl) continue;

            this->BindMaterial(*material_instance, *tpl);
            this->DrawMesh(*mesh, model_matrix, camera_index, model_mat_index);
        }
    }

    // ── Reset ───────────────────────────────────────────────────────────────

    void CommandBuffer::Reset() noexcept {
        cb.reset();
        m_bound_material_pipeline.reset();
    }

} // namespace Engine


















