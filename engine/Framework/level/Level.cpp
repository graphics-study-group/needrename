#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Framework/level/level.h"
#include <Framework/object/GameObject.h>

namespace Engine
{
    Level::Level()
    {
        //ctor
    }

    Level::~Level()
    {
        //dtor
    }

    void Level::Tick(float dt)
    {
        for (auto gameObject : m_gameObjects)
        {
            gameObject->Tick(dt);
        }
    }

    void Level::AddGameObject(std::shared_ptr<GameObject> gameObject)
    {
        m_gameObjects.push_back(gameObject);
    }
}
