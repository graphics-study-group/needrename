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
         * @brief A sub-region of a texture, used for specifying source and
         * destination areas in blit operations.
         *
         * @note The coordinates (x0,y0,z0) to (x1,y1,z1) define an inclusive
         * region in 3D pixel space. Asserted that x0 < x1, y0 < y1, z0 < z1.
         */
        struct TextureArea {
            uint32_t mip_level;         ///< Mip level to operate on.
            uint32_t array_layer_base;  ///< First array layer.
            uint32_t array_layer_count; ///< Number of array layers.
            int32_t x0, y0, z0;         ///< Lower-bound pixel coordinate (inclusive).
            int32_t x1, y1, z1;         ///< Upper-bound pixel coordinate (inclusive).
        };

        /**
         * @brief Construct a CommandBuffer wrapping a raw Vulkan command buffer.
         *
         * @param system         Reference to the RenderSystem, used to access
         *                       camera manager, scene data, swapchain, and
         *                       resource managers from binding/drawing methods.
         * @param cb             The raw Vulkan command buffer handle (non-owning).
         * @param frame_in_flight Frame-in-flight index (0..FRAMES_IN_FLIGHT-1),
         *                        used to select the correct per-frame descriptor set.
         */
        CommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight);

        CommandBuffer(const CommandBuffer &) = delete;
        CommandBuffer(CommandBuffer &&) = default;
        CommandBuffer &operator=(const CommandBuffer &) = delete;
        CommandBuffer &operator=(CommandBuffer &&) = default;

        /**
         * @brief Get the raw Vulkan command buffer handle.
         *
         * @return The wrapped vk::CommandBuffer (non-owning).
         */
        vk::CommandBuffer GetCommandBuffer() const noexcept {
            return cb;
        }

        // ── Transfer operations ──────────────────────────────────────────

        /**
         * @brief Blit an entire color texture from source to destination using
         * linear filtering.
         *
         * Transfers the full mip 0, all array layers, and the full pixel extent
         * of the source texture to the destination texture. Internally delegates
         * to the sub-region overload.
         *
         * @param src Source texture (expected in eTransferSrcOptimal layout).
         * @param dst Destination texture (expected in eTransferDstOptimal layout).
         */
        void BlitColorImage(const Texture &src, const Texture &dst);

        /**
         * @brief Blit a specified sub-region from one color texture to another
         * using vkCmdBlitImage2 with linear filtering.
         *
         * Both textures must be in the appropriate transfer layouts
         * (eTransferSrcOptimal for src, eTransferDstOptimal for dst).
         *
         * @param src      Source texture.
         * @param dst      Destination texture.
         * @param src_area Sub-region of the source texture to read from.
         * @param dst_area Sub-region of the destination texture to write to.
         */
        void BlitColorImage(const Texture &src, const Texture &dst, TextureArea src_area, TextureArea dst_area);

        // ── Render pass ──────────────────────────────────────────────────

        /**
         * @brief Begin a dynamic rendering pass with a single color attachment
         * and an optional depth/stencil attachment.
         *
         * Records vkCmdBeginRendering with the given color and depth
         * attachments. Updates the internal PipelineRuntimeInfoPerRendering
         * with the attachment formats for subsequent pipeline lookup.
         *
         * @param color  Color attachment description (texture, load/store ops,
         *               clear value). May have a null texture to skip.
         * @param depth  Depth/stencil attachment description. May have a null
         *               texture to skip.
         * @param extent Render area extent in pixels.
         * @param name   Optional debug label inserted via
         *               vkCmdBeginDebugUtilsLabelEXT / vkCmdEndDebugUtilsLabelEXT
         *               bracketing the render pass.
         */
        void BeginRendering(
            const AttachmentUtils::AttachmentDescription &color,
            const AttachmentUtils::AttachmentDescription &depth,
            vk::Extent2D extent,
            const std::string &name = ""
        );

        /**
         * @brief Begin a dynamic rendering pass with multiple color attachments
         * (MRT) and an optional depth/stencil attachment.
         *
         * Supports up to 8 color attachments. Each provided attachment updates
         * the internal PipelineRuntimeInfoPerRendering format array.
         *
         * @param colors Vector of color attachment descriptions (up to 8).
         * @param depth  Depth/stencil attachment description.
         * @param extent Render area extent in pixels.
         * @param name   Optional debug label.
         */
        void BeginRendering(
            const std::vector<AttachmentUtils::AttachmentDescription> &colors,
            const AttachmentUtils::AttachmentDescription depth,
            vk::Extent2D extent,
            const std::string &name = ""
        );

        /**
         * @brief End the current dynamic rendering pass.
         *
         * Records vkCmdEndRendering, closes the debug label region (if any),
         * and resets the internal PipelineRuntimeInfoPerRendering to defaults.
         */
        void EndRendering();

        // ── Pipeline runtime info ────────────────────────────────────────

        /**
         * @brief Override the per-rendering pipeline runtime info.
         *
         * This is normally set automatically by BeginRendering. Use this
         * to manually supply attachment format state when bypassing the
         * BeginRendering/EndRendering helpers.
         *
         * @param pripr The new PipelineRuntimeInfoPerRendering value.
         */
        void SetRenderingInfo(PipelineRuntimeInfoPerRendering pripr) noexcept {
            m_pripr = pripr;
        }

        /**
         * @brief Get the current per-rendering pipeline runtime info.
         *
         * Contains color/depth attachment formats and sample count used for
         * pipeline lookup/dispatch.
         *
         * @return Const reference to the current PipelineRuntimeInfoPerRendering.
         */
        const PipelineRuntimeInfoPerRendering &GetRenderingInfo() const noexcept {
            return m_pripr;
        }

        // ── Scene / Camera / Material binding ────────────────────────────

        /**
         * @brief Bind descriptor set 0 (per-scene data) to the graphics pipeline.
         *
         * Set 0 contains scene-level uniforms: lights, ambient/environment
         * data. Uses the common pipeline layout shared with
         * CameraManager::GetCommonPipelineLayout().
         *
         * @param sdm SceneDataManager providing the light descriptor set for
         *            the current frame-in-flight.
         */
        void BindSceneResources(const RenderSystemState::SceneDataManager &);

        /**
         * @brief Bind descriptor set 1 (per-camera data) to the graphics pipeline.
         *
         * Set 1 contains camera-level uniforms: view matrix, projection
         * matrix. Uses the common pipeline layout shared with
         * SceneDataManager::GetCommonPipelineLayout().
         *
         * @param cm CameraManager providing the descriptor set for the
         *           current frame-in-flight.
         */
        void BindCameraResources(const RenderSystemState::CameraManager &);

        /**
         * @brief Bind a material's graphics pipeline and descriptor set 2.
         *
         * Binds the Vulkan pipeline only when it differs from the currently
         * bound pipeline (set 2 is always re-bound). Uploads material UBO
         * data via MaterialInstance::UpdateGPUInfo and binds the descriptor
         * set with dynamic uniform buffer offsets.
         *
         * If the material template has no material data
         * (MaterialTemplate::HasMaterialData returns false), only the
         * pipeline is bound.
         *
         * @param inst Material instance (mutable per-instance UBO values,
         *             textures).
         * @param tpl  Material template (immutable pipeline, pipeline layout,
         *             descriptor set layout, descriptor pool).
         */
        void BindMaterial(MaterialInstance &inst, MaterialTemplate &tpl);

        // ── Viewport ─────────────────────────────────────────────────────

        /**
         * @brief Set the viewport and scissor rectangle.
         *
         * Viewport uses a y-down convention with origin at (0,0) and depth
         * range [0, 1].
         *
         * @param vpWidth  Viewport width in pixels.
         * @param vpHeight Viewport height in pixels.
         * @param scissor  Scissor rectangle (origin and extent).
         */
        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        // ── Drawing ──────────────────────────────────────────────────────

        /**
         * @brief Draw a mesh with the given model matrix and camera index.
         *
         * Binds vertex buffers, index buffer, pushes model matrix + camera
         * index as push constants, and issues vkCmdDrawIndexed. The material
         * must already be bound via BindMaterial.
         *
         * @param mesh         The mesh to draw (provides vertex/index buffers
         *                     and vertex attribute layout).
         * @param model_matrix World-space model transform.
         * @param camera_index Camera index passed to the shader via push
         *                     constants.
         */
        void DrawMesh(const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix, int32_t camera_index);

        /**
         * @brief Draw a mesh using the active camera.
         *
         * Equivalent to DrawMesh(mesh, model_matrix,
         * CameraManager::GetActiveCameraIndex()).
         *
         * @param mesh         The mesh to draw.
         * @param model_matrix World-space model transform.
         */
        void DrawMesh(const IVertexBasedRenderer &mesh, const glm::mat4 &model_matrix);

        /**
         * @brief Draw a mesh with an identity model matrix using the active camera.
         *
         * Equivalent to DrawMesh(mesh, glm::mat4{1.0f}).
         *
         * @param mesh The mesh to draw.
         */
        void DrawMesh(const IVertexBasedRenderer &mesh);

        /**
         * @brief Draw all renderers in a RendererList using the active camera
         * and the current swapchain extent.
         *
         * This is a convenience overload that binds scene and camera
         * resources, sets up viewport/scissor from the swapchain extent, then
         * iterates over each renderer handle: resolving mesh + material,
         * finding the matching MaterialTemplate by tag, binding the material,
         * and issuing the draw call.
         *
         * @param tag       Material tag used to look up the MaterialTemplate
         *                  via MaterialLibrary::FindMaterialTemplate.
         * @param renderers List of renderer handles to draw.
         */
        void DrawRenderers(const std::string &tag, const RendererList &renderers);

        /**
         * @brief Draw all renderers with an explicit camera index and
         * render extent.
         *
         * Same iteration behavior as the convenience overload, but accepts
         * explicit camera and extent parameters for offscreen or multi-camera
         * rendering.
         *
         * @param tag          Material tag for template lookup.
         * @param renderers    List of renderer handles to draw.
         * @param camera_index Camera index passed to the shader via push
         *                     constants.
         * @param extent       Render area extent for viewport and scissor.
         */
        void DrawRenderers(
            const std::string &tag, const RendererList &renderers, int32_t camera_index, vk::Extent2D extent
        );

        // ── Compute ──────────────────────────────────────────────────────

        /**
         * @brief Bind a compute shader pipeline for subsequent dispatch.
         *
         * Records the pipeline binding and stores the ComputeStage reference
         * so that BindComputeResource and DispatchCompute can use it.
         *
         * @param stage ComputeStage owning the compute pipeline, pipeline
         *              layout, and descriptor set layout.
         */
        void BindComputeStage(ComputeStage &stage);

        /**
         * @brief Bind the descriptor set and upload UBO data for the
         * currently bound compute stage.
         *
         * Must be called after BindComputeStage. Calls
         * ComputeResourceBinding::UpdateGPUInfo to write UBO data, then binds
         * descriptor set 0 with dynamic offsets.
         *
         * @param binding ComputeResourceBinding owning the UBO data, texture
         *                bindings, and descriptor set.
         */
        void BindComputeResource(ComputeResourceBinding &binding);

        /**
         * @brief Dispatch workgroups for the currently bound compute pipeline.
         *
         * Issues vkCmdDispatch. Must be called after BindComputeStage and
         * BindComputeResource.
         *
         * @param groupCountX Number of local workgroups in X.
         * @param groupCountY Number of local workgroups in Y.
         * @param groupCountZ Number of local workgroups in Z.
         */
        void DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

        // ── Reset ────────────────────────────────────────────────────────

        /**
         * @brief Reset the Vulkan command buffer and clear all tracked
         * pipeline and stage state.
         *
         * Calls vkResetCommandBuffer, then resets the cached bound material
         * pipeline (m_bound_material_pipeline) and compute stage
         * (m_bound_compute_stage) so the next BindMaterial/BindComputeStage
         * will re-bind unconditionally. Called by FrameManager at the start
         * of each frame.
         */
        void Reset() noexcept;

    private:
        RenderSystem &m_system;          ///< Service locator for all rendering subsystems.
        vk::CommandBuffer cb;            ///< Raw Vulkan command buffer handle (non-owning).
        uint32_t m_inflight_frame_index; ///< Current frame-in-flight index for per-frame descriptor set selection.

        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>>
            m_bound_material_pipeline{}; ///< Cached bound material pipeline + layout to skip redundant binds.
        std::optional<std::reference_wrapper<ComputeStage>> m_bound_compute_stage{
            std::nullopt
        }; ///< Currently bound compute stage for BindComputeResource / DispatchCompute.

        PipelineRuntimeInfoPerRendering m_pripr{}; ///< Current render pass attachment formats for pipeline lookup.
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_COMMANDBUFFER_INCLUDED
