# Render Resource Handle RAII

## Purpose

Defines the RAII-safe ownership semantics for `RenderResourceHandle<ResourceType>` — the typed, non-owning handle system used by the engine's three GPU resource managers (`MaterialInstanceManager`, `MaterialLibraryManager`, `StaticMeshResourceManager`).

## Requirements

### Requirement: Handle auto-release on destruction
`RenderResourceHandle<ResourceType>` SHALL automatically release its acquired reference on the corresponding typed resource manager when its destructor runs, provided the engine and manager are still alive.

#### Scenario: Normal lifecycle — handle acquired then destroyed
- **WHEN** a `MaterialInstanceHandle` has been acquired via `MaterialInstanceManager::Acquire(handle)` and the handle is subsequently destroyed
- **THEN** the destructor SHALL call `MaterialInstanceManager::Release(handle)`, decrementing the manager's refcount and clearing `handle.is_acquired`

#### Scenario: Handle never acquired — destroyed
- **WHEN** a `MaterialInstanceHandle` was never acquired (`is_acquired == false`) and the handle is destroyed
- **THEN** the destructor SHALL return immediately without calling any manager

#### Scenario: Engine shutdown — MainClass expired
- **WHEN** a handle has been acquired but `MainClass::GetInstance()` returns null (engine teardown) and the handle is destroyed
- **THEN** the destructor SHALL return immediately without calling `Release()`, accepting the resource leak

#### Scenario: Manager already destroyed — GetRenderResourceManager returns nullptr
- **WHEN** a handle has been acquired, `MainClass` is alive, but `GetRenderResourceManager<T>()` returns nullptr (manager destroyed during impl teardown) and the handle is destroyed
- **THEN** the destructor SHALL return immediately without calling `Release()`

### Requirement: Template-based type dispatch
`RenderResourceHandle<ResourceType>` SHALL use `ResourceTraits<ResourceType>::ManagerType` to resolve the correct resource manager type at compile time, and `ResourceTraits<ResourceType>::HandleType` to safely downcast `*this`.

#### Scenario: Correct manager is called for each handle type
- **WHEN** a `StaticMeshResourceHandle` destructor runs with `is_acquired == true` and engine alive and manager alive
- **THEN** the destructor SHALL call `GetRenderResourceManager<StaticMeshResourceManager>()` via the `RenderSystem` obtained from `MainClass`

### Requirement: Safe copy semantics
Copy construction of `RenderResourceHandle<ResourceType>` SHALL copy `index` and `generation` but force `is_acquired = false`, preventing ownership duplication.

#### Scenario: Copying an acquired handle
- **WHEN** `handle_b = handle_a` where `handle_a.is_acquired == true`
- **THEN** `handle_b.index == handle_a.index`, `handle_b.generation == handle_a.generation`, and `handle_b.is_acquired == false`

### Requirement: Safe move semantics
Move construction of `RenderResourceHandle<ResourceType>` SHALL transfer `index`, `generation`, and `is_acquired` to the destination, and SHALL reset the source to an invalid state (`index = 0xFFFFFFFFu`).

#### Scenario: Moving an acquired handle
- **WHEN** `handle_b = std::move(handle_a)` where `handle_a.is_acquired == true`
- **THEN** `handle_b.is_acquired == true`, `handle_b.index == handle_a`'s original index, and `handle_a.index == 0xFFFFFFFFu`

### Requirement: Copy-and-swap assignment with auto-release
Assignment of `RenderResourceHandle<ResourceType>` SHALL use copy-and-swap, where the old value held by `*this` is released via the destructor of the temporary.

#### Scenario: Assigning to an acquired handle
- **WHEN** `handle_a` is acquired, and `handle_a = handle_b` executes
- **THEN** the old acquisition on `handle_a` SHALL be released, and `handle_a` SHALL take `handle_b`'s `index`/`generation` with `is_acquired = false`

### Requirement: Initial refcount is zero
`IRenderResourceManager::Create()` SHALL set `record.refcount = 0` instead of `1`. Ownership MUST be established by an explicit `Acquire()` call on the returned handle.

#### Scenario: Resource created but never acquired
- **WHEN** `CreateOrReuseFromAsset` creates a new resource and the caller never calls `Acquire()`
- **THEN** `record.refcount == 0` and the resource SHALL NOT trigger deferred deallocation (no countdown started)

#### Scenario: Resource created then acquired then released
- **WHEN** `CreateOrReuseFromAsset` creates a new resource, caller calls `Acquire()`, then `Release()` (explicitly or via handle destructor)
- **THEN** `record.refcount` SHALL transition `0 → 1 → 0`, and the deferred deallocation countdown SHALL begin

### Requirement: GetRenderResourceManager returns pointer
`RenderSystem::GetRenderResourceManager<T>()` SHALL return `T*` instead of `T&`. The pointer SHALL be set to null when the corresponding manager is destroyed.

#### Scenario: Manager alive
- **WHEN** `GetRenderResourceManager<MaterialInstanceManager>()` is called while the manager is alive
- **THEN** a non-null pointer SHALL be returned

#### Scenario: Manager destroyed
- **WHEN** `GetRenderResourceManager<MaterialInstanceManager>()` is called after the manager has been destroyed
- **THEN** `nullptr` SHALL be returned

### Requirement: Manager destructor notifies RenderSystem
`IRenderResourceManager::~IRenderResourceManager()` SHALL call a method on `m_system` to set the corresponding tuple pointer to null before the base destructor completes.

#### Scenario: Manager destroyed during impl teardown
- **WHEN** `MaterialInstanceManager`'s destructor runs (as part of `RenderSystem::impl` destruction)
- **THEN** the destructor SHALL call `m_system.ResourceManagerDestroyed(this)`, setting the tuple entry to nullptr

### Requirement: Removal of manual release in MaterialInstance
`MaterialInstance::~MaterialInstance()` SHALL no longer contain the explicit `m_system.GetRenderResourceManager<MaterialLibraryManager>().Release(m_library)` call.

#### Scenario: MaterialInstance destroyed — library handle auto-released
- **WHEN** a `MaterialInstance` object is destroyed
- **THEN** its `m_library` member SHALL automatically release its reference via the handle destructor, without explicit code in `MaterialInstance::~MaterialInstance`
