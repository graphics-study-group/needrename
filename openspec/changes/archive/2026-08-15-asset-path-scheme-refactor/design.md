## Context

`AssetPath` today privately inherits `std::filesystem::path` and holds `const FileSystemDatabase &m_database`. This makes the abstract `AssetDatabase` interface depend on a concrete implementation, gives `AssetPath` broken value semantics (runtime asserts on cross-database copy/compare, hash/equality inconsistency), and encodes backend knowledge (`~` builtin root, `/` project root, `to_absolute_path`) into a shared value type. Path-based lookup (`GetNewAssetRef(path)`, path→GUID) exists only on `FileSystemDatabase`, so every consumer that works by path must hold the concrete type.

The in-flight `framework-top-module-render-dll-split` change added `GetAssetPath(GUID)` to the `AssetDatabase` interface and rewrote `ShaderAsset::Compile` to avoid downcasting. This change builds on top of it and amends its render-module "SHALL NOT downcast" requirement (downcasting to `FileSystemDatabase`, which lives in EngineAssetCore, does not reintroduce the Framework/MainClass dependency that split removed).

Serialized asset data is unaffected: `AssetPath` was never serializable, no asset JSON contains `~`/`/` prefixed paths, and `AssetRef` stores only GUIDs.

## Goals / Non-Goals

**Goals:**
- `AssetPath` as a pure value: normalized `scheme://path` string, no database reference, no `std::filesystem::path` inheritance, consistent hash/equality.
- Replace `~`/`/` magic prefixes with `res://` / `builtin://` schemes; reserve `usr`.
- Move disk-path resolution (`ToAbsolutePath` / `FromAbsolutePath`) to `FileSystemDatabase`, backed by a scheme→disk-root mount table with read-only flags.
- Promote the path secondary index into the `AssetDatabase` interface (`GetNewAssetRef(path)`, `GetGUID(path)`), keeping GUID the sole primary identity.
- Editor-phase `ShaderAsset::Compile` resolves its disk path via a `FileSystemDatabase` downcast; packaged builds never hit this path.

**Non-Goals:**
- No packaged-backend implementation (pak): the interface shape leaves room for it, but only `FileSystemDatabase` is touched.
- No `usr` behavior beyond the reserved constant; applications mount it themselves via `RegisterScheme`.
- No `AssetPath` reflection/serialization integration in this change (it becomes possible, not required).
- No data migration; no changes to the `.asset` JSON format.

## Decisions

### D1. AssetPath as a scheme string value (`res://a/b`)

The type stores one normalized generic string. Scheme names are constants (`k_scheme_res = "res"`, `k_scheme_builtin = "builtin"`, `k_scheme_usr = "usr"`); construction parses `scheme://subpath`, lowercases the scheme, and lexically normalizes the subpath (fold `.`/`..`, clamp `..` so it cannot escape the scheme root, use `/` separators). `operator==`, `Hash`, and ordering derive from the normalized string only.

