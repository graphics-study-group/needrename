# asset-path

## ADDED Requirements

### Requirement: AssetPath is a pure value type
`AssetPath` SHALL be a pure value type: it stores a normalized `scheme://path` string, holds no reference to any database or backend, and does not inherit `std::filesystem::path`. It SHALL be copyable, comparable with `operator==`, and hashable with `AssetPath::Hash` such that two paths with the same normalized string are equal and hash equal, regardless of which database instance produced them.

#### Scenario: Copy and compare across databases
- **WHEN** two `AssetPath` values built from the same string `"res://materials/wood.asset"` by two different `FileSystemDatabase` instances are compared with `operator==`
- **THEN** the comparison returns true and their `AssetPath::Hash` values are equal

#### Scenario: No database reference
- **WHEN** `engine/Asset/AssetDatabase/AssetDatabase.h` is inspected
- **THEN** `AssetPath` has no `FileSystemDatabase` member, no `AssetDatabase` member, and no `std::filesystem::path` base class

#### Scenario: Works as an unordered container key
- **WHEN** an `AssetPath` created from `"res://a/./b"` and one created from `"res://a/b"` are inserted into the same `std::unordered_map<AssetPath, V, AssetPath::Hash>`
- **THEN** they occupy the same entry

### Requirement: AssetPath scheme naming convention
The engine SHALL define the scheme convention: `res://` addresses project assets, `builtin://` addresses engine built-in assets, and `usr` is reserved as a future user-data scheme. Scheme names SHALL be treated case-insensitively and normalized to lowercase at construction.

#### Scenario: Default schemes documented as constants
- **WHEN** the AssetCore headers are inspected
- **THEN** constants naming the `res`, `builtin`, and `usr` schemes exist (e.g. `k_scheme_res`, `k_scheme_builtin`, `k_scheme_usr`)

#### Scenario: Scheme case is normalized
- **WHEN** an `AssetPath` is constructed from `"RES://a/b"` and another from `"res://a/b"`
- **THEN** they compare equal and both normalize to `res://a/b`

#### Scenario: Legacy prefixes are gone
- **WHEN** the AssetCore and its consumers are searched for path construction
- **THEN** no code constructs asset paths with the legacy `~` builtin prefix or the legacy leading `/` project prefix

### Requirement: AssetPath subpath normalization
The subpath component of an `AssetPath` SHALL be lexically normalized at construction: redundant `.` segments and separator runs are removed, `..` segments are folded without escaping the scheme root, and the subpath uses `/` separators in generic string form on every platform.

#### Scenario: Dot segments folded
- **WHEN** an `AssetPath` is constructed from `"res://materials/../textures/./wood.asset"`
- **THEN** its normalized form is `res://textures/wood.asset`

#### Scenario: Escaping the root is clamped
- **WHEN** an `AssetPath` is constructed from a string whose `..` folding would escape the scheme root (e.g. `"res://../../etc"`)
- **THEN** the resulting subpath stays within the scheme root and does not contain leading `..` segments

### Requirement: AssetPath structural API
`AssetPath` SHALL expose structural operations without filesystem knowledge: `GetScheme()`, `GetSubPath()`, `IsEmpty()`, `parent_path()`, `filename()`, an append operator (`operator/`), and `ToString()` returning the normalized string. The root path of a scheme SHALL be representable (e.g. `"res://"`), and `parent_path()` of a scheme root SHALL return the root itself.

#### Scenario: Parent of root is root
- **WHEN** `parent_path()` is called on `AssetPath{"res://"}`
- **THEN** the result equals `AssetPath{"res://"}`

#### Scenario: Append composes paths
- **WHEN** `AssetPath{"res://a"}` is appended with `"b/c.asset"` via `operator/`
- **THEN** the result normalizes to `res://a/b/c.asset`

### Requirement: FileSystemDatabase resolves schemes to disk
`FileSystemDatabase` SHALL maintain a scheme-to-disk-root mount table and provide `ToAbsolutePath(const AssetPath&)` and `FromAbsolutePath(const std::filesystem::path&)`. `ToAbsolutePath` SHALL resolve the scheme against the mount table and throw `std::runtime_error` for an unmounted scheme. `FromAbsolutePath` SHALL match the mount whose root contains the absolute path with the longest prefix, comparing on path-segment boundaries.

#### Scenario: Scheme resolved through mount table
- **WHEN** `FileSystemDatabase` has a mount `res` → `D:/proj/assets` and `ToAbsolutePath` is called with `res://meshes/cube.asset`
- **THEN** the result is `D:/proj/assets/meshes/cube.asset`

#### Scenario: Unmounted scheme throws
- **WHEN** `ToAbsolutePath` is called with a path whose scheme has no mount (e.g. `pak://data/x`)
- **THEN** it throws `std::runtime_error`

#### Scenario: Longest prefix wins on reverse mapping
- **WHEN** mounts `res` → `D:/proj/assets` and `builtin` → `D:/proj/assets/engine` exist and `FromAbsolutePath` is called with `D:/proj/assets/engine/mesh/cube.asset`
- **THEN** the result is `builtin://mesh/cube.asset` (not `res://engine/mesh/cube.asset`)

### Requirement: Mount writability gates write operations
Mounts SHALL carry a writability property. The default `builtin` mount SHALL be read-only and the default `res` mount SHALL be writable. `CreateDirectory`, `MovePath`, `DeletePath`, and path-based `SaveArchive` SHALL reject paths whose mount is read-only.

#### Scenario: Builtin mount rejects writes
- **WHEN** `CreateDirectory`, `MovePath`, `DeletePath`, or path-based `SaveArchive` is called with a `builtin://` path
- **THEN** the operation returns false without modifying the disk

#### Scenario: Project mount accepts writes
- **WHEN** `CreateDirectory` is called with a `res://new/dir` path on a database whose `res` mount is writable
- **THEN** the directory is created on disk and the operation returns true

### Requirement: Disk-path resolution is a FileSystemDatabase concern
`AssetPath` SHALL NOT expose `to_absolute_path()` or `from_absolute_path()`. Consumers that need on-disk paths (notably `ShaderAsset::Compile`, which runs only in the editor phase because packaged builds use precompiled SPIR-V) SHALL obtain the active `AssetDatabase`, downcast it to `FileSystemDatabase`, and call `ToAbsolutePath`.

#### Scenario: ShaderAsset resolves disk path via FileSystemDatabase
- **WHEN** `ShaderAsset::Compile` needs the on-disk source path and the active `AssetDatabase` is a `FileSystemDatabase`
- **THEN** it calls `GetAssetPath(GUID)` on the interface, downcasts the database to `FileSystemDatabase`, and passes the resulting `AssetPath` to `ToAbsolutePath`

#### Scenario: Non-filesystem backend fails compile gracefully
- **WHEN** `ShaderAsset::Compile` runs and the downcast to `FileSystemDatabase` fails
- **THEN** compilation fails gracefully (logged and returns false) instead of dereferencing a null pointer
