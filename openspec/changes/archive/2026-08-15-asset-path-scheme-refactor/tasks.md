## 1. AssetPath core value type

- [x] 1.1 Rewrite `AssetPath` in `engine/Asset/AssetDatabase/AssetDatabase.h` as a pure value: single normalized generic string, scheme constants (`k_scheme_res`, `k_scheme_builtin`, `k_scheme_usr`), no `FileSystemDatabase` reference, no `std::filesystem::path` inheritance; delete the `Hash` struct's database coupling and make `Hash`/`operator==` string-based
- [x] 1.2 Implement construction normalization in `FileSystemDatabase.cpp` or a new `AssetPath.cpp`: lowercase scheme, fold `.`/`..` with root clamping, generic `/` separators; expose `GetScheme()`, `GetSubPath()`, `IsEmpty()`, `parent_path()` (root returns itself), `filename()`, `operator/`, `ToString()`
- [x] 1.3 Remove `to_absolute_path()` / `from_absolute_path()` and the legacy `~`/`/` handling from `AssetPath`; drop the now-unused `begin/end/iterator/lexically_normal` usings

## 2. FileSystemDatabase scheme mounts

- [x] 2.1 Add the mount table (`std::unordered_map<std::string, Mount>` with root path + writable flag) and `RegisterScheme` to `FileSystemDatabase`; delete `m_builtin_asset_path` / `m_project_asset_path` members
- [x] 2.2 Implement `ToAbsolutePath(const AssetPath&)` (throw `std::runtime_error` on unmounted scheme) and `FromAbsolutePath(const std::filesystem::path&)` (longest segment-boundary prefix match); replace all internal `path.to_absolute_path()` / `from_absolute_path` uses in `SaveArchive`/`LoadArchive`/`ListDirectory`/`CreateDirectory`/`MovePath`/`DeletePath`
- [x] 2.3 Rework `LoadBuiltinAssets`/`LoadProjectAssets` (signatures unchanged) to register the `builtin`/`res` mounts then scan; replace the `IsBuiltinAssetPath` write-op rejection with mount-writability checks; delete the helper
- [x] 2.4 Update `MovePath` remapping to build remapped paths via `FromAbsolutePath` instead of constructing `AssetPath` with a database reference

## 3. AssetDatabase interface path index

- [x] 3.1 Add `virtual AssetRef GetNewAssetRef(const AssetPath&) = 0` and `virtual std::optional<GUID> GetGUID(const AssetPath&) const = 0` to `AssetDatabase`; verify the header no longer mentions `FileSystemDatabase`
- [x] 3.2 Override both in `FileSystemDatabase` against `m_path_to_guid` (key type is now the pure `AssetPath`; `m_assets_map` unchanged)

## 4. Consumer migration

- [x] 4.1 Rewrite `ShaderAsset::Compile` (`engine/Render/Shader/ShaderAsset.cpp`): obtain `AssetPath` via the interface `GetAssetPath`, `dynamic_cast` the `AssetDatabase` to `FileSystemDatabase`, call `ToAbsolutePath`, log and return false when the cast fails
- [x] 4.2 Migrate `engine/Framework/Tools/ComplexRenderGraphBuilder.cpp` path construction to `AssetPath{"builtin://..."}`
- [x] 4.3 Migrate editor widgets (`ProjectWidget`, `HierarchyWidget`, `SceneWidget`) to scheme paths: breadcrumbs, root checks (`res://`), `parent_path`, file ops via the concrete `FileSystemDatabase&`; replace `std::filesystem::path` composition with `AssetPath::operator/`
- [x] 4.4 Migrate `example/physics_example`, `example/external_resource_loading_example`, `example/editor_run_game_example` path constructions to `builtin://` / `res://`
- [x] 4.5 Grep sweep: no remaining `AssetPath{db, "~` / `AssetPath{db, "/` constructions, no `to_absolute_path`/`from_absolute_path` callers outside `FileSystemDatabase`, no `IsBuiltinAssetPath`

## 5. Tests

- [x] 5.1 Add unit tests for `AssetPath` value semantics: cross-database equality/hash, scheme case normalization, `.`/`..` folding with root clamping, `parent_path` of root, append composition, `IsEmpty`
- [x] 5.2 Add unit tests for mount resolution: `ToAbsolutePath` per scheme, unmounted-scheme throw, longest-prefix `FromAbsolutePath` (nested mount case), read-only `builtin` write rejection, `usr` custom registration
- [x] 5.3 Add tests for the interface path index: `GetNewAssetRef` on registered/unregistered paths, `GetGUID` returning `nullopt` for unknown paths, GUID preserved across `MovePath`
- [x] 5.4 Build and run `ctest --preset debug` with the MSYS2 CLANG64 environment variables; verify the editor example still opens and browses assets

## 6. Cross-change coordination

- [x] 6.1 Verify `framework-top-module-render-dll-split` tasks touching `AssetDatabase.h` / `ShaderAsset.cpp` (tasks 2.4–2.6) are applied first; record the required `render-module` spec amendment (downcast clause) as a delta when that change archives
- [ ] 6.2 Update `openspec/specs/asset-core-module/spec.md` delta sync after archiving this change (via the normal archive flow)