- *Why not a global scheme registry (Godot-style ResourceLoader)?* Only two consumers resolve paths (FileSystemDatabase internals, editor-phase shader compile). A global registry is an abstraction the current engine does not need; the mount table lives in `FileSystemDatabase` and stays open to extraction later.
- *Why not keep `std::filesystem::path` inheritance with an added scheme field?* Inheritance leaked platform-specific behavior (`/` vs `\`, absolute-path semantics colliding with the legacy `/` prefix) and made serialization and cross-DLL equality fragile. A plain string with a small structural API (`GetScheme`, `GetSubPath`, `parent_path`, `filename`, `operator/`, `ToString`) is sufficient for all current call sites (ProjectWidget breadcrumbs, loaders, ShaderAsset).
- *Why keep GUID primary instead of path primary (Godot/Unreal style)?* `AssetRef` serialization and refcounting are GUID-based; path-primary would be a much larger migration. The path index gives readability without breaking rename-independence of references.

### D2. Scheme→disk mounts inside FileSystemDatabase

`FileSystemDatabase` gains `RegisterScheme(std::string_view scheme, std::filesystem::path root)` and a mount table. Each mount has a writability flag (`builtin` read-only, `res`/`usr` writable). `ToAbsolutePath(const AssetPath&)` throws `std::runtime_error` for unmounted schemes; `FromAbsolutePath` picks the mount with the longest root prefix on segment boundaries (fixes the latent `starts_with` prefix bug in today's `from_absolute_path`, e.g. `.../assets2` vs `.../assets`).

- `LoadBuiltinAssets(path)` / `LoadProjectAssets(path)` keep their signatures: they register the `builtin`/`res` mounts and scan for `.asset` files exactly as today, so the 10 test files and 3 examples need no changes for mounting.
- Write ops (`CreateDirectory`/`MovePath`/`DeletePath`/`SaveArchive(archive, path)`) check mount writability instead of the hardcoded `IsBuiltinAssetPath` helper; that helper is deleted.

### D3. Interface gains the path secondary index

`AssetDatabase` declares:

```cpp
virtual AssetRef GetNewAssetRef(const AssetPath &path) = 0;          // throws if unknown
virtual std::optional<GUID> GetGUID(const AssetPath &path) const = 0; // nullopt if unknown
```

`FileSystemDatabase` implements both against `m_path_to_guid` (key type becomes the pure `AssetPath`). `GetGUID` returns `optional` so existence checks are not exception-driven; `GetNewAssetRef` keeps the existing throw behavior. `ListDirectory` and write operations stay on `FileSystemDatabase` (browsing and mutation are editor/filesystem concerns; the editor already holds `FileSystemDatabase&`).

- *Why `optional` for `GetGUID`?* Editor existence checks (drag-and-drop, folder creation) should not use exceptions for control flow; the throw style remains only for `GetNewAssetRef` misuse.
- *Why not move `ListDirectory` into the interface?* It is only consumed by `ProjectWidget`, which takes a concrete `FileSystemDatabase&`. A packaged backend would need a different listing concept anyway (pak TOC vs. directory iteration).

### D4. ShaderAsset disk-path resolution (amends split's render-module requirement)

`ShaderAsset::Compile` obtains `GetAssetRuntime().asset_database`, calls the interface `GetAssetPath(GetGUID())` for the `AssetPath`, then `dynamic_cast`s to `FileSystemDatabase` and calls `ToAbsolutePath`. A failed cast logs an error and returns false (packaged build or non-filesystem backend — shader compilation is editor-phase only; packaged builds load precompiled SPIR-V). This re-introduces the downcast removed by `framework-top-module-render-dll-split`; because `FileSystemDatabase` lives in EngineAssetCore (which Render already links for `Asset`), no DLL layering violation is introduced. The split change's render-module spec will need an amendment when it reaches the main specs; until then, this change documents the dependency and a follow-up task records the delta.

- *Alternative considered: interface method `ResolveToDiskPath`.* Rejected: `std::filesystem::path` in the abstract interface would leak a filesystem concept that packaged backends cannot honor; shader compilation is inherently a filesystem/editor concern.

### D5. Call-site migration mechanics

All `AssetPath{db, "~/..."}` / `AssetPath{db, "/..."}` constructions become `AssetPath{"builtin://..."}` / `AssetPath{"res://..."}`. Editor breadcrumbs and file-tree code switch from `begin()/end()` iteration over a filesystem path to the structural API (`parent_path`, `filename`). `AssetPath(m_database, "")` empty defaults become `AssetPath{}` with `IsEmpty()`. The root path `"res://"` replaces `AssetPath(db, "/")`, and `parent_path()` of the root returns the root (guards the editor's "navigate up" button at the top level).

## Risks / Trade-offs

- [Double migration churn with the in-flight split change] → This change must apply after split lands; its proposal documents the amend relationship, and a task records the render-module delta for when the split change archives.
- [Subtle path-equality bugs during migration] → All normalization lives in the `AssetPath` constructor; call sites never compare raw strings. Tests cover cross-database equality, scheme case-insensitivity, and `..` clamping.
- [Cross-DLL `dynamic_cast` reliability on Windows/MinGW] → Already exercised by the split change (GltfLoader downcast to `FileSystemDatabase`); ShaderAsset follows the same pattern with a null check.
- [Editor UX regressions from path semantics changes] → The `builtin://` read-only rule matches today's behavior (`IsBuiltinAssetPath` rejection); breadcrumbs/tree code is verified by running the editor example.
- [Forgetting a call site of the legacy prefixes] → Mechanical migration is checked with a grep sweep task; the compile will catch remaining `AssetPath(db, ...)` two-argument constructions because that constructor no longer exists.
