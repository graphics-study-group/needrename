# Asset Management

The Asset Management system is responsible for handling various types of game assets, such as textures, models, materials, and game object prefabs. It ensures that these assets are efficiently loaded, managed, and utilized during the game's runtime.

## Structure and Workflow

1. **`Asset` Identification and GUID**
    - Every `Asset` has a unique `GUID` (Globally Unique Identifier), which is stored both in the runtime structure and in the asset file.
    - The `AssetManager` keeps a mapping between `GUID` and file paths, ensuring assets can be located and loaded correctly.
2. **Serialization of `Asset`s**
    - `Asset`s primarily use the serialization system for storage and reading.
    - `Asset`s implement custom serialization functions: `save_to_archive` and `load_from_archive`. These functions ensure that only the `GUID` and `type` information are saved, while other asset data is handled by the `save_asset_to_archive` and `load_asset_from_archive` functions. These two functions are generally only called by the `AssetManager`.
    - When a structure is deserialized (e.g., a `GameObject`), if the structure references an `Asset`, the deserialization function will call the `Asset`'s `load_from_archive` function. This reads the `Asset`'s `GUID` and `type` and adds the `Asset` to the `AssetManager`'s "pending load" queue, marking the `Asset` as unavailable (`m_valid == false`).
    - The `AssetManager` will load the assets in the queue during idle times and mark them as available once they are fully loaded.
3. **Custom `Asset` Serialization**
    - The `save_asset_to_archive` and `load_asset_from_archive` functions will call generated deserialization logic for derived `Asset` classes. These functions automatically store and retrieve the member variables of the classes.
    - If custom data needs to be stored, these functions can be overridden. However, it's essential to ensure that the `Asset::m_guid` is saved and that `m_valid` is set to true when loading completes. One can call `Asset::load_asset_from_archive` at the end of the overridden function to ensure this process is followed correctly.
4. **Project and `Asset` Loading**
    - When the engine starts, `MainClass::LoadProject` is called. At this point, the `AssetManager` scans the asset folder, builds a mapping between `GUID` and file paths, and prepares the assets for later use.
    - During game level loading, any additional referenced assets are added to the `AssetManager`'s queue. These assets are then loaded during idle periods. Currently, the system loads and clears the queue at the start of each frame in the main loop (with potential future improvements for background loading).
5. **Importing External Resources**
    - When importing external resources (e.g., `.obj` files), the appropriate `Loader` is used to read the external file, convert it into an internal asset format, and store it via the `save_asset_to_archive` function.
    - For assets like models (`.obj` files), various asset components such as models, materials, and textures are separated. When importing, a `GameObjectAsset` prefab is created to ensure the engine can later place the entire model into the world.
