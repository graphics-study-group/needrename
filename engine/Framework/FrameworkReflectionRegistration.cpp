#include <AnnoRefl/Type.h>

#include "meta_framework/reflection_init.inc"

extern "C"
{
    void RegisterFrameworkTypes() {
        RegisterAllTypes();
    }
}
