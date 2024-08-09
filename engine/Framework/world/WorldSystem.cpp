#include "Framework/world/WorldSystem.h"

namespace Engine
{
    WorldSystem::WorldSystem()
    {
    }

    WorldSystem::~WorldSystem()
    {
    }

    void WorldSystem::Tick(float dt)
    {
        if (current_level)
            current_level->Tick(dt);
    }
}
