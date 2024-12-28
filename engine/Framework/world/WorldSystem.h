#ifndef FRAMEWORK_WORLD_WORLDSYSTEM_H
#define FRAMEWORK_WORLD_WORLDSYSTEM_H

#include <random>
#include <memory>
#include <Core/guid.h>
#include <Framework/object/GameObject.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
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
        template<typename T, typename... Args>
        std::shared_ptr<T> CreateGameObject();

        void SetCurrentLevel(std::shared_ptr<Level> level);
    protected:
        std::mt19937_64 m_id_gen{std::random_device{}()};

        std::shared_ptr<Level> current_level {};
    };

    template<typename T, typename... Args>
    std::shared_ptr<T> WorldSystem::CreateGameObject()
    {
        static_assert(std::is_base_of<GameObject, T>::value, "T must be derived from GameObject");
        auto game_object = std::make_shared<T>(this);
        game_object->template AddComponent<TransformComponent>();
        game_object->m_transformComponent = dynamic_pointer_cast<TransformComponent>(game_object->m_components[0]);
        return game_object;
    }
}
#endif // FRAMEWORK_WORLD_WORLDSYSTEM_H
