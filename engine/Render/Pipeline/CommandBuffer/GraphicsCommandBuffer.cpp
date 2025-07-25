#include "GraphicsCommandBuffer.h"

#include "Render/ConstantData/PerModelConstants.h"
#include "Render/Memory/Buffer.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/RenderTargetBinding.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/DebugUtils.h"
#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"

#include <SDL3/SDL.h>

namespace Engine {
    GraphicsCommandBuffer::GraphicsCommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight) :
        TransferCommandBuffer(system, cb), m_inflight_frame_index(frame_in_flight) {
    }

    void GraphicsCommandBuffer::BeginRendering(const AttachmentUtils::AttachmentDescription &color,
                                               const AttachmentUtils::AttachmentDescription &depth,
                                               vk::Extent2D extent,
                                               const std::string &name) {
        DEBUG_CMD_START_LABEL(cb, name.c_str());
        std::vector<vk::RenderingAttachmentInfo> color_attachment;

        if (color.image && color.image_view) {
            color_attachment.push_back(
                GetVkAttachmentInfo(color, vk::ImageLayout::eColorAttachmentOptimal, vk::ClearColorValue{0, 0, 0, 0}));
        }

        vk::RenderingAttachmentInfo depth_attachment;
        if (depth.image && depth.image_view) {
            depth_attachment = vk::RenderingAttachmentInfo{GetVkAttachmentInfo(
                depth, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ClearDepthStencilValue{1.0f, 0U})};
        }
        else {
        }

        vk::RenderingInfo info{vk::RenderingFlags{0},
                               vk::Rect2D{{0, 0}, extent},
                               1,
                               0,
                               color_attachment,
                               depth.image ? &depth_attachment : nullptr,
                               nullptr};
        // Begin rendering after transit
        cb.beginRendering(info);
    }

    void GraphicsCommandBuffer::BeginRendering(const RenderTargetBinding &binding,
                                               vk::Extent2D extent,
                                               const std::string &name) {
        DEBUG_CMD_START_LABEL(cb, name.c_str());
        std::vector<vk::RenderingAttachmentInfo> color_attachment_info(binding.GetColorAttachmentCount(),
                                                                       vk::RenderingAttachmentInfo{});

        const auto &color_attachments = binding.GetColorAttachments();
        for (size_t i = 0; i < color_attachments.size(); i++) {
            color_attachment_info[i] = GetVkAttachmentInfo(
                color_attachments[i], vk::ImageLayout::eColorAttachmentOptimal, vk::ClearColorValue{0, 0, 0, 0});
        }

        vk::RenderingAttachmentInfo depth_attachment_info{};
        if (binding.HasDepthAttachment()) {
            const auto &depth_attachment = binding.GetDepthAttachment();
            depth_attachment_info = GetVkAttachmentInfo(depth_attachment,
                                                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                        vk::ClearDepthStencilValue{1.0f, 0U});
        }

        vk::RenderingInfo info{vk::RenderingFlags{0},
                               vk::Rect2D{{0, 0}, extent},
                               1,
                               0,
                               color_attachment_info,
                               binding.HasDepthAttachment() ? &depth_attachment_info : nullptr,
                               nullptr};
        // Begin rendering after transit
        cb.beginRendering(info);
    }

    void GraphicsCommandBuffer::BindMaterial(MaterialInstance &material, uint32_t pass_index) {
        const auto &pipeline = material.GetTemplate().GetPipeline(pass_index);
        const auto &pipeline_layout = material.GetTemplate().GetPipelineLayout(pass_index);

        bool bind_new_pipeline = false;
        if (!m_bound_material_pipeline.has_value()) {
            bind_new_pipeline = true;
        }
        else if (pipeline != m_bound_material_pipeline.value().first
                 || pipeline_layout != m_bound_material_pipeline.value().second) {
            bind_new_pipeline = true;
        }
        if (bind_new_pipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            m_bound_material_pipeline = std::make_pair(pipeline, pipeline_layout);
        }

        const auto &global_pool = m_system.GetGlobalConstantDescriptorPool();
        const auto &per_scene_descriptor_set = global_pool.GetPerSceneConstantSet(m_inflight_frame_index);
        const auto &per_camera_descriptor_set = global_pool.GetPerCameraConstantSet(m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptor(pass_index);

        if (material_descriptor_set) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                  pipeline_layout,
                                  0,
                                  {per_scene_descriptor_set, per_camera_descriptor_set, material_descriptor_set},
                                  {static_cast<uint32_t>(global_pool.GetPerCameraDynamicOffset(
                                      m_inflight_frame_index, m_system.GetActiveCameraId()))});
        }
        else {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                  pipeline_layout,
                                  0,
                                  {per_scene_descriptor_set, per_camera_descriptor_set},
                                  {static_cast<uint32_t>(global_pool.GetPerCameraDynamicOffset(
                                      m_inflight_frame_index, m_system.GetActiveCameraId()))});
        }

        material.WriteUBO(pass_index);
        material.WriteDescriptors(pass_index);
    }

    void GraphicsCommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        cb.setViewport(0, 1, &vp);
        cb.setScissor(0, 1, &scissor);
    }

    void GraphicsCommandBuffer::DrawMesh(const HomogeneousMesh &mesh, const glm::mat4 &model_matrix) {
        auto bindings = mesh.GetBindingInfo();
        cb.bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        cb.bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        cb.pushConstants(m_bound_material_pipeline.value().second,
                         vk::ShaderStageFlagBits::eVertex,
                         0,
                         ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
                         reinterpret_cast<const void *>(&model_matrix));
        cb.drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void GraphicsCommandBuffer::EndRendering() {
        cb.endRendering();
        DEBUG_CMD_END_LABEL(cb);
    }

    void GraphicsCommandBuffer::DrawMesh(const HomogeneousMesh &mesh) {
        auto bindings = mesh.GetBindingInfo();
        cb.bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        cb.bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        cb.drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void GraphicsCommandBuffer::Reset() noexcept {
        cb.reset();
        m_bound_material_pipeline.reset();
    }
} // namespace Engine
