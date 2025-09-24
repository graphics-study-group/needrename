#ifndef RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED
#define RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED

#include "enum_def.h"
#include <string_view>
#include <cassert>

#ifndef DEFINE_ENUM_ITEM
#define DEFINE_ENUM_ITEM(e, x) case _details::hash_string_view(#x): return e::x;
#endif

namespace Engine {
    namespace _enums {
        namespace _details {
            /**
             * @brief constexpr version of a string view hasher (FNV-1a)
             * 
             * Note that the `string_view` does not contain the trailing
             * null character when directly coerced from a `string` or 
             * `const char []`. This will result in a different hash value
             * if a `string_view` explicitly contains a trailing null.
             */
            constexpr size_t hash_string_view (std::string_view v) {
                size_t hash = 0xcbf29ce484222325ull;
                constexpr size_t prime = 0x100000001b3ull;

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

        constexpr AddressMode AddressMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/AddressMode.def"
            }
            assert(!"Undefined enum value.");
        }

        constexpr FilterMode FilterMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/FilterMode.def"
            }
            assert(!"Undefined enum value.");
        }
    }
}

#undef DEFINE_ENUM_ITEM

#endif // RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED
