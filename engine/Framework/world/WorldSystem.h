#ifndef FRAMEWORK_WORLD_WORLDSYSTEM_H
#define FRAMEWORK_WORLD_WORLDSYSTEM_H

#include <random>
#include <Core/guid.h>
#include <Framework/level/Level.h>

namespace Engine
{
    class WorldSystem
    {
    public:
        WorldSystem();
        ~WorldSystem();

        void Tick(float dt);

        GUID GenerateID();

        void SetCurrentLevel(std::shared_ptr<Level> level);
    protected:
        std::mt19937_64 m_id_gen{std::random_device{}()};

        std::shared_ptr<Level> current_level {};
    };
}
#endif // FRAMEWORK_WORLD_WORLDSYSTEM_H
