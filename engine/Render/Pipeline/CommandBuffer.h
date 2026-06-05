#ifndef PIPELINE_COMMANDBUFFER_COMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMMANDBUFFER_INCLUDED

#include "Render/Pipeline/PipelineRuntimeInfo.h"
#include "Render/RenderSystem/RendererManager.h"

#include <optional>
#include <vulkan/vulkan.hpp>

// GLM forward declaration.
#include <fwd.hpp>

namespace Engine {
    class RenderSystem;
    class ComputeStage;
    class ComputeResourceBinding;
    class MaterialTemplate;
    class MaterialInstance;
    class DeviceBuffer;
    class VertexAttribute;
    class IVertexBasedRenderer;
    class Texture;

    namespace RenderSystemState {
        class SceneDataManager;
        class CameraManager;
    }; // namespace RenderSystemState

    namespace AttachmentUtils {
        class AttachmentDescription;
    };

    /**
     * @brief Unified command buffer wrapper for recording Vulkan commands.
     *
     * Non-owning wrapper around a raw vk::CommandBuffer. Provides utility
     * methods for graphics, compute, and transfer operations.
     *
     * The command buffer lifecycle (begin/end) is managed by RenderGraph,
     * not by this class.
     */
    class CommandBuffer {
    public:
        /**
         * @brief An area of a texture, used for operations such as blitting.
         */
        struct TextureArea {
            uint32_t mip_level;
            uint32_t array_layer_base;
            uint32_t array_layer_count;
            int32_t x0, y0, z0, x1, y1, z1;
        };

        CommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight);

        CommandBuffer(const CommandBuffer &) = delete;
        CommandBuffer(CommandBuffer &&) = default;
        CommandBuffer &operator=(const CommandBuffer &) = delete;
        CommandBuffer &operator=(CommandBuffer &&) = default;

        /// @brief Get the raw Vulkan command buffer handle.
        vk::CommandBuffer GetCommandBuffer() const noexcept {
            return cb;
        }

        // ── Transfer operations ──────────────────────────────────────────

        void BlitColorImage(const Texture &src, const Texture &dst);
        void BlitColorImage(const Texture &src, const Texture &dst, TextureArea src_area, TextureArea dst_area);

        // ── Render pass ──────────────────────────────────────────────────

        void BeginRendering(
            const AttachmentUtils::AttachmentDescription &color,
            const AttachmentUtils::AttachmentDescription &depth,
            vk::Extent2D extent,
            const std::string &name = ""
        );
        void BeginRendering(
            const std::vector<AttachmentUtils::AttachmentDescription> &colors,
            const AttachmentUtils::AttachmentDescription depth,
            vk::Extent2D extent,
            const std::string &name = ""
        );
        void EndRendering();

        // ── Pipeline runtime info ────────────────────────────────────────

        void SetRenderingInfo(PipelineRuntimeInfoPerRendering pripr) noexcept {
            m_pripr = pripr;
        }
        const PipelineRuntimeInfoPerRendering &GetRenderingInfo() const noexcept {
            return m_pripr;
        }

        // ── Scene / Camera / Material binding ────────────────────────────

        void BindSceneResources(const RenderSystemState::SceneDataManager &);
        void BindCameraResources(const RenderSystemState::CameraManager &);
        void BindMaterial(MaterialInstance &inst, MaterialTemplate &tpl);

        // ── Viewport ─────────────────────────────────────────────────────

        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        // ── Drawing ──────────────────────────────────────────────────────

        void DrawMesh(const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix, int32_t camera_index);
        void DrawMesh(const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix);
        void DrawMesh(const IVertexBasedRenderer &mesh);

        void DrawRenderers(const std::string &tag, const RendererList &renderers);
        void DrawRenderers(
            const std::string &tag, const RendererList &renderers, int32_t camera_index, vk::Extent2D extent
        );

        // ── Compute ──────────────────────────────────────────────────────

        void BindComputeStage(ComputeStage &stage);
        void BindComputeResource(ComputeResourceBinding &binding);
        void DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

        // ── Reset ────────────────────────────────────────────────────────

        void Reset() noexcept;

    private:
        RenderSystem &m_system;
        vk::CommandBuffer cb;
        uint32_t m_inflight_frame_index;

        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline{};
        std::optional<std::reference_wrapper<ComputeStage>> m_bound_compute_stage{std::nullopt};

        PipelineRuntimeInfoPerRendering m_pripr{};
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_COMMANDBUFFER_INCLUDED
