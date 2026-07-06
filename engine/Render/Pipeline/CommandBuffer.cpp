#include "CommandBuffer.h"

#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Render/AttachmentUtilsFunc.h"
#include "Render/DebugUtils.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Memory/Texture.h"
#include "Render/Pipeline/Compute/ComputeResourceBinding.h"
#include "Render/Pipeline/Compute/ComputeStage.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/Camera.h"
#include "Render/Renderer/IVertexBasedRenderer.h"
#include "Render/Renderer/VertexAttribute.h"
#include "Render/Resource/MaterialInstanceManager.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {

    CommandBuffer::CommandBuffer(RenderSystem &system, vk::CommandBuffer _cb, uint32_t frame_in_flight) :
        m_system(system), cb(_cb), m_inflight_frame_index(frame_in_flight) {
    }

    // ── Transfer ────────────────────────────────────────────────────────────

    void CommandBuffer::BlitColorImage(const Texture &src, const Texture &dst) {
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
        const Texture &src, const Texture &dst, TextureArea src_area, TextureArea dst_area
    ) {
        assert(0 <= src_area.x0 && 0 <= src_area.y0 && 0 <= src_area.z0);
        assert(0 <= dst_area.x0 && 0 <= dst_area.y0 && 0 <= dst_area.z0);
        assert(src_area.x0 < src_area.x1 && src_area.y0 < src_area.y1 && src_area.z0 < src_area.z1);
        assert(dst_area.x0 < dst_area.x1 && dst_area.y0 < dst_area.y1 && dst_area.z0 < src_area.z1);

        vk::ImageBlit2 blit{
            vk::ImageSubresourceLayers{
                vk::ImageAspectFlagBits::eColor,
                src_area.mip_level,
                src_area.array_layer_base,
                src_area.array_layer_count
            },
            {vk::Offset3D{src_area.x0, src_area.y0, src_area.z0}, vk::Offset3D{src_area.x1, src_area.y1, src_area.z1}},
            vk::ImageSubresourceLayers{
                vk::ImageAspectFlagBits::eColor,
                dst_area.mip_level,
                dst_area.array_layer_base,
                dst_area.array_layer_count
            },
            {vk::Offset3D{dst_area.x0, dst_area.y0, dst_area.z0}, vk::Offset3D{dst_area.x1, dst_area.y1, dst_area.z1}},
        };
        vk::BlitImageInfo2 bii{
            src.GetImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            dst.GetImage(),
            vk::ImageLayout::eTransferDstOptimal,
            {blit},
            vk::Filter::eLinear
        };
        cb.blitImage2(bii);
    }

    // ── Render pass ─────────────────────────────────────────────────────────

    void CommandBuffer::BeginRendering(
        const AttachmentUtils::AttachmentDescription &color,
        const AttachmentUtils::AttachmentDescription &depth,
        vk::Extent2D extent,
        const std::string &name
    ) {
        std::vector<vk::RenderingAttachmentInfo> color_attachment;

        if (color.texture) {
            color_attachment.push_back(GetVkAttachmentInfo(color, vk::ImageLayout::eColorAttachmentOptimal));
            m_pripr.color_attachment_format[0] = color.texture->GetTextureDescription().format;
            m_pripr.color_attachment_format[1] = ImageUtils::ImageFormat::UNDEFINED;
        } else {
            m_pripr.color_attachment_format[0] = ImageUtils::ImageFormat::UNDEFINED;
        }

        vk::RenderingAttachmentInfo depth_attachment;
        if (depth.texture) {
            depth_attachment = vk::RenderingAttachmentInfo{
                GetVkAttachmentInfo(depth, vk::ImageLayout::eDepthStencilAttachmentOptimal)
            };
            m_pripr.depth_stencil_attachment_format = depth.texture->GetTextureDescription().format;
        } else {
            m_pripr.depth_stencil_attachment_format = ImageUtils::ImageFormat::UNDEFINED;
        }

        vk::RenderingInfo info{
            vk::RenderingFlags{0},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            color_attachment,
            depth.texture ? &depth_attachment : nullptr,
            nullptr
        };
        DEBUG_CMD_START_LABEL(cb, name.c_str());
        cb.beginRendering(info);
    }

    void CommandBuffer::BeginRendering(
        const std::vector<AttachmentUtils::AttachmentDescription> &colors,
        const AttachmentUtils::AttachmentDescription depth,
        vk::Extent2D extent,
        const std::string &name
    ) {
        std::vector<vk::RenderingAttachmentInfo> color_attachment_info(colors.size(), vk::RenderingAttachmentInfo{});
        assert(colors.size() < 8 && "At most 8 color rendering targets are supported.");
        for (size_t i = 0; i < colors.size(); i++) {
            color_attachment_info[i] = GetVkAttachmentInfo(colors[i], vk::ImageLayout::eColorAttachmentOptimal);
            m_pripr.color_attachment_format[i] = colors[i].texture->GetTextureDescription().format;
        }
        if (colors.size() < 8) {
            m_pripr.color_attachment_format[colors.size()] = ImageUtils::ImageFormat::UNDEFINED;
        }

        vk::RenderingAttachmentInfo depth_attachment_info{};
        if (depth.texture) {
            depth_attachment_info = GetVkAttachmentInfo(depth, vk::ImageLayout::eDepthStencilAttachmentOptimal);
            m_pripr.depth_stencil_attachment_format = depth.texture->GetTextureDescription().format;
        } else {
            m_pripr.depth_stencil_attachment_format = ImageUtils::ImageFormat::UNDEFINED;
        }

        vk::RenderingInfo info{
            vk::RenderingFlags{0},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            color_attachment_info,
            depth.texture ? &depth_attachment_info : nullptr,
            nullptr
        };

        DEBUG_CMD_START_LABEL(cb, name.c_str());
        cb.beginRendering(info);
    }

    void CommandBuffer::EndRendering() {
        cb.endRendering();
        DEBUG_CMD_END_LABEL(cb);
        m_pripr = {};
    }

    // ── Scene / Camera / Material ───────────────────────────────────────────

    void CommandBuffer::BindSceneResources(const RenderSystemState::SceneDataManager &sdm) {
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            sdm.GetCommonPipelineLayout(),
            0,
            {sdm.GetLightDescriptorSet(m_inflight_frame_index)},
            {}
        );
    }

    void CommandBuffer::BindCameraResources(const RenderSystemState::CameraManager &cm) {
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            cm.GetCommonPipelineLayout(),
            1,
            {cm.GetDescriptorSet(m_inflight_frame_index)},
            {}
        );
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
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            m_bound_material_pipeline = std::make_pair(pipeline, pipeline_layout);
        }

        if (!tpl.HasMaterialData()) return;

        auto dynamic_offsets = material.UpdateGPUInfo(tpl, m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptor(tpl, m_inflight_frame_index);
        if (material_descriptor_set) {
            cb.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, pipeline_layout, 2, {material_descriptor_set}, dynamic_offsets
            );
        }
    }

    // ── Viewport ────────────────────────────────────────────────────────────

    void CommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        cb.setViewport(0, 1, &vp);
        cb.setScissor(0, 1, &scissor);
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
        std::vector<vk::DeviceSize> offsets{};
        std::vector<vk::Buffer> buffers{};
        offsets.resize(bindings.size());
        buffers.resize(bindings.size());
        for (size_t i = 0; i < bindings.size(); i++) {
            offsets[i] = bindings[i].offset;
            buffers[i] = bindings[i].buffer->GetBuffer();
        }

        cb.bindVertexBuffers(0, buffers, offsets);
        auto indices = mesh.GetIndexBufferBinding();
        cb.bindIndexBuffer(indices.buffer->GetBuffer(), indices.offset, vk::IndexType::eUint32);

        struct {
            glm::mat4 m;
            int32_t i;
            int32_t mi;
        } push_constants{.m = model_matrix, .i = camera_index, .mi = model_mat_index};

        cb.pushConstants(
            m_bound_material_pipeline.value().second,
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof(push_constants),
            reinterpret_cast<const void *>(&push_constants)
        );
        cb.drawIndexed(mesh.GetIndexCount(), 1, 0, 0, 0);
    }

    void CommandBuffer::DrawRenderers(const std::string &tag, const RendererList &renderers) {
        this->DrawRenderers(
            tag, renderers, m_system.GetCameraManager().GetActiveCameraIndex(), m_system.GetSwapchain().GetExtent()
        );
    }

    void CommandBuffer::DrawRenderers(
        const std::string &tag, const RendererList &renderers, int32_t camera_index, vk::Extent2D extent
    ) {
        auto &renderer_manager = m_system.GetRendererManager();
        auto &material_manager = m_system.GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();

        BindSceneResources(m_system.GetSceneDataManager());
        BindCameraResources(m_system.GetCameraManager());

        vk::Rect2D scissor{{0, 0}, extent};
        this->SetupViewport(extent.width, extent.height, scissor);
        for (const auto &rid : renderers) {
            auto material_handle = renderer_manager.GetMaterialResourceHandle(rid);
            material_manager.EnsureReady(material_handle);
            auto *mesh = renderer_manager.GetRenderer(rid);
            auto *material_instance = material_manager.Resolve(material_handle);
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

    // ── Compute ─────────────────────────────────────────────────────────────

    void CommandBuffer::BindComputeStage(ComputeStage &stage) {
        m_bound_compute_stage = stage;
        this->cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
    }

    void CommandBuffer::BindComputeResource(ComputeResourceBinding &binding) {
        assert(m_bound_compute_stage.has_value() && "Compute pipeline is not bound.");

        auto offsets = binding.UpdateGPUInfo(m_inflight_frame_index);
        this->cb.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            m_bound_compute_stage.value().get().GetPipelineLayout(),
            0,
            {binding.GetDescriptorSet(m_inflight_frame_index)},
            offsets
        );
    }

    void CommandBuffer::DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        assert(m_bound_compute_stage.has_value() && "Compute pipeline is not bound.");
        this->cb.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    // ── Reset ───────────────────────────────────────────────────────────────

    void CommandBuffer::Reset() noexcept {
        cb.reset();
        m_bound_material_pipeline.reset();
        m_bound_compute_stage.reset();
    }

} // namespace Engine
