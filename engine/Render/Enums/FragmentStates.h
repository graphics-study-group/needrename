#ifndef RENDER_ENUMS_FRAGMENTSTATES_INCLUDED
#define RENDER_ENUMS_FRAGMENTSTATES_INCLUDED

#include "enum_factory.h"

#define BLEND_FACTOR_ENUM_DEF(XMACRO) \
    XMACRO(BlendFactor, Zero) \
    XMACRO(BlendFactor, One) \
    XMACRO(BlendFactor, SrcColor) \
    XMACRO(BlendFactor, OneMinusSrcColor) \
    XMACRO(BlendFactor, DstColor) \
    XMACRO(BlendFactor, OneMinusDstColor) \
    XMACRO(BlendFactor, SrcAlpha) \
    XMACRO(BlendFactor, OneMinusSrcAlpha) \
    XMACRO(BlendFactor, DstAlpha) \
    XMACRO(BlendFactor, OneMinusDstAlpha) \


#define BLEND_OPERATION_ENUM_DEF(XMACRO) \
    XMACRO(BlendOperation, None) \
    XMACRO(BlendOperation, Add) \
    XMACRO(BlendOperation, Substract) \
    XMACRO(BlendOperation, ReverseSubstract) \
    XMACRO(BlendOperation, Min) \
    XMACRO(BlendOperation, Max) \


#define STENCIL_OPERATION_ENUM_DEF(XMARCO) \
    XMARCO(StencilOperation, Keep) \
    XMARCO(StencilOperation, Zero) \
    XMARCO(StencilOperation, Replace) \
    XMARCO(StencilOperation, IncrClamp) \
    XMARCO(StencilOperation, DecrClamp) \
    XMARCO(StencilOperation, Invert) \
    XMARCO(StencilOperation, IncrWrap) \
    XMARCO(StencilOperation, DecrWrap) \


DECLARE_ENUM(BlendFactor, BLEND_FACTOR_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(BlendFactor, BLEND_FACTOR_ENUM_DEF)
DECLARE_ENUM(BlendOperation, BLEND_OPERATION_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(BlendOperation, BLEND_OPERATION_ENUM_DEF)
DECLARE_ENUM(StencilOperation, STENCIL_OPERATION_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(StencilOperation, STENCIL_OPERATION_ENUM_DEF)

#endif // RENDER_ENUMS_FRAGMENTSTATES_INCLUDED
