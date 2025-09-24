#ifndef RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED
#define RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED

#include "enum_def.h"
#include <string_view>
#include <stdexcept>
#include <format>

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
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr FillingMode FillingMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/FillingMode.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr CullingMode CullingMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/CullingMode.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr FrontFace FrontFace_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/FrontFace.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr Comparator Comparator_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/Comparator.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr StencilOperation StencilOperation_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/StencilOperation.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr BlendFactor BlendFactor_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/BlendFactor.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr BlendOperation BlendOperation_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/BlendOperation.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr AddressMode AddressMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/AddressMode.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }

        constexpr FilterMode FilterMode_from_string(std::string_view s) {
            switch(_details::hash_string_view(s)) {
                #include "defs/FilterMode.def"
            }
            throw std::invalid_argument(std::format("Invalid enum value: {}", s));
        }
    }
}

#undef DEFINE_ENUM_ITEM

#endif // RENDER_ENUMS_ENUM_FROM_STRING_INCLUDED
