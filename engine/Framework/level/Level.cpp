#include "Framework/level/level.h"
#include "Framework/go/GameObject.h"

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

    void Level::tick(float dt)
    {
        for (auto gameObject : m_gameObjects)
        {
            gameObject->tick(dt);
        }
    }

    void Level::AddGameObject(std::shared_ptr<GameObject> gameObject)
    {
        m_gameObjects.push_back(gameObject);
    }
}