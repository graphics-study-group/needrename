#ifndef RENDER_ENUMS_FRAGMENTSTATES_INCLUDED
#define RENDER_ENUMS_FRAGMENTSTATES_INCLUDED

#include <Reflection/enum_factory.h>

#define COLOR_BLEND_FACTOR_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Zero) \
    XMACRO(enum_name, One) \
    XMACRO(enum_name, SrcColor) \
    XMACRO(enum_name, OneMinusSrcColor) \
    XMACRO(enum_name, DstColor) \
    XMACRO(enum_name, OneMinusDstColor) \
    XMACRO(enum_name, SrcAlpha) \
    XMACRO(enum_name, OneMinusSrcAlpha) \
    XMACRO(enum_name, DstAlpha) \
    XMACRO(enum_name, OneMinusDstAlpha) \


#define COLOR_BLEND_OPERATION_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, None) \
    XMACRO(enum_name, Add) \
    XMACRO(enum_name, Substract) \
    XMACRO(enum_name, ReverseSubstract) \
    XMACRO(enum_name, Min) \
    XMACRO(enum_name, Max) \


#define STENCIL_OPERATION_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Keep) \
    XMACRO(enum_name, Zero) \
    XMACRO(enum_name, Replace) \
    XMACRO(enum_name, IncrClamp) \
    XMACRO(enum_name, DecrClamp) \
    XMACRO(enum_name, Invert) \
    XMACRO(enum_name, IncrWrap) \
    XMACRO(enum_name, DecrWrap) \

namespace Engine::_enum {
    DECLARE_ENUM(ColorBlendFactor, COLOR_BLEND_FACTOR_ENUM_DEF)
    DECLARE_ENUM(ColorBlendOperation, COLOR_BLEND_OPERATION_ENUM_DEF)
    DECLARE_ENUM(StencilOperation, STENCIL_OPERATION_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::ColorBlendFactor, COLOR_BLEND_FACTOR_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::ColorBlendOperation, COLOR_BLEND_OPERATION_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::StencilOperation, STENCIL_OPERATION_ENUM_DEF)

#endif // RENDER_ENUMS_FRAGMENTSTATES_INCLUDED
