#include "core_export.h"
#include <AnnoRefl/Type.h>

#include "meta_core/reflection_init.inc"

extern "C"
{
    CORE_API void RegisterCoreTypes() {
        RegisterAllTypes();
    }
}
