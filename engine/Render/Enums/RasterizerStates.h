#ifndef RENDER_ENUMS_RASTERIZERSTATES_INCLUDED
#define RENDER_ENUMS_RASTERIZERSTATES_INCLUDED

#include <Reflection/enum_factory.h>

#define RASTERIZER_CULLING_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, None) \
    XMACRO(enum_name, Front) \
    XMACRO(enum_name, Back) \
    XMACRO(enum_name, All) \

#define RASTERIZER_FILLING_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Fill) \
    XMACRO(enum_name, Line) \
    XMACRO(enum_name, Point) \


#define RASTERIZER_FRONT_FACE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Counterclockwise) \
    XMACRO(enum_name, Clockwise) \

namespace Engine::_enum {
    DECLARE_ENUM(RasterizerCullingMode, RASTERIZER_CULLING_MODE_ENUM_DEF)
    DECLARE_ENUM(RasterizerFillingMode, RASTERIZER_FILLING_MODE_ENUM_DEF)
    DECLARE_ENUM(RasterizerFrontFace, RASTERIZER_FRONT_FACE_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::RasterizerCullingMode, RASTERIZER_CULLING_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::RasterizerFillingMode, RASTERIZER_FILLING_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::RasterizerFrontFace, RASTERIZER_FRONT_FACE_ENUM_DEF)

#endif // RENDER_ENUMS_RASTERIZERSTATES_INCLUDED
