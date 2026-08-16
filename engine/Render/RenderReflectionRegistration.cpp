#include "Render/render_export.h"
#include <AnnoRefl/Type.h>

#include "meta_render/reflection_init.inc"

extern "C"
{
    RENDER_API void RegisterRenderTypes() {
        RegisterAllTypes();
    }
}
