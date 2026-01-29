#include "GraphicsCommandBuffer.h"

#include "Framework/component/RenderComponent/MeshComponent.h"

#include "Render/AttachmentUtilsFunc.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/Renderer/Camera.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Renderer/VertexAttribute.h"

#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {
    GraphicsCommandBuffer::GraphicsCommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight) :
        TransferCommandBuffer(cb), m_system(system), m_inflight_frame_index(frame_in_flight) {
    }

    void GraphicsCommandBuffer::BeginRendering(
        const AttachmentUtils::AttachmentDescription &color,
        const AttachmentUtils::AttachmentDescription &depth,
        vk::Extent2D extent,
        const std::string &name
    ) {
        DEBUG_CMD_START_LABEL(cb, name.c_str());
        std::vector<vk::RenderingAttachmentInfo> color_attachment;

        if (color.texture) {
            color_attachment.push_back(GetVkAttachmentInfo(color, vk::ImageLayout::eColorAttachmentOptimal));
        }

        vk::RenderingAttachmentInfo depth_attachment;
        if (depth.texture) {
            depth_attachment = vk::RenderingAttachmentInfo{
                GetVkAttachmentInfo(depth, vk::ImageLayout::eDepthStencilAttachmentOptimal)
            };
        } else {
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
        // Begin rendering after transit
        cb.beginRendering(info);
    }

    void GraphicsCommandBuffer::BeginRendering(
        const std::vector<AttachmentUtils::AttachmentDescription> &colors,
        const AttachmentUtils::AttachmentDescription depth,
        vk::Extent2D extent,
        const std::string &name
    ) {
        DEBUG_CMD_START_LABEL(cb, name.c_str());

        std::vector<vk::RenderingAttachmentInfo> color_attachment_info(colors.size(), vk::RenderingAttachmentInfo{});
        for (size_t i = 0; i < colors.size(); i++) {
            color_attachment_info[i] = GetVkAttachmentInfo(colors[i], vk::ImageLayout::eColorAttachmentOptimal);
        }

        vk::RenderingAttachmentInfo depth_attachment_info{};
        if (depth.texture) {
            depth_attachment_info = GetVkAttachmentInfo(depth, vk::ImageLayout::eDepthStencilAttachmentOptimal);
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
        cb.beginRendering(info);
    }

    void GraphicsCommandBuffer::BindMaterial(
        MaterialInstance & material,
        MaterialTemplate & tpl
    ) {
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

        material.UpdateGPUInfo(tpl, m_inflight_frame_index);

        const auto &per_scene_descriptor_set = m_system.GetSceneDataManager().GetLightDescriptorSet(m_inflight_frame_index);
        const auto &per_camera_descriptor_set = m_system.GetCameraManager().GetDescriptorSet(m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptor(tpl, m_inflight_frame_index);

        if (material_descriptor_set) {
            cb.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipeline_layout,
                0,
                {per_scene_descriptor_set, per_camera_descriptor_set, material_descriptor_set},
                {}
            );
        } else {
            cb.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipeline_layout,
                0,
                {per_scene_descriptor_set, per_camera_descriptor_set},
                {}
            );
        }
    }

    void GraphicsCommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        cb.setViewport(0, 1, &vp);
        cb.setScissor(0, 1, &scissor);
    }

    void GraphicsCommandBuffer::DrawMesh(const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix) {
        this->DrawMesh(mesh, model_matrix, m_system.GetCameraManager().GetActiveCameraIndex());
    }

    void GraphicsCommandBuffer::DrawMesh(
        const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix, int32_t camera_index
    ) {
        auto bindings = mesh.GetVertexAttributeBufferBindings();
        std::vector <vk::DeviceSize> offsets{};
        std::vector <vk::Buffer> buffers{};
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
        } push_constants {.m = model_matrix, .i = camera_index};

        cb.pushConstants(
            m_bound_material_pipeline.value().second,
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof (push_constants),
            reinterpret_cast<const void *>(&push_constants)
        );
        cb.drawIndexed(mesh.GetIndexCount(), 1, 0, 0, 0);
    }

    void GraphicsCommandBuffer::DrawRenderers(const std::string & tag, const RendererList &renderers) {
        this->DrawRenderers(
            tag,
            renderers,
            m_system.GetCameraManager().GetActiveCameraIndex(),
            m_system.GetSwapchain().GetExtent()
        );
    }

    void GraphicsCommandBuffer::DrawRenderers(
        const std::string & tag,
        const RendererList &renderers,
        int32_t camera_index,
        vk::Extent2D extent
    ) {
        vk::Rect2D scissor{{0, 0}, extent};
        this->SetupViewport(extent.width, extent.height, scissor);
        for (const auto &rid : renderers) {
            const auto & mesh = m_system.GetRendererManager().GetRendererData(rid);
            glm::mat4 model_matrix = m_system.GetRendererManager().GetRendererComponent(rid)->GetWorldTransform().GetTransformMatrix();
            auto material_instance = m_system.GetRendererManager().GetMaterialInstance(rid);

            auto tpl = material_instance->GetLibrary().FindMaterialTemplate(tag, mesh->GetVertexAttributeFormat());
            if (!tpl)   continue;

            this->BindMaterial(*material_instance, *tpl);
            this->DrawMesh(*mesh, model_matrix, camera_index);
        }
    }

    void GraphicsCommandBuffer::EndRendering() {
        cb.endRendering();
        DEBUG_CMD_END_LABEL(cb);
    }

    void GraphicsCommandBuffer::DrawMesh(const IVertexBasedRenderer &mesh) {
        this->DrawMesh(mesh, glm::mat4{1.0f});
    }

    void GraphicsCommandBuffer::Reset() noexcept {
        cb.reset();
        m_bound_material_pipeline.reset();
    }
} // namespace Engine
