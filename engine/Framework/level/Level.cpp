#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Framework/level/level.h"
#include <Framework/object/GameObject.h>

namespace Engine
{
    Level::Level(std::weak_ptr <AssetManager> manager) : Asset(manager)
    {
        //ctor
    }

    Level::~Level()
    {
        //dtor
    }

    void Level::Load()
    {
        Asset::Load();

        std::filesystem::path json_path = GetMetaPath();
        std::ifstream json_file(json_path);
        nlohmann::json json;
        json_file >> json;
        json_file.close();
        // TODO: use reflection to load game objects
        for(auto & go : m_gameObjects)
        {
            go->Load();
        }
    }

    void Level::Unload()
    {
        Asset::Unload();
        for(auto & go : m_gameObjects)
        {
            go->Unload();
        }
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
