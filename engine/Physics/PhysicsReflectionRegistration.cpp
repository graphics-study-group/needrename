#include "Physics/physics_export.h"
#include <AnnoRefl/Type.h>

#include "meta_physics/reflection_init.inc"

extern "C"
{
    PHYSICS_API void RegisterPhysicsTypes() {
        RegisterAllTypes();
    }
}
