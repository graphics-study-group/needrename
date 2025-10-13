#ifndef RENDER_ENUMS_IMAGEFORMAT_INCLUDED
#define RENDER_ENUMS_IMAGEFORMAT_INCLUDED

#include <Reflection/enum_factory.h>

#define IMAGE_FORMAT_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, UNDEFINED) \
    XMACRO(enum_name, R8G8B8A8SNorm) \
    XMACRO(enum_name, R8G8B8A8UNorm) \
    XMACRO(enum_name, R8G8B8A8SRGB) \
    XMACRO(enum_name, R11G11B10UFloat) \
    XMACRO(enum_name, R32G32B32A32SFloat) \
    XMACRO(enum_name, D32SFLOAT) \

namespace Engine::_enum {
    DECLARE_ENUM(ImageFormat, IMAGE_FORMAT_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::ImageFormat, IMAGE_FORMAT_ENUM_DEF)

#endif // RENDER_ENUMS_IMAGEFORMAT_INCLUDED
