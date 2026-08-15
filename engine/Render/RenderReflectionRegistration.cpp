#include <AnnoRefl/Type.h>

#include "meta_render/reflection_init.inc"

extern "C"
{
    void RegisterRenderTypes() {
        RegisterAllTypes();
    }
}
