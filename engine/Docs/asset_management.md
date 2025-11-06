# Asset Management

The Asset Management system is responsible for handling various types of game assets, such as textures, models, materials, and game object prefabs. It ensures that these assets are efficiently loaded, managed, and utilized during the game's runtime.

## Structure and Workflow

1. **`Asset` Identification and GUID**
    - Every `Asset` has a unique `GUID` (Globally Unique Identifier), which is stored both in the runtime structure and in the asset file.
    - The actual data of the asset is managed by the `AssetDatabase`. Through the GUID, the corresponding asset data can be  loaded or saved in the `AssetDatabase`.
2. **Serialization of `Asset`s**
    - `Asset`s utilize the serialization system for storage and retrieval. However, direct serialization using standard `serialize` functions is **not supported**.
    - `Asset` serialization must be handled exclusively through the `AssetManager`. Attempting to serialize an `Asset` directly will invoke the pre-defined custom serialization functions (`save_to_archive` and `load_from_archive`) and result in a runtime error.
    - The `AssetManager` handles asset storage and loading by invoking the `Asset`'s **`save_asset_to_archive`** and **`load_asset_from_archive`** functions.
    - When other types (e.g., `GameObject`) try to reference an `Asset`, the `AssetRef` class should be used.
        - **`AssetRef`** stores a smart pointer to the `Asset` and its globally unique identifier (GUID). Initially, the pointer is null.
        - During serialization, only the GUID is stored.
        - During deserialization:
            1. `AssetRef` places itself into the `AssetManager`'s "pending load" queue.
            2. At a later point, `AssetManager` locates the corresponding asset using its GUID.
            3. `AssetManager` allocates memory for the `Asset` using its internal memory management system and invokes the `load_asset_from_archive` function to initialize it.
            4. The `AssetRef`'s pointer is then updated to the loaded `Asset`, marking it as fully initialized.
        - `AssetRef` should be regarded as a structure that stores data rather than a pointer or reference. It is necessary to avoid circular references to a certain asset type in `AssetRef`. For example, if Asset A containing Asset B's AssetRef and B containing A's AssetRef, the AssetManager will fall into a dead loop.
3. **Custom `Asset` Serialization**
    - The `save_asset_to_archive` and `load_asset_from_archive` functions will call generated deserialization logic for derived `Asset` classes. These functions automatically store and retrieve the member variables of the classes.
    - If custom data needs to be stored, these functions can be overridden. However, it's essential to ensure that the `Asset::m_guid` is saved. One can call `Asset::load_asset_from_archive` at the end of the overridden function to ensure this process is followed correctly.
4. **Project and `Asset` Loading**
    - When the engine starts, `MainClass::LoadProject` is called. At this point, the `AssetManager` scans the asset folder, builds a mapping between `GUID` and file paths, and prepares the assets for later use.
    - During game level loading, any additional referenced assets are added to the `AssetManager`'s queue. These assets are then loaded during idle periods. Currently, the system loads and clears the queue at the start of each frame in the main loop (with potential future improvements for background loading).
5. **Importing External Resources**
    - When importing external resources (e.g., `.obj` files), the appropriate `Loader` is used to read the external file, convert it into an internal asset format, and store it via the `save_asset_to_archive` function.
    - For assets like models (`.obj` files), various asset components such as models, materials, and textures are separated. When importing, a `GameObjectAsset` prefab is created to ensure the engine can later place the entire model into the world.
