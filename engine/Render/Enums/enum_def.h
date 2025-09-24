/**
 * Definitions for scoped enumerations for rendering.
 * Only those which requires in serialization are defined
 * here.
 */

#ifndef RENDER_ENUMS_ENUM_DEF_INCLUDED
#define RENDER_ENUMS_ENUM_DEF_INCLUDED

#ifndef DEFINE_ENUM_ITEM
#define DEFINE_ENUM_ITEM(e, x) x,
#endif

namespace Engine {
    namespace _enums {
        enum class ImageFormat {
            #include "defs/ImageFormat.def"
        };

        enum class FillingMode {
            #include "defs/FillingMode.def"
        };

        enum class CullingMode {
            #include "defs/CullingMode.def"
        };

        enum class FrontFace {
            #include "defs/FrontFace.def"
        };

        enum class Comparator {
            #include "defs/Comparator.def"
        };

        enum class StencilOperation {
            #include "defs/StencilOperation.def"
        };

        enum class BlendOperation {
            #include "defs/BlendOperation.def"
        };

        enum class BlendFactor {
            #include "defs/BlendFactor.def"
        };

        enum class AddressMode : uint8_t {
            #include "defs/AddressMode.def"
        };

        enum class FilterMode : uint8_t {
            #include "defs/FilterMode.def"
        };
    }
}

#undef DEFINE_ENUM_ITEM

#endif // RENDER_ENUMS_ENUM_DEF_INCLUDED
