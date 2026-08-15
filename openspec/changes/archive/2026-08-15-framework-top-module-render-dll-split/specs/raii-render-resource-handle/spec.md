# raii-render-resource-handle

## MODIFIED Requirements

### Requirement: Handle auto-release on destruction
`RenderResourceHandle<ResourceType>` SHALL automatically release its acquired reference on the corresponding typed resource manager when its destructor runs, provided the render system and manager are still alive. The `RenderSystem` SHALL be resolved through the Render module runtime registry (`GetRenderRuntime().render_system`), not through `MainClass::GetInstance()`.

#### Scenario: Normal lifecycle — handle acquired then destroyed
- **WHEN** a `MaterialInstanceHandle` has been acquired via `MaterialInstanceManager::Acquire(handle)` and the handle is subsequently destroyed
- **THEN** the destructor SHALL call `MaterialInstanceManager::Release(handle)`, decrementing the manager's refcount and clearing `handle.is_acquired`

#### Scenario: Handle never acquired — destroyed
- **WHEN** a `MaterialInstanceHandle` was never acquired (`is_acquired == false`) and the handle is destroyed
- **THEN** the destructor SHALL return immediately without calling any manager

#### Scenario: Engine shutdown — render runtime cleared
- **WHEN** a handle has been acquired but `GetRenderRuntime().render_system` is null (registry cleared during teardown) and the handle is destroyed
- **THEN** the destructor SHALL return immediately without calling `Release()`, accepting the resource leak

#### Scenario: Manager already destroyed — GetRenderResourceManager returns nullptr
- **WHEN** a handle has been acquired, the registry is seeded, but `GetRenderResourceManager<T>()` returns nullptr (manager destroyed during impl teardown) and the handle is destroyed
- **THEN** the destructor SHALL return immediately without calling `Release()`

### Requirement: Template-based type dispatch
`RenderResourceHandle<ResourceType>` SHALL use `ResourceTraits<ResourceType>::ManagerType` to resolve the correct resource manager type at compile time, and `ResourceTraits<ResourceType>::HandleType` to safely downcast `*this`.

#### Scenario: Correct manager is called for each handle type
- **WHEN** a `StaticMeshResourceHandle` destructor runs with `is_acquired == true`, the registry seeded, and manager alive
- **THEN** the destructor SHALL call `GetRenderResourceManager<StaticMeshResourceManager>()` via the `RenderSystem` obtained from `GetRenderRuntime().render_system`
