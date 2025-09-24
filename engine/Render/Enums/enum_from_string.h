#ifndef RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED
#define RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED

#include "enum_def.h"
#include <string_view>
#include <cassert>

#ifndef DEFINE_ENUM_ITEM
#define DEFINE_ENUM_ITEM(e, x) case details::hash_string_view(#x): return e::x;
#endif

namespace Engine {
    namespace _enums {
        namespace _details {
            constexpr size_t hash_string_view (std::string_view v) {
                size_t hash = 0xcbf29ce484222325;
                constexpr size_t prime = 0x100000001b3;

                for (char c : v) {
                    hash ^= static_cast<size_t>(c);
                    hash *= prime;
                }
                return hash;
            }
        }

        constexpr ImageFormat ImageFormat_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/ImageFormat.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr FillingMode FillingMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/FillingMode.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr CullingMode CullingMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/CullingMode.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr FrontFace FrontFace_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/FrontFace.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr Comparator Comparator_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/Comparator.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr StencilOperation StencilOperation_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/StencilOperation.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr BlendFactor BlendFactor_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/BlendFactor.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr BlendOperation BlendOperation_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/BlendOperation.def"
            }
            assert(!"Undefined enum value.");
        }
    }
}

#undef DEFINE_ENUM_ITEM

#endif // RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED
