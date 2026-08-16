#include "Asset/asset_export.h"
#include <AnnoRefl/Type.h>

#include "meta_asset_core/reflection_init.inc"

extern "C"
{
    ASSET_CORE_API void RegisterAssetCoreTypes() {
        RegisterAllTypes();
    }
}
