# Game Framework

This document describes the framework for how a game operates, focusing on how **GameObjects**, **Components**, and other game-related elements are loaded, initialized, updated, and connected to rendering.

## Structure

1. **GameObject and World System**
    - ```GameObject``` is the basic unit in the game world, representing entities or objects within the game. It operates within the ```WorldSystem```.
    - Each ```GameObject``` contains one or more ```Components```, where the game logic primarily resides. A ```GameObject``` itself is merely a container.
    - A ```GameObject``` can be parented to another ```GameObject```, forming a tree structure. Every ```GameObject``` must contain a ```TransformComponent```, which represents its transformation (position, rotation, scale) relative to its parent or the world.
    - Every ```Component``` is always part of a ```GameObject```.
2. **GameObject Creation**
    - ```GameObject``` are generally not created directly (except during deserialization). Instead, they must be created via the ```WorldSystem``` using a factory method ```WorldSystem::CreateGameObject<T>```.
    - After creation, a ```GameObject``` is not immediately added to the world. It will be placed in a "pending addition" queue in ```WorldSystem``` after call ```WorldSystem::AddGameObjectToWorld```.
    - When the game moves to the next update frame, the ```WorldSystem``` checks the "pending addition" queue. It executes the ```Init``` function of the ```Component``` which are possessed by ```GameObject``` in queue and then officially adds the ```GameObject``` to the world.
    - ```Component``` that need to interact with the rendering system should register themselves during the ```Init``` function.
3. **Component Update**
    - The ```WorldSystem``` updates all ```Components``` in the game, calling their ```Tick``` function in no particular order, regardless of the ```GameObject```'s structure.
4. **World Initialization and Asset Management**
    - When the engine starts, the default level asset is loaded into the world. A ```LevelAsset``` is simply an asset that contains many ```GameObjects``` and is not a runtime concept.
    - When a ```LevelAsset``` is loaded, the ```GameObject``` it contains are deserialized and added to the world.
    - Assets referenced by ```GameObject``` are temporarily placed in the ```AssetManager```'s' "pending load" queue. These assets will be loaded at a later time (currently, they are loaded at the start of each frame in the main loop, though this could be changed to asynchronous loading in the future).
