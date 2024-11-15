#include "WorldSystem.h"

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

    GUID WorldSystem::GenerateID()
    {
        return generateGUID(m_id_gen);
    }

    void WorldSystem::SetCurrentLevel(std::shared_ptr<Level> level)
    {
        current_level = level;
    }
}
