# Asset Database

The `AssetDatabase` manages the storage of assets in files. Assets can be read from or written to the database using their GUID.

`AssetDatabase` can be implemented in multiple ways. In the editor, it is typically implemented using the file system directly, while the final packaged game may use a packaged asset database.

`AssetDatabase` is an abstract class. Subclasses must implement the `SaveArchive(Archive, GUID)` and `LoadArchive(Archive, GUID)` methods, which save or load a serialized `Archive` (the result of serializing an object) to or from the database using the given GUID.

### FileSystemDatabase

This is a subclass of `AssetDatabase`. It manages assets directly using the file system and is generally used within the editor.

`FileSystemDatabase` maintains a mapping from GUIDs to file paths. Assets can be accessed using paths like `/abc/file.asset`, which refers to the asset `file` in the `abc` folder of the currently open project. Paths starting with `~` are resolved relative to the engine’s built-in assets.

Additionally, `FileSystemDatabase` supports listing all asset files or subdirectories within a given directory, making it easier for the editor to traverse and manage assets visually.

