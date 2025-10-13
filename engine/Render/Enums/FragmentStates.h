#ifndef RENDER_ENUMS_FRAGMENTSTATES_INCLUDED
#define RENDER_ENUMS_FRAGMENTSTATES_INCLUDED

#include "enum_factory.h"

#define COLOR_BLEND_FACTOR_ENUM_DEF(XMACRO) \
    XMACRO(ColorBlendFactor, Zero) \
    XMACRO(ColorBlendFactor, One) \
    XMACRO(ColorBlendFactor, SrcColor) \
    XMACRO(ColorBlendFactor, OneMinusSrcColor) \
    XMACRO(ColorBlendFactor, DstColor) \
    XMACRO(ColorBlendFactor, OneMinusDstColor) \
    XMACRO(ColorBlendFactor, SrcAlpha) \
    XMACRO(ColorBlendFactor, OneMinusSrcAlpha) \
    XMACRO(ColorBlendFactor, DstAlpha) \
    XMACRO(ColorBlendFactor, OneMinusDstAlpha) \


#define COLOR_BLEND_OPERATION_ENUM_DEF(XMACRO) \
    XMACRO(ColorBlendOperation, None) \
    XMACRO(ColorBlendOperation, Add) \
    XMACRO(ColorBlendOperation, Substract) \
    XMACRO(ColorBlendOperation, ReverseSubstract) \
    XMACRO(ColorBlendOperation, Min) \
    XMACRO(ColorBlendOperation, Max) \


#define STENCIL_OPERATION_ENUM_DEF(XMACRO) \
    XMACRO(StencilOperation, Keep) \
    XMACRO(StencilOperation, Zero) \
    XMACRO(StencilOperation, Replace) \
    XMACRO(StencilOperation, IncrClamp) \
    XMACRO(StencilOperation, DecrClamp) \
    XMACRO(StencilOperation, Invert) \
    XMACRO(StencilOperation, IncrWrap) \
    XMACRO(StencilOperation, DecrWrap) \


DECLARE_ENUM(ColorBlendFactor, COLOR_BLEND_FACTOR_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(ColorBlendFactor, COLOR_BLEND_FACTOR_ENUM_DEF)
DECLARE_ENUM(ColorBlendOperation, COLOR_BLEND_OPERATION_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(ColorBlendOperation, COLOR_BLEND_OPERATION_ENUM_DEF)
DECLARE_ENUM(StencilOperation, STENCIL_OPERATION_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(StencilOperation, STENCIL_OPERATION_ENUM_DEF)

#endif // RENDER_ENUMS_FRAGMENTSTATES_INCLUDED
