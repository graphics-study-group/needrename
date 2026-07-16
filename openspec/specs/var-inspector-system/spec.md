# Var Inspector System

## Purpose

The Var Inspector System provides a pluggable framework for rendering ImGui controls for reflection-based `Var` objects. It dispatches by type name through a registry, allowing custom inspectors for engine-specific types (handles, assets) while providing sensible defaults for basic types, enums, and reflectable structs.

## Requirements

### Requirement: VarInspectorBase interface
An abstract base class `VarInspectorBase` SHALL be defined with a pure virtual method `void Inspect(const std::string& name, Engine::Reflection::Var var)`.

#### Scenario: Custom inspector implements base
- **WHEN** a developer creates a class inheriting `VarInspectorBase` and overrides `Inspect`
- **THEN** the class can be registered with `VarInspectorRegistry`

### Requirement: VarInspectorRegistry
A `VarInspectorRegistry` class SHALL provide `Register(type_name, unique_ptr<VarInspectorBase>)` and `Find(type_name) -> VarInspectorBase*`. It SHALL be a plain class (no static `Get()` method) owned as a value member by `EditorMainClass`, accessed via `EditorMainClass::GetInstance()->GetVarInspectorRegistry()`.

#### Scenario: Register and find inspector
- **WHEN** an inspector is registered with type name "Engine::ObjectHandle"
- **THEN** `Find("Engine::ObjectHandle")` returns the registered inspector pointer

#### Scenario: Find unregistered type
- **WHEN** `Find` is called with a type name that has no registered inspector
- **THEN** `nullptr` is returned

### Requirement: InspectVar dispatch function
A free function `InspectVar(name, var)` SHALL check `VarInspectorRegistry` for the type. If found, it SHALL delegate to the registered inspector. If not found, it SHALL call `DefaultInspectVar`.

#### Scenario: Registered type dispatched
- **WHEN** `InspectVar` is called with a Var whose type name is registered
- **THEN** the registered inspector's `Inspect` is called with the same name and Var

#### Scenario: Unregistered type falls back
- **WHEN** `InspectVar` is called with a Var whose type name is not registered
- **THEN** `DefaultInspectVar` is called with the same name and Var

### Requirement: DefaultInspectVar basic types
`DefaultInspectVar` SHALL render editable ImGui controls for these basic types: `int` (InputInt), `float` (InputFloat), `bool` (Checkbox), `std::string` (InputText), `glm::vec3` (DragFloat3), `glm::quat` (DragFloat4 with normalize).

#### Scenario: Int field is editable
- **WHEN** `DefaultInspectVar` is called with a Var of type "int"
- **THEN** an `ImGui::InputInt` widget is rendered and the Var value is updated on edit

#### Scenario: Float field is editable
- **WHEN** `DefaultInspectVar` is called with a Var of type "float"
- **THEN** an `ImGui::InputFloat` widget is rendered and the Var value is updated on edit

### Requirement: DefaultInspectVar enum types
`DefaultInspectVar` SHALL render a Combo box for types where `GetTypeKind() == TypeKind::Enum`, using `GetEnumString()` and `SetEnumFromString()`.

#### Scenario: Enum field shows combo
- **WHEN** `DefaultInspectVar` is called with a Var of an enum type
- **THEN** an `ImGui::Combo` is rendered with all enum values and selection updates the Var

### Requirement: DefaultInspectVar reflectable types
`DefaultInspectVar` SHALL render a collapsible `ImGui::TreeNodeEx` for types where `IsReflectable()` is true. Inside the node, it SHALL recursively call `InspectVar` for each field and array field.

#### Scenario: Reflected struct is expanded
- **WHEN** `DefaultInspectVar` is called with a Var of a reflectable type
- **THEN** a TreeNode is rendered and expanding it shows all fields inspected recursively

### Requirement: DefaultInspectVar unknown types
`DefaultInspectVar` SHALL render nothing for types that are neither basic, enum, nor reflectable.

#### Scenario: Unknown type is silently skipped
- **WHEN** `DefaultInspectVar` is called with a Var of an unrecognized type
- **THEN** no ImGui widget is rendered for that Var

### Requirement: Caller manages ImGui PushID
`VarInspectorBase::Inspect` and `DefaultInspectVar` SHALL NOT call `ImGui::PushID/PopID`. The caller (InspectorWidget or recursive field iteration) SHALL push IDs before invoking inspection.

#### Scenario: Recursive field iteration preserves ID stack
- **WHEN** `DefaultInspectVar` recursively calls `InspectVar` for struct fields
- **THEN** each call is wrapped with `ImGui::PushID(idx)` and `ImGui::PopID()` by the caller
