#ifndef ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
#define ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED

#include <cstdint>
#include <variant>

#include "Render/Memory/TextureSubresourceView.h"

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
        struct ColorClearValue {
            float r{0.0f}, g{0.0f}, b{0.0f}, a{1.0f};
        };

        /// @brief Clear value for depth-stencil aspects.
        struct DepthClearValue {
            float depth{1.0f};
            uint32_t stencil{0U};
        };

        typedef std::variant<ColorClearValue, DepthClearValue> ClearValue;

        /**
         * @brief Description of an attachment during rendering.
         */
        struct AttachmentDescription {
            /// @brief Which RTT to be written to.
            RenderTargetTexture *texture{nullptr};
            /// @brief What subresources are used by the rendering pass.
            TextureSubresourceRange range{};
            /// @brief Load operation of the attachment.
            LoadOperation load_op{};
            /// @brief Store operation of the attachment.
            StoreOperation store_op{};
            /// @brief Clear value of the attachment.
            /// Only used if load operation is clear.
            ClearValue clear_value{};
        };
    } // namespace AttachmentUtils
} // namespace Engine

#endif // ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
