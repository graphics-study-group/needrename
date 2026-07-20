## Why

`RenderResourceHandle` currently lacks automatic release on destruction — its destructor only asserts `!is_acquired` in debug builds. If a handle holding an active acquire (refcount contribution) is destroyed without a prior explicit `Release()` call, the manager's refcount is permanently leaked and the GPU resource is never reclaimed.

Additionally, `IRenderResourceManager::Create()` sets `record.refcount = 1` but returns a handle with `is_acquired = false`, making the initial refcount impossible to ever release — a latent leak affecting every resource creation path.

## What Changes

- **BREAKING**: Convert `RenderResourceHandle` from a concrete struct to a class template `RenderResourceHandle<ResourceType>`, parameterized on the resource payload type. The template argument enables type-safe automatic release via `ResourceTraits<ResourceType>`.
- Add a custom destructor to `RenderResourceHandle<ResourceType>` that automatically calls `Release()` on the correct typed manager, routed through `MainClass` → `RenderSystem` → `GetRenderResourceManager<T>()`, with defensive null checks.
- Define explicit copy constructor (only copies `index`/`generation`, sets `is_acquired = false`), move constructor (transfers ownership including `is_acquired`, resets source to invalid), and copy-and-swap assignment operator.
- **BREAKING**: Change `GetRenderResourceManager<T>()` return type from `T&` to `T*`. Manager destructors nullify their tuple pointer, enabling handle destructors to detect destroyed managers and skip release safely.
- Fix `IRenderResourceManager::Create()`: `refcount = 0` instead of `refcount = 1` to match the explicit-owner model.
- Add `~IRenderResourceManager()` body that notifies `RenderSystem` to nullify the corresponding tuple pointer.
- Remove the manual `Release()` call from `MaterialInstance::~MaterialInstance`.
- Add a new `RenderResourceHandle.cpp` with explicit template instantiation for the three resource types.
- Update all ~20 call sites of `GetRenderResourceManager<T>()` from `auto &` → `auto *` across engine and test code.

## Capabilities

### New Capabilities
- `raii-render-resource-handle`: `RenderResourceHandle<ResourceType>` handles proper ownership semantics — copy produces a non-owning duplicate, move transfers ownership, and destruction automatically releases on the typed manager (when the manager is still alive).

### Modified Capabilities
<!-- No existing capabilities are modified; this is purely an internal safety improvement. -->

## Impact

- **Affected code**: `RenderResourceHandle.h` (template conversion + copy/move/assign), new `RenderResourceHandle.cpp` (destructor), `RenderSystem.h` (return type + `ResourceManagerDestroyed`), `IRenderResourceManager.h/.inl` (destructor body + `refcount=0`), `MaterialInstance.cpp` (remove manual release), all engine call sites: `RendererManager.cpp`, `SceneDataManager.cpp`, `CommandBuffer.cpp`, `LevelAsset.cpp`, `MaterialInstanceManager.cpp` (`auto &` → `auto *`), ~7 test files.
- **Backward compatibility**: The three typed handle structs preserve their external API. Code that creates, acquires, or stores handles continues to compile after the pointer-return-type migration.
- **Dependencies**: `ResourceTraits<T>` (existing), `MainClass` (global singleton), `RenderSystem::GetRenderResourceManager<T>()` (modified to return pointer).
