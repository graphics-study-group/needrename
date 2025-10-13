#ifndef RENDER_ENUMS_ENUM_FACTORY_INCLUDED
#define RENDER_ENUMS_ENUM_FACTORY_INCLUDED

#include <Reflection/reflection.h>
#include <string_view>
#include <optional>

namespace Engine::Reflection::enum_detail {
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

#define ENUM_ITEM(enum_name, name) name,

#define ENUM_TO_STR_CASE(enum_name, name) case enum_name::name: return #name;

#define ENUM_FROM_STR_CASE(enum_name, name) case enum_detail::hash_string_view(#name): return enum_name::name;

#define DECLARE_ENUM(enum_name, ENUM_DEF_MACRO) \
  enum class enum_name { \
    ENUM_DEF_MACRO(ENUM_ITEM, enum_name) \
  }; \


#define DECLARE_ENUM_WITH_UTYPE(enum_name, underlying_type, ENUM_DEF_MACRO) \
  enum class enum_name : underlying_type { \
    ENUM_DEF_MACRO(ENUM_ITEM, enum_name) \
  }; \


#define DECLARE_REFLECTIVE_FUNCTIONS(EnumType,ENUM_DEF_MACRO) \
namespace Engine::Reflection { \
  constexpr std::string_view to_string(EnumType value) noexcept \
  { \
    switch(value) \
    { \
      ENUM_DEF_MACRO(ENUM_TO_STR_CASE, EnumType) \
      default: return ""; \
    } \
  } \
  template <> \
  constexpr std::optional<EnumType> from_string<EnumType>(std::string_view sv) noexcept \
  { \
    switch(enum_detail::hash_string_view(sv)) {\
        ENUM_DEF_MACRO(ENUM_FROM_STR_CASE, EnumType) \
        default: return std::nullopt; \
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

#endif // RENDER_ENUMS_ENUM_FACTORY_INCLUDED
