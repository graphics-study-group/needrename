## Context

The engine's rendering layer uses a typed handle system for GPU resource lifetime management. Three resource managers (`MaterialInstanceManager`, `MaterialLibraryManager`, `StaticMeshResourceManager`) own GPU resources and expose lightweight handles (`MaterialInstanceHandle`, `MaterialLibraryHandle`, `StaticMeshResourceHandle`) with index + generation validation, reference counting, and deferred deallocation (3-frame countdown after refcount reaches 0).

Currently, `RenderResourceHandle` is a concrete struct whose destructor only asserts `!is_acquired` — it does not call `Release()` on the corresponding manager. Two bugs compound:

1. **Handle destruction without release**: If a handle with `is_acquired == true` is destroyed, refcount leaks. Only caught by a debug assertion.
2. **Impossible-to-release initial refcount**: `Create()` sets `record.refcount = 1` but returns a handle with `is_acquired = false`. No code path can ever decrement this refcount to 0, so every resource is permanently leaked regardless of how carefully Acquire/Release are used.

The engine already has a proven RAII pattern: `AssetRef` destructor calls `Release()` automatically, with defensive null checks for `MainClass`/`AssetManager` to handle shutdown ordering.

## Goals / Non-Goals

**Goals:**
- Make `RenderResourceHandle` destruction automatically call `Release()` on the correct typed manager
- Fix the `refcount = 1` bug so resources can actually be reclaimed
- Make copy/move semantics safe: copy never duplicates ownership, move transfers it
- Use defensive-null pattern (like `AssetRef`) for shutdown safety
- Enable safe detection of destroyed managers during teardown without UB
- Keep the three typed handle structs compile-time identical in external API
- Remove manual `Release()` in `MaterialInstance::~MaterialInstance`

**Non-Goals:**
- Change the deferred deallocation (3-frame countdown) mechanism
- Add thread safety (handles remain single-threaded)
- Modify `MaterialInstance` members beyond removing the manual release line
- Generalize to more than three resource types (extensible later via explicit instantiation)

## Decisions

### Decision 1: Template on ResourceType (compile-time type dispatch via traits)

**Alternatives considered**:
- Store a `RenderSystem*` per handle (+8 bytes, fragile on copy)
- Store a function pointer (+8-16 bytes, per-handle setup overhead)
- Type-erased callback (heap allocation)

**Chosen approach**: `RenderResourceHandle<ResourceType>` uses `ResourceTraits<ResourceType>` (already defined) to resolve `ManagerType` at compile time. Destructor implemented in `.cpp` with explicit template instantiation to avoid circular includes. No data member overhead.

### Decision 2: Route through MainClass singleton rather than storing back-pointer

Matches `AssetRef` pattern:

```
~RenderResourceHandle<T>()
  → MainClass::GetInstance()           // shared_ptr from weak_ptr
    → GetRenderSystem()                // shared_ptr<RenderSystem>
      → GetRenderResourceManager<ManagerType>()  // T* (may be nullptr)
        → if ptr → Release(...)
        → if null → skip (manager already destroyed)
```

**Defensive checks** (in order):
1. `is_acquired == false` → early return
2. `MainClass::GetInstance()` returns null (teardown) → early return
3. `GetRenderSystem()` returns null → early return
4. `GetRenderResourceManager<...>()` returns nullptr (manager destroyed) → early return
5. Call `Release()`, idempotent within manager

### Decision 3: Copy/move/assign semantics

**Copy**: Constructs a handle with same `index`/`generation` but `is_acquired = false`. Never duplicates ownership. Matches `AssetRef` copy behavior and the existing `RendererManager::GetMaterialResourceHandle` pattern.

**Move**: Transfers `is_acquired` and `index`/`generation` to destination. Source reset to `index = 0xFFFFFFFFu, generation = 0, is_acquired = false` (invalid handle).

**Copy-and-swap assignment**: `operator=(RenderResourceHandle other)` — `other` is pass-by-value (copy or move ctor already ran), then swap, then `other` destructor auto-releases old value.

### Decision 4: Fix initial refcount: 0 not 1

`Create()` currently sets `record.refcount = 1` with `is_acquired = false` on the returned handle. Since `Release()` only decrements refcount when `handle.is_acquired == true`, this initial refcount can never be released — a permanent leak on every resource.

**Fix**: Set `record.refcount = 0` in `Create()`. Ownership is established by calling `Acquire()` (refcount → 1, `is_acquired = true`). When the owning handle is destroyed (or explicitly `Release()`'d), refcount → 0 and the countdown starts.

TickFrame is unaffected: it only considers `pending_deallocation_countdown`, not `refcount`.

### Decision 5: GetRenderResourceManager returns pointer, not reference

**Problem**: When managers are destroyed before handles (current impl destruction order), `GetRenderResourceManager<T>()` returned a dangling reference. Calling `Release()` on it is UB.

**Fix**: Return `T*` instead of `T&`. Each manager's destructor notifies `RenderSystem` to set the corresponding tuple pointer to null. Handle destructors check for nullptr and skip release if the manager is dead.

`RenderSystem` gains a template method:
```
template <typename ResourceManagerType>
void ResourceManagerDestroyed(ResourceManagerType *ptr);
```

`IRenderResourceManager::~IRenderResourceManager()` calls `m_system.ResourceManagerDestroyed(this)`.

All existing call sites (`auto &` → `auto *`, `.` → `->`) need updating (~13 engine + ~7 test).

### Decision 6: Manager destructor notification safe during impl teardown

During `RenderSystem::impl` destruction (reverse declaration order), managers are destroyed before `RendererManager`/`SceneDataManager`. When a manager's `~IRenderResourceManager()` runs and calls `m_system.ResourceManagerDestroyed(this)`, the `RenderSystem` object itself is still fully alive (owned by `shared_ptr` in `MainClass`). Only the `impl` sub-objects are being torn down. The tuple pointer nullification is safe.

When handles in `RendererManager`/`SceneDataManager` subsequently destruct and reach step 4 of the defensive check chain, `GetRenderResourceManager<T>()` returns nullptr and release is safely skipped.

## Risks / Trade-offs

- **[Risk] Call sites calling GetRenderResourceManager without null-check**: After changing return type to `T*`, callers that do `auto *mgr = ...; mgr->Foo()` without checking null could crash if the manager was destroyed. **Mitigation**: In practice, call sites access managers during active frames (not during destruction), so the pointer is always non-null in the normal path. During teardown, only handle destructors call this path, and they include the null check.
- **[Risk] Static destruction between TUs**: `RenderResourceHandle.cpp` template instantiations depend on `MainClass`. If `MainClass`'s static storage is destroyed first, `weak_ptr` is expired → safe return. Same as `AssetRef` pattern.
- **[Trade-off] Template in header may increase compile time**: Mitigated by out-of-line destructor in `.cpp` via explicit instantiation — header remains lightweight.
