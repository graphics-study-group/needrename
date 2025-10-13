#ifndef ENUMS_DEFS_COMPARATOR_INCLUDED
#define ENUMS_DEFS_COMPARATOR_INCLUDED

#include <Reflection/enum_factory.h>

#define COMPARATOR_ENUM_DEF(XMACRO, enum_name) \
    XMACRO(enum_name, Never) \
    XMACRO(enum_name, Less) \
    XMACRO(enum_name, Equal) \
    XMACRO(enum_name, LessOrEqual) \
    XMACRO(enum_name, Greater) \
    XMACRO(enum_name, NotEqual) \
    XMACRO(enum_name, GreaterOrEqual) \
    XMACRO(enum_name, Always) \

namespace Engine::_enum {
    DECLARE_ENUM(Comparator, COMPARATOR_ENUM_DEF)
}
DECLARE_REFLECTIVE_FUNCTIONS(Engine::_enum::Comparator, COMPARATOR_ENUM_DEF)

#endif // ENUMS_DEFS_COMPARATOR_INCLUDED
