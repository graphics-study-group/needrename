#ifndef RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED

#include "Render/Resource/RenderResourceManager.h"

#include <Framework/world/Handle.h>
#include <glm.hpp>
#include <memory>
#include <vector>

namespace vk {
    class PushConstantRange;
}

namespace Engine {
    class AssetRef;
    class IVertexBasedRenderer;
    class RenderSystem;

    namespace RenderSystemState {
        /**
         * @brief Runtime draw-entry registry for mesh/material rendering.
         *
         * Top-level design:
         * - This class owns draw entries addressed by RendererHandle.
         * - One draw entry corresponds to one submesh draw unit, bound with:
         *   a mesh resource handle, a material resource handle, and a renderer view.
         * - Asset/GPU resource lifetime is delegated to RenderResourceManager;
         *   this class only retains/release resource handles per entry.
         * - Destruction is deferred by frame-in-flight policy to avoid releasing
         *   resources still referenced by in-flight command buffers.
         *
         * "Caller" in this interface means upper-layer runtime owners of render
         * entries (for example renderer-related components/systems) that:
         * 1) create entries via RegisterRenderer,
         * 2) update per-frame data (model matrix),
         * 3) explicitly Unregister when entries are no longer needed.
         *
         * This class intentionally stores no Component pointers/references and
         * acts as a pure runtime data/service layer.
         */
        class RendererManager {
            RenderSystem &m_system;
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            /**
             * @brief A small struct holding all renderer data pushed to the GPU.
             */
            struct RendererDataStruct {
                glm::mat4 model_matrix;
                int32_t camera_index;
            };

            /**
             * @brief Filter options applied in FilterAndSortRenderers.
             */
            struct FilterCriteria {
                enum class BinaryCriterion : uint8_t {
                    No,
                    Yes,
                    DontCare
                };

                BinaryCriterion is_shadow_caster{BinaryCriterion::DontCare};
                uint32_t layer{0xFFFFFFFF};
            };

            /**
             * @brief Sorting modes for filtered renderers.
             *
             * Only None is currently implemented.
             */
            enum class SortingCriterion {
                None,
                ByPriority,
                ByMaterial,
                ByDistanceToActiveCamera
            };

            using RendererHandle = uint32_t;
            using RendererList = std::vector<RendererHandle>;

            RendererManager(RenderSystem &system);
            ~RendererManager();

            /**
             * @brief Register one submesh draw entry and return its handle.
             *
             * Behavior:
             * - Acquires mesh/material render resources from RenderResourceManager.
             * - Builds a renderer view for the specified submesh_index.
             * - Stores layer/shadow flags and initial model matrix.
             *
             * Ownership contract:
             * - Caller owns the returned handle usage lifecycle.
             * - Caller must eventually call Unregister(handle).
             *
             * @param mesh_asset_ref Mesh asset reference.
             * @param material_asset_ref Material asset reference.
             * @param submesh_index Submesh index inside mesh asset.
             * @param layer Bitmask layer for filtering.
             * @param cast_shadow Whether this entry participates in shadow passes.
             * @param eagerly_loaded True for synchronous resource path, false for async-friendly path.
             * @return RendererHandle for subsequent update/filter/draw operations.
             */
            RendererHandle RegisterRenderer(
                AssetRef mesh_asset_ref,
                AssetRef material_asset_ref,
                uint32_t submesh_index,
                uint32_t layer,
                bool cast_shadow,
                bool eagerly_loaded
            );

            /**
             * @brief Mark a renderer for deferred deallocation.
             *
             * The entry is not removed immediately. Actual cleanup happens in
             * PerformPendingCleanUp after frame countdown reaches zero.
             */
            void Unregister(RendererHandle handle);

            /**
             * @brief Update the model matrix for a renderer.
             *
             * Caller should update this per frame before issuing draw submission.
             */
            void UpdateModelMatrix(RendererHandle handle, const glm::mat4 &matrix);

            /**
             * @brief Advance deferred cleanup and release fully retired entries.
             *
             * For each retired entry whose countdown reaches zero, this method
             * releases mesh/material resource handles and removes the entry.
             */
            void PerformPendingCleanUp();

            /**
             * @brief Build draw list by filtering (and optional sorting).
             *
             * Current behavior:
             * - skips retired entries,
             * - applies layer and shadow-caster criteria,
             * - checks mesh resource readiness through RenderResourceManager.
             *
             * @note Sorting modes other than None are currently unimplemented.
             */
            RendererList FilterAndSortRenderers(FilterCriteria fc, SortingCriterion sc = SortingCriterion::None);

            /**
             * @brief Get renderer geometry view for a draw entry.
             * @return Non-owning pointer valid while the entry is alive.
             */
            const IVertexBasedRenderer *GetRenderer(RendererHandle handle) const noexcept;

            /**
             * @brief Get material resource handle bound to a draw entry.
             */
            RenderResourceHandle GetMaterialResourceHandle(RendererHandle handle) const noexcept;

            /**
             * @brief Get the cached model matrix for a renderer.
             */
            const glm::mat4 &GetModelMatrix(RendererHandle handle) const noexcept;

            /**
             * @brief Return push-constant range definition for draw data.
             */
            static vk::PushConstantRange GetPushConstantRange();
        };
    } // namespace RenderSystemState

    using RendererHandle = RenderSystemState::RendererManager::RendererHandle;
    using RendererList = RenderSystemState::RendererManager::RendererList;
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_RENDERERMANAGER_INCLUDED
