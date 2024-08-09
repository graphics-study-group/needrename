#ifndef WORLDSYSTEM_H
#define WORLDSYSTEM_H

#include "Framework/level/Level.h"

namespace Engine
{
    class WorldSystem
    {
    public:
        WorldSystem();
        ~WorldSystem();

        void Tick(float dt);

        void SetCurrentLevel(std::shared_ptr<Level> level)
        {
            current_level = level;
        }        
    protected:
        std::shared_ptr<Level> current_level;
    };
}
#endif // WORLDSYSTEM_H