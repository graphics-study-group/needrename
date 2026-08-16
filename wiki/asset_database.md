# Asset Database

The `AssetDatabase` manages the storage of assets in files. Assets can be read from or written to the database using their GUID.

`AssetDatabase` can be implemented in multiple ways. In the editor, it is typically implemented using the file system directly, while the final packaged game may use a packaged asset database.

`AssetDatabase` is an abstract class. Subclasses must implement the `SaveArchive(AnnoRefl::Archive, GUID)` and `LoadArchive(AnnoRefl::Archive, GUID)` methods, which save or load a serialized `AnnoRefl::Archive` (the result of serializing an object) to or from the database using the given GUID. The interface also exposes path-based lookups: `GetAssetPath(GUID)` resolves a GUID to its `AssetPath`, while `GetNewAssetRef(AssetPath)` and `GetGUID(AssetPath)` resolve a path back to an `AssetRef` or `GUID`.

## AssetPath

`AssetPath` is a value type that addresses an asset by a named scheme plus a relative path, stored as a normalized `scheme://path` string. The engine reserves three schemes:

- `res://` — project assets
- `builtin://` — engine built-in assets
- `usr://` — user data

`AssetPath` is independent of any storage backend; converting a path to an on-disk location is the responsibility of the backend (see `FileSystemDatabase` below).

### FileSystemDatabase

This is a subclass of `AssetDatabase`. It manages assets directly using the file system and is generally used within the editor.

`FileSystemDatabase` mounts each scheme to a disk root via `RegisterScheme(scheme, root, writable)`. `ToAbsolutePath`/`FromAbsolutePath` convert between `AssetPath` and on-disk paths, and `GetProjectAssetsPath` returns the project assets mount root. It also supports asset management operations: `AddAsset` registers a GUID-path mapping, `ListDirectory` lists asset files and subdirectories, and `CreateDirectory`/`MovePath`/`DeletePath` mutate the project asset tree.
