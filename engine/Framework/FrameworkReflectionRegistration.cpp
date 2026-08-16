#include "Framework/framework_export.h"
#include <AnnoRefl/Type.h>

#include "meta_framework/reflection_init.inc"

extern "C"
{
    FRAMEWORK_API void RegisterFrameworkTypes() {
        RegisterAllTypes();
    }
}
