#ifndef RENDER_ENUMS_RASTERIZERSTATES_INCLUDED
#define RENDER_ENUMS_RASTERIZERSTATES_INCLUDED

#include "enum_factory.h"

#define RASTERIZER_CULLING_MODE_ENUM_DEF(XMACRO) \
    XMACRO(RasterizerCullingMode, None) \
    XMACRO(RasterizerCullingMode, Front) \
    XMACRO(RasterizerCullingMode, Back) \
    XMACRO(RasterizerCullingMode, All) \

#define RASTERIZER_FILLING_MODE_ENUM_DEF(XMACRO) \
    XMACRO(RasterizerFillingMode, Fill) \
    XMACRO(RasterizerFillingMode, Line) \
    XMACRO(RasterizerFillingMode, Point) \


#define RASTERIZER_FRONT_FACE_ENUM_DEF(XMACRO) \
    XMACRO(RasterizerFrontFace, Counterclockwise) \
    XMACRO(RasterizerFrontFace, Clockwise) \


DECLARE_ENUM(RasterizerCullingMode, RASTERIZER_CULLING_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(RasterizerCullingMode, RASTERIZER_CULLING_MODE_ENUM_DEF)
DECLARE_ENUM(RasterizerFillingMode, RASTERIZER_FILLING_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(RasterizerFillingMode, RASTERIZER_FILLING_MODE_ENUM_DEF)
DECLARE_ENUM(RasterizerFrontFace, RASTERIZER_FRONT_FACE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(RasterizerFrontFace, RASTERIZER_FRONT_FACE_ENUM_DEF)

#endif // RENDER_ENUMS_RASTERIZERSTATES_INCLUDED
