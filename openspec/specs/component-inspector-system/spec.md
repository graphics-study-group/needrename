# Component Inspector System

## Purpose

The Component Inspector System provides a pluggable framework for rendering ImGui inspectors for Engine Components. It separates the inspection logic from the UI widget, allowing engine-specific types (handles, assets) to register custom inspectors while falling back to reflection-based default field traversal.

## Requirements

### Requirement: ComponentInspectorBase interface
An abstract base class `ComponentInspectorBase` SHALL be defined with a pure virtual method `void Inspect(Engine::Component& component)`.

#### Scenario: Custom inspector implements base
- **WHEN** a developer creates a class inheriting `ComponentInspectorBase` and overrides `Inspect`
- **THEN** the class can be registered with `ComponentInspectorRegistry`

### Requirement: ComponentInspectorRegistry
A `ComponentInspectorRegistry` class SHALL provide `Register(type_name, unique_ptr<ComponentInspectorBase>)` and `Find(type_name) -> ComponentInspectorBase*`. It SHALL be a plain class (no static `Get()` method) owned as a value member by `EditorMainClass`, accessed via `EditorMainClass::GetInstance()->GetComponentInspectorRegistry()`.

#### Scenario: Register and find inspector
- **WHEN** a Component inspector is registered with type name "Engine::CameraComponent"
- **THEN** `Find("Engine::CameraComponent")` returns the registered inspector pointer

#### Scenario: Find unregistered type
- **WHEN** `Find` is called with a type name that has no registered inspector
- **THEN** `nullptr` is returned

### Requirement: InspectComponent dispatch function
A free function `InspectComponent(component)` SHALL check `ComponentInspectorRegistry` for the Component's type name. If found, it SHALL delegate to the registered inspector. If not found, it SHALL call `DefaultInspectComponent`.

#### Scenario: Registered Component dispatched
- **WHEN** `InspectComponent` is called with a Component whose type name is registered
- **THEN** the registered inspector's `Inspect` is called with the same Component

#### Scenario: Unregistered Component falls back
- **WHEN** `InspectComponent` is called with a Component whose type name is not registered
- **THEN** `DefaultInspectComponent` is called with the same Component

### Requirement: DefaultInspectComponent field traversal
`DefaultInspectComponent` SHALL render a collapsible `ImGui::TreeNodeEx` with the Component type name as header. Inside, it SHALL iterate all fields and array fields, calling `InspectVar` for each.

#### Scenario: Default component shows fields
- **WHEN** `DefaultInspectComponent` is called with a Component
- **THEN** a TreeNode labeled with the Component type name is rendered, and expanding it shows all fields using the Var inspector system

### Requirement: ComponentInspector owns full TreeNode
A registered `ComponentInspectorBase::Inspect` SHALL be responsible for rendering the entire tree node (header and content). Callers SHALL push an ImGui ID before calling Inspect.

#### Scenario: Custom component inspector replaces default layout
- **WHEN** a registered Component inspector's `Inspect` is called
- **THEN** it renders its own TreeNode (or alternative layout), not the default field traversal
