#include "GraphicsCommandBuffer.h"

#include "Framework/component/RenderComponent/MeshComponent.h"

#include "Render/AttachmentUtilsFunc.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/Memory/Buffer.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/Camera.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/DebugUtils.h"
#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {
    GraphicsCommandBuffer::GraphicsCommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight) :
        TransferCommandBuffer(system, cb), m_inflight_frame_index(frame_in_flight) {
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
        MaterialInstance &material,
        const std::string & tag,
        HomogeneousMesh::MeshVertexType type
    ) {
        auto tpl = material.GetLibrary()->FindMaterialTemplate(tag, type);
        const auto &pipeline = tpl->GetPipeline();
        const auto &pipeline_layout = tpl->GetPipelineLayout();

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

        const auto &global_pool = m_system.GetGlobalConstantDescriptorPool();
        const auto &per_scene_descriptor_set = global_pool.GetPerSceneConstantSet(m_inflight_frame_index);
        const auto &per_camera_descriptor_set = global_pool.GetPerCameraConstantSet(m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptor(tpl, m_inflight_frame_index);

        if (material_descriptor_set) {
            cb.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipeline_layout,
                0,
                {per_scene_descriptor_set, per_camera_descriptor_set, material_descriptor_set},
                {static_cast<uint32_t>(
                    global_pool.GetPerCameraDynamicOffset(m_inflight_frame_index, m_system.GetActiveCameraId())
                )}
            );
        } else {
            cb.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                pipeline_layout,
                0,
                {per_scene_descriptor_set, per_camera_descriptor_set},
                {static_cast<uint32_t>(
                    global_pool.GetPerCameraDynamicOffset(m_inflight_frame_index, m_system.GetActiveCameraId())
                )}
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

    void GraphicsCommandBuffer::DrawMesh(const HomogeneousMesh &mesh, const glm::mat4 &model_matrix) {
        auto bindings = mesh.GetVertexBufferInfo();
        std::vector<vk::Buffer> vertex_buffers{bindings.second.size(), bindings.first};
        cb.bindVertexBuffers(0, vertex_buffers, bindings.second);
        auto indices = mesh.GetIndexBufferInfo();
        cb.bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        cb.pushConstants(
            m_bound_material_pipeline.value().second,
            vk::ShaderStageFlagBits::eVertex,
            0,
            ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
            reinterpret_cast<const void *>(&model_matrix)
        );
        cb.drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void GraphicsCommandBuffer::DrawRenderers(const std::string & tag, const RendererList &renderers) {
        auto camera = m_system.GetActiveCamera().lock();
        assert(camera);
        this->DrawRenderers(tag,
            renderers, camera->GetViewMatrix(), camera->GetProjectionMatrix(), m_system.GetSwapchain().GetExtent()
        );
    }

    void GraphicsCommandBuffer::DrawRenderers(
        const std::string & tag,
        const RendererList &renderers,
        const glm::mat4 &view_matrix,
        const glm::mat4 &projection_matrix,
        vk::Extent2D extent
    ) {
        // Write camera transforms
        auto camera_ptr = m_system.GetGlobalConstantDescriptorPool().GetPerCameraConstantMemory(
            m_system.GetFrameManager().GetFrameInFlight(), m_system.GetActiveCameraId()
        );
        ConstantData::PerCameraStruct camera_struct{view_matrix, projection_matrix};
        std::memcpy(camera_ptr, &camera_struct, sizeof camera_struct);

        vk::Rect2D scissor{{0, 0}, extent};
        this->SetupViewport(extent.width, extent.height, scissor);
        for (const auto &rid : renderers) {
            const auto &component = m_system.GetRendererManager().GetRendererData(rid);
            glm::mat4 model_matrix = component->GetWorldTransform().GetTransformMatrix();

            const auto &materials = component->GetMaterials();

            if (auto mesh_ptr = dynamic_cast<const MeshComponent *>(component)) {
                const auto &meshes = mesh_ptr->GetSubmeshes();

                assert(materials.size() == meshes.size());
                for (size_t id = 0; id < materials.size(); id++) {
                    this->BindMaterial(*materials[id], tag, HomogeneousMesh::MeshVertexType::Basic);
                    this->DrawMesh(*meshes[id], model_matrix);
                }
            }
        }
    }

    void GraphicsCommandBuffer::EndRendering() {
        cb.endRendering();
        DEBUG_CMD_END_LABEL(cb);
    }

    void GraphicsCommandBuffer::DrawMesh(const HomogeneousMesh &mesh) {
        this->DrawMesh(mesh, glm::mat4{1.0f});
    }

    void GraphicsCommandBuffer::Reset() noexcept {
        cb.reset();
        m_bound_material_pipeline.reset();
    }
} // namespace Engine
