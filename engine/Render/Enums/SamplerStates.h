#ifndef RENDER_ENUMS_SAMPLERSTATES_INCLUDED
#define RENDER_ENUMS_SAMPLERSTATES_INCLUDED

#include <Reflection/enum_factory.h>
#include <cstdint>

#define ADDRESS_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Repeat) \
    XMACRO(enum_name, MirroredRepeat) \
    XMACRO(enum_name, ClampToEdge) \


#define FILTER_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Point) \
    XMACRO(enum_name, Linear) \

namespace Engine::_enum {
    DECLARE_ENUM_WITH_UTYPE(AddressMode, uint8_t, ADDRESS_MODE_ENUM_DEF)
    DECLARE_ENUM_WITH_UTYPE(FilterMode, uint8_t, FILTER_MODE_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::AddressMode, ADDRESS_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::FilterMode, FILTER_MODE_ENUM_DEF)

#endif // RENDER_ENUMS_SAMPLERSTATES_INCLUDED
