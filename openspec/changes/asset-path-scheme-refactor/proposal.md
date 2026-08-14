## Why

`AssetPath` currently holds a `const FileSystemDatabase &` member, which forces the abstract `AssetDatabase` interface to depend on a concrete implementation and makes `AssetPath` a half-value type (runtime asserts on cross-database copy/compare, hash/equality inconsistency). Its `~`/`/` magic prefixes are cryptic, collide with `std::filesystem::path` absolute-path semantics, and cannot be extended. Path-based lookup is absent from the interface even though every backend — including a future packaged backend — benefits from a readable path index.

## What Changes

- `AssetPath` becomes a pure value type storing a normalized `scheme://path` string (e.g. `res://materials/wood.asset`). It no longer references any database and no longer inherits `std::filesystem::path`. **BREAKING**
- Scheme naming replaces magic prefixes: `~` becomes `builtin://`, `/` becomes `res://`. `usr` is reserved as a convention constant (no special behavior yet). **BREAKING** — all construction sites updated.
- `FileSystemDatabase` maintains a scheme-to-disk-root mount table (`RegisterScheme`); `ToAbsolutePath` / `FromAbsolutePath` move from `AssetPath` to `FileSystemDatabase` with longest-prefix mount matching. **BREAKING**
- The path secondary index is promoted into the `AssetDatabase` interface: `GetNewAssetRef(const AssetPath&)` and `GetGUID(const AssetPath&)` become pure virtuals. **BREAKING**
- Write operations (`CreateDirectory` / `MovePath` / `DeletePath` / `SaveArchive(archive, path)`) stay on `FileSystemDatabase`. Mounts carry a read-only flag: `builtin` is read-only, `res`/`usr` are writable.
- `ShaderAsset::Compile` (editor phase only; packaged builds use precompiled SPIR-V) resolves its on-disk path by downcasting `AssetDatabase` to `FileSystemDatabase` and calling `ToAbsolutePath`. This amends the "SHALL NOT downcast" requirement introduced by the in-flight `framework-top-module-render-dll-split` change.
- `LoadBuiltinAssets` / `LoadProjectAssets` keep their signatures and call sites but internally register the `builtin`/`res` schemes before scanning.
- GUID remains the sole primary identity; the path is a maintained secondary index. `AssetRef` serialization stays GUID-only.

## Capabilities

### New Capabilities
- `asset-path`: the pure-value asset path type, scheme naming convention (`res` / `builtin` / `usr`), normalization rules, and scheme-to-disk resolution owned by `FileSystemDatabase`.

### Modified Capabilities
- `asset-core-module`: `AssetDatabase` interface gains path-based lookup (`GetNewAssetRef`, `GetGUID` by path); `AssetPath` changes from a database-bound type to a pure value; `FileSystemDatabase` gains the scheme mount table and disk-path resolution. Serialized asset representation is unchanged (no asset JSON contains `~`/`/` prefixed paths; `AssetRef` stores only GUIDs).

## Impact

- **Code**: `engine/Asset/AssetDatabase/AssetDatabase.h`, `FileSystemDatabase.{h,cpp}`, `engine/Render/Shader/ShaderAsset.cpp`, `engine/Framework/Tools/ComplexRenderGraphBuilder.cpp`, `editor/Editor/Widget/{ProjectWidget,HierarchyWidget,SceneWidget}.{h,cpp}`, `example/physics_example/*`, `example/external_resource_loading_example/main.cpp`, `example/editor_run_game_example/main.cpp`.
- **Dependencies**: must be applied after `framework-top-module-render-dll-split` lands; amends its `render-module` downcast requirement (a delta for that capability is added when it reaches the main specs).
- **No data migration**: this is a pure code change; asset files on disk are untouched.
