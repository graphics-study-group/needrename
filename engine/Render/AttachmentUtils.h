#ifndef ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
#define ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED

#include <variant>

namespace Engine {
    class Texture;
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
            float r, g, b, a;
        };

        struct DepthClearValue {
            float depth;
            uint32_t stencil;
        };

        typedef std::variant<ColorClearValue, DepthClearValue> ClearValue;

        struct AttachmentOp {
            LoadOperation load_op {};
            StoreOperation store_op {};
            ClearValue clear_value {};
        };

        struct AttachmentDescription {
            LoadOperation load_op {};
            StoreOperation store_op {};

            const Texture * texture {nullptr};
            const SlicedTextureView * texture_view {nullptr};
        };
    }
}

#endif // ENGINE_RENDER_ATTACHMENTUTILS_INCLUDED
