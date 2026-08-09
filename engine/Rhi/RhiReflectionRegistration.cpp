#include "rhi_export.h"
#include <AnnoRefl/Type.h>

#include "meta_rhi/reflection_init.inc"

extern "C"
{
    RHI_API void RegisterRhiTypes() {
        RegisterAllTypes();
    }
}
