#include "Framework/world/WorldSystem.h"

namespace Engine
{
    WorldSystem::WorldSystem()
    {
        // TODO: load level from file or template level
        current_level = std::make_shared<Level>();
    }

    WorldSystem::~WorldSystem()
    {
    }

    void WorldSystem::tick(float dt)
    {
        current_level->tick(dt);
    }
}
