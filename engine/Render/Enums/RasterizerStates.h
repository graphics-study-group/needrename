#ifndef RENDER_ENUMS_RASTERIZERSTATES_INCLUDED
#define RENDER_ENUMS_RASTERIZERSTATES_INCLUDED

#include "enum_factory.h"

#define CULLING_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, None) \
    XMACRO(enum_name, Front) \
    XMACRO(enum_name, Back) \
    XMACRO(enum_name, All) \

#define FILLING_MODE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Fill) \
    XMACRO(enum_name, Line) \
    XMACRO(enum_name, Point) \


#define FRONT_FACE_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Counterclockwise) \
    XMACRO(enum_name, Clockwise) \

namespace Engine::_enum {
    DECLARE_ENUM(CullingMode, CULLING_MODE_ENUM_DEF)
    DECLARE_ENUM(FillingMode, FILLING_MODE_ENUM_DEF)
    DECLARE_ENUM(FrontFace, FRONT_FACE_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::CullingMode, CULLING_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::FillingMode, FILLING_MODE_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::FrontFace, FRONT_FACE_ENUM_DEF)

#endif // RENDER_ENUMS_RASTERIZERSTATES_INCLUDED
