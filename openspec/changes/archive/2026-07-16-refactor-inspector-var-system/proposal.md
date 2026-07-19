## Why

`InspectorWidget::InspectVar()` is a 100-line if-else chain dispatching by type name string comparison. Adding a new inspectable type requires modifying this monolithic function, and engine-specific display logic (Handle resolution, Asset GUID display) is tightly coupled to the Widget. This blocks extensibility for custom editor inspectors and makes the code fragile.

## What Changes

- **New**: `VarInspectorBase` abstract class and `VarInspectorRegistry` singleton — a pluggable system where type-specific inspectors register by type name
- **New**: `ComponentInspectorBase` abstract class and `ComponentInspectorRegistry` singleton — same pattern for Component-level inspection
- **New**: Default Var inspector (`DefaultVarInspector`) handling int, float, bool, std::string, glm::vec3, glm::quat, enum types, and recursive reflected-type traversal — used as fallback for unregistered types
- **New**: Default Component inspector (`DefaultComponentInspector`) that traverses fields using the Var inspector system — used as fallback for unregistered Component types
- **New**: `HandleInspectors` (ObjectHandle, ComponentHandle) registered as Var inspectors in `engine_impl/`
- **New**: `AssetInspector` (AssetRef) registered as Var inspector in `engine_impl/`
- **New**: `InspectorRegistrations.h/.cpp` — centralized registration entry point called from main.cpp
- **Modified**: `InspectorWidget` — removes private `InspectVar` method, uses the new dispatcher functions instead
- **Modified**: `example/editor_run_game_example/main.cpp` — adds registration calls after LoadProject

## Capabilities

### New Capabilities
- `var-inspector-system`: Pluggable type-name-based Var inspection with registry, default fallback, and recursive reflected-type handling
- `component-inspector-system`: Pluggable type-name-based Component inspection with registry and default field-traversal fallback

### Modified Capabilities
<!-- No existing specs to modify -->

## Impact

- **Affected code**: `editor/Editor/Widget/InspectorWidget.h/.cpp`, `example/editor_run_game_example/main.cpp`
- **New files**: 12 files across `editor/Editor/Inspector/` and `editor/Editor/engine_impl/`
- **Dependencies**: No new external dependencies; uses existing ImGui, glm, and engine reflection system
- **Breaking changes**: None — `InspectorWidget` public API unchanged, only internal implementation replaced
