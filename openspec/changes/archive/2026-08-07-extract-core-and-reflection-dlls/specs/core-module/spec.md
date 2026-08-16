## ADDED Requirements

### Requirement: Core is a standalone shared library
The engine SHALL compile Core as a separate `Core.dll` shared library that does not depend on any other engine module except Reflection.dll.

#### Scenario: Core.dll has no Framework or Render dependencies
- **WHEN** the build system links Core.dll
- **THEN** Core.dll SHALL NOT link to Framework.dll, Render.dll, Asset.dll, Physics.dll, or UI.dll

#### Scenario: Core.dll links only Reflection and external libraries
- **WHEN** the build system links Core.dll
- **THEN** Core.dll SHALL link only to Reflection.dll, SDL3, glm, and system libraries (rpcrt4 on Windows, uuid on Unix)

### Requirement: Core public interface is minimal
The Core module SHALL expose no more than 10 public header files, containing only engine-agnostic types.

#### Scenario: Core public headers
- **WHEN** a consumer includes any Core public header
- **THEN** the transitive include graph SHALL NOT pull in any Framework, Render, Asset, Physics, or UI headers

### Requirement: Core provides GUID type
The Core module SHALL provide a `GUID` class implementing RFC 4122 UUID generation (nil, sequential/v1, random/v4).

#### Scenario: Generate random GUID
- **WHEN** `GUID::Random()` is called
- **THEN** the returned GUID SHALL be a valid RFC 4122 version 4 UUID with correct variant bits

#### Scenario: Generate sequential GUID
- **WHEN** `GUID::Sequential()` is called
- **THEN** the returned GUID SHALL be a valid RFC 4122 version 1 UUID with correct variant bits

#### Scenario: GUID string representation
- **WHEN** `GUID::string()` is called on a valid GUID
- **THEN** the returned string SHALL match the format `00000000-0000-0000-0000-000000000000`

### Requirement: Core provides Transform math type
The Core module SHALL provide a `Transform` class with position (vec3), rotation (quat), and scale (vec3) components, supporting serialization via Reflection.

#### Scenario: Transform matrix construction
- **WHEN** `GetTransformMatrix()` is called
- **THEN** the returned mat4 SHALL equal `translate(position) * mat4_cast(rotation) * scale(scale)`

#### Scenario: Transform serialization
- **WHEN** a Transform is serialized via `Engine::Serialization::Archive`
- **THEN** all three components (m_position, m_rotation, m_scale) SHALL be serialized with their full precision

### Requirement: Core provides Delegate and Event system
The Core module SHALL provide a type-safe multicast delegate and event system supporting std::shared_ptr and std::weak_ptr lifetime binding.

#### Scenario: Delegate invocation
- **WHEN** a `Delegate` bound to a valid shared_ptr is invoked
- **THEN** the bound method SHALL be called with the correct arguments

#### Scenario: Delegate invalidation
- **WHEN** all shared_ptr references to a delegate target are released
- **THEN** `IsValid()` SHALL return false and `Invoke()` SHALL be a no-op

#### Scenario: Event multicast
- **WHEN** an `Event` with multiple listeners is invoked
- **THEN** all valid listeners SHALL be called in registration order

### Requirement: Core provides flagbits template
The Core module SHALL provide a `Flags<T>` template for type-safe bitwise operations on scoped and unscoped enums.

#### Scenario: Flag bitwise OR
- **WHEN** two `Flags<MyEnum>` values are combined with `operator|`
- **THEN** the result SHALL have both bits set in the underlying integer

### Requirement: Core provides SDL window wrapper
The Core module SHALL provide an `SDLWindow` class that wraps `SDL_Window` creation and destruction with RAII semantics.

#### Scenario: Window creation
- **WHEN** `SDLWindow` is constructed with valid title, width, height, and flags
- **THEN** a valid `SDL_Window` pointer SHALL be created and owned

#### Scenario: Window destruction
- **WHEN** `SDLWindow` is destroyed
- **THEN** the underlying `SDL_Window` SHALL be destroyed via `SDL_DestroyWindow`

### Requirement: Core provides time system
The Core module SHALL provide a `TimeSystem` class for frame-level delta time tracking.

#### Scenario: Delta time computation
- **WHEN** `TimeSystem::NextFrame()` is called for two consecutive frames
- **THEN** `GetDeltaTime()` SHALL return the wall-clock time between the two calls in microseconds

### Requirement: Core provides CLI option parsing
The Core module SHALL provide an `OptionHandler` for parsing engine startup options (resolution, font, verbose mode, etc.) from command-line arguments.

#### Scenario: Parse startup options
- **WHEN** `ParseOptions(argc, argv)` is called with valid arguments
- **THEN** a `StartupOptions` struct SHALL be returned with correct resolution, font size, and flags

### Requirement: ComponentDelegate and EventQueue live in Framework
The Framework module SHALL own `ComponentDelegate` (component-bound delegate template) and `EventQueue` (deferred component event processing), not Core.

#### Scenario: ComponentDelegate accepts Scene
- **WHEN** `ComponentDelegate` is constructed
- **THEN** its constructor SHALL accept `Scene&` and `ComponentHandle` parameters

#### Scenario: EventQueue processes deferred events
- **WHEN** `EventQueue::ProcessEvents()` is called
- **THEN** all queued delegates that are `IsValid()` SHALL be invoked and dequeued

### Requirement: SDLWindow has no engine-level dependencies
The `SDLWindow.cpp` implementation SHALL NOT include `MainClass.h`, `Render/Memory/RenderTargetTexture.h`, or `vulkan/vulkan.hpp`.

#### Scenario: SDLWindow compilation
- **WHEN** `SDLWindow.cpp` is compiled
- **THEN** the compilation SHALL succeed without MainClass.h, any Render headers, or vulkan.hpp in the include graph
