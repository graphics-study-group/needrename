# Game Framework

This document describes the framework for how a game operates, focusing on the **World → Scene → GameObject / Component** hierarchy, ownership management, and how game-related elements are loaded, initialized, updated, and connected to rendering.

## Ownership and Lifetime Management

- The engine uses a **World → Scene → GameObject / Component** hierarchy for clear ownership boundaries.
- `Scene` internally owns all `GameObject` and `Component` instances using `unique_ptr`.

## World

`World` (implemented as `WorldSystem`) is responsible for managing multiple `Scene` instances.

- Each `Scene` can be accessed via a `uint32_t` scene ID.
- A **main Scene** exists inside `World`, representing the active runtime scene.
- Additional Scenes may be used for:
  - Prefab editing
  - Temporary or auxiliary purposes

## Scene

`Scene` manages **Creation**, **Storage**, and **Lookup** of all `GameObject` and `Component` instances.

- All `GameObject` and `Component` instances **must be created through a Scene**.
- `Scene` uses `unique_ptr` to store `GameObject` and `Component`, ensuring clear ownership.
- Creation and deletion operations are queued and processed via `FlushCmdQueue()` for safe lifetime management.

### Scene Storage Structure

```
Scene
├── m_game_objects (vector<unique_ptr<GameObject>>)
├── m_components (vector<unique_ptr<Component>>)
├── m_go_map (unordered_map<ObjectHandle, GameObject*>)
├── m_comp_map (unordered_map<ComponentHandle, Component*>)
├── m_go_add_queue / m_go_remove_queue
└── m_comp_add_queue / m_comp_remove_queue
```

## Handles and References

All references must go through: `ObjectHandle`, `ComponentHandle`.

### Handle Structure

Each handle stores:
- The object/component ID (`m_ID`)
- The owning Scene ID (`m_sceneID`)

### Handle Resolution

To resolve a handle:
1. Locate the owning `Scene` via the Scene ID
2. Query the `Scene` for the corresponding object or component

```cpp
GameObject* ObjectHandle::GetGameObject() const {
    auto scene = MainClass::GetInstance()->GetWorldSystem()->GetScenePtr(m_sceneID);
    if (scene) {
        return scene->GetGameObject(*this);
    }
    return nullptr;
}
```

This design enforces explicit ownership boundaries and avoids dangling references across scenes.

## GameObject and Component

1. **GameObject**
   - `GameObject` is the basic unit in the game world, representing entities or objects within the game.
   - Each `GameObject` contains one or more `Components`, where the game logic primarily resides. A `GameObject` itself is merely a container.
   - A `GameObject` can be parented to another `GameObject`, forming a tree structure.
   - Every `GameObject` must contain a `TransformComponent`, which represents its transformation (position, rotation, scale) relative to its parent or the world.
   - `GameObject` stores references to its components using `ComponentHandle`.

2. **Component**
   - Every `Component` is always part of a `GameObject`.
   - `Component` stores a reference to its parent `GameObject` using `ObjectHandle`.
   - `Component` that needs to interact with the rendering system should register itself during the `Init` function.

## GameObject and Component Creation

1. **Creation through Scene**
   - `GameObject` and `Component` must be created via `Scene::CreateGameObject()` and `Scene::CreateComponent()`.
   - After creation, objects are placed in a "pending addition" queue.

2. **Initialization**
   - When `Scene::FlushCmdQueue()` is called, the Scene processes the pending queues.
   - It executes the `Init` function of each `Component` and then officially adds the `GameObject` to the world.

3. **Deletion**
   - Deletion is also queued via `Scene::RemoveGameObject()` and `Scene::RemoveComponent()`.
   - Actual removal happens during `FlushCmdQueue()`.

## Component Update

- The `WorldSystem` updates all `Components` in the game, calling their `Tick` function.
- Components are updated in no particular order, regardless of the `GameObject`'s structure.

## World Initialization and Asset Management

1. **Level Loading**
   - When the engine starts, the default `LevelAsset` is loaded into the world.
   - `LevelAsset` extends `SceneAsset` and stores serialized scene data.

2. **SceneAsset and LevelAsset**
   - `SceneAsset` stores raw `Archive` data directly and does **not** deserialize immediately upon loading.
   - Deserialization occurs **only when the asset is added to a runtime Scene**.
   - `LevelAsset` adds level-specific data like default camera and skybox material.

3. **Runtime Instantiation**
   - When instantiating a `SceneAsset` into a runtime Scene:
     - `HandleResolver` remaps stored handles to runtime-safe handles.
     - Prevents ID conflicts between existing and newly added objects/components.

4. **Asset Loading**
   - Assets referenced by `GameObject` are managed via `AssetRef`.
   - `AssetRef::as<T>()` loads the asset immediately if not already loaded.
   - Assets with the same GUID are loaded **only once** in memory.
