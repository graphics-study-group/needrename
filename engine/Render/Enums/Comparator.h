#ifndef ENUMS_DEFS_COMPARATOR_INCLUDED
#define ENUMS_DEFS_COMPARATOR_INCLUDED

#include "enum_factory.h"

#define COMPARATOR_ENUM_DEF(XMACRO) \
    XMACRO(Comparator, Never) \
    XMACRO(Comparator, Less) \
    XMACRO(Comparator, Equal) \
    XMACRO(Comparator, LEqual) \
    XMACRO(Comparator, Greater) \
    XMACRO(Comparator, NEqual) \
    XMACRO(Comparator, GEqual) \
    XMACRO(Comparator, Always) \


DECLARE_ENUM(Comparator, COMPARATOR_ENUM_DEF)
DECLARE_REFLECTIVE_FUNCTIONS(Comparator, COMPARATOR_ENUM_DEF)

#endif // ENUMS_DEFS_COMPARATOR_INCLUDED
