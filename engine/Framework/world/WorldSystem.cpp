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
        for (auto & go : m_go_loading_queue)
        {
            for(auto & comp : go->m_components)
                comp->Init();
            m_all_components.insert(m_all_components.end(), go->m_components.begin(), go->m_components.end());
        }
        m_go_loading_queue.clear();

        for (auto & comp : m_all_components)
        {
            comp->Tick(dt);
        }
    }

    GUID WorldSystem::GenerateID()
    {
        return generateGUID(m_id_gen);
    }
}
