#ifndef RENDER_ENUMS_ENUM_FACTORY_INCLUDED
#define RENDER_ENUMS_ENUM_FACTORY_INCLUDED

#define ENUM_ITEM(enum_name, name) name,

#define ENUM_TO_STR_CASE(enum_name, name) case enum_name::name: return #name;

#define ENUM_FROM_STR_CASE(enum_name, name) case _detail::hash_string_view(#name): return enum_name::name;

#define DECLARE_ENUM(enum_name, ENUM_DEF_MACRO) \
namespace Engine::_enum { \
  enum class enum_name { \
    ENUM_DEF_MACRO(ENUM_ITEM) \
  }; \
} \


#define DECLARE_ENUM_WITH_UTYPE(enum_name, underlying_type, ENUM_DEF_MACRO) \
namespace Engine::_enum { \
  enum class enum_name : underlying_type { \
    ENUM_DEF_MACRO(ENUM_ITEM) \
  }; \
} \


#define DECLARE_REFLECTIVE_FUNCTIONS(EnumType,ENUM_DEF_MACRO) \
namespace Engine::_enum { \
  constexpr std::string_view to_string(EnumType value) noexcept \
  { \
    switch(value) \
    { \
      ENUM_DEF_MACRO(ENUM_TO_STR_CASE) \
      default: return ""; \
    } \
  } \
  constexpr EnumType to_##EnumType(std::string_view sv) \
  { \
    switch(_detail::hash_string_view(sv)) {\
        ENUM_DEF_MACRO(ENUM_FROM_STR_CASE) \
        default: throw std::invalid_argument(std::format("Invalid enum string: {} for enum {}", sv, #EnumType)); \
    } \
  } \
} \


/**
 * Define a new enum as such:
```cpp
#define SOME_ENUM_MACRO(XMACRO) \
XMACRO(some_enum, e1) \
XMACRO(some_enum, e2) \
```
 */

#include <string_view>
#include <stdexcept>
#include <format>

namespace Engine::_enum::_detail {
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

#endif // RENDER_ENUMS_ENUM_FACTORY_INCLUDED
