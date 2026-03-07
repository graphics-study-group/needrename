# Asset Management

The Asset Management system is responsible for handling various types of game assets, such as textures, models, materials, and game object prefabs. It ensures that these assets are efficiently loaded, managed, and utilized during the game's runtime.

## Structure and Workflow

1. **`Asset` Identification and GUID**
    - Every `Asset` has a unique `GUID` (Globally Unique Identifier), which is stored both in the runtime structure and in the asset file.
    - The actual data of the asset is managed by the `AssetDatabase`. Through the GUID, the corresponding asset data can be loaded or saved in the `AssetDatabase`.

2. **Serialization of `Asset`s**
    - `Asset`s utilize the serialization system for storage and retrieval. However, direct serialization using standard `serialize` functions is **not supported**.
    - `Asset` serialization must be handled exclusively through the `AssetManager`. Attempting to serialize an `Asset` directly will invoke the pre-defined custom serialization functions (`save_to_archive` and `load_from_archive`) and result in a runtime error.
    - The `AssetManager` handles asset storage and loading by invoking the `Asset`'s **`save_asset_to_archive`** and **`load_asset_from_archive`** functions.

## AssetRef

`AssetRef` is the primary way to reference assets at runtime.

### AssetRef Structure

- `AssetRef` is a `final` value-type class for referencing assets at runtime.
- It stores:
  - `mutable std::shared_ptr<Asset> m_asset{}` - A mutable shared pointer to the loaded asset data
  - `std::optional<GUID> m_guid{}` - The asset's GUID

### Serialization

- During serialization, `AssetRef` is stored directly as a GUID string.
- Example:
  ```json
  "LevelAsset::m_skybox_material": "07BF9F33F68CCAB150A6078964D33EF1"
  ```

### Loading and Lifetime

- **`AssetRef::as<T>()`**:
  - Queries `AssetManager` using the asset GUID
  - Loads the asset immediately if not already loaded
  - Returns a `std::shared_ptr<T>` to the loaded asset
  - Uses lazy loading: the asset is loaded only when accessed

```cpp
template <AssetClass T>
std::shared_ptr<T> AssetRef::as() {
    static_assert(std::is_base_of<Asset, T>::value, "T must be a derived class of Asset");
    if (!IsLoaded()) Load();
    return std::dynamic_pointer_cast<T>(m_asset);
}
```

### Reference Counting

- `AssetRef` internally contains a `mutable shared_ptr` pointing to the loaded asset data.
- This shared pointer acts purely as a reference counter.
- `AssetManager` uses it to determine whether an asset is still in use.
- Unreferenced assets are automatically cleaned up via `AssetManager::UnloadUnusedAssets()`.

```cpp
void AssetManager::UnloadUnusedAssets() {
    for (auto it = m_loaded_assets.begin(); it != m_loaded_assets.end();) {
        if (it->second.use_count() == 1) {  // Only AssetManager holds the reference
            it = m_loaded_assets.erase(it);
        } else {
            ++it;
        }
    }
}
```

### Key Properties

- Assets with the same GUID are loaded **only once** in memory.
- The queued asset loading mechanism in `AssetManager` supports future background or preloading workflows.
- Preloading needs to explicitly hold asset references to prevent premature cleanup.

### Usage Example

```cpp
// Get an AssetRef and access the asset
auto level_asset = adb.GetNewAssetRef(Engine::AssetPath{adb, "/default_level.asset"})
                    .as<Engine::LevelAsset>();
```

## SceneAsset and LevelAsset

`SceneAsset` and `LevelAsset` are specialized assets for representing serialized runtime scenes.

### SceneAsset

- `SceneAsset` stores the raw `Archive` data directly.
- Does **not** deserialize immediately upon loading.
- Deserialization occurs **only when the asset is added to a runtime Scene**.

#### Serialization Format

`SceneAsset` stores data in two arrays within `%main_data`:

- **`SceneAsset::objects`**: Array of `GameObject` data
- **`SceneAsset::components`**: Array of `Component` data

Each `GameObject` and `Component` stores:
- `Component::m_handle` / `GameObject::m_handle`: The handle ID (used for reference resolution)
- `Component::m_parentGameObject`: Parent `GameObject` handle ID
- `GameObject::m_components`: Array of component handle IDs

Example:
```json
{
    "%main_data": {
        "%type": "Engine::LevelAsset",
        "SceneAsset::components": [
            {
                "%type": "Engine::TransformComponent",
                "Component::m_handle": 1,
                "Component::m_parentGameObject": 1,
                "TransformComponent::m_transform": { ... }
            }
        ],
        "SceneAsset::objects": [
            {
                "%type": "Engine::GameObject",
                "GameObject::m_handle": 1,
                "GameObject::m_components": [1, 6],
                "GameObject::m_parentGameObject": 0
            }
        ]
    }
}
```

```cpp
class SceneAsset : public Asset {
public:
    void SaveFromScene(const Scene &scene);
    void AddToScene(Scene &scene);

    std::unique_ptr<Serialization::Archive> m_archive{};
};
```

### LevelAsset

- `LevelAsset` extends `SceneAsset` with level-specific data.
- Contains additional level configuration:
  - `m_default_camera`: Default camera handle
  - `m_skybox_material`: Skybox material reference

```cpp
class LevelAsset : public SceneAsset {
public:
    void LoadToWorld();

    ComponentHandle m_default_camera{};
    AssetRef m_skybox_material{};
};
```

### Runtime Instantiation

When instantiating a `SceneAsset` into a runtime `Scene`:

1. **Phase 1 - Create Objects**:
   - Create all `GameObject` instances
   - Create all `Component` instances
   - Build ID mappings in `HandleResolver`

2. **Phase 2 - Deserialize Data**:
   - `HandleResolver` remaps stored handles to runtime-safe handles
   - Prevents ID conflicts between existing and newly added objects/components
   - Deserialize all `GameObject` and `Component` data

This two-phase approach ensures correct handle resolution when `GameObject` or `Component` instances are instantiated into a runtime Scene.

### Future Work

- Incremental scene storage is planned: Scenes will retain references to external prefabs.
- Only overridden or modified data will be stored.

## Custom `Asset` Serialization

- The `save_asset_to_archive` and `load_asset_from_archive` functions will call generated deserialization logic for derived `Asset` classes. These functions automatically store and retrieve the member variables of the classes.
- If custom data needs to be stored, these functions can be overridden. However, it's essential to ensure that the `Asset::m_guid` is saved. One can call `Asset::load_asset_from_archive` at the end of the overridden function to ensure this process is followed correctly.

## Project and `Asset` Loading

- When the engine starts, `MainClass::LoadProject` is called. At this point, the `AssetManager` scans the asset folder, builds a mapping between `GUID` and file paths, and prepares the assets for later use.
- During game level loading, any additional referenced assets are added to the `AssetManager`'s queue. These assets are then loaded during idle periods. Currently, the system loads and clears the queue at the start of each frame in the main loop (with potential future improvements for background loading).

## Importing External Resources

- When importing external resources (e.g., `.obj` files), the appropriate `Loader` is used to read the external file, convert it into an internal asset format, and store it via the `save_asset_to_archive` function.
- For assets like models (`.obj` files), various asset components such as models, materials, and textures are separated. When importing, a `GameObjectAsset` prefab is created to ensure the engine can later place the entire model into the world.
