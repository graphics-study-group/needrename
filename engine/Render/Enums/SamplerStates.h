#ifndef RENDER_ENUMS_SAMPLERSTATES_INCLUDED
#define RENDER_ENUMS_SAMPLERSTATES_INCLUDED

#include <Reflection/enum_factory.h>
#include <cstdint>

#define SAMPLER_ADDRESS_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Repeat) \
    XMACRO(enum_name, MirroredRepeat) \
    XMACRO(enum_name, ClampToEdge) \


#define SAMPLER_FILTER_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Point) \
    XMACRO(enum_name, Linear) \

namespace Engine::_enum {
    DECLARE_ENUM_WITH_UTYPE(SamplerAddressMode, uint8_t, SAMPLER_ADDRESS_MODE_ENUM_DEF)
    DECLARE_ENUM_WITH_UTYPE(SamplerFilterMode, uint8_t, SAMPLER_FILTER_MODE_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::SamplerAddressMode, SAMPLER_ADDRESS_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::SamplerFilterMode, SAMPLER_FILTER_MODE_ENUM_DEF)

#endif // RENDER_ENUMS_SAMPLERSTATES_INCLUDED
