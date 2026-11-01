#ifndef ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
#define ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED

#include "Render/render_export.h"
#include <cstdint>
#include <variant>

#include "Rhi/Pipeline/PipelineDescription.h"
#include "Rhi/Texture/TextureSubresourceView.h"

namespace Engine {
    class RenderTargetTexture;
    class SlicedTextureView;

    /**
     * @brief Utility classes for attachment manipulation.
     */
    namespace AttachmentUtils {
        /**
         * @brief How the attachment is loaded.
         * @see https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentLoadOp.html
         */
        enum class LoadOperation {
            Load,
            Clear,
            DontCare
        };

        /**
         * @brief How the attachment is stored.
         * @see https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html
         */
        enum class StoreOperation {
            Store,
            DontCare
        };

        /// @brief Clear value for color aspects.
        struct RENDER_API ColorClearValue {
            float r{0.0f}, g{0.0f}, b{0.0f}, a{1.0f};
        };

        /// @brief Clear value for depth-stencil aspects.
        struct RENDER_API DepthClearValue {
            float depth{1.0f};
            uint32_t stencil{0U};
        };

        typedef std::variant<ColorClearValue, DepthClearValue> ClearValue;

        /**
         * @brief Description of an attachment during rendering.
         */
        struct RENDER_API AttachmentDescription {
            /// @brief Which RTT to be written to.
            RenderTargetTexture *texture{nullptr};
            /// @brief What subresources are used by the rendering pass.
            Rhi::TextureSubresourceRange range{};
            /// @brief Load operation of the attachment.
            LoadOperation load_op{};
            /// @brief Store operation of the attachment.
            StoreOperation store_op{};
            /// @brief Clear value of the attachment.
            /// Only used if load operation is clear.
            ClearValue clear_value{};
        };

        // ── The same vocabulary in the Rhi seam's terms ──────────────────────
        //
        // An attachment is described here in the render layer's own enums and
        // passed to the seam as Rhi's. The two sets mean the same things, so the
        // conversion is a table and belongs next to the definitions rather than
        // repeated wherever an attachment crosses into the seam.

        /// @brief The seam's load operation for this one.
        inline Rhi::LoadOperation ToRhiLoadOperation(LoadOperation operation) noexcept {
            switch (operation) {
            case LoadOperation::Load:
                return Rhi::LoadOperation::Load;
            case LoadOperation::Clear:
                return Rhi::LoadOperation::Clear;
            case LoadOperation::DontCare:
            default:
                return Rhi::LoadOperation::DontCare;
            }
        }

        /// @brief The seam's store operation for this one.
        inline Rhi::StoreOperation ToRhiStoreOperation(StoreOperation operation) noexcept {
            return operation == StoreOperation::Store ? Rhi::StoreOperation::Store : Rhi::StoreOperation::DontCare;
        }

        /// @brief The colour a cleared attachment is cleared to.
        inline Rhi::ClearColor ToRhiClearColor(const ClearValue &value) noexcept {
            if (const auto *color = std::get_if<ColorClearValue>(&value)) {
                return Rhi::ClearColor{color->r, color->g, color->b, color->a};
            }
            return Rhi::ClearColor{};
        }

        /// @brief The depth a cleared attachment is cleared to.
        inline float ToRhiClearDepth(const ClearValue &value) noexcept {
            if (const auto *depth = std::get_if<DepthClearValue>(&value)) {
                return depth->depth;
            }
            return 1.0f;
        }
    } // namespace AttachmentUtils
} // namespace Engine

#endif // ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED

