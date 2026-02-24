#ifndef ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
#define ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED

#include <cstdint>
#include <variant>

#include "Render/Memory/TextureSubresourceView.h"

namespace Engine {
    class RenderTargetTexture;
    class SlicedTextureView;

    namespace AttachmentUtils {
        enum class LoadOperation {
            Load,
            Clear,
            DontCare
        };

        enum class StoreOperation {
            Store,
            DontCare
        };

        struct ColorClearValue {
            float r{0.0f}, g{0.0f}, b{0.0f}, a{1.0f};
        };

        struct DepthClearValue {
            float depth{1.0f};
            uint32_t stencil{0U};
        };

        typedef std::variant<ColorClearValue, DepthClearValue> ClearValue;

        struct AttachmentDescription {
            RenderTargetTexture *texture{nullptr};
            TextureSubresourceRange range{};

            LoadOperation load_op{};
            StoreOperation store_op{};
            ClearValue clear_value{};
        };
    } // namespace AttachmentUtils
} // namespace Engine

#endif // ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
