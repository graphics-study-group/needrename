#ifndef RENDER_ENUMS_ENUM_TO_STRING_INCLUDED
#define RENDER_ENUMS_ENUM_TO_STRING_INCLUDED

#include "enum_def.h"
#include <string_view>

#ifndef DEFINE_ENUM_ITEM
#define DEFINE_ENUM_ITEM(e, x) case e::x: return #x;
#endif

namespace Engine {
    /**
     * C++ standard forbids generating preprocessor directives
     * in macros. So we must write these functions manually.
     */
    namespace _enums {
        constexpr std::string_view to_string (ImageFormat e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/ImageFormat.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (FillingMode e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/FillingMode.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (CullingMode e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/CullingMode.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (FrontFace e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/FrontFace.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (Comparator e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/Comparator.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (StencilOperation e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/StencilOperation.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (BlendFactor e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/BlendFactor.def"
            }
            return ""sv;
        }

        constexpr std::string_view to_string (BlendOperation e) noexcept {
            using namespace std::literals;
            switch(e) {
                #include "defs/BlendOperation.def"
            }
            return ""sv;
        }
    }
}

#undef DEFINE_ENUM_ITEM

#endif // RENDER_ENUMS_ENUM_TO_STRING_INCLUDED
